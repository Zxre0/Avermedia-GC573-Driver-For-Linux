#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Run as root by gc573-native-system.service from the root-owned copy that
# tools/install-system-boot.sh creates. User files are only read as that user.
set -euo pipefail
export PATH=/usr/bin:/usr/sbin
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
[[ $EUID -eq 0 ]] || { echo 'Started by gc573-native-system.service only.' >&2; exit 1; }
# Never run root code from a location the desktop account could change.
for path in "$project_dir" "$project_dir/tools" "$project_dir/driver" "$project_dir/app"; do
    [[ ! -L $path && $(stat -c '%u %a' -- "$path") =~ ^0\ [0-7]*[0-7][0145][0145]$ ]] || {
        echo "$path is not root-owned; reinstall with: sudo tools/install-system-boot.sh" >&2
        exit 1
    }
done

account=${GC573_USER:-}
home=
if [[ "$account" =~ ^[a-zA-Z_][a-zA-Z0-9_-]*$ ]] && entry=$(getent passwd "$account"); then
    IFS=: read -r _ _ uid gid _ home _ <<< "$entry"
    groups=$gid
    for name in video audio; do
        if group=$(getent group "$name"); then
            groups+=",$(cut -d: -f3 <<< "$group")"
        fi
    done
fi
as_user() {
    setpriv --reuid="$uid" --regid="$gid" --groups="$groups" -- \
        env -i HOME="$home" PATH=/usr/bin LANG=C "$@"
}

mode=capture
if [[ -n $home ]]; then
    saved=$(as_user cat -- "$home/.config/gc573-control/mode" 2>/dev/null || true)
    mode=${saved:-capture}
fi
case $mode in
    scaled) start=--start-scaled ;;
    capture) start=--start ;;
    passthrough) start=--start-passthrough ;;
    *) echo 'Unknown GC573 mode preference; expected capture, scaled or passthrough.' >&2; exit 2 ;;
esac

# Share the lock used by the development helper so hardware calls never overlap.
install -d -o root -g root -m 0700 /run/gc573-codex
exec 9>/run/gc573-codex/probe.lock
flock -w 30 9 || exit 75
# Rebuild for the running kernel after updates; the source is root-owned.
"$project_dir/tools/build.sh" >/dev/null
cd /
env -i PATH=/usr/bin:/usr/sbin LANG=C \
    /usr/bin/bash --noprofile --norc "$project_dir/tools/load-probe.sh" "$start"
exec 9>&-
if [[ -n $home ]]; then
    as_user python3 -I "$project_dir/app/gc573_control.py" --restore ||
        echo 'GC573 started, but saved RGB settings were not restored.' >&2
fi
