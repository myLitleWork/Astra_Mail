#!/usr/bin/env python3
"""Linux integration tests: actual startup, defaults, paths and reset command.

Usage: python3 tests/test_config_bootstrap_native.py /path/to/Debug-build
Requires g++, the ordinary WITH_SODIUM=ON build, and a non-root test user.
Only copied executables and temporary synthetic HOME/configurations are used.
"""
from __future__ import print_function
import glob
import os
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

SOURCE = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BUILD = os.path.abspath(sys.argv.pop(1))
NAMES = ['subject_rules.conf', 'attachment_groups.conf',
         'outgoing_subject_rules.conf', 'idle_settings.conf',
         'network_settings.conf', 'mail_settings.conf', 'contacts.conf']


class BootstrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.geteuid() == 0:
            raise RuntimeError('Run as an unprivileged user to test real permission failures')
        cls.compiled = tempfile.TemporaryDirectory(prefix='astramail-bootstrap-driver-')
        cls.driver = os.path.join(cls.compiled.name, 'config_bootstrap_driver')
        libs = sorted(glob.glob(os.path.join(BUILD, '*.a')) +
                      glob.glob(os.path.join(BUILD, 'src/*/*.a')))
        subprocess.check_call([
            'g++', '-std=gnu++17', '-O0', '-g', '-I'+SOURCE,
            '-I'+os.path.join(SOURCE, 'src'),
            os.path.join(SOURCE, 'tests/config_bootstrap_driver.cpp'),
            '-Wl,--start-group'] + libs + [
                '-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl',
                '-pthread', '-no-pie', '-o', cls.driver])
        cls.defaults = [subprocess.check_output([cls.driver, 'defaults', str(i)])
                        for i in range(len(NAMES))]

    @classmethod
    def tearDownClass(cls):
        cls.compiled.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='astramail-bootstrap-test-')
        self.addCleanup(self.temp.cleanup)
        self.bin = os.path.join(self.temp.name, 'bin')
        self.home = os.path.join(self.temp.name, 'home')
        self.cwd = os.path.join(self.temp.name, 'unrelated-cwd')
        for directory in [self.bin, self.home, self.cwd]:
            os.mkdir(directory)
        self.app = os.path.join(self.bin, 'AstraMail')
        self.helper = os.path.join(self.bin, 'config_bootstrap_driver')
        shutil.copy2(os.path.join(BUILD, 'AstraMail'), self.app)
        shutil.copy2(self.driver, self.helper)
        self.paths = [os.path.join(self.bin, name) for name in NAMES[:5]] + [
            os.path.join(self.home, '.astramail', 'profile_cache', name)
            for name in NAMES[5:]]
        self.env = os.environ.copy()
        self.env.update(HOME=self.home, LC_ALL='C.UTF-8', TERM='dumb')

    def call(self, args=None, text=b'', preexec_fn=None):
        proc = subprocess.Popen(args or [self.app], env=self.env, cwd=self.cwd,
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, preexec_fn=preexec_fn)
        try:
            output = proc.communicate(text, timeout=10)[0]
        except subprocess.TimeoutExpired:
            proc.kill()
            output = proc.communicate()[0]
            self.fail('Process did not exit: '+output.decode('utf-8', 'replace'))
        return proc.returncode, output.decode('utf-8', 'replace')

    def read(self, path):
        with open(path, 'rb') as stream:
            return stream.read()

    def seed(self):
        for i, path in enumerate(self.paths):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as stream:
                stream.write(('synthetic-user-setting-{}\n'.format(i)).encode('ascii'))
        return [self.read(path) for path in self.paths]

    def assert_startup_ok(self):
        code, output = self.call()
        self.assertEqual(code, 0, output)
        self.assertNotIn('Не удалось подготовить настройки', output)

    def assert_file_error(self, code, output, path):
        self.assertEqual(code, 1, output)
        self.assertIn('Не удалось подготовить настройки', output)
        self.assertIn(path, output)
        self.assertIn('Конфигурация', output)

    def test_first_launch_creates_all_defaults_and_parent_directories(self):
        self.assertFalse(os.path.exists(os.path.dirname(self.paths[5])))
        self.assert_startup_ok()
        self.assertEqual([self.read(path) for path in self.paths], self.defaults)
        self.assertEqual(os.listdir(self.cwd), [])

    def test_second_launch_preserves_nonempty_user_files_and_mtime(self):
        before = self.seed()
        for path in self.paths:
            os.utime(path, (1000000000, 1000000000))
        self.assert_startup_ok()
        self.assert_startup_ok()
        self.assertEqual([self.read(path) for path in self.paths], before)
        self.assertTrue(all(os.stat(path).st_mtime == 1000000000 for path in self.paths))

    def test_restore_each_missing_file_without_changing_others(self):
        missing_sets = [(i,) for i in range(len(NAMES))] + [(0, 1), (0, 2), (1, 2)]
        for missing in missing_sets:
            with self.subTest(files=[NAMES[i] for i in missing]):
                expected = self.seed()
                for i in missing:
                    os.unlink(self.paths[i])
                    expected[i] = self.defaults[i]
                self.assert_startup_ok()
                self.assertEqual([self.read(p) for p in self.paths], expected)

    def test_empty_files_receive_defaults(self):
        self.seed()
        for path in self.paths:
            with open(path, 'wb'):
                pass
        self.assert_startup_ok()
        self.assertEqual([self.read(path) for path in self.paths], self.defaults)

    def test_manual_reset_forces_defaults_and_preserves_other_files(self):
        for i in range(len(NAMES)):
            with self.subTest(file=NAMES[i]):
                expected = self.seed()
                code, output = self.call([self.helper, 'reset', str(i)], b'y\n\n')
                self.assertEqual(code, 0, output)
                self.assertIn('[OK] Конфигурация пересоздана', output)
                self.assertIn('Нажмите Enter', output)
                expected[i] = self.defaults[i]
                self.assertEqual([self.read(p) for p in self.paths], expected)

    def test_canceled_and_eof_reset_preserve_all_files(self):
        for i in range(len(NAMES)):
            for answer in [b'n\n\n', b'', b'y']:
                with self.subTest(file=NAMES[i], answer=answer):
                    expected = self.seed()
                    code, output = self.call([self.helper, 'reset', str(i)], answer)
                    self.assertEqual(code, 0, output)
                    self.assertIn('Отменено.', output)
                    self.assertNotIn('[OK]', output)
                    self.assertEqual([self.read(p) for p in self.paths], expected)

    def test_readonly_existing_nonempty_files_need_no_write_access(self):
        expected = self.seed()
        for path in self.paths:
            os.chmod(path, 0o400)
        self.assert_startup_ok()
        self.assertEqual([self.read(p) for p in self.paths], expected)

    def test_missing_file_in_readonly_directory_reports_path(self):
        for i, path in enumerate(self.paths):
            with self.subTest(file=NAMES[i]):
                self.seed()
                os.unlink(path)
                parent = os.path.dirname(path)
                os.chmod(parent, 0o500)
                try:
                    self.assert_file_error(*self.call(), path=path)
                    self.assertFalse(os.path.exists(path))
                finally:
                    os.chmod(parent, 0o700)

    def test_missing_home_subdirectories_cannot_be_created(self):
        os.chmod(self.home, 0o500)
        try:
            self.assert_file_error(*self.call(), path=self.paths[5])
        finally:
            os.chmod(self.home, 0o700)

    def test_parent_component_is_a_file(self):
        with open(os.path.join(self.home, '.astramail'), 'wb') as stream:
            stream.write(b'synthetic-blocker')
        self.assert_file_error(*self.call(), path=self.paths[5])

    def test_directory_instead_of_config_is_reported(self):
        os.mkdir(self.paths[0])
        self.assert_file_error(*self.call(), path=self.paths[0])

    def test_stat_permission_error_does_not_truncate_user_file(self):
        expected = self.seed()
        parent = os.path.dirname(self.paths[5])
        os.chmod(parent, 0)
        try:
            self.assert_file_error(*self.call(), path=self.paths[5])
        finally:
            os.chmod(parent, 0o700)
        self.assertEqual([self.read(p) for p in self.paths], expected)

    def test_empty_readonly_file_is_reported(self):
        with open(self.paths[0], 'wb'):
            pass
        os.chmod(self.paths[0], 0o400)
        self.assert_file_error(*self.call(), path=self.paths[0])
        self.assertEqual(self.read(self.paths[0]), b'')

    def test_write_failure_at_close_is_reported(self):
        def limit_file_size():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (1, 1))
        self.assert_file_error(*self.call(preexec_fn=limit_file_size), path=self.paths[0])

    def test_failed_manual_reset_has_no_false_success_and_returns_to_menu(self):
        for i, path in enumerate(self.paths):
            with self.subTest(file=NAMES[i]):
                expected = self.seed()
                os.chmod(path, 0o400)
                try:
                    code, output = self.call([self.helper, 'reset', str(i)], b'y\n\n')
                    self.assertEqual(code, 0, output)
                    self.assertIn('[Ошибка] Не удалось пересоздать конфигурацию', output)
                    self.assertIn(path, output)
                    self.assertIn('Нажмите Enter', output)
                    self.assertNotIn('[OK]', output)
                    self.assertEqual([self.read(p) for p in self.paths], expected)
                finally:
                    os.chmod(path, 0o600)

    def test_version_query_does_not_create_configuration(self):
        code, output = self.call([self.app, '--version'])
        self.assertEqual(code, 0, output)
        self.assertFalse(any(os.path.exists(path) for path in self.paths))


if __name__ == '__main__':
    unittest.main(verbosity=2)
