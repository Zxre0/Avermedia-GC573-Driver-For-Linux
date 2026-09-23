#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
case ${1:-} in
    --help|-h)
        echo "Usage: $0 [--keep-settings]"
        echo 'Remove boot/login startup, unload the module, remove helper access, app, settings and generated build files.'
        echo 'Preserves source, recordings, OBS configuration and shared distribution packages.'
        echo 'Close OBS and GC573 Control first. Run as the account that installed the project.'
        exit 0 ;;
    ''|--keep-settings) ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo 'Expected at most one option.' >&2; exit 2; }
[[ $EUID -ne 0 ]] || { echo 'Run ./uninstall.sh as your normal desktop user, without sudo.' >&2; exit 1; }

# Disable login startup first so it cannot reload the module during removal.
"$project/tools/install-startup.sh" --remove
if sudo "$project/tools/remove-system.sh"; then
    :
else
    result=$?
    if [[ $result != 3 ]] || ! systemctl --user is-active --quiet wireplumber.service; then
        exit "$result"
    fi
    # WirePlumber can hold the ALSA control node even with OBS closed. Restore
    # it on every exit path, including a second failed unload or interruption.
    echo 'Temporarily pausing WirePlumber to release the card; desktop audio may pause.'
    trap 'systemctl --user start wireplumber.service' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    systemctl --user stop wireplumber.service
    sudo "$project/tools/remove-system.sh"
    systemctl --user start wireplumber.service
    trap - EXIT INT TERM
fi
python3 "$project/tools/remove-user.py" "$@"
printf '\nGC573 uninstalled: startup, driver, helper access, control app and generated build files removed.\n'
printf 'Shared packages, source, recordings and OBS configuration were preserved.\n'
