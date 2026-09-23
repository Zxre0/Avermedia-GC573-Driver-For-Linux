#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Fixed system paths only; called by uninstall.sh through sudo.
set -euo pipefail
export PATH=/usr/bin:/usr/sbin
[[ $EUID -eq 0 && $# == 0 ]] || { echo 'Use ./uninstall.sh from your desktop account.' >&2; exit 1; }
unit=gc573-native-boot.service
if [[ -e /etc/systemd/system/$unit ]]; then
    systemctl disable --now "$unit"
    rm -f -- "/etc/systemd/system/$unit"
    systemctl daemon-reload
fi
# Serialize against any helper invocation still finishing after service shutdown.
install -d -o root -g root -m 0700 /run/gc573-codex
exec 9>/run/gc573-codex/probe.lock
flock -w 10 9 || { echo 'Another GC573 operation is running. Retry uninstall when it finishes.' >&2; exit 1; }
if [[ -d /sys/module/gc573_native ]]; then
    if ! rmmod gc573_native; then
        echo 'Startup is disabled, but the driver is still in use. Close capture/audio apps and retry ./uninstall.sh.' >&2
        echo 'An audio session manager may hold the ALSA control device. No forced unload was attempted.' >&2
        exit 3
    fi
fi
rm -f -- /etc/sudoers.d/gc573-codex /usr/local/sbin/gc573-codex-probe
rm -f -- /run/gc573-codex/startup-*.json /run/gc573-codex/startup-*.tmp
rm -f -- /run/gc573-codex/probe.lock
rmdir -- /run/gc573-codex
if [[ -d /run/gc573-native-boot ]]; then
    rm -f -- /run/gc573-native-boot/gc573-native-start.lock
    rmdir -- /run/gc573-native-boot
fi
