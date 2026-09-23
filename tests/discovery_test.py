import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('discovery',Path(__file__).resolve().parents[1]/'tools/find-card.py')
discovery=importlib.util.module_from_spec(spec);spec.loader.exec_module(discovery)
class DiscoveryTest(unittest.TestCase):
    def test_identity_and_ambiguity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with self.assertRaises(ValueError): discovery.find_card(root=root)
            for address in ('0000:03:00.0','0000:04:00.0'):
                dev=root/address;dev.mkdir()
                for key,value in discovery.IDS.items(): (dev/key).write_text(value+'\n')
                self.assertEqual(discovery.find_card(address,root),address)
            with self.assertRaises(ValueError): discovery.find_card(root=root)
            (root/'0000:04:00.0/subsystem_device').write_text('0x9999')
            self.assertEqual(discovery.find_card(root=root),'0000:03:00.0')
            with self.assertRaises(ValueError): discovery.find_card('../../etc',root)
