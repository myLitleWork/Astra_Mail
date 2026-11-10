#!/usr/bin/env python3
"""AM-02 tests with real libsodium and application modules; Python 3.5+."""
from __future__ import print_function
import glob
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from test_am01_native import SMTPServer, IMAP_PORT

SOURCE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BUILD = os.path.abspath(sys.argv[1])
DRIVER = os.path.join(BUILD, 'am02_driver')
PIN = 'synthetic-am02-pin'
checks = 0


def check(value, name):
    global checks
    if not value:
        raise AssertionError(name)
    checks += 1
    print('PASS', name, flush=True)


def compile_driver():
    libraries = sorted(glob.glob(os.path.join(BUILD, '*.a')) + glob.glob(os.path.join(BUILD, 'src', '*', '*.a')))
    if len(libraries) != 8:
        raise AssertionError('Expected eight application libraries')
    subprocess.check_call(['g++', '-std=gnu++17', '-O0', '-g', '-I' + SOURCE,
        '-I' + os.path.join(SOURCE, 'src'), '-I' + os.path.join(SOURCE, 'secrets'),
        os.path.join(SOURCE, 'tests/am02_driver.cpp'), '-Wl,--start-group'] + libraries +
        ['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', DRIVER])


def start(mode, variant, home, server, pin=None, faults=None):
    env = os.environ.copy()
    env['HOME'] = home
    if faults:
        env['LD_PRELOAD'] = os.path.join(BUILD, 'am01_faults.so')
        env.update(faults)
    args = [DRIVER, mode, variant, str(server.port), str(IMAP_PORT)]
    if pin is not None:
        args.append(pin)
    return subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)


def finish(process, text=''):
    try:
        output = process.communicate(text.encode('utf-8'), timeout=20)[0].decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        process.kill()
        raise AssertionError('driver timeout: ' + process.communicate()[0].decode('utf-8', 'replace'))
    if process.returncode:
        raise AssertionError('driver failed: ' + output)
    return output


def call(mode, variant, home, server, pin=None, text='', faults=None):
    return finish(start(mode, variant, home, server, pin, faults), text)


def blob_files(home):
    return set(glob.glob(os.path.join(home, '.astramail/accounts/*.bin')))


def legacy_hash(home):
    with open(os.path.join(home, '.astramail/secret.bin'), 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def new_mail(home):
    folder = os.path.join(home, '.local/share/AstraMail/outbox/target@example.invalid')
    os.makedirs(folder)
    with open(os.path.join(folder, 'synthetic.txt'), 'w') as f:
        f.write('Synthetic AM-02 attachment')
    return folder


def main():
    compile_driver()
    server = SMTPServer()
    with tempfile.TemporaryDirectory(prefix='am02-fixtures-', dir=BUILD) as root:
        def fixture(name):
            home = os.path.join(root, name)
            os.makedirs(home)
            return home

        home = fixture('separate')
        check('HAS=0 LEGACY=0' in call('has', 'A', home, server), 'fresh account has neither bound secret nor legacy fallback')
        out = call('pinmenu', 'A', home, server, text='0\n')
        check('Ввести PIN' not in out and 'UNLOCKED=' in out,
              'fresh account menu offers setup, not PIN entry for nonexistent secret (AM-10)')
        check('STORED=1' in call('store', 'A', home, server), 'store A with real libsodium')
        a_file = next(iter(blob_files(home)))
        check('HAS=0' in call('has', 'B', home, server), 'account B never sees A secret as its own')
        check('LOADED=0 MATCH=0 CLEARED=1' in call('load', 'B', home, server), 'missing B secret cannot return stale output')
        call('store', 'B', home, server)
        b_file = next(iter(blob_files(home) - {a_file}))
        check('LOADED=1 MATCH=1' in call('load', 'A', home, server) and
              'LOADED=1 MATCH=1' in call('load', 'B', home, server), 'distinct account passwords survive separate processes with same PIN')
        check((os.stat(a_file).st_mode & 0o777) == 0o600 and
              (os.stat(os.path.dirname(a_file)).st_mode & 0o777) == 0o700,
              'bound files 0600 and account directory 0700')
        with open(a_file, 'rb') as f:
            a_bytes = f.read()
        with open(b_file, 'rb') as f:
            b_bytes = f.read()
        check(b'synthetic-password-A' not in a_bytes and b'synthetic-profile-A' not in a_bytes,
              'password and authenticated context are not stored as plaintext')
        shutil.copyfile(a_file, b_file)
        check('LOADED=0 MATCH=0 CLEARED=1' in call('load', 'B', home, server),
              'copy A ciphertext under B filename still fails authenticated context check')
        with open(b_file, 'wb') as f:
            f.write(b_bytes)
        check('LOADED=0 MATCH=0 CLEARED=1' in call('load', 'A', home, server, pin='wrong-pin'),
              'wrong PIN fails and clears output')
        out = call('enter', 'A', home, server, text='wrong-pin\n')
        check('UNLOCKED=0 MATCH=0' in out, 'failed PIN command clears previously unlocked memory')
        out = call('enter', 'A', home, server, text=PIN + '\n')
        check('UNLOCKED=1 MATCH=1' in out, 'PIN command unlocks only selected account')

        for name, index, value in [('length', 16, 0), ('KDF memory', 12, 0xffffffff), ('KDF ops', 8, 0xffffffff)]:
            mutated = bytearray(a_bytes)
            struct.pack_into('<I', mutated, index, value)
            with open(a_file, 'wb') as f:
                f.write(mutated)
            check('LOADED=0 MATCH=0 CLEARED=1' in call('load', 'A', home, server),
                  'invalid bound ' + name + ' is rejected before unsafe KDF/allocation')
        with open(a_file, 'wb') as f:
            f.write(a_bytes[:-1])
        check('LOADED=0 MATCH=0 CLEARED=1' in call('load', 'A', home, server), 'truncated bound secret fails safely')
        with open(a_file, 'wb') as f:
            f.write(a_bytes)

        out = call('reset', 'A', home, server, text='synthetic-password-A\nsynthetic-password-A\nnew-test-pin\nnew-test-pin\n\n')
        check('UNLOCKED=1 MATCH=1' in out and
              'LOADED=1 MATCH=1' in call('load', 'A', home, server, pin='new-test-pin') and
              'LOADED=1 MATCH=1' in call('load', 'B', home, server),
              'reset A changes only A PIN and leaves B intact')
        check('STORED=0' in call('store', 'B', home, server, pin='replacement-pin',
              faults={'AM02_FAIL_SECRET_RENAME_AT': '1'}) and
              'LOADED=1 MATCH=1' in call('load', 'B', home, server),
              'failed atomic replacement keeps previous B secret usable')

        home = fixture('switch')
        check('SWITCH_OK=1' in call('switch', 'A', home, server), 'A-B-A switches clear memory and retain separate encrypted records')
        check('CONTEXT_FIELDS_OK=9 CANONICAL_HOST=1' in call('contexts', 'A', home, server),
              'profile/account/server/port/login/identity binding and host normalization')
        check('GATE_OK=1' in call('gate', 'A', home, server), 'same-thread and cross-thread operations prevent account mutation')
        check('APPLY_COMMAND_BLOCKED=1' in call('blocked-apply', 'A', home, server),
              'actual account-selection command preserves old context while mail operation is active')

        home = fixture('locked')
        folder = new_mail(home)
        connections = server.connections
        check('LOCKED_SEND_BLOCKED=1' in call('locked-outbox', 'A', home, server) and
              server.connections == connections and os.path.exists(folder),
              'switch A to locked B sends no A password or message to B')
        check('STALE_COMMANDS_BLOCKED=1' in call('stale', 'A', home, server, text='\n') and
              server.connections == connections, 'old send/export commands cannot reuse A credentials after switch to B')

        home = fixture('inflight')
        new_mail(home)
        server.mode = 'hold_final'
        check('INFLIGHT_SWITCH_BLOCKED=1' in call('inflight', 'A', home, server),
              'actual in-flight SMTP prevents switch, then permits it after operation ends')
        server.mode = 'normal'

        home = fixture('migration')
        call('legacy', 'A', home, server)
        before = legacy_hash(home)
        check('HAS=0 LEGACY=1' in call('has', 'A', home, server), 'old unbound file is offered for explicit migration')
        check('UNLOCKED=0' in call('enter', 'A', home, server, text=PIN + '\nno\n') and not blob_files(home),
              'declining migration creates no account secret and unlocks nothing')
        check('UNLOCKED=0' in call('enter', 'A', home, server, text='wrong-pin\nyes\n') and
              not os.path.exists(os.path.join(home, '.astramail/accounts/legacy-binding')),
              'wrong legacy PIN cannot claim migration binding')
        check('UNLOCKED=1 MATCH=1' in call('enter', 'A', home, server, text=PIN + '\nyes\n'),
              'explicit migration confirmation plus PIN unlocks selected A')
        check(legacy_hash(home) == before, 'original secret.bin preserved byte for byte')
        check('HAS=0 LEGACY=0' in call('has', 'B', home, server) and
              'MIGRATED=0' in call('migrate', 'B', home, server), 'old file claimed by A cannot be rebound to B')
        check('LOADED=1 MATCH=1' in call('load', 'A', home, server), 'migrated bound secret survives restart')

        home = fixture('resume')
        call('legacy', 'A', home, server)
        check('MIGRATED=0' in call('migrate', 'A', home, server,
              faults={'AM02_FAIL_SECRET_RENAME_AT': '2'}), 'failure after legacy claim does not report migration success')
        check('HAS=0 LEGACY=1' in call('has', 'A', home, server) and
              'HAS=0 LEGACY=0' in call('has', 'B', home, server), 'interrupted migration can resume only for claimed account')
        check('MIGRATED=1' in call('migrate', 'A', home, server) and
              'LOADED=1 MATCH=1' in call('load', 'A', home, server), 'same account safely resumes interrupted migration')

        home = fixture('no-fallback')
        call('legacy', 'A', home, server)
        call('store', 'A', home, server, pin='different-new-pin')
        check('UNLOCKED=0' in call('enter', 'A', home, server, text=PIN + '\n') and
              not os.path.exists(os.path.join(home, '.astramail/accounts/legacy-binding')),
              'bound-secret authentication failure never falls back to old secret.bin')

        home = fixture('race')
        call('legacy', 'A', home, server)
        before = legacy_hash(home)
        a = start('migrate', 'A', home, server)
        b = start('migrate', 'B', home, server)
        outcomes = [finish(a), finish(b)]
        check(sum('MIGRATED=1' in out for out in outcomes) == 1 and len(blob_files(home)) == 1 and
              legacy_hash(home) == before, 'concurrent migration processes can claim old file for only one account')

        home = fixture('bad-legacy')
        call('legacy', 'A', home, server)
        path = os.path.join(home, '.astramail/secret.bin')
        with open(path, 'rb') as f:
            original = f.read()
        for name, data in [('short-header', original[:2]), ('short-cipher', original[:-1]),
                           ('zero-length', original[:52] + struct.pack('<I', 0)),
                           ('unbounded-KDF', original[:4] + struct.pack('<II', 0xffffffff, 0xffffffff) + original[12:])]:
            with open(path, 'wb') as f:
                f.write(data)
            check('MIGRATED=0' in call('migrate', 'A', home, server) and not blob_files(home),
                  'migration rejects malformed legacy ' + name)

        home = fixture('full-legacy-startup')
        call('legacyseed', 'A', home, server)
        before = legacy_hash(home)
        env = os.environ.copy()
        env['HOME'] = home
        app = subprocess.Popen([os.path.join(BUILD, 'AstraMail')], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        try:
            output = app.communicate(('1\n' + PIN + '\nyes\n\n\n0\n').encode('utf-8'), timeout=15)[0].decode('utf-8', 'replace')
        except subprocess.TimeoutExpired:
            app.kill()
            raise AssertionError('full legacy startup timeout: ' + app.communicate()[0].decode('utf-8', 'replace'))
        check(app.returncode == 0 and 'PIN принят' in output and len(blob_files(home)) == 1 and legacy_hash(home) == before,
              'complete application migrates legacy only after confirmation and exits normally')
    print('AM02_NATIVE_CHECKS={}/{}'.format(checks, checks), flush=True)


if __name__ == '__main__':
    main()
