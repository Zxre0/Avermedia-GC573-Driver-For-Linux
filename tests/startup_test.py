#!/usr/bin/env python3
import importlib.util
from pathlib import Path
from unittest import TestCase, main, mock
import tempfile

spec = importlib.util.spec_from_file_location('startup', Path(__file__).resolve().parents[1] / 'tools/start-capture.py')
startup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(startup)
READY = {'capture_video_registered': '1', 'capture_error': '0', 'led_error': '0',
         'led_rgb_complete': '1', 'led_live_divider': '0x000003af', 'led_live_enabled': '0x0000001f'}
INPUT = {'bar0[0x00001004]': '0x00000001', 'bar0[0x00001008]': '0x00000780', 'bar0[0x0000100c]': '0x00000438'}

class StartupTest(TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.checkpoint = Path(temporary.name) / 'startup.json'
        for name, value in [('CHECKPOINT', self.checkpoint), ('BOOT_ID', 'test-boot'), ('BDF', '0000:05:00.0')]:
            patcher = mock.patch.object(startup, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_scaled_start_waits_for_deferred_capture_and_preserves_live_scaled_mode(self):
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'initialize') as base, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = 'hdmi_ready=0'
            with self.assertRaises(startup.WaitingForSignal):
                startup.initialize_scaled()
            step.assert_not_called()
            status.read_text.return_value = 'hdmi_ready=1'
            startup.initialize_scaled()
            self.assertEqual(step.call_args.args[0], '--scaled-video')
            step.reset_mock()
            base.reset_mock()
            status.read_text.return_value = 'scaled_capture=1\ncombined_error=0'
            startup.initialize_scaled()
            base.assert_not_called()
            step.assert_not_called()
            status.read_text.return_value = 'scaled_capture=1\ncombined_error=-110'
            with self.assertRaises(RuntimeError):
                startup.initialize_scaled()
            step.assert_not_called()

    def test_existing_stream_is_preserved(self):
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in READY.items())
            startup.initialize()
            step.assert_not_called()
        self.assertFalse(startup.ready(dict(READY, led_live_divider='0x0003ffff')))

    def test_switch_to_passthrough_does_not_replay_cold_resets(self):
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in READY.items())
            startup.initialize(passthrough=True)
            self.assertEqual([c.args[0] for c in step.call_args_list], ['--passthrough-video'])

    def test_capture_switch_relocks_splitter_and_preserves_prepared_receiver(self):
        data = dict(READY, passthrough_only='1', external_phase='3', external_error='0')
        for gpio, phase in [('0x0001fd7c', 9), ('0x0001fd78', 6)]:
            with self.subTest(gpio=gpio), mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
                status.exists.return_value = True
                status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in data.items())
                step.side_effect = [{}, {}, {'bar0[0x00000040]': gpio}, READY]
                startup.initialize()
                self.assertEqual([c.args[0] for c in step.call_args_list],
                                 ['--splitter-tx-finish', '--splitter-link-status', '--board-state', '--capture-wait'])
                self.assertEqual(startup.read_checkpoint()['next'], phase)
                self.assertTrue(startup.read_checkpoint()['pending'])

    def test_failed_passthrough_is_not_automatically_reset(self):
        data = dict(READY, passthrough_only='1', external_phase='3', external_error='-110')
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in data.items())
            with self.assertRaisesRegex(RuntimeError, 'did not finish'):
                startup.initialize()
            step.assert_not_called()

    def test_capture_recovery_after_checked_unsupported_format_rejection(self):
        data = dict(READY, passthrough_only='1', external_phase='3', external_error='-95',
                    external_video_phase='7', external_video_last_reg='0x13',
                    external_video_expected='0x00', external_video_observed='0x00', external_last_status='0x00000004')
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in data.items())
            step.side_effect = [{}, {}, {'bar0[0x00000040]': '0x0001fd7c'}, READY]
            startup.initialize()
            self.assertEqual(startup.read_checkpoint()['next'], 9)

    def test_recovery_refuses_incomplete_output_programming(self):
        data = dict(READY, passthrough_only='1', external_phase='3', external_error='-95',
                    external_video_phase='8', external_video_last_reg='0xc1')
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in data.items())
            with self.assertRaisesRegex(RuntimeError, 'did not finish'):
                startup.initialize()
            step.assert_not_called()

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

    def test_link_wait_handles_lock_drop_after_activation(self):
        values = [dict(splitter_link_tx1_reg03='0x17', splitter_link_rx13='0xbf', splitter_link_rx19='0x30'),
                  dict(splitter_link_tx1_reg03='0x9f', splitter_link_rx13='0xbf', splitter_link_rx19='0xb0')]
        with mock.patch.object(startup, 'step', side_effect=values) as step, mock.patch.object(startup.time, 'sleep') as sleep:
            startup.wait_splitter()
        self.assertEqual([c.args[0] for c in step.call_args_list], ['--splitter-link-status'] * 2)
        sleep.assert_called_once_with(1)

    def test_wait_is_bounded_and_uses_only_status_reads(self):
        with mock.patch.object(startup, 'step', return_value={}) as step, mock.patch.object(startup.time, 'sleep'):
            with self.assertRaises(startup.WaitingForSignal): startup.wait_splitter()
        self.assertEqual(step.call_count, 10)
        self.assertTrue(all(c.args[0] == '--splitter-link-status' for c in step.call_args_list))

    def test_deferred_handoff_does_not_replay_calibration(self):
        index = next(i for i, (mode, _) in enumerate(startup.PHASES) if mode == '--splitter-port1-output')
        startup.save_checkpoint(index)
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step', side_effect=[{}, {'bar0[0x00000040]': '0x0001fd7c'}, READY]), mock.patch.object(startup, 'run_phase') as phase:
            status.exists.return_value = False
            startup.initialize()
            phase.assert_not_called()
        self.assertEqual(startup.read_checkpoint()['next'], index)
        self.assertTrue(startup.read_checkpoint()['pending'])

    def test_partial_write_failure_blocks_automatic_replay(self):
        index = next(i for i, (mode, _) in enumerate(startup.PHASES) if mode == '--splitter-port1-output')
        startup.save_checkpoint(index, pending=True)
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step', side_effect=[{}, {'bar0[0x00000040]': '0x0001fd7c'}]), mock.patch.object(startup, 'run_phase') as phase:
            status.exists.return_value = False
            with self.assertRaisesRegex(RuntimeError, 'interrupted'): startup.initialize()
            phase.assert_not_called()

    def test_checkpoint_cannot_cross_boots_or_cards(self):
        startup.save_checkpoint(3)
        with mock.patch.object(startup, 'BOOT_ID', 'different-boot'):
            with self.assertRaisesRegex(RuntimeError, 'stale'): startup.read_checkpoint()
        with mock.patch.object(startup, 'BDF', '0000:06:00.0'):
            with self.assertRaisesRegex(RuntimeError, 'stale'): startup.read_checkpoint()

    def test_cold_plan_registers_devices_before_first_signal_wait(self):
        modes = [mode for mode, _ in startup.PHASES]
        activation = modes.index('--splitter-port1-activate')
        self.assertEqual(modes[activation + 1], 'wait-link')
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step', side_effect=[{}, {'bar0[0x00000040]': '0x0001f850'}, READY]), mock.patch.object(startup, 'run_phase') as phase:
            status.exists.return_value = False
            startup.initialize()
        self.assertEqual([call.args[0] for call in phase.call_args_list], modes[:6])
        self.assertEqual(startup.read_checkpoint()['next'], 6)
        self.assertTrue(startup.read_checkpoint()['pending'])

    def test_restart_preserves_registered_device_while_waiting_for_hdmi(self):
        startup.save_checkpoint(6, pending=True)
        data = dict(READY, hdmi_ready='0', capture_error='-67')
        with mock.patch.object(startup, 'STATUS') as status, mock.patch.object(startup, 'step') as step:
            status.exists.return_value = True
            status.read_text.return_value = '\n'.join(f'{k}={v}' for k,v in data.items())
            startup.initialize()
            step.assert_not_called()
        self.assertTrue(startup.read_checkpoint()['pending'])

if __name__ == '__main__': main()
