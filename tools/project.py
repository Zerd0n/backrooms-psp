#!/usr/bin/env python3
"""Build, test, verify, package and install Backrooms PSP; Python stdlib only."""
from __future__ import annotations
import argparse
import contextlib
import datetime as dt
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parent.parent
IMAGE = 'pspdev/pspdev@sha256:c1dd948b190e9141242cf321316eb2b54562b274c9be78e24a66fe6d30a5dc24'
LOG = logging.getLogger('backrooms')
AUDIO = ('ambient_level0.raw', 'ambient_poolrooms.raw', 'chase.raw')

class ProjectError(Exception):
    """Actionable user-facing failure."""

def stamp():
    return dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')

def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def config():
    values = {'BACKROOMS_DOCKER_IMAGE': IMAGE, 'BACKROOMS_PLATFORM': 'linux/amd64'}
    path = ROOT / '.env'
    if path.exists():
        for number, raw in enumerate(path.read_text().splitlines(), 1):
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            key, sep, value = line.partition('=')
            if not sep or key not in values:
                raise ProjectError(f'Invalid .env line {number}; see .env.example.')
            values[key] = value.strip().strip('\"\'')
    for key in values:
        values[key] = os.environ.get(key, values[key])
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9._/@:-]{1,240}', values['BACKROOMS_DOCKER_IMAGE']):
        raise ProjectError('Invalid Docker image reference.')
    if values['BACKROOMS_PLATFORM'] not in ('linux/amd64', 'linux/arm64'):
        raise ProjectError('Platform must be linux/amd64 or linux/arm64.')
    return values

def run(command, *, cwd=ROOT, timeout=600):
    LOG.info('Running: %s', ' '.join(map(str, command)))
    # A file avoids PIPE deadlocks while preserving full diagnostic output.
    with tempfile.TemporaryFile(mode='w+t') as output:
        process = subprocess.Popen(list(map(str, command)), cwd=cwd, stdout=output,
                                   stderr=subprocess.STDOUT)
        started = time.monotonic()
        try:
            while process.poll() is None:
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    LOG.info('Still running (%.0f seconds)...', time.monotonic()-started)
                if time.monotonic()-started > timeout:
                    raise ProjectError(f'Command timed out after {timeout} seconds.')
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            output.seek(0)
            for line in output:
                LOG.info('%s', line.rstrip())
        if process.returncode:
            raise ProjectError(f'Command failed (exit {process.returncode}); see log above.')

def verify_assets():
    manifest = json.loads((ROOT/'config/assets.sha256.json').read_text())
    for relative, expected in manifest.items():
        path = ROOT / relative
        if not path.is_file() or digest(path) != expected:
            raise ProjectError(f'Required asset changed or missing: {relative}. Restore it from Git or a release archive.')
    LOG.info('All %d runtime assets and embedded texture headers match SHA-256.', len(manifest))

def verify_pbp(path):
    data = path.read_bytes()
    if len(data) < 40 or data[:4] != b'\x00PBP':
        raise ProjectError(f'Invalid PSP executable: {path}')
    version, *offsets = struct.unpack_from('<9I', data, 4)
    if version != 0x10000 or offsets[0] != 40 or sorted(offsets) != offsets or offsets[-1] > len(data):
        raise ProjectError('Invalid PBP section offsets/version.')
    if data[offsets[0]:offsets[0]+4] != b'\x00PSF':
        raise ProjectError('Missing PARAM.SFO metadata.')
    if data[offsets[6]:offsets[6]+4] != b'\x7fELF':
        raise ProjectError('PBP does not contain the expected homebrew ELF.')
    elf = offsets[6]
    if data[elf+4:elf+6] != b'\x01\x01' or struct.unpack_from('<H', data, elf+18)[0] != 8:
        raise ProjectError('Executable is not little-endian 32-bit MIPS.')
    for index, name in ((1, 'ICON0.PNG'), (4, 'PIC1.PNG')):
        if data[offsets[index]:offsets[index+1]] != (ROOT/'assets'/name).read_bytes():
            raise ProjectError(f'PBP menu art does not match preserved {name}.')
    LOG.info('Valid MIPS PSP executable: %s (%d bytes)', path, len(data))

def test():
    compiler = shutil.which('clang') or shutil.which('cc')
    if not compiler:
        raise ProjectError('Install a C compiler: xcode-select --install (macOS) or apt install build-essential (Linux).')
    with tempfile.TemporaryDirectory(prefix='backrooms-tests-') as folder:
        exe = Path(folder)/'test_game'
        run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
             '-fsanitize=address,undefined', 'src/core/world.c', 'src/core/game.c',
             'tests/test_game.c', '-lm', '-o', exe])
        run([exe], timeout=90)
        storage = Path(folder)/'test_storage'
        run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
             '-fsanitize=address,undefined', 'src/core/world.c', 'src/core/game.c',
             'src/psp/storage.c', 'tests/test_storage.c', '-lm', '-o', storage])
        run([storage], cwd=folder)
    LOG.info('Core and persistence tests passed under ASan/UBSan.')

def package():
    verify_assets(); verify_pbp(ROOT/'EBOOT.PBP')
    dist = ROOT/'dist'; dist.mkdir(exist_ok=True)
    target = dist/'BACKROOMS3D'
    if target.is_symlink():
        raise ProjectError('Refusing to replace a symlink at dist/BACKROOMS3D.')
    with tempfile.TemporaryDirectory(prefix='.package-', dir=dist) as staging:
        stage = Path(staging)/'BACKROOMS3D'; (stage/'assets').mkdir(parents=True)
        shutil.copy2(ROOT/'EBOOT.PBP', stage/'EBOOT.PBP')
        for name in AUDIO:
            shutil.copy2(ROOT/'assets'/name, stage/'assets'/name)
        shutil.copy2(ROOT/'README_RU.txt', stage/'README_RU.txt')
        shutil.copy2(ROOT/'README_EN.txt', stage/'README_EN.txt')
        hashes = {str(p.relative_to(stage)): digest(p) for p in sorted(stage.rglob('*')) if p.is_file()}
        (stage/'manifest.json').write_text(json.dumps(hashes, indent=2)+'\n')
        backup = dist/f'BACKROOMS3D.backup.{stamp()}'
        if target.exists():
            target.rename(backup); LOG.info('Previous package saved: %s', backup)
        try:
            stage.rename(target)
        except OSError:
            if backup.exists(): backup.rename(target)
            raise
    release = ROOT/'release'; release.mkdir(exist_ok=True)
    archive = release/'Backrooms-PSP-Rebuilt-v2.0.zip'
    temporary = archive.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(target.rglob('*')):
            if path.is_file(): zipped.write(path, path.relative_to(dist))
    temporary.replace(archive)
    archive.with_suffix('.zip.sha256').write_text(f'{digest(archive)}  {archive.name}\n')
    LOG.info('Ready: %s', archive)

def verify():
    verify_assets(); verify_pbp(ROOT/'EBOOT.PBP')
    target = ROOT/'dist/BACKROOMS3D'
    manifest = json.loads((target/'manifest.json').read_text())
    for name, expected in manifest.items():
        if digest(target/name) != expected: raise ProjectError(f'Package checksum mismatch: {name}')
    if digest(target/'EBOOT.PBP') != digest(ROOT/'EBOOT.PBP'):
        raise ProjectError('Packaged EBOOT is stale. Run package again.')
    LOG.info('Package checksums passed.')

def build():
    verify_assets(); test()
    values=config()
    if shutil.which('psp-config'):
        run(['make', '-j4'])
    else:
        if not shutil.which('docker'): raise ProjectError('Install Docker Desktop, start it, then retry.')
        run(['docker', 'info', '--format', '{{.ServerVersion}}'], timeout=30)
        image=values['BACKROOMS_DOCKER_IMAGE']
        exists=subprocess.run(['docker','image','inspect',image], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=30).returncode==0
        if not exists:
            # Only network pulls are retried. Compiler errors fail immediately.
            for attempt in range(3):
                try:
                    run(['docker','pull','--platform',values['BACKROOMS_PLATFORM'],image], timeout=600)
                    break
                except ProjectError:
                    if attempt==2: raise
                    delay=2**(attempt+1);LOG.warning('Pull failed; retrying in %d s.', delay);time.sleep(delay)
        run(['docker','run','--rm','--platform',values['BACKROOMS_PLATFORM'],'--network','none',
             '--mount',f'type=bind,source={ROOT},target=/work','-w','/work',image,'make','-j4'])
    package(); verify()
    run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_project.py', '-v'])

def safe_game_folder(mount):
    mount=Path(mount).expanduser().absolute()
    if not mount.is_dir() or mount.is_symlink() or mount == Path('/'):
        raise ProjectError('Specify the mounted PSP Memory Stick folder, never the filesystem root.')
    if not (mount/'PSP').is_dir():
        raise ProjectError('Selected folder has no PSP directory. Select the Memory Stick root.')
    game=mount/'PSP/GAME'
    for folder in (mount/'PSP', game, game/'BACKROOMS3D'):
        if folder.is_symlink(): raise ProjectError(f'Refusing symlink destination: {folder}')
    game.mkdir(exist_ok=True)
    return game

def install(mount):
    verify(); game=safe_game_folder(mount); target=game/'BACKROOMS3D'
    if shutil.disk_usage(game).free < 16*1024*1024: raise ProjectError('Need at least 16 MiB free on the Memory Stick.')
    with tempfile.TemporaryDirectory(prefix='.backrooms-',dir=game) as tmp:
        stage=Path(tmp)/'BACKROOMS3D';shutil.copytree(ROOT/'dist/BACKROOMS3D',stage)
        for name in ('settings.ini','settings.ini.bak'):
            source=target/name
            if source.is_file() and not source.is_symlink(): shutil.copy2(source,stage/name)
        backup=game/f'BACKROOMS3D.backup.{stamp()}'
        if target.exists():target.rename(backup);LOG.info('Undo backup: %s',backup)
        try:stage.rename(target)
        except OSError:
            if backup.exists():backup.rename(target)
            raise
    LOG.info('Installed: %s. Safely eject the Memory Stick.',target)

def restore(mount,backup_name):
    game=safe_game_folder(mount)
    if not re.fullmatch(r'BACKROOMS3D\.backup\.[0-9-]+',backup_name):raise ProjectError('Use the exact backup folder name printed by install.')
    backup=game/backup_name;target=game/'BACKROOMS3D'
    if backup.is_symlink() or not backup.is_dir():raise ProjectError('Backup folder not found.')
    verify_pbp(backup/'EBOOT.PBP')
    current=game/f'BACKROOMS3D.backup.{stamp()}'
    if target.exists():target.rename(current)
    try:backup.rename(target)
    except OSError:
        if current.exists():current.rename(target)
        raise
    LOG.info('Restored %s. Replaced version retained at %s.',backup_name,current)

@contextlib.contextmanager
def lock():
    path=ROOT/'logs/project.lock'
    try:fd=os.open(path,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    except FileExistsError:raise ProjectError('Another project command is running. If it crashed, remove logs/project.lock and retry.')
    try:
        os.write(fd,str(os.getpid()).encode());os.close(fd);yield
    finally:path.unlink(missing_ok=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    for name in ('build','test','package','verify'):sub.add_parser(name)
    p=sub.add_parser('install');p.add_argument('mount')
    p=sub.add_parser('restore');p.add_argument('mount');p.add_argument('backup_name')
    args=parser.parse_args();(ROOT/'logs').mkdir(exist_ok=True)
    log_path=ROOT/'logs'/f'{args.command}-{stamp()}.log'
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(levelname)s %(message)s',
                        handlers=[logging.StreamHandler(),logging.FileHandler(log_path,encoding='utf-8')])
    try:
        with lock():
            if args.command=='install':install(args.mount)
            elif args.command=='restore':restore(args.mount,args.backup_name)
            else:globals()[args.command]()
        LOG.info('Done. Log: %s',log_path);return 0
    except (ProjectError,OSError,ValueError,subprocess.SubprocessError) as error:
        LOG.error('%s',error);LOG.error('Log: %s',log_path);return 1
    except KeyboardInterrupt:
        LOG.warning('Cancelled. Existing game/backup retained.');return 130

if __name__=='__main__':sys.exit(main())
