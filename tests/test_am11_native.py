#!/usr/bin/env python3
"""EOF/error propagation through actual startup, menus, PIN and background SMTP."""
from __future__ import print_function
import glob
import os
import select
import subprocess
import tempfile
import threading
import time
import test_am01_native as old
import test_am03_native as network
import test_am06_native as terminal

SOURCE, BUILD = old.SOURCE, old.BUILD
DRIVER = os.path.join(BUILD, 'am11_driver')
APP = os.path.join(BUILD, 'AstraMail')
checks = 0


def check(value, label):
    global checks
    if not value:
        raise AssertionError(label)
    checks += 1
    print('PASS', label, flush=True)


def compile_driver():
    libs = sorted(glob.glob(os.path.join(BUILD, '*.a')) + glob.glob(os.path.join(BUILD, 'src/*/*.a')))
    subprocess.check_call(['g++', '-std=gnu++17', '-O0', '-g', '-I'+SOURCE,
                           '-I'+os.path.join(SOURCE, 'src'), '-I'+os.path.join(SOURCE, 'secrets'),
                           os.path.join(SOURCE, 'tests/am11_driver.cpp'), '-Wl,--start-group'] + libs +
                          ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', DRIVER])
    # The single-suite runner also works after a fresh build.
    for name in ['am01', 'am06']:
        path = os.path.join(BUILD, name+'_driver')
        if not os.path.exists(path):
            subprocess.check_call(['g++', '-std=gnu++17', '-O0', '-g', '-I'+SOURCE,
                                   '-I'+os.path.join(SOURCE, 'src'), '-I'+os.path.join(SOURCE, 'secrets'),
                                   os.path.join(SOURCE, 'tests/'+name+'_driver.cpp'), '-Wl,--start-group'] + libs +
                                  ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', path])


def call(args, home, text=b'', timeout=8):
    proc = subprocess.Popen(args, env=terminal.environment(home), stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        out = proc.communicate(text, timeout=timeout)[0].decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise AssertionError('EOF exit timed out: '+str(args))
    return proc.returncode, out


def seed(home, smtp, imap):
    code, out = call([os.path.join(BUILD, 'am01_driver'), 'seed', str(smtp.port), str(imap.port)], home)
    if code or 'REAL_SODIUM_ROUNDTRIP=1' not in out:
        raise AssertionError('synthetic seed failed: '+out)


def pipe_prompt(proc, text, timeout=10):
    data, deadline = b'', time.monotonic()+timeout
    while text.encode('utf-8') not in data:
        if time.monotonic() > deadline or proc.poll() is not None:
            raise AssertionError('pipe startup timeout: '+data.decode('utf-8', 'replace'))
        if select.select([proc.stdout], [], [], .1)[0]:
            data += os.read(proc.stdout.fileno(), 65536)
        if len(data) > 80000:
            raise AssertionError('pipe output is not bounded')
    return data


def snapshots(home):
    result = terminal.snapshots(home)
    for path in glob.glob(os.path.join(home, '.astramail/secret*')):
        if os.path.isfile(path):
            with open(path, 'rb') as file:
                result[path] = file.read()
    return result


def tty_exit(tty, data=b'\x04'):
    started = time.monotonic()
    tty.send(data)
    out = tty.finish(timeout=3)
    check(tty.proc.returncode == 0 and 'Ввод завершён.' in out, 'EOF reaches normal application exit')
    check(tty.restored(), 'EOF restores all terminal attributes')
    check(time.monotonic()-started < 2, 'EOF exit completes without menu retry delay')
    check(len(out) < 80000, 'EOF produces bounded output without repeated repainting')
    return out


def unlock(tty):
    tty.read_until('Выбор: ')
    tty.send('1\n')
    tty.read_until('Введите PIN: ')
    tty.send('synthetic-pin-123456\n')
    tty.read_until('Нажмите Enter')
    tty.send('\n')
    tty.read_until('Основное меню Astra_Mail_v3.3')


class HeldSMTP(old.SMTPServer):
    def __init__(self):
        self.release = threading.Event()
        super(HeldSMTP, self).__init__()

    def reply(self, conn, text):
        if text.startswith('250 queued'):
            self.release.wait(10)
        return super(HeldSMTP, self).reply(conn, text)


def main():
    compile_driver()
    idle = os.path.join(BUILD, 'idle_settings.conf')
    with tempfile.TemporaryDirectory(prefix='am11-fixtures-', dir=BUILD) as root:
        def fixture(name):
            path = os.path.join(root, name)
            os.makedirs(path)
            return path
        home = fixture('menu')
        for mode, data, eof in [('menu', b'', True), ('menu', b'1', True),
                                ('nested', b'1\n', True), ('nested', b'1\n1', True),
                                ('bad', b'', False)]:
            code, out = call([DRIVER, mode], home, data)
            check(code == 0 and 'RETURNED EOF='+str(int(eof)) in out,
                  '{} returns on closed/error input {}'.format(mode, repr(data)))
            check('ACTION_EXECUTED' not in out and len(out) < 5000,
                  'incomplete input executes no command and does not spin')
        for data in [b'\n0\n', b'abc\n0\n', b'1xyz\n0\n', b'9\n0\n']:
            code, out = call([DRIVER, 'menu'], home, data)
            check(code == 0 and 'RETURNED EOF=0' in out and 'ACTION_EXECUTED' not in out,
                  'complete blank/invalid choice preserves ordinary menu behavior '+repr(data))
        code, out = call([DRIVER, 'menu'], home, b'1\n0\n')
        check(code == 0 and out.count('ACTION_EXECUTED') == 1, 'complete command still executes once')
        for data, accepted in [(b'', False), (b'y', False), (b'yes', False), (b'\n', False),
                               (b'n\n', False), (b'y\n', True), (b' yes\n', True)]:
            code, out = call([DRIVER, 'confirm'], home, data)
            check(code == 0 and 'CONFIRMED='+str(int(accepted)) in out,
                  'confirmation requires a complete affirmative line '+repr(data))

        home = fixture('fresh')
        code, out = call([APP], home)
        check(code == 0 and 'Ввод завершён.' in out and len(out) < 15000,
              'fresh startup with closed pipe exits without profile/BOOT loop')
        # Reading a directory as stdin produces an actual read error (EISDIR).
        fd = os.open(home, os.O_RDONLY | os.O_DIRECTORY)
        try:
            proc = subprocess.Popen([APP], env=terminal.environment(home), stdin=fd,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            out = proc.communicate(timeout=3)[0].decode('utf-8', 'replace')
            check(proc.returncode == 1 and 'Ошибка чтения ввода.' in out and len(out) < 15000,
                  'actual stdin read error exits with status 1 instead of spinning')
        finally:
            os.close(fd)

        smtp, imap = old.SMTPServer(), network.IMAP()
        for name, choice, prompt, data in [
                ('security', None, 'Выбор: ', b'\x04'),
                ('pin-empty', '1\n', 'Введите PIN: ', b'\x04'),
                ('pin-partial', '1\n', 'Введите PIN: ', b'synthetic-partial\x04\x04')]:
            home = fixture(name)
            seed(home, smtp, imap)
            before = snapshots(home)
            connections = smtp.connections
            tty = terminal.Terminal('', home, app=True)
            try:
                tty.read_until('Выбор: ')
                if choice:
                    tty.send(choice)
                    tty.read_until(prompt)
                    check(tty.hidden(), 'secret input remains hidden before EOF')
                out = tty_exit(tty, data)
                check(snapshots(home) == before and smtp.connections == connections,
                      'canceled startup keeps original synthetic secret and opens no SMTP session')
                check('synthetic-partial' not in out, 'partial PIN is absent from terminal output')
            finally:
                tty.close()

        fields = [('Введите новый пароль почты: ', 'synthetic-new-password\n'),
                  ('Повторите пароль: ', 'synthetic-new-password\n'),
                  ('Введите новый PIN (без пробелов): ', 'synthetic-new-pin\n'),
                  ('Повторите PIN: ', 'synthetic-new-pin\n')]
        for stop_at in range(4):
            home = fixture('reset-'+str(stop_at))
            seed(home, smtp, imap)
            before = snapshots(home)
            tty = terminal.Terminal('', home, app=True)
            try:
                tty.read_until('Выбор: ')
                tty.send('2\n')
                for i, (prompt, answer) in enumerate(fields):
                    tty.read_until(prompt)
                    if i == stop_at:
                        check(tty.hidden(), 'reset field {} is hidden'.format(i+1))
                        tty_exit(tty, b'incomplete\x04\x04')
                        break
                    tty.send(answer)
                check(snapshots(home) == before, 'EOF at reset field {} saves no replacement secret'.format(stop_at+1))
            finally:
                tty.close()

        home = fixture('pin-pause')
        seed(home, smtp, imap)
        connections = smtp.connections
        tty = terminal.Terminal('', home, app=True)
        try:
            tty.read_until('Выбор: ')
            tty.send('1\n')
            tty.read_until('Введите PIN: ')
            tty.send('synthetic-pin-123456\n')
            tty.read_until('Нажмите Enter')
            tty_exit(tty)
            check(smtp.connections == connections, 'EOF after successful PIN skips startup server checks')
        finally:
            tty.close()

        home = fixture('legacy')
        code, out = call([os.path.join(BUILD, 'am06_driver'), 'seed-legacy'], home)
        check(code == 0 and 'SEEDED=1' in out, 'synthetic legacy secret fixture created')
        before = snapshots(home)
        code, out = call([APP], home, ('1\n'+terminal.OLD_PIN+'\nyes').encode('utf-8'))
        check(code == 0 and 'Ввод завершён.' in out and snapshots(home) == before,
              'incomplete yes at EOF cannot migrate or claim a legacy secret')

        with open(idle, 'w') as file:
            file.write('idle_threshold_seconds=300\ncheck_interval_seconds=30\n')
        try:
            home = fixture('closed-pipe')
            seed(home, smtp, imap)
            proc = subprocess.Popen([APP], env=terminal.environment(home), stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                proc.stdin.write(b'1\nsynthetic-pin-123456\n\n')
                proc.stdin.flush()
                data = pipe_prompt(proc, 'Основное меню Astra_Mail_v3.3')
                started = time.monotonic()
                proc.stdin.close()
                proc.stdin = None
                out = proc.communicate(timeout=3)[0]
                check(proc.returncode == 0 and 'Ввод завершён.'.encode('utf-8') in out
                      and time.monotonic()-started < 2 and len(data+out) < 80000,
                      'closing stdin pipe after startup stops full application and idle thread promptly')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()

            for name, navigate, prompt in [('main', '', 'Основное меню Astra_Mail_v3.3'),
                                           ('nested-outbox', '1\n4\n', 'Очистить исходящие')]:
                home = fixture(name)
                seed(home, smtp, imap)
                tty = terminal.Terminal('', home, app=True)
                try:
                    unlock(tty)
                    if navigate:
                        tty.send(navigate)
                        tty.read_until(prompt)
                    tty_exit(tty)
                finally:
                    tty.close()

            home = fixture('cleanup')
            seed(home, smtp, imap)
            folder = old.mail(home, 'a@example.invalid')
            tty = terminal.Terminal('', home, app=True)
            try:
                unlock(tty)
                tty.send('1\n4\n4\n')
                tty.read_until('Будут безвозвратно удалены')
                tty_exit(tty, b'y\x04\x04')
                check(os.path.exists(os.path.join(folder, 'document.txt')),
                      'partial destructive confirmation does not delete outbox letter')
            finally:
                tty.close()

            # Generate U using the real SMTP/pipeline, then reject incomplete manual D.
            home = fixture('review')
            failed = old.SMTPServer()
            failed.mode = 'drop_final'
            seed(home, failed, imap)
            folder = old.mail(home, 'a@example.invalid')
            code, out = call([os.path.join(BUILD, 'am01_driver'), 'outbox', str(failed.port), str(imap.port)], home)
            # Receipt pathname is shared with the existing integration suite.
            paths = glob.glob(os.path.join(folder, '.astramail-delivery*'))
            saved = {p: open(p, 'rb').read() for p in paths if os.path.isfile(p)}
            check(code == 0 and 'U "a@example.invalid"' in old.records(folder), 'unconfirmed delivery fixture created by actual pipeline')
            code, out = call([os.path.join(BUILD, 'am01_driver'), 'review', str(failed.port), str(imap.port)],
                             home, b'1\n1\n1\nyes')
            check(code == 0 and all(open(p, 'rb').read() == value for p, value in saved.items())
                  and 'U "a@example.invalid"' in old.records(folder), 'incomplete yes cannot mark unconfirmed mail delivered')

            # EOF while the real background is waiting for final SMTP acknowledgement.
            for name, busy in [('background', False), ('busy-ack', True)]:
                with open(idle, 'w') as file:
                    file.write('idle_threshold_seconds=1\ncheck_interval_seconds=1\n')
                held = HeldSMTP()
                home = fixture(name)
                seed(home, held, imap)
                folder = old.mail(home, 'a@example.invalid')
                tty = terminal.Terminal('', home, app=True)
                try:
                    unlock(tty)
                    check(held.data_seen.wait(8), 'actual background reached final SMTP response wait')
                    if busy:
                        tty.send('1\n4\n4\n')
                        tty.read_until('Команда не выполнена:')
                        tty.read_until('Нажмите Enter')
                    tty_exit(tty)
                    check(os.path.exists(os.path.join(folder, 'document.txt')) and 'U "a@example.invalid"' in old.records(folder),
                          'shutdown without SMTP acknowledgement preserves payload and U for automatic retry')
                    check(not os.path.exists('/proc/'+str(tty.proc.pid)) and not glob.glob('/tmp/astramail-mime-*'),
                          'shutdown finishes process/background and leaves no named temporary MIME')
                finally:
                    held.release.set()
                    tty.close()
                previous_deliveries = len(held.deliveries)
                code, out = call([os.path.join(BUILD, 'am01_driver'), 'outbox', str(held.port), str(imap.port)], home)
                check(code == 0 and 'ALL_COMPLETE=1' in out and not os.path.exists(folder)
                      and len(held.deliveries) == previous_deliveries+1,
                      'preserved letter is retried and removed after positive confirmation')
        finally:
            os.unlink(idle)
    print('AM11_NATIVE_CHECKS={}/{}'.format(checks, checks), flush=True)


if __name__ == '__main__':
    main()
