#!/usr/bin/env python3
"""Checked, resumable GC573 startup; invoked by the privileged project helper."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time

PROJECT = Path(__file__).resolve().parents[1]
BDF = None
STATUS = Path('/sys/bus/pci/devices')
CHECKPOINT = None
BOOT_ID = None


class WaitingForSignal(RuntimeError):
    """Exit 75 asks the startup service to resume after a short delay."""


class StepError(RuntimeError):
    def __init__(self, message, data):
        super().__init__(message)
        self.data = data


def fields(text):
    return dict(line.split('=', 1) for line in text.splitlines() if '=' in line)


def require(data, expected):
    for key, value in expected.items():
        if data.get(key) != str(value):
            raise StepError(f'{key}: expected {value}, got {data.get(key, "missing")}', data)


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
    return data.get('capture_error') in ('0', '-67') and all(data.get(k) == str(v) for k, v in {
        'capture_video_registered': 1,
        'led_error': 0, 'led_rgb_complete': 1,
        'led_live_divider': '0x000003af', 'led_live_enabled': '0x0000001f'}.items())


def number(data, key):
    try:
        return int(data.get(key, '0'), 0)
    except ValueError:
        return 0


def input_ready(data):
    return bool(number(data, 'bar0[0x00001004]') & 1 and
                (number(data, 'bar0[0x00001008]'), number(data, 'bar0[0x0000100c]'))
                in ((1920, 1080), (1280, 720)))


def poll(mode, expected, predicate, description):
    # Read-only observations: never replay GPIO/reset/calibration while waiting.
    for attempt in range(10):
        data = step(mode, **expected)
        if predicate(data):
            return
        if attempt != 9:
            time.sleep(1)
    raise WaitingForSignal(description)


def wait_splitter(power_only=False):
    def locked(data):
        if power_only:
            return number(data, 'splitter_link_rx13') & 1
        return (number(data, 'splitter_link_tx1_reg03') & 7 == 7 and
                number(data, 'splitter_link_rx13') & 0x10 and
                number(data, 'splitter_link_rx19') & 0x80)
    poll('--splitter-link-status', {'splitter_link_error': 0, 'splitter_link_complete': 1},
         locked, 'Waiting for HDMI source power' if power_only else 'Waiting for HDMI link lock')


def wait_receiver(power_only=False):
    poll('--receiver-status', {'signal_error': 0},
         lambda data: data.get('port0_5v') == '1' if power_only else
         data.get('port0_clock_valid') == '1' and data.get('scdt') == '1',
         'Waiting for receiver source power' if power_only else 'Waiting for capture receiver lock')


# Advance only after each checked operation succeeds. Wait stages can be resumed.
PHASES = [
    ('--receiver-id', dict(block_error=0, receiver_setup_complete=1)),
    ('--receiver-init', dict(init_error=0, init_table_complete=1)),
    ('--receiver-calibrate', dict(cal_error=0, cal_completion_seen=1, cal_cleanup_complete=1)),
    ('--splitter-startup', dict(splitter_error=0, splitter_start_complete=1)),
    ('--splitter-prepare', dict(splitter_prepare_error=0, splitter_prepare_complete=1)),
    ('--splitter-tx-finish', dict(splitter_ports_error=0, splitter_ports_tail_complete=1)),
    ('wait-receiver-power', {}),
    ('--receiver-input', dict(input_error=0, input_setup_complete=1, input_hpd_verified=1)),
    ('wait-source-power', {}),
    ('--splitter-input', dict(splitter_hpd_error=0, splitter_hpd_setup_complete=1)),
    ('--splitter-edid-enable', dict(splitter_hpd_error=0, splitter_ddc_configured=1)),
    ('wait-link', {}),
    ('--splitter-port1-activate', dict(splitter_hpd_error=0, splitter_hpd_setup_complete=1)),
    ('wait-link', {}),
    ('--splitter-port1-output', dict(splitter_video_error=0, splitter_video_output_enabled=1)),
    ('wait-receiver', {}),
    ('--receiver-output', dict(receiver_video_error=0, receiver_video_output_enabled=1)),
    ('wait-fpga', {}),
]


def save_checkpoint(index, pending=False):
    if CHECKPOINT is None:
        return
    data = dict(version=1, boot=BOOT_ID, bdf=BDF, next=index, pending=pending)
    temporary = CHECKPOINT.with_suffix('.tmp')
    with open(temporary, 'w', opener=lambda path, flags: os.open(path, flags, 0o600)) as out:
        json.dump(data, out)
        out.flush()
        os.fsync(out.fileno())
    temporary.replace(CHECKPOINT)


def read_checkpoint():
    if CHECKPOINT is None or not CHECKPOINT.exists():
        return None
    data = json.loads(CHECKPOINT.read_text())
    if (data.get('version') != 1 or data.get('boot') != BOOT_ID or data.get('bdf') != BDF or
            type(data.get('next')) is not int or not 0 <= data['next'] <= len(PHASES) or
            type(data.get('pending')) is not bool):
        raise RuntimeError('Invalid or stale startup checkpoint; refusing to replay hardware writes')
    return data


def clear_checkpoint():
    if CHECKPOINT is not None:
        CHECKPOINT.unlink(missing_ok=True)


def run_phase(mode, expected):
    if mode == 'wait-link':
        wait_splitter()
    elif mode == 'wait-source-power':
        wait_splitter(power_only=True)
    elif mode == 'wait-receiver-power':
        wait_receiver(power_only=True)
    elif mode == 'wait-receiver':
        wait_receiver()
    elif mode == 'wait-fpga':
        poll('--fpga-status', {}, input_ready, 'Waiting for supported 720p/1080p FPGA input')
    else:
        step(mode, **expected)


def safe_link_race(mode, data):
    # A link can drop between the read-only wait and the output preflight.
    prefix = {'--splitter-port1-output': 'splitter_video',
              '--receiver-output': 'receiver_video'}.get(mode)
    return bool(prefix and data.get(prefix + '_error') == '-67' and
                data.get(prefix + '_writes_started') == '0')


def initialize(passthrough=False):
    current = fields(STATUS.read_text()) if STATUS.exists() else {}
    if ready(current) and bool(number(current, 'passthrough_only')) == passthrough:
        # Keep the guard against reloading an interrupted in-kernel writing
        # phase until that worker has completed; restarting the service is safe.
        if fields(STATUS.read_text()).get('hdmi_ready', '1') == '1':
            clear_checkpoint()
        print('GC573 requested mode and RGB are already loaded; preserving the open device.')
        return
    if passthrough and ready(current):
        step('--passthrough-video', capture_video_registered=1, passthrough_only=1,
             led_error=0, led_rgb_complete=1)
        return
    if not passthrough and number(current, 'passthrough_only'):
        # Phase 7 can reject a newly selected source format after restoring
        # bank zero and before output programming. This is not a bus failure.
        rejected_format = (number(current, 'external_error') == -95 and
                           number(current, 'external_video_phase') == 7 and
                           number(current, 'external_video_last_reg') == 0x13 and
                           current.get('external_video_expected') in ('0', '0x00') and
                           current.get('external_video_observed') in ('0', '0x00') and
                           number(current, 'external_last_status') == 4)
        if (number(current, 'external_error') and not rejected_format) or number(current, 'external_phase') != 3:
            raise RuntimeError('Passthrough initialization did not finish; inspect diagnostics before switching')
        # Leaving a high TMDS ratio needs receiver relocking, not just SRAM
        # restoration. Use the existing checked splitter startup once, then
        # hand off the established receiver/input sequence to the worker.
        save_checkpoint(5, pending=True)
        step('--splitter-tx-finish', splitter_ports_error=0, splitter_ports_tail_complete=1)
        step('--splitter-link-status', splitter_link_error=0, splitter_link_complete=1)
        board = step('--board-state')
        gpio = number(board, 'bar0[0x00000040]')
        if gpio in (0, 0xffffffff, 0xeeeeeeee):
            raise RuntimeError('Invalid board state during capture handoff')
        # GPIO HPD bit 2 means the internal receiver already completed input
        # setup; its DDC preflight deliberately forbids repeating that phase.
        save_checkpoint(9 if gpio & 4 else 6, pending=True)
        step('--capture-wait', capture_error=0, capture_video_registered=1,
             led_error=0, led_rgb_complete=1, hdmi_deferred=1)
        print('Capture devices restored; HDMI is reacquiring the 1080p signal.')
        return
    data = step('--fpga-status')
    if not input_ready(data):
        board = step('--board-state')
        if board.get('bar0[0x00000040]') == '0x0001f850':
            index = 0
            save_checkpoint(index)
        else:
            checkpoint = read_checkpoint()
            if checkpoint is None:
                raise RuntimeError('Partially initialized card without a startup checkpoint; refusing cold resets')
            if checkpoint['pending']:
                raise RuntimeError('A hardware-writing startup phase was interrupted; automatic replay refused')
            index = checkpoint['next']
            print(f'Resuming checked startup at phase {index}', flush=True)
        for index in range(index, len(PHASES)):
            if index >= 6:
                # Devices and RGB are independent of the source. The module
                # continues the remaining phases without unloading open nodes.
                save_checkpoint(index, pending=True)
                if passthrough:
                    step('--passthrough-video', capture_video_registered=1, passthrough_only=1,
                         led_error=0, led_rgb_complete=1)
                else:
                    step('--capture-wait', capture_error=0, capture_video_registered=1,
                         led_error=0, led_rgb_complete=1, hdmi_deferred=1)
                print('GC573 devices and RGB are ready; HDMI setup continues in the background.')
                return
            mode, expected = PHASES[index]
            save_checkpoint(index, pending=mode.startswith('--'))
            try:
                run_phase(mode, expected)
            except StepError as exc:
                if safe_link_race(mode, exc.data):
                    save_checkpoint(index)
                    raise WaitingForSignal('HDMI link changed before output setup') from exc
                raise
            save_checkpoint(index + 1)
    if passthrough:
        step('--passthrough-video', capture_video_registered=1, passthrough_only=1,
             led_error=0, led_rgb_complete=1)
    else:
        step('--capture-video', capture_error=0, capture_video_registered=1,
             led_error=0, led_rgb_complete=1)
    clear_checkpoint()
    print('GC573 native capture and RGB lighting are ready.')


if __name__ == '__main__':
    passthrough = len(sys.argv) == 3 and sys.argv[1] == '--passthrough'
    handoff = len(sys.argv) == 3 and sys.argv[1] == '--handoff-phase'
    if os.geteuid() != 0 or (len(sys.argv) != 2 and not handoff and not passthrough):
        sys.exit('Use the installed GC573 helper with --start.')
    try:
        import re
        BDF = sys.argv[-1]
        if not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]', BDF):
            raise RuntimeError('Invalid PCI address')
        STATUS = Path('/sys/bus/pci/devices') / BDF / 'bringup_status'
        runtime = Path('/run/gc573-codex')
        runtime.mkdir(mode=0o700, exist_ok=True)
        CHECKPOINT = runtime / f'startup-{BDF}.json'
        BOOT_ID = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        if handoff:
            checkpoint = read_checkpoint()
            if checkpoint is None or not checkpoint['pending'] or not 6 <= checkpoint['next'] <= 18:
                raise RuntimeError('No checked deferred startup handoff')
            print(checkpoint['next'])
        else:
            initialize(passthrough=passthrough)
    except WaitingForSignal as exc:
        print(f'GC573 startup waiting: {exc} (temporary, exit 75).', file=sys.stderr)
        sys.exit(75)
    except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as exc:
        sys.exit(f'GC573 startup stopped: {exc}')
