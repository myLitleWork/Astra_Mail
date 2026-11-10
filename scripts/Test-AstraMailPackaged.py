#!/usr/bin/env python3
"""Run an extracted copy of the real package against synthetic IMAP/SMTP/profile."""
from __future__ import print_function
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import test_am01_native as old
import test_am03_native as network
import test_am06_native as terminal
import test_am11_native as eof

build, package, result = sys.argv[1:4]
with tempfile.TemporaryDirectory(prefix='release-ui-', dir=build) as root:
    runtime, home = os.path.join(root, 'runtime'), os.path.join(root, 'home')
    shutil.copytree(package, runtime)
    os.mkdir(home)
    smtp, imap = old.SMTPServer(), network.IMAP()
    subprocess.check_call([os.path.join(build, 'am01_driver'), 'seed', str(smtp.port), str(imap.port)],
                          env=terminal.environment(home), stdout=subprocess.DEVNULL)
    terminal.BUILD = runtime
    tty = terminal.Terminal('', home, app=True)
    try:
        eof.unlock(tty)
        tty.send('3\n')
        tty.read_until('Импорт контактов из CSV (выбор папки/файла)')
        tty.read_until('Выбор: ')
        choices = re.findall(r'(\d+)\. Импорт контактов из CSV', tty.data.decode('utf-8', 'replace'))
        assert choices, 'CSV menu entry missing'
        tty.send(choices[-1]+'\n')
        tty.read_until('Импорт CSV контактов: выбор папки')
        tty.read_until('Выбор: ')
        tty.send('3\n')
        tty.read_until('contacts-import-example.csv')
        tty.read_until('Выбор: ')
        tty.send('1\n')
        tty.read_until('[CSV-IMPORT] ОК: contacts-import-example.csv')
        tty.read_until('Загружено контактов: 3')
        tty.read_until('Нажмите Enter')
        tty.send(b'\x04')
        tty.finish(timeout=3)
        assert tty.proc.returncode == 0 and tty.restored(), 'packaged app did not exit cleanly'
        with open(os.path.join(home, '.astramail/profile_cache/contacts.conf'), encoding='utf-8') as file:
            contacts = file.read()
        for value in ['demo_office', 'office@example.invalid', 'demo_duty', 'duty@example.invalid', 'demo_team', 'team@example.invalid']:
            assert value in contacts, 'CSV record not persisted: '+value
        with open(result, 'w', encoding='utf-8') as file:
            json.dump({'packagedUiCsvImport': True, 'contacts': 3, 'cleanExit': True}, file)
        print('PACKAGED_UI_CSV_CONTACTS=3 CLEAN_EXIT=1', flush=True)
    finally:
        tty.close()
