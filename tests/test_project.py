"""Integration tests use a temporary Memory Stick, never a real installation."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest

REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('project', REPO/'tools/project.py')
project=importlib.util.module_from_spec(spec)
spec.loader.exec_module(project)

class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='backrooms-package-test-')
        self.root=Path(self.temp.name)/'project';self.root.mkdir()
        self.original_root=project.ROOT;project.ROOT=self.root
        (self.root/'config').mkdir();(self.root/'assets').mkdir()
        shutil.copy2(REPO/'EBOOT.PBP',self.root/'EBOOT.PBP')
        shutil.copy2(REPO/'README_RU.txt',self.root/'README_RU.txt')
        shutil.copy2(REPO/'README_EN.txt',self.root/'README_EN.txt')
        manifest={}
        for name in (*project.AUDIO,'ICON0.PNG','PIC1.PNG'):
            p=self.root/'assets'/name;shutil.copy2(REPO/'assets'/name,p)
            manifest[f'assets/{name}']=project.digest(p)
        (self.root/'config/assets.sha256.json').write_text(json.dumps(manifest))
        self.mount=Path(self.temp.name)/'stick';(self.mount/'PSP').mkdir(parents=True)

    def tearDown(self):
        project.ROOT=self.original_root;self.temp.cleanup()

    def test_package_install_restore(self):
        project.package();project.verify();project.install(self.mount)
        target=self.mount/'PSP/GAME/BACKROOMS3D'
        (target/'settings.ini').write_text('volume=40\n')
        old_hash=project.digest(target/'EBOOT.PBP')
        project.install(self.mount)
        backup=next((self.mount/'PSP/GAME').glob('BACKROOMS3D.backup.*'))
        self.assertEqual((target/'settings.ini').read_text(),'volume=40\n')
        project.restore(self.mount,backup.name)
        self.assertEqual(project.digest(target/'EBOOT.PBP'),old_hash)
        self.assertEqual((target/'settings.ini').read_text(),'volume=40\n')
        self.assertTrue(list((self.root/'release').glob('*.zip')))

    def test_reject_corrupt_package_and_asset(self):
        project.package()
        (self.root/'dist/BACKROOMS3D/EBOOT.PBP').write_bytes(b'broken')
        with self.assertRaises(project.ProjectError):project.verify()
        (self.root/'assets/chase.raw').write_bytes(b'changed')
        with self.assertRaises(project.ProjectError):project.verify_assets()

    def test_destination_validation(self):
        with self.assertRaises(project.ProjectError):project.safe_game_folder('/')
        with self.assertRaises(project.ProjectError):project.safe_game_folder(self.root)
        (self.mount/'PSP/GAME').symlink_to(self.root,target_is_directory=True)
        with self.assertRaises(project.ProjectError):project.safe_game_folder(self.mount)

    def test_configuration_is_data(self):
        (self.root/'.env').write_text('BACKROOMS_DOCKER_IMAGE=$(touch /tmp/unsafe)\n')
        with self.assertRaises(project.ProjectError):project.config()
        (self.root/'.env').write_text('UNKNOWN=1\n')
        with self.assertRaises(project.ProjectError):project.config()

class GpuLayoutTests(unittest.TestCase):
    """Exercise the linked-ELF check, including the actual v2.0 list address."""
    def elf(self, *, address=0x088d11c0, size=1048576, omit=None):
        names = b'\0display_list\0geometry\0vertices\0'
        symbols = bytearray(16)  # ELF's undefined symbol
        for name, value, length in (('display_list', address, size),
                                    ('geometry', 0x089d1900, 576000),
                                    ('vertices', 0x08a5e340, 288000)):
            if name != omit:
                symbols += struct.pack('<IIIBBH', names.index(name.encode()), value,
                                       length, 1, 0, 1)
        string_offset = 52+3*40
        symbol_offset = string_offset+len(names)
        header = bytearray(52)
        header[:6] = b'\x7fELF\x01\x01'
        struct.pack_into('<I', header, 32, 52)
        struct.pack_into('<HH', header, 46, 40, 3)
        sections = bytes(40)
        sections += struct.pack('<10I', 0, 3, 0, 0, string_offset, len(names), 0, 0, 1, 0)
        sections += struct.pack('<10I', 0, 2, 0, 0, symbol_offset, len(symbols), 1, 0, 4, 16)
        return header+sections+names+symbols

    def check(self, data):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'game.elf'
            path.write_bytes(data)
            project.verify_gpu_layout(path)

    def test_cache_isolated_buffers(self):
        self.check(self.elf())

    def test_original_v2_address_is_rejected(self):
        # The shipped ELF placed the list at 0x088d1190, in the same cache line
        # as initialized/overflow/used at 0x088d1180..0x088d1187.
        with self.assertRaisesRegex(project.ProjectError, 'display_list shares'):
            self.check(self.elf(address=0x088d1190))

    def test_partial_last_cache_line_is_rejected(self):
        with self.assertRaisesRegex(project.ProjectError, 'display_list shares'):
            self.check(self.elf(size=1048576-4))

    def test_missing_symbols_and_truncated_elf(self):
        with self.assertRaisesRegex(project.ProjectError, 'Missing GPU symbols'):
            self.check(self.elf(omit='display_list'))
        with self.assertRaises(project.ProjectError):
            self.check(self.elf()[:80])

if __name__=='__main__':unittest.main()
