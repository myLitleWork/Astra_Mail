#!/usr/bin/env python3
"""Python 3.5; synthetic loopback peers in a private mount/network namespace."""
from __future__ import print_function
import glob
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import test_am01_native as previous

SOURCE = previous.SOURCE
BUILD = previous.BUILD
DRIVER = os.path.join(BUILD, 'am03_driver')
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
                           os.path.join(SOURCE, 'tests/am03_driver.cpp'), '-Wl,--start-group'] + libs +
                          ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', DRIVER])


def drop(port, enabled, destination=False):
    subprocess.check_call(['sudo', '-n', '/sbin/iptables', '-A' if enabled else '-D', 'OUTPUT',
                           '-o', 'lo', '-p', 'tcp', '--dport' if destination else '--sport',
                           str(port), '-j', 'DROP'])


class SMTP(previous.SMTPServer):
    def __init__(self, final_delay=0, greeting_delay=0, blackhole=False):
        self.final_delay = final_delay
        self.greeting_delay = greeting_delay
        self.blackhole = blackhole
        self.rule_active = False
        self.blocked = threading.Event()
        super(SMTP, self).__init__()

    def reply(self, conn, text):
        if text.startswith('220 ') and self.greeting_delay:
            time.sleep(self.greeting_delay)
        if text.startswith('250 queued'):
            if self.blackhole and not self.rule_active:
                drop(self.port, True)
                self.rule_active = True
                self.blocked.set()
            if self.final_delay:
                time.sleep(self.final_delay)
        return super(SMTP, self).reply(conn, text)

    def restore(self):
        if self.rule_active:
            drop(self.port, False)
            self.rule_active = False
        self.blackhole = False


class IMAP(object):
    body = b'Subject: synthetic\r\n\r\n' + b'x' * 20000

    def __init__(self, mode='normal', delay=0):
        self.mode, self.delay = mode, delay
        self.appends = []
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(32)
        self.port = self.listener.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            conn, _ = self.listener.accept()
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()

    def handle(self, conn):
        try:
            conn.settimeout(8)
            stream = conn.makefile('rb')
            if self.mode == 'banner-stall':
                time.sleep(3)
                return
            conn.sendall(b'* OK synthetic IMAP\r\n')
            while True:
                raw = stream.readline(100000)
                if not raw:
                    break
                tag, _, command = raw.decode('ascii', 'replace').strip().partition(' ')
                tag = tag.encode('ascii')
                if command.startswith('LOGIN '):
                    conn.sendall(tag + b' OK login\r\n')
                elif command.startswith('LIST '):
                    if self.delay:
                        time.sleep(self.delay)
                    if self.mode == 'list-no':
                        conn.sendall(tag + b' NO synthetic denied\r\n')
                    else:
                        conn.sendall(b'* LIST () "/" "INBOX"\r\n' + tag + b' OK list\r\n')
                elif command.startswith('SELECT '):
                    conn.sendall(tag + b' OK selected\r\n')
                elif command.startswith('UID SEARCH'):
                    conn.sendall(b'* SEARCH\r\n' + tag + b' OK search\r\n')
                elif command.startswith('UID FETCH'):
                    if self.mode == 'fetch-no':
                        conn.sendall(tag + b' NO synthetic denied\r\n')
                        continue
                    conn.sendall(b'* 1 FETCH (BODY[] {' + str(len(self.body)).encode('ascii') + b'}\r\n')
                    for offset in range(0, len(self.body), 2000):
                        conn.sendall(self.body[offset:offset+2000])
                        if self.delay:
                            time.sleep(self.delay)
                    conn.sendall(b')\r\n' + tag + b' OK fetched\r\n')
                elif command.startswith('APPEND '):
                    if self.mode == 'append-no':
                        conn.sendall(tag + b' NO synthetic denied\r\n')
                        continue
                    size = int(re.search(r'\{(\d+)\}', command).group(1))
                    conn.sendall(b'+ proceed\r\n')
                    body = stream.read(size)
                    stream.read(2)
                    self.appends.append(body)
                    if self.delay:
                        time.sleep(self.delay)
                    conn.sendall(tag + b' OK appended\r\n')
                elif command == 'LOGOUT':
                    conn.sendall(b'* BYE\r\n' + tag + b' OK logout\r\n')
                    break
                else:
                    conn.sendall(tag + b' BAD unexpected\r\n')
            stream.close()
        except (OSError, socket.timeout):
            pass
        finally:
            conn.close()


def launch(mode, smtp, imap, home, extra=None, settings=None):
    env = os.environ.copy()
    env['HOME'] = home
    env.update(settings or {})
    if env.get('AM03_SLOW_DNS'):
        env['LD_PRELOAD'] = os.path.join(BUILD, 'am01_faults.so')
    return subprocess.Popen([DRIVER, mode, str(smtp.port), str(imap.port)] + (extra or []),
                            env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def finish(proc, timeout=10):
    try:
        out = proc.communicate(timeout=timeout)[0].decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        proc.kill()
        raise AssertionError('timeout ' + proc.communicate()[0].decode('utf-8', 'replace'))
    if proc.returncode:
        raise AssertionError('driver failed: ' + out)
    return out


def elapsed(out):
    return int(re.search(r'ELAPSED_MS=(\d+)', out).group(1))


def main():
    compile_driver()
    smtp, imap = SMTP(), IMAP()
    with tempfile.TemporaryDirectory(prefix='am03-fixtures-', dir=BUILD) as root:
        def fixture(name):
            home = os.path.join(root, name)
            os.makedirs(home)
            return home

        home = fixture('imap')
        out = finish(launch('imap-list', smtp, imap, home))
        check('IMAP_OK=1 FOLDERS=1' in out, 'actual IMAP LIST succeeds')
        peer = IMAP(mode='list-no')
        out = finish(launch('imap-list', smtp, peer, home))
        check('IMAP_OK=0' in out and elapsed(out) < 400 and 'synthetic denied' in out, 'IMAP NO fails immediately with reason')
        peer = IMAP(delay=0.09)
        out = finish(launch('imap-fetch', smtp, peer, home))
        check('IMAP_OK=1 SIZE={}'.format(len(peer.body)) in out and elapsed(out) > 700,
              'progressing FETCH exceeds command/idle duration without premature timeout')
        peer = IMAP(delay=0.6)
        out = finish(launch('imap-fetch', smtp, peer, home))
        check('IMAP_OK=0' in out and elapsed(out) < 900, 'stalled IMAP literal has bounded idle wait')
        out = finish(launch('imap-fetch', smtp, IMAP(mode='fetch-no'), home))
        check('IMAP_OK=0' in out and elapsed(out) < 400 and 'denied' in out, 'FETCH NO exits without waiting for a nonexistent literal')
        peer = IMAP(delay=0.7)
        out = finish(launch('imap-append', smtp, peer, home))
        check('IMAP_OK=1' in out and len(peer.appends) == 1 and elapsed(out) > 600,
              'APPEND waits for its distinct final acknowledgement deadline')
        out = finish(launch('imap-append', smtp, IMAP(mode='append-no'), home))
        check('IMAP_OK=0' in out and elapsed(out) < 400, 'APPEND NO does not wait for continuation')
        out = finish(launch('imap-append', smtp, IMAP(delay=2), home))
        check('IMAP_OK=0' in out and 1000 <= elapsed(out) < 1800, 'missing APPEND acknowledgement has bounded wait')
        out = finish(launch('imap-list', smtp, IMAP(mode='banner-stall'), home))
        check('IMAP_OK=0' in out and elapsed(out) < 900 and 'ожидания' in out, 'IMAP greeting deadline reports timeout')

        home = fixture('smtp')
        mime = os.path.join(home, 'synthetic.mime')
        with open(mime, 'wb') as file:
            file.write(b'Subject: synthetic\n\nbody\n')
        peer = SMTP(final_delay=0.7)
        out = finish(launch('smtp', peer, imap, home, [mime]))
        check('SMTP_OK=1' in out and elapsed(out) > 600, 'SMTP final reply can exceed normal command deadline')
        out = finish(launch('smtp', SMTP(greeting_delay=1.3), imap, home, [mime]))
        check('SMTP_OK=0' in out and elapsed(out) < 900, 'SMTP greeting wait is bounded')
        out = finish(launch('cancel', SMTP(greeting_delay=2), imap, home))
        check('CANCELLED=1' in out and elapsed(out) < 700, 'shutdown interrupts a waiting SMTP session promptly')
        out = finish(launch('dns', smtp, imap, home, settings={'AM03_SLOW_DNS': '1'}))
        check('DNS_OK=0' in out and elapsed(out) < 700 and 'DNS' in out, 'slow resolver cannot block caller beyond DNS deadline')
        out = finish(launch('dns-cancel', smtp, imap, home, settings={'AM03_SLOW_DNS': '1'}))
        check('CANCELLED=1' in out and elapsed(out) < 700, 'shutdown interrupts pending DNS wait')
        out = finish(launch('fds', smtp, imap, home))
        check('FD_DELTA=0' in out, 'SMTP exception unwinding and IMAP scope exit leak no descriptors (30 cycles)')

        home = fixture('connect-loss')
        folder = previous.mail(home, 'a@example.invalid')
        peer = SMTP()
        drop(peer.port, True, destination=True)
        try:
            out = finish(launch('outbox', peer, imap, home))
        finally:
            drop(peer.port, False, destination=True)
        check('QUEUED=1' in out and elapsed(out) < 900 and 'P "a@example.invalid"' in previous.records(folder),
              'complete channel loss before connect preserves pending mail and bounds connect wait')
        out = finish(launch('outbox', peer, imap, home))
        check('COMPLETE=1 CONFIRMED=1 QUEUED=0' in out and not os.path.exists(folder), 'restored channel drains the queued mail with confirmed APPEND')

        home = fixture('final-blackhole')
        folder = previous.mail(home, 'a@example.invalid')
        peer = SMTP(blackhole=True)
        try:
            out = finish(launch('outbox', peer, imap, home, settings={'FINAL_MS': '20000'}), timeout=12)
        finally:
            peer.restore()
        check('UNKNOWN=1' in out and 1000 < elapsed(out) < 9000 and 'U "a@example.invalid"' in previous.records(folder),
              'kernel detects silent established-channel loss before long SMTP deadline; unconfirmed mail retained')
        out = finish(launch('outbox', peer, imap, home))
        check('COMPLETE=1' in out and not os.path.exists(folder) and len(peer.deliveries) == 2,
              'lost final acknowledgement retries automatically after recovery, permitted duplicate confirmed')

        home = fixture('short-blackhole')
        peer = SMTP(blackhole=True)
        proc = launch('smtp', peer, imap, home, [mime], settings={'FINAL_MS': '20000'})
        try:
            check(peer.blocked.wait(3), 'short outage starts after DATA terminator')
            time.sleep(0.3)
            peer.restore()
            out = finish(proc)
        finally:
            peer.restore()
            if proc.poll() is None:
                proc.kill()
        check('SMTP_OK=1' in out and len(peer.deliveries) == 1, 'brief packet loss recovers within same connection without premature retry')

        home = fixture('batch')
        previous.mail(home, 'a@example.invalid')
        previous.mail(home, 'b@example.invalid')
        peer = SMTP(greeting_delay=2)
        out = finish(launch('outbox', peer, imap, home))
        check('QUEUED=2' in out and peer.connections == 1 and elapsed(out) < 1000,
              'one failed channel ends batch; all remaining mail waits for next cycle')

        home = fixture('partial-stats')
        folder = previous.mail(home)
        peer = SMTP()
        peer.reject = {'b@example.invalid': 450}
        out = finish(launch('outbox', peer, imap, home))
        check('CONFIRMED=1 QUEUED=1 AWAITING=1 UNKNOWN=0' in out, 'operator counters distinguish confirmed and pending recipients')
        peer.reject = {}
        out = finish(launch('outbox', peer, imap, home))
        check('COMPLETE=1 CONFIRMED=1' in out and peer.envelopes[-1] == ['b@example.invalid'],
              'confirmed recipients remain excluded from all later retries')

        idle_config = os.path.join(BUILD, 'idle_settings.conf')
        with open(idle_config, 'w') as file:
            file.write('idle_threshold_seconds=1\ncheck_interval_seconds=1\n')
        try:
            home = fixture('background')
            folder = previous.mail(home, 'a@example.invalid')
            peer = SMTP()
            peer.mode = 'drop_final'
            proc = launch('background', peer, IMAP(mode='banner-stall'), home)
            lines = []
            def collect():
                for line in iter(proc.stdout.readline, b''):
                    lines.append(line.decode('utf-8', 'replace'))
            reader = threading.Thread(target=collect, daemon=True)
            reader.start()
            def wait_text(text, timeout=8):
                deadline = time.time() + timeout
                while time.time() < deadline and text not in ''.join(lines):
                    time.sleep(0.05)
                return text in ''.join(lines)
            try:
                check(wait_text('будет автоповтор, возможен дубль'), 'background reports unknown result and automatic repeat after IMAP timeout')
                check('Последняя ошибка:' in ''.join(lines) and 'Следующий цикл после 1 с' in ''.join(lines),
                      'background displays last error and idle retry schedule')
                peer.mode = 'normal'
                check(wait_text('Исходящая очередь обработана.'), 'idle background automatically retries and drains queue after recovery')
                check(not os.path.exists(folder) and len(peer.deliveries) >= 2, 'background retry preserves mail until a positive final response')
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                proc.wait(timeout=3)
                reader.join(timeout=1)
                check(proc.returncode == 0 and re.search(r'STOP_MS=(\d+)', ''.join(lines)) and
                      int(re.search(r'STOP_MS=(\d+)', ''.join(lines)).group(1)) < 500,
                      'background stop joins promptly and exits normally')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()

            home = fixture('background-cancel')
            previous.mail(home, 'a@example.invalid')
            peer = SMTP(final_delay=4)
            proc = launch('background', peer, imap, home, settings={'FINAL_MS': '20000'})
            try:
                check(peer.data_seen.wait(5), 'background shutdown test reaches final SMTP wait')
                proc.stdin.write(b'\n')
                proc.stdin.flush()
                out = finish(proc, timeout=2)
                check('STOP_MS=' in out and int(re.search(r'STOP_MS=(\d+)', out).group(1)) < 500,
                      'normal exit cancels active background SMTP wait')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()

            home = fixture('full-app-cancel')
            peer = SMTP(final_delay=4)
            env = os.environ.copy()
            env['HOME'] = home
            seed = subprocess.Popen([os.path.join(BUILD, 'am01_driver'), 'seed', str(peer.port), str(imap.port)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            finish(seed, timeout=30)
            folder = previous.mail(home, 'a@example.invalid')
            proc = subprocess.Popen([os.path.join(BUILD, 'AstraMail')], env=env,
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            try:
                proc.stdin.write(b'1\nsynthetic-pin-123456\n\n')
                proc.stdin.flush()
                check(peer.data_seen.wait(8), 'complete AstraMail binary reaches background final SMTP wait')
                started = time.time()
                proc.stdin.write(b'0\n')
                proc.stdin.flush()
                finish(proc, timeout=2)
                check(time.time() - started < 1 and 'U "a@example.invalid"' in previous.records(folder),
                      'main-menu exit cancels network and preserves unconfirmed mail in complete binary')
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()

            home = fixture('thread-exception')
            inbox = os.path.join(home, '.local/share/AstraMail')
            os.makedirs(os.path.dirname(inbox))
            with open(inbox, 'w') as file:
                file.write('synthetic blocker')
            proc = launch('background', smtp, imap, home)
            time.sleep(2.3)
            os.unlink(inbox)
            time.sleep(2.3)
            proc.stdin.write(b'\n')
            proc.stdin.flush()
            out = finish(proc)
            check('Фоновый цикл прерван:' in out and 'Исходящая очередь обработана.' in out,
                  'background catches filesystem exception and completes a later cycle')
        finally:
            os.unlink(idle_config)

        network_config = os.path.join(BUILD, 'network_settings.conf')
        try:
            with open(network_config, 'w') as file:
                file.write('dns_seconds=2\nconnect_seconds=0\nsmtp_final_seconds=999999\nkeep_probes=20\n')
            out = finish(launch('config', smtp, imap, home))
            check('CONFIG=2000,10000,600000,3' in out, 'network configuration accepts valid limits and rejects unsafe zero/overflow values')
        finally:
            os.unlink(network_config)
    print('AM03_NATIVE_CHECKS={}/{}'.format(checks, checks), flush=True)


if __name__ == '__main__':
    main()
