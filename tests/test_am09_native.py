#!/usr/bin/env python3
"""Owned anonymous MIME file, real app pipeline/TCP, faults and parallel processes."""
from __future__ import print_function
import email
import glob
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
import test_am01_native as old
import test_am03_native as network

SOURCE,BUILD = old.SOURCE,old.BUILD
DRIVER = os.path.join(BUILD,'am09_driver')
checks = 0


def check(value,label):
    global checks
    if not value:
        raise AssertionError(label)
    checks += 1
    print('PASS',label,flush=True)


def compile_driver():
    libs = sorted(glob.glob(os.path.join(BUILD,'*.a'))+glob.glob(os.path.join(BUILD,'src/*/*.a')))
    subprocess.check_call(['g++','-std=gnu++17','-O0','-g','-I'+SOURCE,'-I'+os.path.join(SOURCE,'src'),
                           '-I'+os.path.join(SOURCE,'secrets'),os.path.join(SOURCE,'tests/am09_driver.cpp'),
                           '-Wl,--start-group']+libs+['-Wl,--end-group','-lsodium','-lstdc++fs','-ldl','-pthread','-no-pie','-o',DRIVER])
    subprocess.check_call(['gcc','-shared','-fPIC',os.path.join(SOURCE,'tests/am09_faults.c'),'-ldl','-o',os.path.join(BUILD,'am09_faults.so')])


def start(mode,home,smtp,imap,marker='synthetic',faults=None):
    env = os.environ.copy()
    env['HOME'] = home
    if faults:
        env['LD_PRELOAD'] = os.path.join(BUILD,'am09_faults.so')
        env.update(faults)
    return subprocess.Popen([DRIVER,mode,str(smtp.port),str(imap.port),marker],env=env,
                            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)


def finish(proc,text=b'',timeout=12):
    try:
        out = proc.communicate(text,timeout=timeout)[0].decode('utf-8','replace')
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise AssertionError('MIME integration driver timeout')
    if proc.returncode:
        raise AssertionError('driver failed: '+out[-3000:])
    return out


def ready(proc):
    line = proc.stdout.readline().decode('utf-8','replace')
    found = re.search(r'READY_FD=(\d+) MODE=(\d+) LINKS=(\d+) CLOEXEC=(\d+)',line)
    if not found:
        raise AssertionError('missing private descriptor checkpoint: '+line)
    return [int(value) for value in found.groups()]


def contents(body):
    parsed = email.message_from_bytes(body)
    return [part.get_payload(decode=True) for part in parsed.walk() if part.get_filename()]


def names():
    return glob.glob('/tmp/astramail-mime-*')


def mime_fds(pid):
    found = []
    for entry in glob.glob('/proc/{}/fd/*'.format(pid)):
        try:
            if os.readlink(entry).startswith('/tmp/astramail-mime-'):
                found.append(entry)
        except OSError:
            pass
    return found


def letter(home,address='a@example.invalid',marker=b'synthetic payload'):
    folder = os.path.join(home,'.local/share/AstraMail/outbox',address)
    os.makedirs(folder)
    with open(os.path.join(folder,'document.bin'),'wb') as file:
        file.write(marker)
    return folder


class SMTP(old.SMTPServer):
    def __init__(self,stage):
        self.stage = stage
        self.held = threading.Event()
        self.release = threading.Event()
        super(SMTP,self).__init__()

    def reply(self,conn,text):
        prefix = '235 ' if self.stage=='auth' else '250 queued'
        if text.startswith(prefix) and not self.held.is_set():
            self.held.set()
            self.release.wait(10)
        return super(SMTP,self).reply(conn,text)


def main():
    compile_driver()
    with tempfile.TemporaryDirectory(prefix='am09-fixtures-',dir=BUILD) as root:
        def fixture(name):
            path = os.path.join(root,name)
            os.makedirs(path)
            return path
        home = fixture('owned')
        canary = os.path.join(root,'synthetic-canary')
        with open(canary,'wb') as file:
            file.write(b'synthetic canary must not be overwritten')
        legacy = '/tmp/astramail.mime'
        os.symlink(canary,legacy)
        try:
            smtp,imap = old.SMTPServer(),network.IMAP()
            for mode in ['owned','restrictive']:
                proc = start(mode,home,smtp,imap,marker=mode)
                replacement = None
                try:
                    fd,mode_bits,links,cloexec = ready(proc)
                    path = '/proc/{}/fd/{}'.format(proc.pid,fd)
                    target = os.readlink(path)
                    check(mode_bits==0o600 and links==0 and cloexec==1 and target.endswith(' (deleted)'),
                          mode+' umask creates owner-only, unlinked, close-on-exec MIME file')
                    check(not names(),'no named MIME file remains even while content is in use')
                    with open(path,'rb') as file:
                        expected = file.read()
                    check(contents(expected)==[mode.encode('ascii')+bytes(range(256))*4],
                          'owned stream contains complete binary attachment: '+mode)
                    if mode=='owned':
                        probe = subprocess.call(['sudo','-n','-u','nobody','python3','-c',
                            'import sys\ntry: open(sys.argv[1],"rb").read(1)\nexcept OSError: sys.exit(13)\n',path],
                            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                        check(probe==13,'another OS user cannot open the live MIME descriptor')
                        replacement = target[:-len(' (deleted)')]
                        os.symlink(canary,replacement)
                    out = finish(proc,b'send\n')
                    check('SMTP_OK=1 APPEND_OK=1' in out and 'FD_DELTA=0' in out,
                          'SMTP and APPEND complete without descriptor leak: '+mode)
                    check(smtp.deliveries[-1][1]==expected and imap.appends[-1]==expected,
                          'SMTP and Sent copy read exact same owned MIME bytes: '+mode)
                    if replacement:
                        check(os.path.islink(replacement),'recreated old random name is neither reopened nor deleted by stream owner')
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.communicate()
                    if replacement and os.path.lexists(replacement):
                        os.unlink(replacement)
            check(os.path.islink(legacy) and open(canary,'rb').read()==b'synthetic canary must not be overwritten',
                  'legacy global symlink and canary remain untouched')
            check(not names(),'successful completion leaves no named MIME files')

            out = finish(start('exec',home,smtp,imap))
            check('EXEC_PRIVATE=1' in out and 'FD_DELTA=0' in out,'exec does not inherit private MIME descriptor')
            out = finish(start('lifetime',home,smtp,imap))
            check('BUILT=80' in out and 'FD_DELTA=0' in out and not names(),
                  '80 exception-unwind cycles close every MIME and attachment stream')
            out = finish(start('missing',home,smtp,imap))
            check('BUILD_OK=0' in out and 'FD_DELTA=0' in out and not names(),
                  'missing attachment aborts construction and cleans owned stream')

            proc = start('owned',home,smtp,imap,marker='killed')
            fd,_,_,_ = ready(proc)
            check(not names(),'process awaiting use already has no named MIME file')
            proc.kill()
            proc.communicate(timeout=3)
            check(proc.returncode==-signal.SIGKILL and not names() and not os.path.exists('/proc/{}/fd/{}'.format(proc.pid,fd)),
                  'SIGKILL closes anonymous MIME file without relying on destructors')

            for field,value in [('AM09_FAIL_CREATE','1'),('AM09_FAIL_MODE','1'),('AM09_FAIL_UNLINK','1'),
                                ('AM09_FAIL_FDOPEN','1'),('AM09_FAIL_WRITE','ENOSPC'),('AM09_FAIL_WRITE','EINTR'),
                                ('AM09_FAIL_FLUSH','1'),('AM09_FAIL_SEEK_AT','1'),('AM09_FAIL_SEEK_AT','2')]:
                case = fixture(field+'-'+value)
                folder = letter(case)
                connections = smtp.connections
                out = finish(start('outbox',case,smtp,imap,faults={field:value}))
                check('COMPLETE=0 QUEUED=1' in out and 'FD_DELTA=0' in out and smtp.connections==connections and
                      os.path.exists(os.path.join(folder,'document.bin')) and 'U ' not in old.records(folder),
                      field+'='+value+' preserves pending mail before any SMTP session')
                leftover = names()
                if field=='AM09_FAIL_UNLINK':
                    check(leftover and all(os.stat(path).st_size==0 and (os.stat(path).st_mode&0o777)==0o600 for path in leftover),
                          'unlink failure never writes mail data; only empty private entries may remain')
                    for path in leftover:
                        os.unlink(path)
                else:
                    check(not leftover,'failed MIME preparation leaves no directory entry: '+field+'='+value)
                out = finish(start('outbox',case,smtp,imap))
                check('COMPLETE=1 QUEUED=0' in out and not os.path.exists(folder) and not names(),
                      'normal retry succeeds after '+field+'='+value)
            out = finish(start('fault-loop',home,smtp,imap,faults={'AM09_FAIL_FDOPEN':'1'}))
            check('BUILT=0' in out and 'FD_DELTA=0' in out and not names(),'80 failed stream wrappers close raw descriptors')

            case = fixture('short-write')
            marker = b'synthetic short file write '+bytes(range(256))*5
            letter(case,marker=marker)
            out = finish(start('outbox',case,smtp,imap,faults={'AM09_SHORT_WRITE':'1'}))
            check('COMPLETE=1' in out and contents(smtp.deliveries[-1][1])==[marker] and imap.appends[-1]==smtp.deliveries[-1][1],
                  'short stdio writes preserve full binary attachment through SMTP and APPEND')
            case = fixture('smtp-rewind')
            folder = letter(case)
            before = len(smtp.deliveries)
            out = finish(start('outbox',case,smtp,imap,faults={'AM09_FAIL_SEEK_AT':'3'}))
            check('COMPLETE=0 QUEUED=1' in out and len(smtp.deliveries)==before and 'U ' not in old.records(folder) and not names(),
                  'SMTP rewind error sends no DATA and retains mail')
            case = fixture('append-rewind')
            folder = letter(case)
            before = len(imap.appends)
            out = finish(start('outbox',case,smtp,imap,faults={'AM09_FAIL_SEEK_AT':'4'}))
            check('COMPLETE=1 QUEUED=0' in out and len(imap.appends)==before and 'Копия в Отправленные не сохранена' in out and not names(),
                  'APPEND rewind error does not repeat already confirmed SMTP delivery')

            for mode in ['auth_fail','data_fail','final_fail','drop_final']:
                case = fixture(mode)
                folder = letter(case)
                server = old.SMTPServer()
                server.mode = mode
                out = finish(start('outbox',case,server,imap))
                check('COMPLETE=0 QUEUED=1' in out and 'FD_DELTA=0' in out and os.path.exists(folder) and not names(),
                      'SMTP '+mode+' retains queue and closes anonymous MIME')
                if mode=='drop_final':
                    check('U "a@example.invalid"' in old.records(folder),'lost final reply retains durable unknown state for automatic retry')
                server.mode = 'normal'
                out = finish(start('outbox',case,server,imap))
                check('COMPLETE=1' in out and not os.path.exists(folder) and not names(),'retry after '+mode+' confirms and removes queue')

            server = SMTP('auth')
            homes = [fixture('parallel-alpha'),fixture('parallel-beta')]
            markers = [b'synthetic ALPHA data '+bytes(range(256))*2,b'synthetic BETA data '+bytes(range(255,-1,-1))*2]
            folders = [letter(homes[i],address=('alpha' if i==0 else 'beta')+'@example.invalid',marker=markers[i]) for i in range(2)]
            first = start('outbox',homes[0],server,imap)
            second = None
            try:
                check(server.held.wait(5),'parallel test holds first instance before SMTP reads MIME')
                second = start('outbox',homes[1],server,imap)
                deadline = time.time()+5
                first_fds,second_fds = [],[]
                while time.time()<deadline:
                    first_fds,second_fds = mime_fds(first.pid),mime_fds(second.pid)
                    if first_fds and second_fds:
                        break
                    time.sleep(.02)
                check(len(first_fds)==len(second_fds)==1 and os.stat(first_fds[0]).st_ino!=os.stat(second_fds[0]).st_ino and not names(),
                      'simultaneous instances own distinct unlinked MIME inodes')
                server.release.set()
                one,two = finish(first),finish(second)
                check('COMPLETE=1' in one and 'COMPLETE=1' in two and all(not os.path.exists(folder) for folder in folders),
                      'both concurrent send operations complete independently')
                delivered = {addresses[0]:contents(body)[0] for addresses,body in server.deliveries}
                check(delivered=={'alpha@example.invalid':markers[0],'beta@example.invalid':markers[1]},
                      'concurrent instances never exchange or overwrite message payloads')
            finally:
                server.release.set()
                for proc in [first,second]:
                    if proc is not None and proc.poll() is None:
                        proc.kill()
                        proc.communicate()

            check(not names() and os.path.islink(legacy) and open(canary,'rb').read()==b'synthetic canary must not be overwritten',
                  'all scenarios leave legacy symlink/canary unchanged and no named MIME payload')
        finally:
            if os.path.lexists(legacy):
                os.unlink(legacy)
            for path in names():
                os.unlink(path)
    print('AM09_NATIVE_CHECKS={}/{}'.format(checks,checks),flush=True)


if __name__=='__main__':
    main()
