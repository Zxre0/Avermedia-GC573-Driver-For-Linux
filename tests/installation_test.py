#!/usr/bin/env python3
"""Exercise removal and installer failures in a temporary filesystem, never on hardware."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('remove_user', PROJECT / 'tools/remove-user.py')
remove_user = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remove_user)


class InstallationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = self.root / 'checkout with spaces'
        self.project.mkdir()
        self.home = self.root / 'home'
        self.home.mkdir()
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.calls = self.root / 'calls'
        self.env = dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / 'config'),
                        XDG_DATA_HOME=str(self.home / 'data'), XDG_RUNTIME_DIR=str(self.home / 'run'),
                        PATH=f'{self.bin}:/usr/bin:/bin', CALLS=str(self.calls))

    def put(self, path, text='test', executable=False):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        if executable:
            path.chmod(0o755)
        return path

    def fake(self, name, body):
        return self.put(self.bin / name, '#!/bin/bash\n' + body, True)

    def test_user_cleanup_is_complete_repeatable_and_preserves_personal_files(self):
        removed = [self.home / '.local/bin/gc573-control', self.home / 'data/applications/gc573-control.desktop',
                   self.home / '.local/bin/gc573-preview', self.home / 'data/applications/gc573-preview.desktop',
                   self.home / 'config/gc573-control/settings.json', self.home / 'run/gc573-native-start.lock']
        removed += [self.project / item for item in ('.build/modules/kernel/gc573_native.ko', '.build/tests/block_test',
                    'driver/gc573_pci.o', 'driver/.gc573_pci.o.cmd', 'driver/gc573_native.mod.c',
                    'driver/Module.symvers', 'driver/modules.order', 'app/__pycache__/test.pyc',
                    'reports/auto-test.log', 'reports/automatic-build.log')]
        preserved = [self.project / item for item in ('driver/gc573_pci.c', '.build/obs-backup/user.ini',
                     'reports/capture.mkv', 'reports/evidence.log', '.reference/research.bin', 'README.md')]
        for path in removed + preserved:
            self.put(path)
        for _ in range(2):
            remove_user.cleanup(self.project, self.home, self.env)
        self.assertTrue(all(not p.exists() for p in removed))
        self.assertTrue(all(p.exists() for p in preserved))

    def test_settings_can_be_kept_and_symlink_targets_are_preserved(self):
        settings = self.put(self.home / 'config/gc573-control/settings.json')
        outside = self.put(self.root / 'outside/keep.txt')
        (self.project / '.build').mkdir()
        (self.project / '.build/modules').symlink_to(outside.parent, target_is_directory=True)
        remove_user.cleanup(self.project, self.home, self.env, keep_settings=True)
        self.assertTrue(settings.exists())
        self.assertTrue(outside.exists())
        self.assertFalse((self.project / '.build/modules').is_symlink())

    def system_remover(self):
        source = (PROJECT / 'tools/remove-system.sh').read_text()
        # Redirect every system path and privileged operation in this fixture.
        source = source.replace('export PATH=/usr/bin:/usr/sbin', 'export PATH="$PATH"')
        source = source.replace('$EUID -eq 0 && ', '')
        for prefix in ('/etc/', '/usr/local/', '/run/', '/sys/'):
            source = source.replace(prefix, str(self.root) + prefix)
        self.fake('install', 'mkdir -p -- "${@: -1}"\n')
        self.fake('systemctl', 'echo "systemctl $*" >> "$CALLS"\n')
        self.fake('rmmod', 'echo "rmmod $*" >> "$CALLS"\nexit "${BUSY:-0}"\n')
        for item in ('etc/systemd/system/gc573-native-boot.service', 'etc/sudoers.d/gc573-codex',
                     'usr/local/sbin/gc573-codex-probe', 'run/gc573-codex/startup-card.json',
                     'run/gc573-codex/startup-card.tmp', 'run/gc573-native-boot/gc573-native-start.lock'):
            self.put(self.root / item)
        (self.root / 'sys/module/gc573_native').mkdir(parents=True)
        return self.put(self.root / 'remove-system.sh', source, True)

    def test_busy_module_stops_cleanup_and_retry_succeeds(self):
        script = self.system_remover()
        result = subprocess.run([script], env=dict(self.env, BUSY='1'), capture_output=True, text=True)
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertTrue((self.root / 'etc/sudoers.d/gc573-codex').exists())
        self.assertTrue((self.root / 'run/gc573-codex/startup-card.json').exists())
        self.assertFalse((self.root / 'etc/systemd/system/gc573-native-boot.service').exists())
        for _ in range(2):
            result = subprocess.run([script], env=self.env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / 'etc/sudoers.d/gc573-codex').exists())
        self.assertFalse((self.root / 'usr/local/sbin/gc573-codex-probe').exists())
        self.assertFalse((self.root / 'run/gc573-codex').exists())
        self.assertNotIn('rmmod -f', self.calls.read_text())

    def test_login_removal_is_idempotent_without_a_user_service(self):
        tools = self.project / 'tools'
        tools.mkdir()
        shutil.copy(PROJECT / 'tools/install-startup.sh', tools)
        self.fake('systemctl', 'echo "$*" >> "$CALLS"\n')
        for _ in range(2):
            result = subprocess.run([tools / 'install-startup.sh', '--remove'], env=self.env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.calls.exists())

    def installer(self):
        source = (PROJECT / 'install.sh').read_text().replace('$EUID -ne 0', '1 -eq 1')
        source = source.replace('/run/systemd/system', str(self.root))
        self.fake('python3', 'echo "python3 $*" >> "$CALLS"\n')
        self.fake('sudo', 'echo "sudo $*" >> "$CALLS"\n"$@"\n')
        for tool in ('build.sh', 'enable-unattended-probes.sh', 'run-probe.sh', 'install-app.sh', 'install-startup.sh'):
            body = '#!/bin/bash\necho "' + tool + ' $*" >> "$CALLS"\n'
            if tool == 'build.sh':
                body += 'exit "${BUILD_EXIT:-0}"\n'
            self.put(self.project / 'tools' / tool, body, True)
        return self.put(self.project / 'install.sh', source, True)

    def test_build_failure_never_installs_privileged_helper_or_startup(self):
        script = self.installer()
        result = subprocess.run([script, '--skip-deps'], env=dict(self.env, BUILD_EXIT='1'), capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn('sudo', self.calls.read_text())

    def test_install_configures_app_and_boot_after_build(self):
        script = self.installer()
        result = subprocess.run([script, '--skip-deps'], env=self.env, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls.read_text()
        self.assertLess(calls.index('build.sh'), calls.index('enable-unattended-probes.sh'))
        self.assertLess(calls.index('install-app.sh'), calls.index('install-startup.sh --boot'))

    def test_arch_dependencies_select_running_kernel_variant(self):
        script = self.installer()
        kernel_root = self.root / 'modules'
        self.fake('uname', 'echo test-kernel\n')
        self.put(kernel_root / 'test-kernel/pkgbase', 'linux-cachyos-lts\n')
        script.write_text(script.read_text().replace('/lib/modules/', str(kernel_root) + '/'))
        self.fake('pacman', 'echo "pacman $*" >> "$CALLS"\n')
        result = subprocess.run([script], env=self.env, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls.read_text()
        self.assertIn('pacman -S --needed', calls)
        self.assertIn('linux-cachyos-lts-headers', calls)
        self.assertIn('obs-studio', calls)
        self.assertLess(calls.index('pacman -S'), calls.index('build.sh'))

    def test_debian_dependencies_and_failure_stop_before_build(self):
        script = self.installer()
        script.write_text(script.read_text().replace('if command -v pacman >/dev/null;', 'if false;'))
        self.fake('uname', 'echo test-kernel\n')
        self.fake('apt-get', 'echo "apt-get $*" >> "$CALLS"\nexit "${APT_EXIT:-0}"\n')
        result = subprocess.run([script], env=dict(self.env, APT_EXIT='42'), capture_output=True)
        self.assertEqual(result.returncode, 42)
        self.assertNotIn('build.sh', self.calls.read_text())
        self.calls.unlink()
        result = subprocess.run([script], env=self.env, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls.read_text()
        self.assertIn('linux-headers-test-kernel', calls)
        self.assertIn('python3-gi gir1.2-gtk-4.0', calls)

    def test_uninstall_restores_wireplumber_when_second_unload_fails(self):
        source = (PROJECT / 'uninstall.sh').read_text().replace('$EUID -ne 0', '1 -eq 1')
        script = self.put(self.project / 'uninstall.sh', source, True)
        self.put(self.project / 'tools/install-startup.sh', '#!/bin/bash\nexit 0\n', True)
        self.fake('sudo', 'echo "sudo $*" >> "$CALLS"\nexit 3\n')
        self.fake('systemctl', 'echo "systemctl $*" >> "$CALLS"\n')
        self.fake('python3', 'echo "user cleanup" >> "$CALLS"\n')
        result = subprocess.run([script], env=self.env, capture_output=True)
        self.assertEqual(result.returncode, 3)
        calls = self.calls.read_text()
        self.assertIn('stop wireplumber.service', calls)
        self.assertTrue(calls.rstrip().endswith('start wireplumber.service'))
        self.assertNotIn('user cleanup', calls)


if __name__ == '__main__':
    unittest.main()
