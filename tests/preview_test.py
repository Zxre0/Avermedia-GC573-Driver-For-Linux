import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

APP = Path(__file__).resolve().parents[1] / 'app'
sys.path.insert(0, str(APP))
spec = importlib.util.spec_from_file_location('preview', APP / 'gc573_preview.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


class PreviewTest(unittest.TestCase):
    def test_measured_capture_rate_and_stream_counter_reset(self):
        rate = preview.FrameRate()
        self.assertIsNone(rate.update(500, 1))
        self.assertIsNone(rate.update(560, 1.5))
        self.assertEqual(rate.update(620, 2), 120)
        self.assertEqual(rate.update(740, 4), 60)
        self.assertIsNone(rate.update(0, 4.5))
        self.assertEqual(rate.update(120, 5.5), 120)

    def test_native_capture_requires_both_link_capacity_and_combined_mode(self):
        state = dict(scaled_capture=1, capture_native_1440p120=1, input_width=2560)
        self.assertEqual(preview.preview_format(state), (2560, 1440, 120))
        self.assertEqual(preview.preview_format(state, False), (1920, 1080, 60))
        self.assertEqual(preview.preview_format(dict(state, capture_native_1440p120=0)), (1920, 1080, 60))
        self.assertEqual(preview.preview_format(dict(state, scaled_capture=0)), (1920, 1080, 60))
        self.assertEqual(preview.preview_format(dict(state, input_width=1280)), (1280, 720, 60))

    def test_capture_guards_do_not_treat_registered_passthrough_as_video(self):
        good = dict(hdmi_ready=1, input_present=1, input_width=1920, input_height=1080)
        self.assertIsNone(preview.capture_problem(good))
        for bad in (dict(passthrough_only=1), dict(hdmi_ready=0), dict(input_present=0),
                    dict(hdmi_error=-110), dict(input_width=2560, input_height=1440)):
            with self.subTest(bad=bad):
                self.assertIsNotNone(preview.capture_problem(dict(good, **bad)))

    def test_scaled_capture_accepts_1440_only_in_combined_mode(self):
        state = dict(scaled_capture=1, combined_ready=1, hdmi_ready=1,
                     input_present=1, input_width=2560, input_height=1440)
        self.assertIsNone(preview.capture_problem(state))
        self.assertIsNotNone(preview.capture_problem(dict(state, combined_error=-110)))
        self.assertIsNotNone(preview.capture_problem(dict(state, input_width=3840, input_height=2160)))

    def test_scaled_format_wait_clears_when_supported_input_returns(self):
        state = dict(scaled_capture=1, combined_ready=1, hdmi_ready=1,
                     input_present=1, input_width=2560, input_height=1440,
                     external_active=0, external_waiting=1)
        self.assertIsNone(preview.capture_problem(state))
        self.assertIn('resume automatically', preview.capture_problem(
            dict(state, external_format_rejected=1)))
        self.assertIsNone(preview.capture_problem(dict(state, external_format_rejected=0)))

    def test_slow_renderer_discards_stale_frames(self):
        slot = preview.LatestFrame()
        for i in range(100):
            slot.put(i)
        self.assertEqual(slot.take(), 99)
        self.assertIsNone(slot.take())
        self.assertEqual(slot.count, 100)

    def test_audio_is_matched_to_same_pci_card(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pci = root / 'pci-card'
            pci.mkdir()
            for number, target in [(1, root / 'unrelated'), (6, pci)]:
                card = root / f'card{number}'
                card.mkdir()
                (card / 'device').symlink_to(target)
            self.assertEqual(preview.audio_device(pci / 'bringup_status', root), 'hw:6,0')
            self.assertIsNone(preview.audio_device(root / 'missing/bringup_status', root))
