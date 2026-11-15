#!/usr/bin/env python3
"""Python 3.5+: actual package installation in synthetic user homes only."""
from __future__ import print_function
import errno
import glob
import hashlib
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


def digest(path):
    with open(path, 'rb') as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def write(path, text, executable=False):
    parent = os.path.dirname(path)
    if not os.path.isdir(parent):
        os.makedirs(parent)
    with open(path, 'w', encoding='utf-8') as stream:
        stream.write(text)
    if executable:
        os.chmod(path, 0o755)


package = os.path.realpath(sys.argv[1])
root = tempfile.mkdtemp(prefix='astramail-installer-', dir=os.path.dirname(package))
checks = 0


def check(value, message):
    global checks
    if not value:
        raise AssertionError(message + '; evidence=' + root)
    checks += 1
    print('PASS ' + message, flush=True)


def environment(home):
    env = os.environ.copy()
    for name in list(env):
        if name.startswith('XDG_') or name in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'LD_DEBUG'):
            env.pop(name)
    env.update(HOME=home, XDG_CONFIG_HOME=os.path.join(home, '.config'),
               XDG_DATA_HOME=os.path.join(home, '.local/share'), LC_ALL='C.UTF-8')
    return env


def install(home, cwd, source=package, extra=None, success=True):
    if not os.path.isdir(home):
        os.makedirs(home)
    env = environment(home)
    if extra:
        env.update(extra)
    process = subprocess.Popen(['bash', os.path.join(source, 'scripts/install_astramail.sh')],
                               cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = process.communicate(timeout=90)[0].decode('utf-8', 'replace')
    with open(os.path.join(root, 'run-{}.log'.format(len(glob.glob(root + '/run-*.log')))), 'w', encoding='utf-8') as stream:
        stream.write(output)
    check((process.returncode == 0) == success, 'installer exit status: ' + os.path.basename(home))
    if not success:
        check('Готово.' not in output, 'failed installation has no success message')
    return output


def installed(home):
    target = os.path.join(home, '.local/opt/AstraMail')
    launcher = os.path.join(home, '.local/bin/AstraMail')
    version = subprocess.check_output([launcher, '--version'], cwd='/', env=environment(home)).decode('ascii').strip()
    check(version == open(package + '/version.txt').read().strip(), 'installed launcher version')
    for name in ('AstraMail', 'lib/libsodium.so.18', 'contacts-import-example.csv', 'Run-AstraMail.sh'):
        check(digest(target + '/' + name) == digest(package + '/' + name), 'installed bytes: ' + name)
    check(os.path.isfile(home + '/.local/share/applications/AstraMail.desktop'), 'menu entry exists')
    env = environment(home)
    env['LD_DEBUG'] = 'libs'
    output = subprocess.check_output([launcher, '--version'], env=env, stderr=subprocess.STDOUT).decode('utf-8', 'replace')
    check(any('calling init:' in line and '/opt/AstraMail/lib/libsodium.so.18' in line for line in output.splitlines()),
          'loader uses installed bundled libsodium')
    return target, launcher


try:
    if os.environ.get('ASTRAMAIL_TEST_NOEXEC') == '1':
        try:
            subprocess.check_call([package + '/AstraMail', '--version'])
        except OSError as error:
            check(error.errno == errno.EACCES, 'source package ELF denied by noexec mount')
        else:
            check(False, 'source package must be mounted noexec')
    # A different current directory must never supply an unrelated AstraMail.
    elsewhere = root + '/unrelated'
    os.mkdir(elsewhere)
    write(elsewhere + '/AstraMail', 'UNRELATED_BINARY\n')
    home = root + '/home normal'
    install(home, elsewhere)
    target, launcher = installed(home)
    check(os.path.isfile(home + '/Desktop/AstraMail.desktop'), 'missing Desktop is created')
    install(home, package + '/scripts')
    check(bool(glob.glob(home + '/.local/opt/.AstraMail.install.*/previous-app/AstraMail')), 'previous installation retained')

    # Runtime configuration is preserved, never included in release payload.
    write(target + '/attachment_groups.conf', 'SYNTHETIC_CONFIG\n')
    install(home, '/')
    check(open(target + '/attachment_groups.conf').read() == 'SYNTHETIC_CONFIG\n', 'existing configuration preserved')
    write(home + '/.local/bin/subject_rules.conf', 'SYNTHETIC_UNRELATED_CONFIG\n')
    check(not os.path.exists(target + '/subject_rules.conf'), 'new-layout update ignores bin configuration')

    # Migration from the former single-binary installer.
    legacy = root + '/legacy'
    write(legacy + '/.local/bin/AstraMail', 'PREVIOUS_LEGACY_BINARY\n', True)
    write(legacy + '/.local/bin/subject_rules.conf', 'SYNTHETIC_LEGACY_CONFIG\n')
    install(legacy, package)
    check(open(legacy + '/.local/opt/AstraMail/subject_rules.conf').read() == 'SYNTHETIC_LEGACY_CONFIG\n', 'legacy configuration migrated')
    check(any(open(path).read() == 'PREVIOUS_LEGACY_BINARY\n' for path in glob.glob(legacy + '/.local/opt/.AstraMail.install.*/previous/launcher')),
          'legacy binary retained')

    # Localized desktop and all desktop Exec metacharacters.
    special = root + '/Пользователь space $ ` " \\ %'
    write(special + '/.config/user-dirs.dirs', r'XDG_DESKTOP_DIR="$HOME/Рабочий стол \\ \" \$ \`"' + '\n')
    install(special, elsewhere)
    special_target, special_launcher = installed(special)
    desktop = special + '/Рабочий стол \\ " $ `/AstraMail.desktop'
    check(os.path.isfile(desktop), 'localized desktop exists')
    from gi.repository import Gio, GLib
    info = Gio.DesktopAppInfo.new_from_filename(desktop)
    check(info is not None, 'GLib accepts desktop file')
    # Launch through GLib's actual field-code and Exec parser. Suppress only
    # the terminal window for this --version probe; the shipped entry keeps it.
    key_file = GLib.KeyFile()
    key_file.load_from_file(desktop, GLib.KeyFileFlags.NONE)
    check(key_file.get_boolean('Desktop Entry', 'Terminal'), 'shipped desktop entry opens a terminal')
    key_file.set_boolean('Desktop Entry', 'Terminal', False)
    key_file.set_string('Desktop Entry', 'Exec', key_file.get_string('Desktop Entry', 'Exec') + ' --version')
    os.rename(special_launcher, special_launcher + '.real')
    write(special_launcher, '#!/bin/sh\n' + shlex.quote(special_launcher + '.real') + ' "$@" > "$HOME/desktop-version.txt"\n', True)
    context = Gio.AppLaunchContext()
    for name, value in environment(special).items():
        context.setenv(name, value)
    check(Gio.DesktopAppInfo.new_from_keyfile(key_file).launch([], context), 'native desktop parser launches exact path')
    version_file = special + '/desktop-version.txt'
    for attempt in range(100):
        if os.path.isfile(version_file) and os.path.getsize(version_file):
            break
        time.sleep(0.05)
    check(os.path.isfile(version_file) and open(version_file).read().strip() == open(package + '/version.txt').read().strip(),
          'desktop executable starts installed version with metacharacters')
    os.rename(special_launcher + '.real', special_launcher)

    disabled = root + '/disabled-desktop'
    write(disabled + '/.config/user-dirs.dirs', 'XDG_DESKTOP_DIR="$HOME"\n')
    install(disabled, '/')
    check(not os.path.exists(disabled + '/Desktop'), 'disabled desktop is respected')
    check(os.path.isfile(disabled + '/.local/share/applications/AstraMail.desktop'), 'menu remains available without desktop icons')

    # A corruption must be rejected before replacing any installed bytes.
    corrupt = root + '/corrupt-package'
    shutil.copytree(package, corrupt)
    write(corrupt + '/AstraMail', 'CORRUPTED_BINARY\n')
    before = digest(target + '/AstraMail')
    install(home, '/', source=corrupt, success=False)
    check(digest(target + '/AstraMail') == before, 'corrupt source leaves old installation intact')

    # Permission failure after the app switch must roll back all changed files.
    shim = root + '/shim'
    marker = root + '/copy-failed-once'
    cp = shutil.which('cp')
    write(shim + '/cp', '#!/bin/bash\nlast="${@: -1}"\nif [[ $last = "$FAIL_DEST" && ! -e "$FAIL_MARKER" ]]; then\n  : > "$FAIL_MARKER"\n  printf PARTIAL > "$last"\n  exit 23\nfi\nexec "' + cp + '" "$@"\n', True)
    launcher_before = digest(launcher)
    menu_before = digest(home + '/.local/share/applications/AstraMail.desktop')
    install(home, '/', extra={'PATH': shim + ':' + os.environ['PATH'], 'FAIL_DEST': home + '/.local/share/applications/AstraMail.desktop',
                             'FAIL_MARKER': marker}, success=False)
    check(digest(target + '/AstraMail') == before, 'failed update restores old app')
    check(digest(launcher) == launcher_before, 'failed update restores old launcher')
    check(digest(home + '/.local/share/applications/AstraMail.desktop') == menu_before, 'partial desktop write restored')
    check(open(target + '/attachment_groups.conf').read() == 'SYNTHETIC_CONFIG\n', 'rollback preserves configuration')

    blocked = root + '/blocked'
    write(blocked + '/.local/bin', 'SYNTHETIC_BLOCKER\n')
    install(blocked, '/', success=False)
    check(open(blocked + '/.local/bin').read() == 'SYNTHETIC_BLOCKER\n', 'blocked destination unchanged')
    check(not os.path.exists(blocked + '/.local/opt/AstraMail'), 'failed first install creates no installed app')
    print('INSTALLER_CHECKS={0}/{0}'.format(checks), flush=True)
    print('INSTALLER_EVIDENCE=' + root, flush=True)
except Exception:
    print('INSTALLER_EVIDENCE=' + root, file=sys.stderr)
    raise
