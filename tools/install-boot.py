#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Install the system boot unit through the already authorized project helper."""
from pathlib import Path
import fcntl
import grp
import os
import pwd
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
UNIT = 'gc573-native-boot.service'
TARGET = Path('/etc/systemd/system') / UNIT


def quoted(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') + '"'


def unit_text(project, account, groups):
    executable = quoted(str(project / 'tools/auto-start.sh').replace('$', '$$'))
    return f'''[Unit]
Description=GC573 native HDMI capture at system boot
After=local-fs.target systemd-udev-trigger.service
RequiresMountsFor={quoted(project)} {quoted(account.pw_dir)}
StartLimitIntervalSec=0

[Service]
Type=oneshot
User={account.pw_name}
SupplementaryGroups={' '.join(groups)}
Environment={quoted('HOME=' + account.pw_dir)}
Environment=XDG_RUNTIME_DIR=/run/gc573-native-boot
RuntimeDirectory=gc573-native-boot
RuntimeDirectoryMode=0700
ExecStart={executable}
RemainAfterExit=yes
TimeoutStartSec=5min
Restart=no
RestartForceExitStatus=75
RestartSec=10s

[Install]
WantedBy=multi-user.target
'''


def main():
    if os.geteuid() != 0 or len(sys.argv) != 2 or sys.argv[1] not in ('install', 'remove'):
        sys.exit('Use tools/install-startup.sh --boot or --remove-boot as your normal user.')
    if sys.argv[1] == 'remove':
        subprocess.run(['systemctl', 'disable', '--now', UNIT], check=True)
        TARGET.unlink(missing_ok=True)
        subprocess.run(['systemctl', 'daemon-reload'], check=True)
        return
    account = pwd.getpwuid(PROJECT.stat().st_uid)
    if account.pw_uid == 0:
        sys.exit('The checkout must belong to the normal desktop account.')
    groups = []
    for name in ('video', 'audio'):
        try:
            grp.getgrnam(name)
            groups.append(name)
        except KeyError:
            pass
    temporary = TARGET.with_suffix('.service.tmp')
    temporary.write_text(unit_text(PROJECT, account, groups))
    temporary.chmod(0o644)
    temporary.replace(TARGET)
    subprocess.run(['systemctl', 'daemon-reload'], check=True)
    subprocess.run(['systemctl', 'enable', UNIT], check=True)
    # Configuration is complete. Release the helper's hardware lock before
    # waiting for a service which invokes that same helper to initialize the card.
    try:
        fcntl.flock(9, fcntl.LOCK_UN)
    except OSError:
        pass  # Direct administrator invocation has no inherited helper lock.
    subprocess.run(['systemctl', 'restart', '--no-block', UNIT], check=True)
    print(f'{UNIT} enabled for boot and queued to start as {account.pw_name}.')


if __name__ == '__main__':
    main()
