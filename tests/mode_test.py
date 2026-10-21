import importlib.util
from pathlib import Path
import subprocess
import tempfile
from unittest import TestCase, mock

spec = importlib.util.spec_from_file_location('mode', Path(__file__).resolve().parents[1] / 'tools/set-mode.py')
mode = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mode)


class ModeTest(TestCase):
    def check(self, selected, reports):
        with mock.patch.object(mode, 'Path') as path, mock.patch.object(mode.time, 'sleep'):
            node = mock.Mock()
            node.read_text.side_effect = reports
            path.return_value.glob.return_value = [node]
            mode.check_acquisition(selected)
            return node.read_text.call_count

    def test_waits_for_worker_then_succeeds(self):
        self.assertEqual(self.check('passthrough', [
            'passthrough_only=1\nexternal_active=0\nexternal_error=0',
            'passthrough_only=1\nexternal_active=1\nexternal_error=0']), 2)
        self.assertEqual(self.check('capture', ['hdmi_ready=1\nhdmi_error=0']), 1)

    def test_worker_failure_prevents_preference_save(self):
        with self.assertRaisesRegex(RuntimeError, 'error -110'):
            self.check('passthrough', ['passthrough_only=1\nexternal_error=-110\nexternal_phase=2'])

    def test_wrong_loaded_mode_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            self.check('passthrough', ['hdmi_ready=1\nhdmi_error=0'])


class SwitchTest(TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.target = self.root / 'config/gc573-control/mode'
        self.unit = self.root / 'gc573-native-system.service'
        patches = [mock.patch.object(mode, 'SYSTEM_UNIT', self.unit),
                   mock.patch.object(mode.subprocess, 'run'),
                   mock.patch.object(mode, 'check_acquisition')]
        self.run_, self.check = [p.start() for p in patches][1:]
        for p in patches:
            self.addCleanup(p.stop)

    def test_system_service_restarts_with_saved_mode(self):
        self.unit.write_text('')
        self.run_.side_effect = lambda *a, **k: self.assertEqual(self.target.read_text(), 'scaled\n')
        mode.switch('scaled', self.target)
        self.run_.assert_called_once_with(
            ['sudo', 'systemctl', 'restart', 'gc573-native-system.service'], check=True)
        self.check.assert_called_once_with('scaled')
        self.assertEqual(self.target.read_text(), 'scaled\n')

    def test_system_service_failure_restores_previous_mode(self):
        self.unit.write_text('')
        self.target.parent.mkdir(parents=True)
        self.target.write_text('capture\n')
        self.check.side_effect = RuntimeError('error -110')
        with self.assertRaises(RuntimeError):
            mode.switch('passthrough', self.target)
        self.assertEqual(self.target.read_text(), 'capture\n')
        self.run_.side_effect = subprocess.CalledProcessError(1, 'sudo')
        self.target.unlink()
        with self.assertRaises(subprocess.CalledProcessError):
            mode.switch('scaled', self.target)
        self.assertFalse(self.target.exists())

    def test_helper_setup_saves_only_after_success(self):
        self.run_.side_effect = lambda *a, **k: self.assertFalse(self.target.exists())
        mode.switch('passthrough', self.target)
        self.assertEqual(self.run_.call_args.kwargs['env']['GC573_MODE'], 'passthrough')
        self.assertTrue(str(self.run_.call_args.args[0][0]).endswith('tools/auto-start.sh'))
        self.assertEqual(self.target.read_text(), 'passthrough\n')
