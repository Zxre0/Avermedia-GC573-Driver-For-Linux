import importlib.util
from pathlib import Path
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
