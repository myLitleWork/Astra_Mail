#!/usr/bin/env python3
"""Python 3.5+, real loopback SMTP + actual application libraries, no real mail."""
from __future__ import print_function
import glob
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

SOURCE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BUILD = os.path.abspath(sys.argv[1])
DRIVER = os.path.join(BUILD, 'am01_driver')
checks = 0


def check(condition, name):
    global checks
    if not condition:
        raise AssertionError(name)
    checks += 1
    print('PASS', name, flush=True)


class SMTPServer(object):
    def __init__(self):
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(8)
        self.port = self.listener.getsockname()[1]
        self.reject = {}
        self.mode = 'normal'
        self.fragment = False
        self.deliveries = []
        self.envelopes = []
        self.data_seen = threading.Event()
        self.connections = 0
        self.thread = threading.Thread(target=self.serve)
        self.thread.daemon = True
        self.thread.start()

    def reply(self, conn, text):
        if self.mode == 'bare':
            text = text.splitlines()[-1][:3] + '\r\n'
        data = text.encode('ascii')
        if self.fragment:
            for byte in data:
                conn.sendall(bytes([byte]))
        else:
            conn.sendall(data)

    def serve(self):
        while True:
            try:
                conn, _ = self.listener.accept()
            except OSError:
                return
            self.connections += 1
            try:
                conn.settimeout(8)
                stream = conn.makefile('rb')
                self.reply(conn, '220 loopback synthetic\r\n')
                auth = 0
                accepted = []
                while True:
                    raw = stream.readline(100000)
                    if not raw:
                        break
                    line = raw.decode('ascii', 'replace').rstrip('\r\n')
                    if auth:
                        if auth == 1:
                            auth = 2
                            self.reply(conn, '334 password\r\n')
                        else:
                            auth = 0
                            self.reply(conn, '535 denied\r\n' if self.mode == 'auth_fail' else '235 accepted\r\n')
                    elif line.startswith('EHLO '):
                        self.reply(conn, '250-loopback\r\n250-AUTH LOGIN\r\n250 OK\r\n')
                    elif line == 'AUTH LOGIN':
                        auth = 1
                        self.reply(conn, '334 username\r\n')
                    elif line.startswith('MAIL FROM:'):
                        accepted = []
                        self.envelopes.append([])
                        self.reply(conn, '550 sender denied\r\n' if self.mode == 'sender_fail' else '250 sender\r\n')
                    elif line.startswith('RCPT TO:'):
                        address = line[len('RCPT TO:<'):-1]
                        self.envelopes[-1].append(address)
                        code = self.reject.get(address, 250)
                        if code in (250, 251, 252):
                            accepted.append(address)
                        self.reply(conn, str(code) + ' recipient\r\n')
                    elif line == 'DATA':
                        if self.mode == 'data_fail':
                            self.reply(conn, '554 DATA denied\r\n')
                            continue
                        self.reply(conn, '354 proceed\r\n')
                        body = b''
                        while True:
                            part = stream.readline(100000)
                            if not part:
                                break
                            if part == b'.\r\n':
                                break
                            if part.startswith(b'..'):
                                part = part[1:]
                            body += part
                        else:
                            raise AssertionError('unreachable')
                        if not part:
                            break
                        self.data_seen.set()
                        if self.mode == 'hold_final':
                            time.sleep(1)
                            break
                        if self.mode == 'final_fail':
                            self.reply(conn, '554 rejected body\r\n')
                        else:
                            self.deliveries.append((list(accepted), body))
                            if self.mode == 'drop_final':
                                break
                            self.reply(conn, '250 queued\r\n')
                    elif line == 'QUIT':
                        self.reply(conn, '221 bye\r\n')
                        break
                    else:
                        self.reply(conn, '500 unexpected command\r\n')
                stream.close()
            except (OSError, socket.timeout):
                pass
            finally:
                conn.close()


# IMAP APPEND is outside AM-01. A closed local port makes its existing quick
# check return immediately, without touching any real mailbox or server.
closed_socket = socket.socket()
closed_socket.bind(('127.0.0.1', 0))
IMAP_PORT = closed_socket.getsockname()[1]


def call(mode, server, fixture, extra=None, input_text=None, timeout=12, faults=None):
    env = os.environ.copy()
    env['HOME'] = fixture
    if faults:
        env['LD_PRELOAD'] = os.path.join(BUILD, 'am01_faults.so')
        env.update(faults)
    args = [DRIVER, mode, str(server.port), str(IMAP_PORT)] + (extra or [])
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env=env)
    try:
        output = p.communicate(None if input_text is None else input_text.encode('utf-8'), timeout=timeout)[0]
    except subprocess.TimeoutExpired:
        p.kill()
        output = p.communicate()[0]
        raise AssertionError('driver timeout: ' + output.decode('utf-8', 'replace'))
    if p.returncode:
        raise AssertionError('driver exit {}: {}'.format(p.returncode, output.decode('utf-8', 'replace')))
    return output.decode('utf-8', 'replace')


def mail(home, recipients='a@example.invalid,b@example.invalid'):
    folder = os.path.join(home, '.local/share/AstraMail/outbox', recipients)
    os.makedirs(folder)
    with open(os.path.join(folder, 'document.txt'), 'wb') as f:
        f.write(b'Only synthetic test data.\n')
    return folder


def records(folder):
    with open(os.path.join(folder, '.astramail-delivery-state'), 'r') as f:
        return f.read()


def compile_driver():
    libraries = sorted(glob.glob(os.path.join(BUILD, '*.a')) +
                       glob.glob(os.path.join(BUILD, 'src', '*', '*.a')))
    if len(libraries) != 8:
        raise AssertionError('Expected eight application libraries, got ' + repr(libraries))
    args = ['g++', '-std=gnu++17', '-O0', '-g', '-I' + SOURCE, '-I' + os.path.join(SOURCE, 'src'),
            '-I' + os.path.join(SOURCE, 'secrets'), os.path.join(SOURCE, 'tests/am01_driver.cpp'),
            '-Wl,--start-group'] + libraries + ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl',
                                               '-pthread', '-no-pie', '-o', DRIVER]
    subprocess.check_call(args)
    subprocess.check_call(['gcc', '-shared', '-fPIC', os.path.join(SOURCE, 'tests/am01_faults.c'),
                           '-ldl', '-o', os.path.join(BUILD, 'am01_faults.so')])


def main():
    compile_driver()
    server = SMTPServer()
    with tempfile.TemporaryDirectory(prefix='am01-fixtures-', dir=BUILD) as root:
        def fixture(name):
            folder = os.path.join(root, name)
            os.makedirs(folder)
            return folder

        home = fixture('auth')
        folder = mail(home)
        server.mode = 'auth_fail'
        out = call('outbox', server, home)
        check('ALL_COMPLETE=0' in out and os.path.exists(os.path.join(folder, 'document.txt')) and
              '535' in out and not server.deliveries, 'authentication failure preserves outbox and explains 535')

        for name, mode in [('sender', 'sender_fail'), ('data', 'data_fail'), ('final', 'final_fail')]:
            home = fixture(name)
            folder = mail(home)
            server.mode = mode
            out = call('outbox', server, home)
            check('ALL_COMPLETE=0' in out and os.path.exists(folder) and 'U ' not in records(folder),
                  name + ' rejection preserves all pending recipients')

        server.mode = 'normal'
        server.reject = {'a@example.invalid': 550, 'b@example.invalid': 450}
        home = fixture('all-rejected')
        folder = mail(home)
        before = len(server.deliveries)
        out = call('outbox', server, home)
        check('ALL_COMPLETE=0' in out and len(server.deliveries) == before and
              'U ' not in records(folder) and 'D ' not in records(folder),
              'all recipients rejected: no DATA, both recipients remain pending')
        server.reject = {'b@example.invalid': 550}
        home = fixture('partial')
        folder = mail(home)
        out = call('outbox', server, home)
        check('ALL_COMPLETE=0' in out and os.path.exists(folder) and
              'D "a@example.invalid"' in records(folder) and 'P "b@example.invalid"' in records(folder),
              'partial SMTP success durably records accepted and rejected recipients')
        check(server.deliveries[-1][0] == ['a@example.invalid'], 'DATA delivered only to accepted recipient')
        check((os.stat(os.path.join(folder, '.astramail-delivery-state')).st_mode & 0o777) == 0o600,
              'receipt file is private (0600)')
        flash = os.path.join(home, 'flash')
        new_folder = os.path.join(flash, 'a@example.invalid,b@example.invalid')
        os.makedirs(new_folder)
        with open(os.path.join(new_folder, 'new.txt'), 'w') as f:
            f.write('new synthetic attachment')
        call('import', server, home, [flash], input_text='\n')
        check(os.path.exists(os.path.join(new_folder, 'new.txt')) and
              not os.path.exists(os.path.join(folder, 'new.txt')),
              'import never mixes new payload into partially sent letter')
        server.reject = {}
        out = call('outbox', server, home)
        check('ALL_COMPLETE=1' in out and not os.path.exists(folder) and
              server.envelopes[-1] == ['b@example.invalid'], 'new process retries only rejected recipient, then removes outbox')

        home = fixture('save-failure')
        folder = mail(home, 'a@example.invalid')
        before = len(server.deliveries)
        out = call('outbox', server, home, faults={'AM01_FAIL_RECEIPT_RENAME_AT': '3'})
        check('ALL_COMPLETE=0' in out and len(server.deliveries) == before + 1 and
              'U "a@example.invalid"' in records(folder),
              'failure to commit acknowledgement preserves durable in-flight receipt and mail')
        connections = server.connections
        call('outbox', server, home)
        check(server.connections > connections and not os.path.exists(folder), 'uncommitted acknowledgement is automatically retried until confirmed')
        home = fixture('initial-save-failure')
        folder = mail(home, 'a@example.invalid')
        connections = server.connections
        out = call('outbox', server, home, faults={'AM01_FAIL_RECEIPT_RENAME_AT': '1'})
        check('ALL_COMPLETE=0' in out and server.connections == connections and os.path.exists(folder),
              'cannot save initial receipt: SMTP never starts')
        for action in ['confirm', 'retry']:
            home = fixture('unknown-' + action)
            folder = mail(home, 'a@example.invalid')
            server.mode = 'drop_final'
            before = len(server.deliveries)
            out = call('outbox', server, home)
            check('ALL_COMPLETE=0' in out and 'U "a@example.invalid"' in records(folder) and
                  len(server.deliveries) == before + 1, 'lost final response is unknown (' + action + ')')
            server.mode = 'normal'
            connections = server.connections
            if action == 'confirm':
                call('review', server, home, input_text='1\n1\n1\nyes\n\n')
            else:
                check(os.path.exists(folder), 'unknown delivery remains queued before automatic retry')
            out = call('outbox', server, home)
            check('ALL_COMPLETE=1' in out and not os.path.exists(folder) and
                  len(server.deliveries) == before + (1 if action == 'confirm' else 2),
                  'confirmation or automatic retry ' + action + ' completes the queue')

        home = fixture('mixed-unknown')
        folder = mail(home)
        server.mode = 'drop_final'
        server.reject = {'b@example.invalid': 450}
        call('outbox', server, home)
        check('U "a@example.invalid"' in records(folder) and 'P "b@example.invalid"' in records(folder),
              'uncertain accepted recipient and definite rejection remain distinct')
        server.mode = 'normal'
        server.reject = {}
        call('outbox', server, home)
        check(server.envelopes[-1] == ['a@example.invalid', 'b@example.invalid'] and not os.path.exists(folder),
              'unknown and rejected recipients both retry until confirmed')

        home = fixture('corrupt')
        folder = mail(home, 'a@example.invalid')
        with open(os.path.join(folder, '.astramail-delivery-state'), 'w') as f:
            f.write('corrupted\n')
        connections = server.connections
        out = call('outbox', server, home)
        check(server.connections == connections and os.path.exists(folder) and 'ALL_COMPLETE=0' in out,
              'corrupt receipt blocks sending and deletion')

        home = fixture('changed')
        folder = mail(home)
        server.reject = {'b@example.invalid': 550}
        call('outbox', server, home)
        with open(os.path.join(folder, 'document.txt'), 'ab') as f:
            f.write(b'changed')
        connections = server.connections
        out = call('outbox', server, home)
        check(server.connections == connections and os.path.exists(folder) and 'ALL_COMPLETE=0' in out,
              'changed payload does not reset receipts or delete new content')
        server.reject = {}

        home = fixture('raw')
        # Correct a permanently rejected address without resending to a delivered one.
        repair_home = fixture('repair')
        repair_folder = mail(repair_home)
        server.reject = {'b@example.invalid': 550}
        call('outbox', server, repair_home)
        call('review', server, repair_home, input_text='1\n1\n3\nc@example.invalid\nyes\n\n')
        server.reject = {}
        out = call('outbox', server, repair_home)
        check('ALL_COMPLETE=1' in out and not os.path.exists(repair_folder) and
              server.envelopes[-1] == ['c@example.invalid'],
              'repair rejected address without repeating delivery to accepted address')
        mime = os.path.join(home, 'synthetic.mime')
        with open(mime, 'wb') as f:
            f.write(b'Subject: dots\n\nfirst\r\n.\r\n..text\nlast')
        server.fragment = True
        out = call('smtp', server, home, [mime, 'a@example.invalid'])
        check('D=a@example.invalid' in out and server.deliveries[-1][1] ==
              b'Subject: dots\r\n\r\nfirst\r\n.\r\n..text\r\nlast\r\n',
              'fragmented multiline responses and dot-stuffed DATA preserve message')
        server.fragment = False
        server.mode = 'bare'
        out = call('smtp', server, home, [mime, 'a@example.invalid'])
        check('D=a@example.invalid' in out, 'bare SMTP reply codes without text are accepted')
        server.mode = 'normal'
        out = call('smtp', server, home, [mime, 'a@example.invalid\r\nNOOP'])
        check('P=' in out and 'Invalid recipient' in out, 'recipient CRLF rejected before SMTP envelope')
        with open(mime, 'wb') as f:
            f.write((b'.' + b'x' * 79 + b'\n') * 400)
        out = call('smtp', server, home, [mime, 'a@example.invalid'], faults={'AM01_SHORT_IO': '1'})
        check('D=a@example.invalid' in out and server.deliveries[-1][1] ==
              (b'.' + b'x' * 79 + b'\r\n') * 400,
              'short sends and EINTR preserve complete DATA across chunk boundaries')

        home = fixture('crash')
        folder = mail(home, 'a@example.invalid')
        server.mode = 'hold_final'
        server.data_seen.clear()
        env = os.environ.copy()
        env['HOME'] = home
        proc = subprocess.Popen([DRIVER, 'outbox', str(server.port), str(IMAP_PORT)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        try:
            check(server.data_seen.wait(8), 'crash test reached real SMTP DATA')
            connections = server.connections
            out = call('outbox', server, home)
            check(server.connections == connections and 'ALL_COMPLETE=0' in out,
                  'concurrent sender cannot transmit a letter locked by first sender')
            proc.kill()
            proc.communicate(timeout=4)
        finally:
            if proc.poll() is None:
                proc.kill()
        check('U "a@example.invalid"' in records(folder), 'SIGKILL leaves durable unknown state')
        server.mode = 'normal'
        connections = server.connections
        call('outbox', server, home)
        check(server.connections > connections and not os.path.exists(folder), 'restart after SIGKILL automatically retries until confirmed')

        home = fixture('crypto')
        check('REAL_SODIUM_ROUNDTRIP=1' in call('crypto', server, home, timeout=30),
              'installed real libsodium round trip uses only isolated synthetic secret')
        home = fixture('full-startup')
        call('seed', server, home, timeout=30)
        env = os.environ.copy()
        env['HOME'] = home
        app = subprocess.Popen([os.path.join(BUILD, 'AstraMail')], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        try:
            output = app.communicate(b'1\nsynthetic-pin-123456\n\n\n0\n', timeout=12)[0].decode('utf-8', 'replace')
        except subprocess.TimeoutExpired:
            app.kill()
            output = app.communicate()[0].decode('utf-8', 'replace')
            raise AssertionError('full startup timeout: ' + output)
        check(app.returncode == 0 and 'PIN принят' in output,
              'complete AstraMail binary starts with synthetic cache/PIN and exits normally')
    print('AM01_NATIVE_CHECKS={}/{}'.format(checks, checks), flush=True)


if __name__ == '__main__':
    main()
