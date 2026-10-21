#!/usr/bin/env python3
"""Switch GC573 operating mode and save it for the installed startup service."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

PROJECT = Path(__file__).resolve().parents[1]
SYSTEM_UNIT = Path('/etc/systemd/system/gc573-native-system.service')


def check_acquisition(mode):
    paths = list(Path('/sys/bus/pci/devices').glob('*/bringup_status'))
    if len(paths) != 1:
        raise RuntimeError('Expected exactly one loaded GC573 status node')
    for _ in range(30):
        data = dict(line.split('=', 1) for line in paths[0].read_text().splitlines() if '=' in line)
        if ((data.get('passthrough_only') == '1') != (mode == 'passthrough') or
                (data.get('scaled_capture') == '1') != (mode == 'scaled')):
            raise RuntimeError('Loaded mode does not match the requested mode')
        prefix = 'external' if mode == 'passthrough' else 'combined' if mode == 'scaled' else 'hdmi'
        if int(data.get(prefix + '_error', '0')):
            raise RuntimeError(f"HDMI setup stopped at phase {data.get(prefix + '_phase')}: "
                               f"error {data[prefix + '_error']}")
        if data.get('external_active' if mode == 'passthrough' else 'hdmi_ready') == '1':
            return
        time.sleep(1)
    print('Still waiting for HDMI acquisition; use the control app to check status.')


def write_mode(target, text):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix('.tmp')
    temporary.write_text(text)
    temporary.replace(target)


def switch(selected, target):
    if not SYSTEM_UNIT.exists():
        # Development setup: the password-free helper loads the module.
        subprocess.run([str(PROJECT / 'tools/auto-start.sh')],
                       env=dict(os.environ, GC573_MODE=selected), check=True)
        check_acquisition(selected)
        write_mode(target, selected + '\n')
        return
    # The root-owned boot service loads the saved mode; restore the previous
    # preference if the switch fails.
    previous = target.read_text() if target.exists() else None
    write_mode(target, selected + '\n')
    try:
        subprocess.run(['sudo', 'systemctl', 'restart', SYSTEM_UNIT.name], check=True)
        check_acquisition(selected)
    except BaseException:
        if previous is None:
            target.unlink(missing_ok=True)
        else:
            write_mode(target, previous)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('capture', 'scaled', 'passthrough'))
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('Run as your desktop user; sudo is requested to reload the driver.')
    print('Close OBS, GC573 Preview and other capture applications before switching. '
          'Passthrough mode disables host video/audio capture.', flush=True)
    active = subprocess.run(['systemctl', '--user', 'is-active', '--quiet', 'wireplumber']).returncode == 0
    try:
        if active:
            subprocess.run(['systemctl', '--user', 'stop', 'wireplumber'], check=True)
        target = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'gc573-control/mode'
        switch(args.mode, target)
        print(f'Saved {args.mode} mode for startup. HDMI acquisition continues in the background.')
    finally:
        if active:
            subprocess.run(['systemctl', '--user', 'start', 'wireplumber'], check=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        sys.exit(f'Mode switch failed; preference was not updated: {exc}')
