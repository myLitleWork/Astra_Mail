#!/usr/bin/env python3
"""Python 3.5+: validate transferred allowlist, assemble ELF package, record evidence."""
from __future__ import print_function
import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

mode, run = sys.argv[1:3]
source, build, package = [os.path.join(run, part) for part in ['source', 'build', 'package']]


def read_json(path):
    with open(path, encoding='utf-8') as file:
        return json.load(file)


def sha(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as file:
        for block in iter(lambda: file.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def require(value, text):
    if not value:
        raise RuntimeError(text)


request = read_json(os.path.join(run, 'build-request.json'))
inputs = read_json(os.path.join(run, 'input-state.json'))
version = read_json(os.path.join(source, 'app-version.json'))
actual_version = '{}.{}.{}'.format(version['major'], version['minor'], version['patch'])
require(actual_version == request['Version'], 'canonical version mismatch')
require(inputs['InputSha256'] == request['InputSha256'] == version['releaseInputSha256'], 'input identity mismatch')

if mode == 'verify':
    lines = []
    for item in inputs['Files']:
        rel = item['Path']
        require(not os.path.isabs(rel) and '..' not in rel.split('/') and '\\' not in rel, 'unsafe input path')
        require(sha(os.path.join(source, rel)) == item['Sha256'], 'source mismatch: '+rel)
        lines.append(rel+'  '+item['Sha256'])
    require(hashlib.sha256(('\n'.join(lines)+'\n').encode('utf-8')).hexdigest() == inputs['InputSha256'], 'fingerprint mismatch')
    print('SOURCE_INPUTS_VERIFIED={}'.format(len(lines)), flush=True)
elif mode == 'prepare':
    with open(os.path.join(run, 'native-tests.log'), encoding='utf-8') as file:
        results = re.findall(r'AM\d+_NATIVE_CHECKS=(\d+)/(\d+)', file.read())
    require(len(results) == 8 and sum(int(a) for a, b in results) == 474 and all(a == b for a, b in results), 'native regressions incomplete')
    binary = os.path.join(build, 'AstraMail')
    version_home = os.path.join(run, 'version-home')
    os.mkdir(version_home)
    env = os.environ.copy()
    env['HOME'] = version_home
    require(subprocess.check_output([binary, '--version'], env=env).decode('ascii').strip() == actual_version, 'binary version mismatch')
    require(not os.listdir(version_home), '--version wrote user files')
    with open(binary, 'rb') as file:
        require(file.read(5) == b'\x7fELF\x02', 'expected ELF64')
    libs = sorted(glob.glob(os.path.join(build, '*.a')) + glob.glob(os.path.join(build, 'src/*/*.a')))
    require(len(libs) == 8, 'missing app libraries')
    csv_driver = os.path.join(build, 'release_csv_driver')
    subprocess.check_call(['g++', '-std=gnu++17', '-I'+source, '-I'+os.path.join(source, 'src'),
                           '-I'+os.path.join(source, 'secrets'), os.path.join(source, 'tests/release_csv_driver.cpp'),
                           '-Wl,--start-group']+libs+['-Wl,--end-group', '-lsodium', '-lstdc++fs', '-ldl', '-pthread', '-no-pie', '-o', csv_driver])
    payload = os.path.join(source, 'deployment/AstraMailPortable')
    subprocess.check_call([csv_driver, os.path.join(payload, 'contacts-import-example.csv')])
    os.mkdir(package)
    shutil.copy2(binary, package)
    for name in ['Run-AstraMail.sh', 'Verify-Package.sh', 'contacts-import-example.csv', 'README.md']:
        shutil.copy2(os.path.join(payload, name), package)
    shutil.copytree(os.path.join(source, 'tutorials'), os.path.join(package, 'tutorials'))
    shutil.copy2(os.path.join(source, 'app-version.json'), package)
    with open(os.path.join(package, 'version.txt'), 'w') as file:
        file.write(actual_version+'\n')
    output = subprocess.check_output(['ldd', binary]).decode('ascii')
    found = re.search(r'libsodium\.so\.18\s+=>\s+(\S+)', output)
    require(found is not None and 'not found' not in output, 'runtime dependency missing')
    os.mkdir(os.path.join(package, 'lib'))
    shutil.copy2(found.group(1), os.path.join(package, 'lib/libsodium.so.18'))
    os.mkdir(os.path.join(package, 'licenses'))
    for path, name in [('/usr/share/doc/libsodium18/copyright', 'libsodium.txt'),
                       ('/usr/share/doc/libstdc++6/copyright', 'gcc-runtime.txt')]:
        shutil.copy2(path, os.path.join(package, 'licenses', name))
elif mode == 'finalize':
    ui = read_json(os.path.join(run, 'packaged-tests.json'))
    require(ui['packagedUiCsvImport'] and ui['contacts'] == 3 and ui['cleanExit'], 'packaged UI gate failed')
    entries = []
    for root, dirs, files in os.walk(package):
        for name in sorted(files):
            full = os.path.join(root, name)
            require(not os.path.islink(full), 'symlink in package')
            entries.append({'path': os.path.relpath(full, package).replace(os.sep, '/'), 'sha256': sha(full), 'size': os.path.getsize(full)})
    entries.sort(key=lambda entry: entry['path'])
    manifest = {'formatVersion': 1, 'product': 'AstraMail', 'version': actual_version,
                'configuration': 'Release', 'architecture': 'x86_64', 'platform': 'Astra Linux 1.6',
                'createdUtc': datetime.datetime.utcnow().isoformat()+'Z', 'containsLocalSettings': False,
                'source': {'head': request['Head'], 'dirty': request['Dirty'], 'inputSha256': inputs['InputSha256'], 'inputCount': len(inputs['Files'])},
                'validation': {'nativeChecks': 474, 'csvSampleContacts': 3, 'packagedUiCsvImport': True,
                               'versionFlag': True, 'elf64': True}, 'files': entries}
    path = os.path.join(package, 'package-manifest.json')
    with open(path, 'w', encoding='utf-8') as file:
        json.dump(manifest, file, ensure_ascii=False, indent=2, sort_keys=True)
        file.write('\n')
    with open(os.path.join(package, 'package-checksums.sha256'), 'w', encoding='utf-8') as file:
        for entry in entries:
            file.write(entry['sha256']+'  '+entry['path']+'\n')
        file.write(sha(path)+'  package-manifest.json\n')
    subprocess.check_call(['sh', os.path.join(package, 'Verify-Package.sh')])
    print('PACKAGE_VERIFIED_FILES={}'.format(len(entries)+2), flush=True)
    with zipfile.ZipFile(os.path.join(run, 'artifacts.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for root, dirs, files in os.walk(package):
            for name in files:
                full = os.path.join(root, name)
                archive.write(full, os.path.relpath(full, run))
        for name in ['native-tests.log', 'packaged-tests.log', 'packaged-tests.json']:
            archive.write(os.path.join(run, name), name)
        for name in ['configure.log', 'build.log']:
            archive.write(os.path.join(build, name), name)
else:
    raise RuntimeError('unknown release stage')
