#!/usr/bin/env python3
"""Fixed GC573 startup using checked bring-up steps; called by the root helper."""
from pathlib import Path
import os
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
BDF = None
STATUS = Path('/sys/bus/pci/devices')


def fields(text):
    return dict(line.split('=', 1) for line in text.splitlines() if '=' in line)


def require(data, expected):
    for key, value in expected.items():
        if data.get(key) != str(value):
            raise RuntimeError(f'{key}: expected {value}, got {data.get(key, "missing")}')


def step(mode, **expected):
    result = subprocess.run([str(PROJECT / 'tools/load-probe.sh'), mode, BDF],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120)
    print(result.stdout, end='', flush=True)
    if result.returncode:
        raise RuntimeError(f'{mode} exited {result.returncode}')
    data = fields(result.stdout)
    require(data, expected)
    return data


def ready(data):
    return all(data.get(k) == str(v) for k, v in {
        'capture_video_registered': 1, 'capture_error': 0,
        'led_error': 0, 'led_rgb_complete': 1,
        'led_live_divider': '0x000003af', 'led_live_enabled': '0x0000001f'}.items())


def input_ready(data):
    try:
        valid = int(data.get('bar0[0x00001004]', '0'), 16) & 1
        width = int(data.get('bar0[0x00001008]', '0'), 16)
        height = int(data.get('bar0[0x0000100c]', '0'), 16)
        return bool(valid and (width, height) in ((1920, 1080), (1280, 720)))
    except ValueError:
        return False


def initialize():
    if STATUS.exists() and ready(fields(STATUS.read_text())):
        print('GC573 native capture and RGB are already ready; preserving the open device.')
        return
    data = step('--fpga-status')
    if not input_ready(data):
        board = step('--board-state')
        # Cold sequence is restricted to the power-on GPIO state seen on this board.
        require(board, {'bar0[0x00000040]': '0x0001f850'})
        step('--receiver-id', block_error=0, receiver_setup_complete=1)
        step('--receiver-init', init_error=0, init_table_complete=1)
        step('--receiver-calibrate', cal_error=0, cal_completion_seen=1, cal_cleanup_complete=1)
        step('--splitter-startup', splitter_error=0, splitter_start_complete=1)
        step('--splitter-prepare', splitter_prepare_error=0, splitter_prepare_complete=1)
        step('--splitter-tx-finish', splitter_ports_error=0, splitter_ports_tail_complete=1)
        step('--receiver-input', input_error=0, input_setup_complete=1, input_hpd_verified=1)
        step('--splitter-input', splitter_hpd_error=0, splitter_hpd_setup_complete=1)
        step('--splitter-edid-enable', splitter_hpd_error=0, splitter_ddc_configured=1,
             splitter_hpd_lock_seen=1)
        step('--splitter-port1-activate', splitter_hpd_error=0, splitter_hpd_setup_complete=1)
        step('--splitter-port1-output', splitter_video_error=0, splitter_video_output_enabled=1)
        step('--receiver-output', receiver_video_error=0, receiver_video_output_enabled=1)
        if not input_ready(step('--fpga-status')):
            raise RuntimeError('The FPGA does not report the supported 720p/1080p input.')
    step('--capture-video', capture_error=0, capture_video_registered=1,
         led_error=0, led_rgb_complete=1)
    print('GC573 native capture and RGB lighting are ready.')


if __name__ == '__main__':
    if os.geteuid() != 0 or len(sys.argv) != 2:
        sys.exit('Use the installed GC573 helper with --start.')
    try:
        import re
        BDF = sys.argv[1]
        if not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]', BDF):
            raise RuntimeError('Invalid PCI address')
        STATUS = Path('/sys/bus/pci/devices') / BDF / 'bringup_status'
        initialize()
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as exc:
        sys.exit(f'GC573 startup stopped: {exc}')
