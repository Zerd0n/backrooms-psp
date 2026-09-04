"""Integration tests use a temporary Memory Stick, never a real installation."""
import importlib.util
import json
from pathlib import Path
import shutil
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

if __name__=='__main__':unittest.main()
