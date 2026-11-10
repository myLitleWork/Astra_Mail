#!/usr/bin/env python3
"""Real Linux PTYs, real commands/libsodium; synthetic inputs only, Python 3.5+."""
from __future__ import print_function
import fcntl
import glob
import os
import pty
import resource
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time

SOURCE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BUILD = os.path.abspath(sys.argv[1])
DRIVER = os.path.join(BUILD, 'am06_driver')
OLD_PIN = '  synthetic legacy pin with spaces  '
NEW_PIN = 'synthetic-new-pin'
PASSWORD = '  synthetic mail password with spaces  '
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
                           os.path.join(SOURCE, 'tests/am06_driver.cpp'), '-Wl,--start-group'] + libs +
                          ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', DRIVER])


def environment(home, extra=None):
    env = os.environ.copy()
    env['HOME'] = home
    env.update(extra or {})
    return env


def call(mode, home, text='', extra=None):
    proc = subprocess.Popen([DRIVER, mode], env=environment(home, extra), stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        out = proc.communicate(text.encode('utf-8'), timeout=20)[0]
    except subprocess.TimeoutExpired:
        proc.kill()
        raise AssertionError('pipe test timed out')
    if proc.returncode:
        raise AssertionError('driver exit {}: {}'.format(proc.returncode, out.decode('utf-8', 'replace')))
    return out.decode('utf-8', 'replace')


class Terminal(object):
    def __init__(self, mode, home, initial_echo=True, extra=None, app=False):
        self.master, self.slave = pty.openpty()
        attrs = termios.tcgetattr(self.slave)
        if initial_echo:
            attrs[3] |= termios.ECHO | termios.ECHONL
        else:
            attrs[3] &= ~(termios.ECHO | termios.ECHONL)
        termios.tcsetattr(self.slave, termios.TCSANOW, attrs)
        self.original = termios.tcgetattr(self.slave)
        def session():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        self.proc = subprocess.Popen([os.path.join(BUILD, 'AstraMail')] if app else [DRIVER, mode],
                                     env=environment(home, extra), stdin=self.slave, stdout=self.slave,
                                     stderr=self.slave, preexec_fn=session, close_fds=True)
        self.data = b''
        self.cursor = 0

    def read_until(self, text, timeout=12):
        wanted = text.encode('utf-8')
        deadline = time.time() + timeout
        while wanted not in self.data[self.cursor:]:
            if time.time() > deadline:
                raise AssertionError('PTY prompt timeout: ' + text)
            if select.select([self.master], [], [], 0.1)[0]:
                self.data += os.read(self.master, 65536)
            elif self.proc.poll() is not None:
                raise AssertionError('PTY exited before prompt: ' + text)
        self.cursor = self.data.index(wanted, self.cursor) + len(wanted)

    def hidden(self):
        return not (termios.tcgetattr(self.slave)[3] & (termios.ECHO | termios.ECHONL))

    def restored(self):
        return termios.tcgetattr(self.slave) == self.original

    def send(self, value):
        os.write(self.master, value.encode('utf-8') if isinstance(value, str) else value)

    def finish(self, timeout=12):
        deadline = time.time() + timeout
        while self.proc.poll() is None:
            if time.time() > deadline:
                self.proc.kill()
                raise AssertionError('PTY exit timeout')
            if select.select([self.master], [], [], 0.05)[0]:
                self.data += os.read(self.master, 65536)
        while select.select([self.master], [], [], 0)[0]:
            self.data += os.read(self.master, 65536)
        return self.data.decode('utf-8', 'replace')

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        os.close(self.master)
        os.close(self.slave)


def snapshots(home):
    result = {}
    for path in glob.glob(os.path.join(home, '.astramail/accounts/*.bin')):
        with open(path, 'rb') as file:
            result[path] = file.read()
    return result


def main():
    compile_driver()
    with tempfile.TemporaryDirectory(prefix='am06-fixtures-', dir=BUILD) as root:
        def fixture(name):
            path = os.path.join(root, name)
            os.makedirs(path)
            return path

        home = fixture('input')
        tty = Terminal('read', home)
        try:
            tty.read_until('SECRET> ')
            check(tty.hidden(), 'ECHO and ECHONL disabled before prompt is shown')
            tty.send('  synthetic value \t tail  \n')
            out = tty.finish()
            check('READ_OK=1' in out and 'EXACT=1' in out and 'synthetic value' not in out,
                  'hidden input preserves leading/internal/trailing whitespace without echo')
            check(tty.restored(), 'all original terminal attributes restored after successful input')
        finally:
            tty.close()

        tty = Terminal('read', home, initial_echo=False)
        try:
            tty.read_until('SECRET> ')
            tty.send('synthetic\n')
            tty.finish()
            check(tty.restored(), 'initially disabled echo stays disabled after input')
        finally:
            tty.close()

        for mode, data, expected, name in [
                ('read', b'\x04', 'READ_OK=0 CLEARED=1', 'empty EOF'),
                ('read', b'synthetic-partial\x04\x04', 'READ_OK=0 CLEARED=1', 'partial EOF'),
                ('read', b'\n', 'READ_OK=0 CLEARED=1', 'empty line'),
                ('short', b'123456789\n', 'READ_OK=0 CLEARED=1', 'oversized line'),
                ('throw', b'\x04', 'EXCEPTION=1', 'istream exception')]:
            tty = Terminal(mode, home)
            try:
                tty.read_until('SECRET> ')
                tty.send(data)
                out = tty.finish()
                check(expected in out and tty.restored(), name + ' clears secret and restores terminal')
            finally:
                tty.close()

        for sig in [signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT]:
            tty = Terminal('read', home)
            try:
                tty.read_until('SECRET> ')
                tty.send('synthetic-unfinished')
                os.kill(tty.proc.pid, sig)
                tty.finish()
                check(tty.proc.returncode == -sig and tty.restored(),
                      'terminating signal {} retains exit semantics and restores terminal'.format(sig))
            finally:
                tty.close()
        tty = Terminal('thread', home)
        try:
            tty.read_until('SECRET> ')
            tty.finish()
            check(tty.proc.returncode == -signal.SIGTERM and tty.restored(),
                  'termination delivered to another thread also restores terminal')
        finally:
            tty.close()

        tty = Terminal('read', home, extra={'LD_PRELOAD': os.path.join(BUILD, 'am01_faults.so'), 'AM06_FAIL_TTY_SET': '1'})
        try:
            out = tty.finish()
            check('READ_OK=0 CLEARED=1' in out and 'SECRET> ' not in out and tty.restored(),
                  'cannot disable echo: fail without requesting visible secret')
        finally:
            tty.close()
        check('EXACT=1' in call('read', home, '  synthetic value \t tail  \n'), 'redirected input uses the same exact-line semantics')
        check('READ_OK=0 CLEARED=1' in call('read', home, 'synthetic-partial'), 'incomplete redirected input is rejected and cleared')

        for name, pin in [('space', 'a b'), ('leading-space', ' abc'), ('trailing-space', 'abc '),
                          ('tab', 'a\tb'), ('NUL', 'a\0b'), ('NBSP', 'a\u00a0b'), ('em-space', 'a\u2003b')]:
            check('VALID=0' in call('policy', home, pin+'\n'), 'new PIN policy rejects ' + name)
        check('VALID=1' in call('policy', home, 'Синтетический-PIN_123\n'), 'nonspace UTF-8 PIN remains supported')

        home = fixture('bound-old')
        check('SEEDED=1' in call('seed-old', home), 'synthetic old bound record with spaces created by real libsodium')
        tty = Terminal('enter', home)
        try:
            tty.read_until('Введите PIN: ')
            check(tty.hidden(), 'existing PIN entry is hidden')
            tty.send(OLD_PIN+'\n')
            tty.read_until('Нажмите Enter')
            check(tty.restored(), 'terminal echo restored before ordinary pause')
            tty.send('\n')
            out = tty.finish()
            check('UNLOCKED=1 EXACT=1' in out and OLD_PIN not in out,
                  'existing bound PIN with leading/internal/trailing spaces unlocks exactly')
        finally:
            tty.close()
        check('UNLOCKED=0 EXACT=0' in call('enter', home, 'synthetic legacy pin with spaces\n'),
              'legacy PIN is not silently trimmed or accepted with missing spaces')

        before = snapshots(home)
        for pin in ['bad pin', 'bad\tpin', 'bad\u00a0pin']:
            tty = Terminal('reset', home)
            try:
                tty.read_until('Введите новый пароль почты: ')
                tty.send(PASSWORD+'\n')
                tty.read_until('Повторите пароль: ')
                tty.send(PASSWORD+'\n')
                tty.read_until('Введите новый PIN (без пробелов): ')
                tty.send(pin+'\n')
                tty.read_until('Новый PIN не должен содержать')
                check(tty.restored(), 'invalid new PIN restores terminal before error/pause')
                tty.send('\n')
                out = tty.finish()
                check('UNLOCKED=0 EXACT=0' in out and PASSWORD not in out and snapshots(home) == before,
                      'invalid new PIN does not change encrypted old record or unlock stale memory')
            finally:
                tty.close()
        check('LOADED=1 EXACT=1' in call('check-old', home), 'old spaced PIN remains usable after rejected resets')

        tty = Terminal('reset', home)
        try:
            for prompt, value in [('Введите новый пароль почты: ', PASSWORD), ('Повторите пароль: ', PASSWORD),
                                  ('Введите новый PIN (без пробелов): ', NEW_PIN), ('Повторите PIN: ', NEW_PIN)]:
                tty.read_until(prompt)
                check(tty.hidden(), 'reset hides ' + prompt.rstrip(': '))
                tty.send(value+'\n')
            tty.read_until('Нажмите Enter')
            tty.send('\n')
            out = tty.finish()
            check('UNLOCKED=1 EXACT=1' in out and PASSWORD not in out and NEW_PIN not in out and tty.restored(),
                  'valid reset hides all four inputs and retains password whitespace')
        finally:
            tty.close()
        check('LOADED=1 EXACT=1' in call('check-new', home) and 'LOADED=0' in call('check-old', home),
              'valid new PIN persists across restart and replaces only old PIN')
        before = snapshots(home)
        for text, label in [('', 'EOF before password'), ('synthetic\nother\n\n', 'password mismatch'),
                             (PASSWORD+'\n'+PASSWORD+'\n'+NEW_PIN+'\nother\n\n', 'PIN mismatch')]:
            out = call('reset', home, text)
            check('UNLOCKED=0 EXACT=0' in out and snapshots(home) == before, label + ' preserves saved record and clears stale memory')

        home = fixture('legacy-v1')
        call('seed-legacy', home)
        path = os.path.join(home, '.astramail/secret.bin')
        with open(path, 'rb') as file:
            legacy_before = file.read()
        tty = Terminal('enter', home)
        try:
            tty.read_until('Введите PIN: ')
            tty.send(OLD_PIN+'\n')
            tty.read_until('Для привязки введите yes: ')
            check(tty.restored(), 'migration confirmation uses normal terminal echo after hidden PIN')
            tty.send('yes\n')
            tty.read_until('Нажмите Enter')
            tty.send('\n')
            out = tty.finish()
            with open(path, 'rb') as file:
                unchanged = file.read() == legacy_before
            check('UNLOCKED=1 EXACT=1' in out and OLD_PIN not in out and unchanged,
                  'v1 PIN with spaces migrates only after explicit confirmation; original file preserved')
        finally:
            tty.close()
        check('LOADED=1 EXACT=1' in call('check-old', home), 'migrated spaced PIN remains usable in new bound format')

        home = fixture('full-app')
        call('seed-old', home)
        tty = Terminal('', home, app=True)
        try:
            tty.read_until('Выбор: ')
            tty.send('1\n')
            tty.read_until('Введите PIN: ')
            check(tty.hidden(), 'complete application disables echo for startup PIN')
            tty.send(OLD_PIN+'\n')
            tty.read_until('Нажмите Enter')
            tty.send('\n')
            tty.read_until('[Старт] Нет соединения')
            tty.send('\n')
            tty.read_until('Основное меню Astra_Mail_v3.3')
            tty.send('0\n')
            out = tty.finish()
            check(tty.proc.returncode == 0 and 'PIN принят' in out and OLD_PIN not in out and tty.restored(),
                  'complete application unlocks spaced PIN through PTY and exits normally')
        finally:
            tty.close()
    print('AM06_NATIVE_CHECKS={}/{}'.format(checks, checks), flush=True)


if __name__ == '__main__':
    main()
