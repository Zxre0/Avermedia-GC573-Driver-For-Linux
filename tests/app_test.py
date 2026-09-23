import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
spec = importlib.util.spec_from_file_location('app', Path(__file__).resolve().parents[1] / 'app/gc573_control.py')
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)

class AppTest(unittest.TestCase):
    def test_input_status_is_live_and_preserves_fractional_fps(self):
        self.assertEqual(app.parse_status('input_fps_milli=59940\ninput_present=0\nstage=x\ninvalid\n'), {'input_fps_milli':59940, 'input_present':0})
    def test_reject_invalid_controls_before_open(self):
        for values in ((3,0,100),(0,0xffffff+1,50),(0,0,-1),(True,0,100)):
            with patch.object(app.os, 'open') as opened:
                with self.assertRaises(ValueError):app.apply('/dev/video0',*values)
                opened.assert_not_called()
    def test_ioctl_failure_closes_device(self):
        with patch.object(app.os,'open',return_value=77), patch.object(app.os,'close') as close, patch.object(app.fcntl,'ioctl',side_effect=OSError('unplugged')):
            with self.assertRaises(OSError):app.apply('/dev/video0',1,0xff0000,80)
            close.assert_called_once_with(77)
    def test_rgb_ioctl_payloads(self):
        with patch.object(app.os,'open',return_value=77), patch.object(app.os,'close'), patch.object(app.fcntl,'ioctl') as ioctl:
            app.apply('/dev/video0',2,0x112233,65)
            self.assertEqual([struct.unpack('Ii', c.args[2]) for c in ioctl.call_args_list],[(app.CID_COLOR,0x112233),(app.CID_BRIGHTNESS,65),(app.CID_MODE,2)])
