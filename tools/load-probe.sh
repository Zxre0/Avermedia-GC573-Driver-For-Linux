#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
original_args=("$@")
# Service installation changes no device registers and needs no built module.
if [[ ${1:-} == --install-boot-startup || ${1:-} == --remove-boot-startup ]]; then
    [[ $EUID -eq 0 && $# -eq 2 ]] || { echo 'Use tools/install-startup.sh --boot.' >&2; exit 1; }
    action=install
    [[ $1 != --remove-boot-startup ]] || action=remove
    exec /usr/bin/python3 "$project_dir/tools/install-boot.py" "$action"
fi
auto_start=0
identity_read=0
i2c_read=0
layout_read=0
block_read=0
video_capture=0
frame_capture=0
fpga_read=0
board_read=0
gpio_prepare=0
receiver_read=0
receiver_output=0
receiver_video=0
signal_read=0
write_test=0
receiver_init=0
receiver_calibrate=0
clock_read=0
timing_program=0
edid_read=0
edid_configure=0
input_start=0
splitter_read=0
splitter_start=0
splitter_prepare=0
splitter_clock=0
splitter_video_output=0
splitter_video_setup=0
splitter_video=0
splitter_activate=0
splitter_tx_port=1
splitter_ddc=0
splitter_edid=0
splitter_hpd=0
splitter_link=0
splitter_tail=0
splitter_all=0
splitter_ports=0
splitter_tx=0
splitter_finish=0
splitter_setup=0
splitter_cal=0
splitter_map=0
splitter_timing=0
case ${1:-} in
    --start) identity_read=1; auto_start=1; shift ;;
    --capture-video) identity_read=1; video_capture=1; shift ;;
    --capture-once) identity_read=1; frame_capture=1; shift ;;
    --fpga-status) identity_read=1; fpga_read=1; shift ;;
    --receiver-output) identity_read=1; receiver_output=1; shift ;;
    --receiver-video) identity_read=1; receiver_video=1; shift ;;
    --splitter-port2-output) identity_read=1; splitter_tx_port=2; splitter_video_output=1; shift ;;
    --splitter-port2-activate) identity_read=1; splitter_tx_port=2; splitter_activate=1; shift ;;
    --splitter-port1-output) identity_read=1; splitter_video_output=1; shift ;;
    --splitter-port1-setup) identity_read=1; splitter_video_setup=1; shift ;;
    --splitter-port1-clock) identity_read=1; splitter_video=1; shift ;;
    --splitter-port1-activate) identity_read=1; splitter_activate=1; shift ;;
    --splitter-edid-enable) identity_read=1; splitter_ddc=1; shift ;;
    --splitter-edid-read) identity_read=1; splitter_edid=1; shift ;;
    --splitter-input) identity_read=1; splitter_hpd=1; shift ;;
    --splitter-link-status) identity_read=1; splitter_link=1; shift ;;
    --splitter-tx-finish) identity_read=1; splitter_tail=1; shift ;;
    --splitter-tx-initialize) identity_read=1; splitter_all=1; shift ;;
    --splitter-tx-ports) identity_read=1; splitter_ports=1; shift ;;
    --splitter-tx-prepare) identity_read=1; splitter_tx=1; shift ;;
    --splitter-rx-finish) identity_read=1; splitter_finish=1; shift ;;
    --splitter-rx-setup) identity_read=1; splitter_setup=1; shift ;;
    --splitter-rx-calibrate) identity_read=1; splitter_cal=1; shift ;;
    --splitter-map) identity_read=1; splitter_map=1; shift ;;
    --splitter-timing) identity_read=1; splitter_timing=1; shift ;;
    --splitter-clock) identity_read=1; splitter_clock=1; shift ;;
    --splitter-prepare) identity_read=1; splitter_prepare=1; shift ;;
    --splitter-startup) identity_read=1; splitter_start=1; shift ;;
    --splitter-id) identity_read=1; splitter_read=1; shift ;;
    --receiver-input) identity_read=1; input_start=1; shift ;;
    --receiver-edid-configure) identity_read=1; edid_configure=1; shift ;;
    --receiver-edid-read) identity_read=1; edid_read=1; shift ;;
    --receiver-timing) identity_read=1; timing_program=1; shift ;;
    --receiver-clock) identity_read=1; clock_read=1; shift ;;
    --receiver-calibrate) identity_read=1; receiver_calibrate=1; shift ;;
    --receiver-init) identity_read=1; receiver_init=1; shift ;;
    --receiver-write-test) identity_read=1; write_test=1; shift ;;
    --receiver-status) identity_read=1; signal_read=1; shift ;;
    --receiver-id) identity_read=1; receiver_read=1; shift ;;
    --identity) identity_read=1; shift ;;
    --i2c-id) identity_read=1; i2c_read=1; shift ;;
    --i2c-layout) identity_read=1; layout_read=1; shift ;;
    --block-id) identity_read=1; block_read=1; shift ;;
    --board-state) identity_read=1; board_read=1; shift ;;
    --gpio-prepare) identity_read=1; gpio_prepare=1; shift ;;
esac
bdf=${1:-}
if [[ $# -eq 0 ]]; then
    bdf=$(python3 "$project_dir/tools/find-card.py")
fi
if [[ $# -gt 1 || ! "$bdf" =~ ^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[[:xdigit:]]{2}\.[0-7]$ ]]; then
    printf 'Invalid arguments received:' >&2
    printf ' %q' "${original_args[@]}" >&2
    printf '\nExpected one mode followed by an optional PCI address.\n' >&2
    echo "Usage: $0 [--start|--identity|--i2c-id|--i2c-layout|--block-id|--board-state|--fpga-status|--capture-once|--capture-video|--gpio-prepare|--receiver-id|--receiver-status|--receiver-video|--receiver-output|--receiver-write-test|--receiver-init|--receiver-calibrate|--receiver-clock|--receiver-timing|--receiver-edid-read|--receiver-edid-configure|--receiver-input|--splitter-id|--splitter-startup|--splitter-prepare|--splitter-clock|--splitter-timing|--splitter-map|--splitter-rx-calibrate|--splitter-rx-setup|--splitter-rx-finish|--splitter-tx-prepare|--splitter-tx-ports|--splitter-tx-initialize|--splitter-tx-finish|--splitter-link-status|--splitter-input|--splitter-edid-read|--splitter-edid-enable|--splitter-port1-activate|--splitter-port1-clock|--splitter-port1-setup|--splitter-port1-output|--splitter-port2-activate|--splitter-port2-output] [PCI-address]" >&2
    exit 2
fi
kernel_release=$(uname -r)
module="$project_dir/.build/modules/$kernel_release/gc573_native.ko"
device="/sys/bus/pci/devices/$bdf"
[[ -r "$module" ]] || { echo "Run tools/build.sh for $kernel_release first." >&2; exit 1; }
vermagic=$(modinfo -F vermagic "$module")
[[ "${vermagic%% *}" == "$kernel_release" ]] || { echo "Wrong kernel module; refusing load." >&2; exit 1; }
if [[ "$splitter_tx_port" == 2 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"splitter_tx_port:"* ]] || {
        echo "Rebuild for $kernel_release to enable port-2 output." >&2
        exit 1
    }
fi
if [[ "$splitter_video_output" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"output_splitter_video:"* ]] || {
        echo "Rebuild for $kernel_release to enable transmitter output." >&2
        exit 1
    }
fi
if [[ "$splitter_video_setup" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"configure_splitter_video:"* ]] || {
        echo "Rebuild for $kernel_release to enable transmitter video setup." >&2
        exit 1
    }
fi
if [[ "$splitter_video" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"measure_splitter_video:"* ]] || {
        echo "Rebuild for $kernel_release to enable transmitter clock measurement." >&2
        exit 1
    }
fi
if [[ "$splitter_activate" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"activate_splitter_port1:"* ]] || {
        echo "Rebuild for $kernel_release to enable transmitter activation." >&2
        exit 1
    }
fi
if [[ "$splitter_ddc" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"enable_splitter_edid:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter EDID configuration." >&2
        exit 1
    }
fi
if [[ "$splitter_edid" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_splitter_edid:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter EDID reads." >&2
        exit 1
    }
fi
if [[ "$splitter_hpd" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"request_splitter_hpd:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter input hotplug." >&2
        exit 1
    }
fi
if [[ "$splitter_link" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_splitter_link:"* ]] || {
        echo "Rebuild for $kernel_release to enable runtime status reads." >&2
        exit 1
    }
fi
if [[ "$splitter_tail" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"finish_splitter_tx:"* ]] || {
        echo "Rebuild for $kernel_release to enable the shared startup tail." >&2
        exit 1
    }
fi
if [[ "$splitter_all" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"initialize_splitter_tx:"* ]] || {
        echo "Rebuild for $kernel_release to enable all transmitter ports." >&2
        exit 1
    }
fi
if [[ "$splitter_ports" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"initialize_splitter_ports:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter TX port initialization." >&2
        exit 1
    }
fi
if [[ "$splitter_tx" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"prepare_splitter_tx:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter TX preparation." >&2
        exit 1
    }
fi
if [[ "$splitter_finish" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"finish_splitter_rx:"* ]] || {
        echo "Rebuild for $kernel_release to enable the splitter RX startup tail." >&2
        exit 1
    }
fi
if [[ "$splitter_setup" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"setup_splitter_rx:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter RX setup." >&2
        exit 1
    }
fi
if [[ "$splitter_cal" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"calibrate_splitter_rx:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter RX calibration." >&2
        exit 1
    }
fi
if [[ "$splitter_map" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"configure_splitter_map:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter address configuration." >&2
        exit 1
    }
fi
if [[ "$splitter_timing" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"program_splitter_timing:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter timing settings." >&2
        exit 1
    }
fi
if [[ "$splitter_clock" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_splitter_clock:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter reference-clock reads." >&2
        exit 1
    }
fi
if [[ "$splitter_prepare" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"prepare_splitter:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter control preparation." >&2
        exit 1
    }
fi
if [[ "$splitter_start" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"start_splitter:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter GPIO startup." >&2
        exit 1
    }
fi
if [[ "$splitter_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_splitter:"* ]] || {
        echo "Rebuild for $kernel_release to enable splitter identification." >&2
        exit 1
    }
fi
if [[ "$input_start" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"start_input:"* ]] || {
        echo "Rebuild for $kernel_release to enable input startup." >&2
        exit 1
    }
fi
if [[ "$edid_configure" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"configure_edid:"* ]] || {
        echo "Rebuild for $kernel_release to enable EDID configuration." >&2
        exit 1
    }
fi
if [[ "$edid_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_edid:"* ]] || {
        echo "Rebuild for $kernel_release to enable EDID memory reads." >&2
        exit 1
    }
fi
if [[ "$timing_program" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"program_timing:"* ]] || {
        echo "Rebuild for $kernel_release to enable receiver timing settings." >&2
        exit 1
    }
fi
if [[ "$clock_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_clock:"* ]] || {
        echo "Rebuild for $kernel_release to enable reference-clock reads." >&2
        exit 1
    }
fi
if [[ "$receiver_calibrate" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"calibrate_receiver:"* ]] || {
        echo "Rebuild for $kernel_release to enable receiver calibration." >&2
        exit 1
    }
fi
if [[ "$receiver_init" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"initialize_receiver:"* ]] || {
        echo "Rebuild for $kernel_release to enable receiver initialization." >&2
        exit 1
    }
fi
if [[ "$write_test" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_write:"* ]] || {
        echo "Rebuild for $kernel_release to enable the receiver write test." >&2
        exit 1
    }
fi
if [[ "$signal_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_signal:"* ]] || {
        echo "Rebuild for $kernel_release to enable receiver status." >&2
        exit 1
    }
fi
if [[ "$receiver_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_receiver:"* ]] || {
        echo "Rebuild for $kernel_release to enable receiver startup." >&2
        exit 1
    }
fi
if [[ "$gpio_prepare" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"prepare_gpio:"* ]] || {
        echo "Rebuild for $kernel_release to enable GPIO preparation." >&2
        exit 1
    }
fi
if [[ "$block_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_block:"* ]] || {
        echo "Rebuild for $kernel_release to enable the block-controller test." >&2
        exit 1
    }
fi
if [[ "$layout_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"inspect_access:"* ]] || {
        echo "Rebuild for $kernel_release to enable the access-width comparison." >&2
        exit 1
    }
fi
if [[ "$i2c_read" == 1 ]]; then
    parameters=$(modinfo -p "$module")
    [[ "$parameters" == *"probe_i2c:"* ]] || {
        echo "This module predates the I2C test. Rebuild it for $kernel_release first." >&2
        exit 1
    }
fi
for field in vendor device subsystem_vendor subsystem_device; do
    case "$field" in
        vendor|subsystem_vendor) expected=0x1461 ;;
        device) expected=0x0054 ;;
        subsystem_device) expected=0x5730 ;;
    esac
    [[ -r "$device/$field" && "$(cat "$device/$field")" == "$expected" ]] || {
        echo "Not the expected GC573 at $bdf ($field mismatch)." >&2; exit 1;
    }
done
existing_driver=""
if [[ -L "$device/driver" ]]; then
    existing_driver=$(basename -- "$(readlink -- "$device/driver")")
fi
if [[ -n "$existing_driver" && !( "$identity_read" == 1 && "$existing_driver" == gc573_native ) ]]; then
    echo "Device already has a driver; refusing to unbind it." >&2
    exit 1
fi
if [[ -d /sys/module/gc573_native && !( "$identity_read" == 1 && "$existing_driver" == gc573_native ) ]]; then
    echo "gc573_native is already loaded; refusing to affect another device." >&2
    exit 1
fi
if [[ $EUID -ne 0 ]]; then
    echo "Preflight passed. Loading a kernel module requires root. Run in your terminal:" >&2
    if [[ "$splitter_video_output" == 1 ]]; then
        printf 'sudo %q --splitter-port%s-output %q\n' "$0" "$splitter_tx_port" "$bdf" >&2
    elif [[ "$splitter_video_setup" == 1 ]]; then
        printf 'sudo %q --splitter-port1-setup %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_video" == 1 ]]; then
        printf 'sudo %q --splitter-port1-clock %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_activate" == 1 ]]; then
        printf 'sudo %q --splitter-port%s-activate %q\n' "$0" "$splitter_tx_port" "$bdf" >&2
    elif [[ "$splitter_ddc" == 1 ]]; then
        printf 'sudo %q --splitter-edid-enable %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_edid" == 1 ]]; then
        printf 'sudo %q --splitter-edid-read %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_hpd" == 1 ]]; then
        printf 'sudo %q --splitter-input %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_link" == 1 ]]; then
        printf 'sudo %q --splitter-link-status %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_tail" == 1 ]]; then
        printf 'sudo %q --splitter-tx-finish %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_all" == 1 ]]; then
        printf 'sudo %q --splitter-tx-initialize %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_ports" == 1 ]]; then
        printf 'sudo %q --splitter-tx-ports %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_tx" == 1 ]]; then
        printf 'sudo %q --splitter-tx-prepare %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_finish" == 1 ]]; then
        printf 'sudo %q --splitter-rx-finish %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_setup" == 1 ]]; then
        printf 'sudo %q --splitter-rx-setup %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_cal" == 1 ]]; then
        printf 'sudo %q --splitter-rx-calibrate %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_map" == 1 ]]; then
        printf 'sudo %q --splitter-map %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_timing" == 1 ]]; then
        printf 'sudo %q --splitter-timing %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_clock" == 1 ]]; then
        printf 'sudo %q --splitter-clock %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_prepare" == 1 ]]; then
        printf 'sudo %q --splitter-prepare %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_start" == 1 ]]; then
        printf 'sudo %q --splitter-startup %q\n' "$0" "$bdf" >&2
    elif [[ "$splitter_read" == 1 ]]; then
        printf 'sudo %q --splitter-id %q\n' "$0" "$bdf" >&2
    elif [[ "$input_start" == 1 ]]; then
        printf 'sudo %q --receiver-input %q\n' "$0" "$bdf" >&2
    elif [[ "$edid_configure" == 1 ]]; then
        printf 'sudo %q --receiver-edid-configure %q\n' "$0" "$bdf" >&2
    elif [[ "$edid_read" == 1 ]]; then
        printf 'sudo %q --receiver-edid-read %q\n' "$0" "$bdf" >&2
    elif [[ "$timing_program" == 1 ]]; then
        printf 'sudo %q --receiver-timing %q\n' "$0" "$bdf" >&2
    elif [[ "$clock_read" == 1 ]]; then
        printf 'sudo %q --receiver-clock %q\n' "$0" "$bdf" >&2
    elif [[ "$receiver_calibrate" == 1 ]]; then
        printf 'sudo %q --receiver-calibrate %q\n' "$0" "$bdf" >&2
    elif [[ "$receiver_init" == 1 ]]; then
        printf 'sudo %q --receiver-init %q\n' "$0" "$bdf" >&2
    elif [[ "$write_test" == 1 ]]; then
        printf 'sudo %q --receiver-write-test %q\n' "$0" "$bdf" >&2
    elif [[ "$signal_read" == 1 ]]; then
        printf 'sudo %q --receiver-status %q\n' "$0" "$bdf" >&2
    elif [[ "$receiver_read" == 1 ]]; then
        printf 'sudo %q --receiver-id %q\n' "$0" "$bdf" >&2
    elif [[ "$gpio_prepare" == 1 ]]; then
        printf 'sudo %q --gpio-prepare %q\n' "$0" "$bdf" >&2
    elif [[ "$board_read" == 1 ]]; then
        printf 'sudo %q --board-state %q\n' "$0" "$bdf" >&2
    elif [[ "$block_read" == 1 ]]; then
        printf 'sudo %q --block-id %q\n' "$0" "$bdf" >&2
    elif [[ "$layout_read" == 1 ]]; then
        printf 'sudo %q --i2c-layout %q\n' "$0" "$bdf" >&2
    elif [[ "$i2c_read" == 1 ]]; then
        printf 'sudo %q --i2c-id %q\n' "$0" "$bdf" >&2
    elif [[ "$identity_read" == 1 ]]; then
        printf 'sudo %q --identity %q\n' "$0" "$bdf" >&2
    else
        printf 'sudo %q %q\n' "$0" "$bdf" >&2
    fi
    exit 1
fi
if [[ "$auto_start" == 1 ]]; then
    exec /usr/bin/python3 "$project_dir/tools/start-capture.py" "$bdf"
fi
modprobe snd-pcm
modprobe videodev
modprobe videobuf2-vmalloc
modprobe videobuf2-v4l2
if [[ "$identity_read" == 1 ]]; then
    if [[ "$existing_driver" == gc573_native ]]; then
        rmmod gc573_native
    fi
    if [[ "$video_capture" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 capture_video=1
    elif [[ "$frame_capture" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 capture_once=1
    elif [[ "$fpga_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0,0x10,0x1c,0x300,0x304,0x1000,0x1004,0x1008,0x100c,0x1010,0x1040,0x107c,0x1088
    elif [[ "$receiver_output" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 output_receiver_video=1
    elif [[ "$receiver_video" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_receiver_video=1
    elif [[ "$splitter_video_output" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 output_splitter_video=1 "splitter_tx_port=$splitter_tx_port"
    elif [[ "$splitter_video_setup" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 configure_splitter_video=1
    elif [[ "$splitter_video" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 measure_splitter_video=1
    elif [[ "$splitter_activate" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 activate_splitter_port1=1 "splitter_tx_port=$splitter_tx_port"
    elif [[ "$splitter_ddc" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 enable_splitter_edid=1
    elif [[ "$splitter_edid" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_splitter_edid=1
    elif [[ "$splitter_hpd" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 request_splitter_hpd=1
    elif [[ "$splitter_link" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_splitter_link=1
    elif [[ "$splitter_tail" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 finish_splitter_tx=1
    elif [[ "$splitter_all" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 initialize_splitter_tx=1
    elif [[ "$splitter_ports" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 initialize_splitter_ports=1
    elif [[ "$splitter_tx" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 prepare_splitter_tx=1
    elif [[ "$splitter_finish" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 finish_splitter_rx=1
    elif [[ "$splitter_setup" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 setup_splitter_rx=1
    elif [[ "$splitter_cal" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 calibrate_splitter_rx=1
    elif [[ "$splitter_map" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 configure_splitter_map=1
    elif [[ "$splitter_timing" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 program_splitter_timing=1
    elif [[ "$splitter_clock" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_splitter_clock=1
    elif [[ "$splitter_prepare" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 prepare_splitter=1
    elif [[ "$splitter_start" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 start_splitter=1
    elif [[ "$splitter_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_splitter=1
    elif [[ "$input_start" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 start_input=1
    elif [[ "$edid_configure" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 configure_edid=1
    elif [[ "$edid_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_edid=1
    elif [[ "$timing_program" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 program_timing=1
    elif [[ "$clock_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_clock=1
    elif [[ "$receiver_calibrate" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 calibrate_receiver=1
    elif [[ "$receiver_init" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 initialize_receiver=1
    elif [[ "$write_test" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_write=1
    elif [[ "$signal_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_signal=1
    elif [[ "$receiver_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_receiver=1
    elif [[ "$gpio_prepare" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 prepare_gpio=1
    elif [[ "$board_read" == 1 ]]; then
        # Known vendor-read registers only; exclude RX FIFO and commands.
        insmod "$module" "target_bdf=$bdf" read_offsets=0,0x10,0x1c,0x40,0x64,0x180,0x1a4
    elif [[ "$block_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_block=1
    elif [[ "$layout_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 inspect_access=1
    elif [[ "$i2c_read" == 1 ]]; then
        insmod "$module" "target_bdf=$bdf" read_offsets=0 probe_i2c=1
    else
        insmod "$module" "target_bdf=$bdf" read_offsets=0
    fi
else
    insmod "$module" "target_bdf=$bdf"
fi
if [[ ! -r "$device/bringup_status" ]]; then
    rmmod gc573_native
    echo "Module registered but PCI probe failed. Check: sudo dmesg | tail -60" >&2
    exit 1
fi
if [[ "$video_capture" == 1 ]]; then
    udevadm settle
fi
cat "$device/bringup_status"
if [[ "$splitter_video" == 1 ]]; then
    echo "TX1 clock counter measurement; configured reference recovered from timer. Capture remains unimplemented."
fi
if [[ "$splitter_activate" == 1 ]]; then
    echo "Selected transmitter activation prefix only; hpd_poll reports splitter input. Runtime setup and capture remain incomplete."
fi
if [[ "$splitter_ddc" == 1 ]]; then
    echo "EDID checksum/DDC settings and hotplug changes persist; capture remains unimplemented."
fi
if [[ "$splitter_edid" == 1 ]]; then
    echo "Splitter EDID mapping persists; SRAM, DDC and HPD unchanged. Capture remains unimplemented."
fi
if [[ "$splitter_hpd" == 1 ]]; then
    echo "One input/HPD request; changes persist, events unacknowledged. Capture remains unimplemented."
fi
if [[ "$splitter_link" == 1 ]]; then
    echo "Splitter status reads only; no chip register writes or event acknowledgements. Capture remains unimplemented."
fi
if [[ "$splitter_tail" == 1 ]]; then
    echo "TX/shared startup settings persist; runtime link management remains pending. Capture remains unimplemented."
fi
if [[ "$splitter_all" == 1 ]]; then
    echo "Four TX port routines complete only if reported; shared startup tail remains pending. Capture remains unimplemented."
fi
if [[ "$splitter_ports" == 1 ]]; then
    echo "Port 0/3 and shared control changes persist; port 1/2 initialization is pending. Capture remains unimplemented."
fi
if [[ "$splitter_tx" == 1 ]]; then
    echo "Shared TX reset completed only if reported; port registers were read only. Settings persist; capture remains unimplemented."
fi
if [[ "$splitter_finish" == 1 ]]; then
    echo "Splitter RX startup settings persist; transmitter setup remains pending. Capture remains unimplemented."
fi
if [[ "$splitter_setup" == 1 ]]; then
    echo "Splitter RX control settings persist after unload; later startup steps remain pending. Capture remains unimplemented."
fi
if [[ "$splitter_cal" == 1 ]]; then
    echo "One splitter RX reset/calibration attempt; register changes persist after unload. Capture remains unimplemented."
fi
if [[ "$splitter_map" == 1 ]]; then
    echo "Splitter TX address settings persist; splitter RX registers were read only. Capture remains unimplemented."
fi
if [[ "$splitter_timing" == 1 ]]; then
    echo "Splitter clock-dependent settings persist after unload. Capture remains unimplemented."
fi
if [[ "$splitter_clock" == 1 ]]; then
    echo "Internal splitter reference-clock data only; no timing programming. Capture remains unimplemented."
fi
if [[ "$splitter_prepare" == 1 ]]; then
    echo "Splitter control/address changes persist after unload; this is partial initialization. Capture remains unimplemented."
fi
if [[ "$splitter_start" == 1 ]]; then
    echo "Splitter startup/ID continuation; any GPIO changes persist after unload. Capture remains unimplemented."
fi
if [[ "$splitter_read" == 1 ]]; then
    echo "Splitter bank/identity reads only; GPIO and receiver settings preserved. Capture remains unimplemented."
fi
if [[ "$input_start" == 1 ]]; then
    echo "Input settings and any asserted HPD persist after unload. Capture remains unimplemented."
fi
if [[ "$edid_configure" == 1 ]]; then
    echo "EDID checksum/DDC settings persist; HPD is unchanged. Capture remains unimplemented."
fi
if [[ "$edid_read" == 1 ]]; then
    echo "EDID memory read only; address mapping and clock/timing setup persist. Capture remains unimplemented."
fi
if [[ "$timing_program" == 1 ]]; then
    echo "Clock-dependent settings persist after unload; receiver outputs remain disabled. Capture is unimplemented."
fi
if [[ "$clock_read" == 1 ]]; then
    echo "Internal reference-clock data only; receiver outputs remain disabled. Capture is unimplemented."
fi
if [[ "$receiver_calibrate" == 1 ]]; then
    echo "One CAOF attempt; receiver changes persist after unload. Capture remains unimplemented."
fi
if [[ "$receiver_init" == 1 ]]; then
    echo "Receiver table changes persist after unload; this is not complete HDMI initialization."
fi
if [[ "$receiver_read" == 1 ]]; then
    echo "GPIO startup changes persist after unload; only one receiver read is attempted."
fi
if [[ "$gpio_prepare" == 1 ]]; then
    echo "If set, GPIO bit 0x08 remains set after unload; no new I2C request was issued."
fi
if [[ "$video_capture" == 1 ]]; then
    echo "Native V4L2 capture mode loaded; inspect capture_error and LED status above."
else
    echo "Diagnostic module loaded. To release the card: sudo rmmod gc573_native"
fi
