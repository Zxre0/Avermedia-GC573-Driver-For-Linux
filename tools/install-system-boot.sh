#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Boot startup from a root-owned copy of this checkout. Unlike the helper used
# by install.sh, this adds no password-free sudo rule: nothing the desktop
# account can modify is executed or loaded as root.
set -euo pipefail
export PATH=/usr/bin:/usr/sbin
umask 022
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
target=/usr/local/lib/gc573-native
unit=gc573-native-system.service
unit_path=/etc/systemd/system/$unit
if [[ $# -gt 1 || ( $# == 1 && $1 != --remove ) ]]; then
    echo "Usage: sudo $0 [--remove]" >&2
    exit 2
fi
[[ $EUID -eq 0 ]] || { echo "Run with sudo: sudo $0 ${1:-}" >&2; exit 1; }

if [[ ${1:-} == --remove ]]; then
    if [[ -e $unit_path ]]; then
        systemctl disable --now "$unit"
        rm -f -- "$unit_path"
        systemctl daemon-reload
    fi
    rm -rf -- "$target"
    echo 'GC573 boot startup removed. The loaded module is unchanged.'
    exit 0
fi

account=${SUDO_USER:-}
[[ "$account" =~ ^[a-zA-Z_][a-zA-Z0-9_-]*$ && "$account" != root ]] || {
    echo 'Run from your normal account through sudo.' >&2; exit 1;
}
[[ $(id -u "$account") -gt 0 ]]
if [[ -e /etc/systemd/system/gc573-native-boot.service ]]; then
    echo 'The helper-based gc573-native-boot.service is installed.' >&2
    echo 'Remove it first with: tools/install-startup.sh --remove-boot' >&2
    exit 1
fi

# Copy only what boot startup needs, then build it as root from the copy.
staging=$(mktemp -d /usr/local/lib/.gc573-native.XXXXXXXX)
trap 'rm -rf -- "$staging"' EXIT
chmod 0755 "$staging"
install -d -m 0755 "$staging/driver" "$staging/tools" "$staging/app"
for file in "$project"/driver/*.[ch]; do
    [[ $file == *.mod.c ]] || install -m 0644 -- "$file" "$staging/driver/"
done
install -m 0644 -- "$project/driver/Makefile" "$staging/driver/"
for file in build.sh prepare-module.sh load-probe.sh system-start.sh start-capture.py find-card.py; do
    install -m 0755 -- "$project/tools/$file" "$staging/tools/"
done
install -m 0644 -- "$project/app/gc573_control.py" "$staging/app/"
python3 -I "$staging/tools/find-card.py" >/dev/null
"$staging/tools/build.sh" >/dev/null

if [[ -d $target ]]; then
    rm -rf -- "$target.old"
    mv -- "$target" "$target.old"
fi
mv -- "$staging" "$target"
rm -rf -- "$target.old"
"$target/tools/prepare-module.sh" "$target/.build/modules/$(uname -r)/gc573_native.ko" "$(uname -r)"

cat > "$unit_path.tmp" <<EOF
[Unit]
Description=GC573 native HDMI capture at system boot (root-owned copy)
After=local-fs.target systemd-udev-trigger.service
StartLimitIntervalSec=0

[Service]
Type=oneshot
Environment=GC573_USER=$account
ExecStart=$target/tools/system-start.sh
RemainAfterExit=yes
TimeoutStartSec=5min
Restart=no
RestartForceExitStatus=75
RestartSec=10s

[Install]
WantedBy=multi-user.target
EOF
chmod 0644 "$unit_path.tmp"
mv -f -- "$unit_path.tmp" "$unit_path"
systemctl daemon-reload
systemctl enable "$unit"
printf 'Installed %s from %s and enabled %s.\n' "$target" "$project" "$unit"
printf 'The driver loads at the next boot. To start it now (an already loaded card is preserved):\n'
printf '  sudo systemctl start %s\n' "$unit"
printf 'After changing or updating the checkout, rerun: sudo %s\n' "$0"
