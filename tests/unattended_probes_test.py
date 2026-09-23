#!/usr/bin/env python3
"""Exercise the launcher without sudo privileges or hardware operations."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class UnattendedProbesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "tools").mkdir()
        (self.root / "bin").mkdir()
        self.helper = self.root / "helper"
        self.helper.write_text("#!/bin/sh\nexit 0\n")
        self.helper.chmod(0o755)
        self.client = self.root / "tools/run-probe.sh"
        source = (PROJECT / "tools/run-probe.sh").read_text()
        self.client.write_text(source.replace(
            "helper=/usr/local/sbin/gc573-codex-probe",
            f"helper={self.helper}",
        ))
        self.client.chmod(0o755)
        sudo = self.root / "bin/sudo"
        sudo.write_text("""#!/bin/bash
[[ $1 == -n ]] || exit 99
printf '%s\\n' "$*" >> "$CALLS"
if [[ $3 == --check ]]; then
    echo 'mock access check'
    exit "${CHECK_EXIT:-0}"
fi
echo 'mock probe stdout'
echo 'mock probe stderr' >&2
exit "${PROBE_EXIT:-0}"
""")
        sudo.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{self.root}/bin:/usr/bin:/bin",
                        CALLS=str(self.root / "calls"))

    def run_client(self, *args, **env):
        return subprocess.run([str(self.client), *args],
                              env=dict(self.env, **env), text=True,
                              capture_output=True)

    def test_check_does_not_probe_or_log(self):
        result = self.run_client("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "reports").exists())
        self.assertEqual(len((self.root / "calls").read_text().splitlines()), 1)

    def test_probe_logs_both_streams_and_retains_failure(self):
        for status in ("0", "7"):
            result = self.run_client("--splitter-tx-ports", PROBE_EXIT=status)
            self.assertEqual(result.returncode, int(status), result.stderr)
        logs = list((self.root / "reports").glob("auto-*.log"))
        self.assertEqual(len(logs), 2)
        for log in logs:
            self.assertEqual(log.read_text(), "mock probe stdout\nmock probe stderr\n")

    def test_denied_access_stops_before_probe(self):
        result = self.run_client("--identity", CHECK_EXIT="1")
        self.assertEqual(result.returncode, 1)
        self.assertFalse((self.root / "reports").exists())
        self.assertEqual(len((self.root / "calls").read_text().splitlines()), 1)

    def test_missing_installation(self):
        self.helper.unlink()
        result = self.run_client("--identity")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Run once:", result.stderr)
        self.assertFalse((self.root / "calls").exists())

    def test_invalid_arguments_never_invoke_sudo(self):
        for args in ((), ("--identity", "extra"), ("--identity;id",),
                     ("/bin/sh",), ("--identity\n",)):
            self.assertEqual(self.run_client(*args).returncode, 2)
        self.assertFalse((self.root / "calls").exists())

    @unittest.skipIf(os.geteuid() == 0, "Checks unprivileged guards only")
    def test_privileged_entry_points_refuse_nonroot(self):
        for filename in ("gc573-codex-probe", "enable-unattended-probes.sh"):
            result = subprocess.run([str(PROJECT / "tools" / filename)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stderr)


if __name__ == "__main__":
    unittest.main()
