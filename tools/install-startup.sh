#!/usr/bin/env bash
# Install boot or login startup; privileged changes use the authorized helper.
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
units="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
if [[ ${1:-} == --boot || ${1:-} == --remove-boot ]]; then
    [[ $# == 1 ]] || { echo 'Expected one startup option.' >&2; exit 2; }
    if [[ $1 == --remove-boot ]]; then
        exec "$project/tools/run-probe.sh" --remove-boot-startup
    fi
    "$project/tools/run-probe.sh" --install-boot-startup
    # Avoid a second initialization attempt from the legacy login unit.
    if [[ -f "$units/gc573-native.service" ]]; then
        systemctl --user disable --now gc573-native.service
    fi
    printf 'Boot startup enabled. Check: systemctl status gc573-native-boot.service\n'
    exit
fi
if [[ $# -gt 1 || ( $# == 1 && $1 != --remove ) ]]; then
    echo "Usage: $0 [--boot|--remove-boot|--remove]" >&2; exit 2
fi
if [[ ${1:-} == --remove ]]; then
    systemctl --user disable --now gc573-native.service
    rm -f "$units/gc573-native.service"
    systemctl --user daemon-reload
    exit
fi
"$project/tools/run-probe.sh" --check
mkdir -p "$units"
python3 - "$project" "$units/gc573-native.service" <<'PY'
from pathlib import Path
import sys
project, target = sys.argv[1:]
# systemd quoting and specifier escaping, without a shell.
exe = (project + '/tools/auto-start.sh').replace('\\','\\\\').replace('"','\\"').replace('%','%%')
Path(target).write_text('[Unit]\nDescription=GC573 native HDMI capture and RGB lighting\nStartLimitIntervalSec=0\n\n'
 '[Service]\nType=oneshot\nExecStart="' + exe + '"\nRemainAfterExit=yes\nTimeoutStartSec=5min\nRestart=no\nRestartForceExitStatus=75\nRestartSec=10s\n\n'
 '[Install]\nWantedBy=default.target\n')
PY
systemctl --user daemon-reload
systemctl --user enable gc573-native.service
printf 'Startup enabled at user login; run systemctl --user start gc573-native.service to start now.\n'
