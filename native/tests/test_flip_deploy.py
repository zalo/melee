"""Exercise one-time disc installation and the runtime-only upload boundary."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('flip_deploy', Path(__file__).parents[1] / 'tools/flip_deploy.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeploymentTest(unittest.TestCase):
    def test_existing_disc_is_not_uploaded(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'disc.rvz'
            image.write_bytes(b'test-image')
            checksum = hashlib.sha256(image.read_bytes()).hexdigest()
            with patch('sys.argv', ['flip_deploy', 'disc', str(image), '--nodtool', 'nodtool']), \
                 patch.object(deploy.subprocess, 'check_output', return_value=checksum + '  disc.img\n'), \
                 patch.object(deploy.subprocess, 'run') as run:
                deploy.main()
                self.assertFalse(any('push' in call.args[0] for call in run.call_args_list))

    def test_different_disc_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'disc.rvz'
            image.write_bytes(b'new-image')
            with patch('sys.argv', ['flip_deploy', 'disc', str(image), '--nodtool', 'nodtool']), \
                 patch.object(deploy.subprocess, 'check_output', return_value='0' * 64 + '  disc.img\n'), \
                 patch.object(deploy.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'Refusing to replace'):
                    deploy.main()
                self.assertFalse(any('push' in call.args[0] for call in run.call_args_list))

    def test_bundle_containing_rom_is_rejected_before_any_upload(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp)
            (bundle / 'melee_native').write_bytes(b'binary')
            (bundle / 'disc.rvz').write_bytes(b'image')
            with patch('sys.argv', ['flip_deploy', 'build', tmp]), \
                 patch.object(deploy.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'allowlist'):
                    deploy.main()
                self.assertFalse(any('push' in call.args[0] for call in run.call_args_list))

    def test_failed_checksum_does_not_install_disc(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'disc.rvz'
            image.write_bytes(b'new-image')
            with patch('sys.argv', ['flip_deploy', 'disc', str(image), '--nodtool', 'nodtool']), \
                 patch.object(deploy.subprocess, 'check_output', side_effect=['', '', '0' * 64]) as shell, \
                 patch.object(deploy.subprocess, 'run'):
                with self.assertRaisesRegex(RuntimeError, 'checksum failed'):
                    deploy.main()
                self.assertFalse(any(call.args[0][-1].startswith('mv ') for call in shell.call_args_list))


if __name__ == '__main__':
    unittest.main()
