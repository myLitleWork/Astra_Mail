#!/usr/bin/env python3
"""Real background SMTP hold, actual commands, contact stress and full PTY UI."""
from __future__ import print_function
import glob
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import test_am01_native as old
import test_am03_native as network
import test_am06_native as terminal

SOURCE = old.SOURCE
BUILD = old.BUILD
DRIVER = os.path.join(BUILD, 'am08_driver')
checks = 0


def check(value, label):
    global checks
    if not value:
        raise AssertionError(label)
    checks += 1
    print('PASS', label, flush=True)


def inspect(output):
    global checks
    result = re.search(r'AM08_DRIVER_CHECKS=(\d+)', output)
    if not result:
        raise AssertionError('missing native results: '+output[-3000:])
    checks += int(result.group(1))
    for line in output.splitlines():
        if line.startswith('PASS '):
            print(line, flush=True)


def compile_driver():
    libs = sorted(glob.glob(os.path.join(BUILD, '*.a')) + glob.glob(os.path.join(BUILD, 'src/*/*.a')))
    subprocess.check_call(['g++', '-std=gnu++17', '-O0', '-g', '-I'+SOURCE,
                           '-I'+os.path.join(SOURCE,'src'), '-I'+os.path.join(SOURCE,'secrets'),
                           os.path.join(SOURCE,'tests/am08_driver.cpp'),
                           os.path.join(SOURCE,'src/menu/Commands/Server/CmdEditLocalContacts.cpp'),
                           os.path.join(SOURCE,'src/menu/Commands/Server/CmdEditLocalProfileSettings.cpp'), '-Wl,--start-group']+libs+
                          ['-Wl,--end-group','-lsodium','-lstdc++fs','-ldl','-pthread','-no-pie','-o',DRIVER])


class SMTP(old.SMTPServer):
    def __init__(self):
        self.release = threading.Event()
        super(SMTP,self).__init__()

    def reply(self,conn,text):
        if text.startswith('250 queued'):
            self.release.wait(15)
        return super(SMTP,self).reply(conn,text)


class Live(object):
    def __init__(self,mode,home,smtp,imap,editor):
        env = os.environ.copy()
        env['HOME'],env['EDITOR'] = home,editor
        self.proc = subprocess.Popen([DRIVER,mode,str(smtp.port),str(imap.port)],env=env,
                                     stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.lines = []
        def collect():
            for line in iter(self.proc.stdout.readline,b''):
                self.lines.append(line.decode('utf-8','replace'))
        self.reader = threading.Thread(target=collect,daemon=True)
        self.reader.start()

    def wait(self,text,timeout=10):
        deadline = time.time()+timeout
        while text not in ''.join(self.lines):
            if time.time()>deadline or self.proc.poll() is not None:
                raise AssertionError('native checkpoint missing: '+text+' '+''.join(self.lines)[-3000:])
            time.sleep(0.02)

    def send(self):
        self.proc.stdin.write(b'continue\n')
        self.proc.stdin.flush()

    def finish(self,timeout=10):
        self.proc.wait(timeout=timeout)
        self.reader.join(timeout=2)
        out = ''.join(self.lines)
        if self.proc.returncode:
            raise AssertionError('native failure: '+out[-4000:])
        inspect(out)
        return out

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()


def main():
    compile_driver()
    idle = os.path.join(BUILD,'idle_settings.conf')
    with tempfile.TemporaryDirectory(prefix='am08-fixtures-',dir=BUILD) as root:
        def fixture(name):
            path = os.path.join(root,name)
            os.makedirs(path)
            return path
        editor = os.path.join(root,'editor.py')
        with open(editor,'w') as file:
            file.write('#!/usr/bin/python3\nimport os,time\n'
                       'home=os.environ["HOME"]\n'
                       'open(home+"/editor-started","w").write("synthetic editor\\n")\n'
                       'deadline=time.time()+10\n'
                       'while not os.path.exists(home+"/editor-release") and time.time()<deadline: time.sleep(.02)\n')
        os.chmod(editor,0o700)
        with open(idle,'w') as file:
            file.write('idle_threshold_seconds=1\ncheck_interval_seconds=1\n')
        try:
            smtp,imap = SMTP(),network.IMAP()
            live = Live('contacts',fixture('contacts'),smtp,imap,editor)
            try:
                live.finish()
            finally:
                live.close()
            check(smtp.connections==0,'contact concurrency tests perform no network requests')

            smtp,imap = SMTP(),network.IMAP()
            home = fixture('blocked')
            live = Live('blocked',home,smtp,imap,editor)
            try:
                live.wait('READY_FOR_CHECK')
                check(smtp.data_seen.wait(8),'actual idle background reaches SMTP final-response wait')
                connections = smtp.connections
                live.send()
                live.wait('BLOCKED_DONE')
                check(smtp.connections==connections,'rejected send/check commands open no additional SMTP sessions')
                check('Команда не выполнена:' in ''.join(live.lines) and 'Нажмите Enter' in ''.join(live.lines),
                      'busy result explains retry and remains visible for acknowledgement')
                smtp.release.set()
                live.send()
                live.finish()
            finally:
                smtp.release.set()
                live.close()

            smtp,imap = SMTP(),network.IMAP()
            live = Live('stop',fixture('stop'),smtp,imap,editor)
            try:
                live.finish(timeout=5)
            finally:
                live.close()
            check(smtp.connections==0,'background skips held data-change gate instead of queuing behind it')

            smtp,imap = SMTP(),network.IMAP()
            home = fixture('editor')
            live = Live('editor',home,smtp,imap,editor)
            try:
                deadline = time.time()+5
                while not os.path.exists(os.path.join(home,'editor-started')) and time.time()<deadline:
                    time.sleep(.02)
                check(os.path.exists(os.path.join(home,'editor-started')),'real external editor entered while operation gate is free')
                time.sleep(1.5)
                check(smtp.connections==0,'idle background does not start during external contact editing')
                with open(os.path.join(home,'editor-release'),'w') as file:
                    file.write('release synthetic editor')
                live.finish()
            finally:
                live.close()

            with open(idle,'w') as file:
                file.write('idle_threshold_seconds=2\ncheck_interval_seconds=1\n')
            smtp,imap = SMTP(),network.IMAP()
            home = fixture('full-app')
            env = os.environ.copy()
            env['HOME'] = home
            subprocess.check_call([os.path.join(BUILD,'am01_driver'),'seed',str(smtp.port),str(imap.port)],
                                  env=env,stdout=subprocess.DEVNULL)
            folder = old.mail(home,'a@example.invalid')
            tty = terminal.Terminal('',home,app=True)
            try:
                tty.read_until('Выбор: ')
                tty.send('1\n')
                tty.read_until('Введите PIN: ')
                tty.send('synthetic-pin-123456\n')
                tty.read_until('Нажмите Enter')
                tty.send('\n')
                tty.read_until('Основное меню Astra_Mail_v3.3')
                check(smtp.data_seen.wait(8),'complete application starts real background send before conflicting UI command')
                tty.send('1\n')
                tty.read_until('Локальные исходящие')
                tty.send('4\n')
                tty.read_until('Очистить исходящие')
                tty.send('4\n')
                tty.read_until('Команда не выполнена:')
                tty.read_until('Нажмите Enter')
                check(os.path.exists(os.path.join(folder,'document.txt')),'full UI rejects cleanup before destructive confirmation and preserves active letter')
                tty.send('\n')
                smtp.release.set()
                tty.read_until('Исходящая очередь обработана.')
                check(not os.path.exists(folder),'background completes confirmed send after rejected UI cleanup')
                repeat = old.mail(home,'repeat@example.invalid')
                tty.send('4\n')
                tty.read_until('Будут безвозвратно удалены')
                tty.send('y\n')
                tty.read_until('[OK] Папка исходящих очищена.')
                check(not os.path.exists(repeat),'same cleanup succeeds when operator repeats after background completion')
                tty.send('\n0\n0\n0\n')
                tty.finish(timeout=3)
                check(tty.proc.returncode==0 and tty.restored(),'complete application exits normally after busy rejection and successful retry')
            finally:
                smtp.release.set()
                tty.close()
        finally:
            os.unlink(idle)
    print('AM08_NATIVE_CHECKS={}/{}'.format(checks,checks),flush=True)


if __name__=='__main__':
    main()
