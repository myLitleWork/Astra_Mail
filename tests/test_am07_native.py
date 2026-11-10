#!/usr/bin/env python3
"""Strict independent IMAP parser + SMTP AUTH verifier, synthetic loopback data."""
from __future__ import print_function
import base64
import binascii
import glob
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import test_am06_native as terminal_tests

SOURCE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BUILD = os.path.abspath(sys.argv[1])
DRIVER = os.path.join(BUILD, 'am07_driver')
checks = 0


def check(value, name):
    global checks
    if not value:
        raise AssertionError(name)
    checks += 1
    print('PASS', name, flush=True)


def compile_driver():
    libs = sorted(glob.glob(os.path.join(BUILD, '*.a')) + glob.glob(os.path.join(BUILD, 'src/*/*.a')))
    subprocess.check_call(['g++', '-std=gnu++17', '-O0', '-g', '-I' + SOURCE,
                           '-I' + os.path.join(SOURCE, 'src'), '-I' + os.path.join(SOURCE, 'secrets'),
                           os.path.join(SOURCE, 'tests/am07_driver.cpp'), '-Wl,--start-group'] + libs +
                          ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', DRIVER])


class IMAP(object):
    def __init__(self, user, password, mode='normal', reject_literal=1, fragment=False):
        self.user, self.password = user, password
        self.mode, self.reject_literal, self.fragment = mode, reject_literal, fragment
        self.commands, self.logins, self.literals, self.headers = [], [], [], []
        self.kinds, self.appends, self.errors = [], [], []
        self.premature = b''
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(16)
        self.port = self.listener.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def send(self, conn, data):
        if self.fragment:
            for item in data:
                conn.sendall(bytes([item]))
        else:
            conn.sendall(data)

    def serve(self):
        while True:
            conn, _ = self.listener.accept()
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()

    def parse(self, conn, stream):
        line = stream.readline(100000)
        if not line:
            return None
        tokens, kinds = [], []
        i = 0
        while True:
            if not line.endswith(b'\r\n'):
                raise ValueError('missing command CRLF')
            if i >= len(line)-2:
                return tokens, kinds
            if line[i:i+1] == b' ':
                i += 1
                continue
            if line[i:i+1] == b'"':
                i += 1
                value = bytearray()
                while True:
                    if i >= len(line)-2:
                        raise ValueError('unterminated quoted argument')
                    c = line[i]
                    i += 1
                    if c == 34:
                        break
                    if c == 92:
                        c = line[i]
                        i += 1
                        if c not in (34, 92):
                            raise ValueError('invalid quoted escape')
                    if c < 32 or c >= 127:
                        raise ValueError('nonprintable or 8-bit quoted argument')
                    value.append(c)
                tokens.append(bytes(value))
                kinds.append('quoted')
            elif line[i:i+1] == b'{':
                match = re.match(br'\{([0-9]+)\}\r\n$', line[i:])
                if not match:
                    raise ValueError('bad literal marker')
                size = int(match.group(1))
                self.headers.append((tokens[1], size))
                count = sum(kind == 'literal' for kind in kinds)+1
                if self.mode != 'normal' and count == self.reject_literal:
                    if self.mode == 'stall':
                        time.sleep(0.8)
                    elif self.mode == 'drop':
                        return None
                    elif self.mode == 'early-ok':
                        self.send(conn, tokens[0]+b' OK unexpected completion\r\n')
                    elif self.mode == 'bye':
                        self.send(conn, b'* BYE synthetic closure\r\n')
                    else:
                        self.send(conn, tokens[0]+b' NO literal denied\r\n')
                    self.premature += conn.recv(65536)
                    return None
                self.send(conn, b'* CAPABILITY IMAP4rev1\r\n+ literal permitted\r\n')
                value = stream.read(size)
                if len(value) != size:
                    raise ValueError('incomplete literal')
                self.literals.append(value)
                tokens.append(value)
                kinds.append('literal')
                line = stream.readline(100000)
                i = 0
            else:
                end = i
                while end < len(line)-2 and line[end:end+1] != b' ':
                    end += 1
                tokens.append(line[i:end])
                kinds.append('atom')
                i = end

    def handle(self, conn):
        try:
            conn.settimeout(4)
            stream = conn.makefile('rb')
            self.send(conn, b'* OK synthetic strict IMAP\r\n')
            authenticated = False
            while True:
                parsed = self.parse(conn, stream)
                if parsed is None:
                    break
                tokens, kinds = parsed
                if len(tokens) < 2:
                    raise ValueError('missing command')
                tag, command = tokens[:2]
                self.commands.append(tokens)
                if command == b'LOGIN':
                    self.logins.append(tokens[2:])
                    self.kinds.append(kinds[2:])
                    authenticated = tokens[2:] == [self.user, self.password]
                    if authenticated:
                        self.send(conn, tag+b' OK authenticated\r\n')
                    else:
                        # Deliberately echoes a credential: the client must not log it.
                        self.send(conn, tag+b' NO rejected '+tokens[-1]+b'\r\n')
                elif not authenticated:
                    self.send(conn, tag+b' BAD not authenticated\r\n')
                elif command == b'LIST':
                    self.send(conn, b'* LIST () "/" "INBOX"\r\n'+tag+b' OK list\r\n')
                elif command == b'SELECT':
                    self.send(conn, tag+b' OK selected\r\n')
                elif command == b'UID' and tokens[2] == b'SEARCH':
                    self.send(conn, b'* SEARCH 1\r\n'+tag+b' OK search\r\n')
                elif command == b'UID' and tokens[2] == b'FETCH':
                    body = b'synthetic body\r\n'
                    self.send(conn, b'* 1 FETCH (BODY[] {'+str(len(body)).encode('ascii')+b'}\r\n'+body+b')\r\n'+tag+b' OK fetch\r\n')
                elif command == b'UID' and tokens[2] == b'STORE':
                    self.send(conn, tag+b' OK stored\r\n')
                elif command == b'APPEND':
                    self.appends.append(tokens[-1])
                    self.send(conn, tag+b' OK appended\r\n')
                elif command == b'LOGOUT':
                    self.send(conn, b'* BYE\r\n'+tag+b' OK logout\r\n')
                    break
                else:
                    raise ValueError('unexpected command')
            stream.close()
        except (OSError, socket.timeout):
            pass
        except Exception as error:
            self.errors.append(type(error).__name__)
        finally:
            conn.close()


class SMTP(object):
    def __init__(self, user, password):
        self.user, self.password = user, password
        self.attempts = []
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(16)
        self.port = self.listener.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            conn, _ = self.listener.accept()
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()

    def handle(self, conn):
        try:
            conn.settimeout(4)
            stream = conn.makefile('rb')
            conn.sendall(b'220 synthetic strict SMTP\r\n')
            while True:
                line = stream.readline().rstrip(b'\r\n')
                if line.startswith(b'EHLO '):
                    conn.sendall(b'250-synthetic\r\n250 AUTH LOGIN\r\n')
                elif line == b'AUTH LOGIN':
                    conn.sendall(b'334 username\r\n')
                    user = base64.b64decode(stream.readline().strip(), validate=True)
                    conn.sendall(b'334 password\r\n')
                    password = base64.b64decode(stream.readline().strip(), validate=True)
                    self.attempts.append((user, password))
                    conn.sendall(b'235 accepted\r\n' if (user, password) == (self.user, self.password) else b'535 denied\r\n')
                elif line == b'QUIT':
                    conn.sendall(b'221 bye\r\n')
                    break
                elif not line:
                    break
                else:
                    conn.sendall(b'500 unexpected\r\n')
            stream.close()
        except (OSError, ValueError):
            pass
        finally:
            conn.close()


def call(mode, home, imap, smtp, user, password, folder=b'INBOX', faults=False):
    env = os.environ.copy()
    env['HOME'] = home
    if faults:
        env['LD_PRELOAD'] = os.path.join(BUILD, 'am01_faults.so')
        env['AM01_SHORT_IO'] = '1'
    data = b'\n'.join(binascii.hexlify(value) for value in [user, password, folder])+b'\n'
    proc = subprocess.Popen([DRIVER, mode, str(imap.port), str(smtp.port)], env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        out = proc.communicate(data, timeout=15)[0].decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise AssertionError('synthetic authentication timed out')
    if proc.returncode:
        raise AssertionError('native driver failed: '+out)
    return out


def main():
    compile_driver()
    default_user, default_pass = b'sender@example.invalid', b'synthetic-password'
    with tempfile.TemporaryDirectory(prefix='am07-fixtures-', dir=BUILD) as root:
        def fixture(name):
            home = os.path.join(root, name)
            os.makedirs(home)
            return home
        home = fixture('protocol')
        cases = [('plain', default_user, default_pass),
                 ('spaces', default_user, b'  synthetic pass word  '),
                 ('quotes', default_user, b'synthetic "quoted" password'),
                 ('backslashes', default_user, b'synthetic\\path\\tail\\'),
                 ('punctuation', default_user, b'{}[]()%*+!;:<>='),
                 ('command-looking text', default_user, b'" A9 NOOP {99} \\'),
                 ('UTF8 password', default_user, 'Синтетический-пароль'.encode('utf-8')),
                 ('mixed UTF8 password', default_user, '  тест "пароль" \\ конец  '.encode('utf-8')),
                 ('tab password', default_user, b'synthetic\tpassword'),
                 ('spaced username', b'  synthetic user  ', b'synthetic password'),
                 ('UTF8 username', 'тест@example.invalid'.encode('utf-8'), b'synthetic "password"'),
                 ('two literals', 'тест@example.invalid'.encode('utf-8'), 'пароль тест'.encode('utf-8'))]
        for name, user, password in cases:
            imap, smtp = IMAP(user, password, fragment=True), SMTP(user, password)
            out = call('login', home, imap, smtp, user, password)
            expected_kinds = ['quoted' if all(32 <= byte < 127 for byte in value) else 'literal' for value in (user,password)]
            check('OK=1' in out and imap.logins == [[user,password]] and imap.kinds == [expected_kinds] and not imap.errors,
                  'strict IMAP authenticates exact '+name)
            out = call('smtp', home, imap, smtp, user, password)
            check('OK=1' in out and smtp.attempts == [(user,password)], 'SMTP AUTH preserves exact '+name)

        imap, smtp = IMAP(default_user,default_pass), SMTP(default_user,default_pass)
        wrong = b'synthetic-wrong-"password"'
        out = call('login', home, imap, smtp, default_user, wrong)
        check('OK=0' in out and 'NO' in out and wrong.decode() not in out,
              'wrong IMAP password is rejected and echoed credential is absent from error output')
        out = call('smtp', home, imap, smtp, default_user, wrong)
        check('OK=0' in out and '535' in out and wrong.decode() not in out, 'wrong SMTP password is rejected')

        password = '  тест "пароль" \\ конец  '.encode('utf-8')
        folder = b'Folder "quoted" \\ child'
        for mode in ['list', 'legacy-list', 'select', 'select-api', 'search', 'fetch', 'mark', 'append']:
            imap, smtp = IMAP(default_user,password), SMTP(default_user,password)
            out = call(mode, home, imap, smtp, default_user, password, folder)
            check('OK=1' in out and imap.logins == [[default_user,password]] and not imap.errors,
                  'actual '+mode+' API authenticates special password')
            selected = [command[2] for command in imap.commands if command[1] == b'SELECT']
            if mode in ['select','select-api','search','fetch','mark']:
                check(selected == [folder], mode+' preserves exact quoted folder name')
            if mode == 'append':
                check(imap.appends == [b'Subject: synthetic\r\n\r\nsynthetic body\r\n'], 'APPEND payload is distinct from LOGIN literal and remains exact')
        imap, smtp = IMAP(default_user,password), SMTP(default_user,password)
        raw_folder = 'Папка тест'.encode('utf-8')
        out = call('select', home, imap, smtp, default_user, password, raw_folder)
        check('OK=1' in out and [cmd[2] for cmd in imap.commands if cmd[1]==b'SELECT']==[raw_folder],
              'literal fallback preserves non-ASCII string argument bytes')

        for value, name in [(b'bad\rA9 NOOP','CR'),(b'bad\nA9 NOOP','LF'),(b'bad\0tail','NUL')]:
            for field in ['password','username','folder']:
                user = 'тест'.encode('utf-8') if field=='password' else default_user
                supplied = [user,default_pass,b'INBOX']
                supplied[['username','password','folder'].index(field)] = value
                imap, smtp = IMAP(user,default_pass), SMTP(user,default_pass)
                out = call('select' if field=='folder' else 'login', home, imap, smtp, *supplied)
                forbidden = [cmd for cmd in imap.commands if cmd[1] in (b'NOOP',b'SELECT')]
                check('OK=0' in out and 'CR/LF/NUL' in out and not forbidden and
                      (field=='folder' or (not imap.logins and not imap.headers)),
                      name+' in '+field+' rejected before affected command; no injected command or partial LOGIN')

        user = 'тест'.encode('utf-8')
        password = 'пароль'.encode('utf-8')
        for mode, at in [('deny',1),('deny',2),('early-ok',1),('bye',1),('drop',1),('stall',1)]:
            imap, smtp = IMAP(user,password,mode=mode,reject_literal=at), SMTP(user,password)
            started = time.time()
            out = call('login', home, imap, smtp, user, password)
            check('OK=0' in out and not imap.logins and len(imap.literals)==at-1 and not imap.premature and time.time()-started < 1.5,
                  'literal '+mode+' at stage '+str(at)+' sends no unpermitted bytes and exits promptly')
        imap, smtp = IMAP(user,password,fragment=True), SMTP(user,password)
        out = call('login', home, imap, smtp, user, password, faults=True)
        check('OK=1' in out and imap.logins == [[user,password]], 'short send/recv and EINTR preserve both LOGIN literals')
        imap, smtp = IMAP(default_user,default_pass), SMTP(default_user,default_pass)
        for uid in [b'0',b'4294967296',b'1:2',b'1\r\nA9 NOOP',b'1\0tail']:
            for mode in ['uid-fetch','uid-store']:
                out = call(mode,home,imap,smtp,default_user,default_pass,uid)
                check('OK=0' in out and 'invalid UID' in out and not any(cmd[1] in (b'UID',b'NOOP') for cmd in imap.commands),
                      mode+' rejects invalid numeric token without command injection')

        for name, password in [('quoted',b'  synthetic "pass" \\ tail  '),('UTF8','  тест "пароль" \\ конец  '.encode('utf-8'))]:
            home = fixture('vault-'+name)
            imap, smtp = IMAP(default_user,password), SMTP(default_user,password)
            out = call('stored',home,imap,smtp,default_user,password)
            check('OK=1' in out and imap.logins==[[default_user,password]] and len(imap.appends)==1,
                  'real encrypted vault round trip authenticates '+name+' password and saves Sent copy')

            home = fixture('full-'+name)
            check('OK=1' in call('seed',home,imap,smtp,default_user,password), 'seed synthetic full-app '+name+' fixture')
            tty = terminal_tests.Terminal('',home,app=True)
            try:
                tty.read_until('Выбор: ')
                tty.send('1\n')
                tty.read_until('Введите PIN: ')
                tty.send('synthetic-am07-pin\n')
                tty.read_until('Нажмите Enter')
                tty.send('\n')
                tty.read_until('Основное меню Astra_Mail_v3.3')
                tty.send('0\n')
                out = tty.finish()
                check(tty.proc.returncode==0 and len(imap.logins)==2 and smtp.attempts==[(default_user,password)] and
                      '[Старт] Нет соединения' not in out and password.decode('utf-8') not in out and tty.restored(),
                      'complete app unlocks vault, passes strict IMAP/SMTP authentication and exits: '+name)
            finally:
                tty.close()
    print('AM07_NATIVE_CHECKS={}/{}'.format(checks, checks), flush=True)


if __name__ == '__main__':
    main()
