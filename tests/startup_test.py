#!/usr/bin/env python3
import importlib.util
from pathlib import Path
from unittest import TestCase, main, mock

spec = importlib.util.spec_from_file_location('startup', Path(__file__).resolve().parents[1] / 'tools/start-capture.py')
startup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(startup)
READY = {'capture_video_registered': '1', 'capture_error': '0', 'led_error': '0',
         'led_rgb_complete': '1', 'led_live_divider': '0x000003af', 'led_live_enabled': '0x0000001f'}
INPUT = {'bar0[0x00001004]': '0x00000001', 'bar0[0x00001008]': '0x00000780', 'bar0[0x0000100c]': '0x00000438'}

class StartupTest(TestCase):
    def test_existing_stream_is_preserved(self):
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in READY.items())
            startup.initialize()
            step.assert_not_called()
        self.assertFalse(startup.ready(dict(READY, led_live_divider='0x0003ffff')))

    def test_prepared_input_needs_no_chip_resets(self):
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step', side_effect=[INPUT, READY]) as step:
            status.exists.return_value = False
            startup.initialize()
            self.assertEqual([c.args[0] for c in step.call_args_list], ['--fpga-status', '--capture-video'])

    def test_unknown_board_state_stops_before_writes(self):
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step', side_effect=[{}, {'bar0[0x00000040]': '0xffffffff'}]) as step:
            status.exists.return_value = False
            with self.assertRaises(RuntimeError): startup.initialize()
            self.assertEqual(step.call_count, 2)

    def test_failed_calibration_cannot_enable_dma(self):
        calls = []
        def fake(mode, **expected):
            calls.append(mode)
            if mode == '--board-state': return {'bar0[0x00000040]': '0x0001f850'}
            if mode == '--receiver-calibrate': raise RuntimeError('calibration failed')
            return {}
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step', side_effect=fake):
            status.exists.return_value = False
            with self.assertRaises(RuntimeError): startup.initialize()
        self.assertEqual(calls[-1], '--receiver-calibrate')
        self.assertNotIn('--capture-video', calls)

    def test_exit_zero_does_not_hide_hardware_error(self):
        with mock.patch.object(startup.subprocess, 'run') as run:
            run.return_value = mock.Mock(returncode=0, stdout='capture_error=-110\n')
            with self.assertRaises(RuntimeError): startup.step('--capture-video', capture_error=0)

if __name__ == '__main__': main()
