#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
case ${1:-} in
    --help|-h)
        echo "Usage: $0 [--skip-deps]"
        echo 'Install dependencies, build the driver, install GC573 Control and enable boot startup.'
        echo 'Run as your normal desktop user. sudo is requested for system changes.'
        exit 0 ;;
    ''|--skip-deps) ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo 'Expected at most one option.' >&2; exit 2; }
[[ $EUID -ne 0 ]] || { echo 'Run ./install.sh as your normal desktop user, without sudo.' >&2; exit 1; }
[[ $(stat -c %u "$project") == "$UID" ]] || { echo 'The checkout must belong to your desktop account.' >&2; exit 1; }
command -v sudo >/dev/null || { echo 'Install sudo and grant your account administrator access first.' >&2; exit 1; }
[[ -d /run/systemd/system ]] || { echo 'Automatic installation requires a running systemd system.' >&2; exit 1; }
trap 'echo "Installation did not finish. Correct the error and rerun ./install.sh; ./uninstall.sh removes a partial installation." >&2' ERR

if [[ ${1:-} != --skip-deps ]]; then
    kernel=$(uname -r)
    if command -v pacman >/dev/null; then
        packages=(base-devel git clang llvm python python-gobject gtk4 kmod sudo util-linux v4l-utils alsa-utils obs-studio)
        if [[ -r /lib/modules/$kernel/pkgbase ]]; then
            read -r pkgbase < "/lib/modules/$kernel/pkgbase"
            [[ $pkgbase =~ ^[a-zA-Z0-9][a-zA-Z0-9.+_-]*$ ]] || { echo 'Invalid kernel package name.' >&2; exit 1; }
            packages+=("$pkgbase-headers")
        elif [[ ! -f /lib/modules/$kernel/build/Makefile ]]; then
            echo 'Cannot identify your kernel package. Install its matching headers, then use --skip-deps.' >&2
            exit 1
        fi
        sudo pacman -S --needed "${packages[@]}"
    elif command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install build-essential git clang llvm python3 python3-gi gir1.2-gtk-4.0 \
            kmod sudo util-linux v4l-utils alsa-utils obs-studio "linux-headers-$kernel"
    else
        echo 'Automatic dependencies support Arch/CachyOS and Debian/Ubuntu.' >&2
        echo 'Install the README dependencies for your distro, then run ./install.sh --skip-deps.' >&2
        exit 1
    fi
fi

# Validate everything we can before granting helper access or enabling startup.
python3 -c "import gi; gi.require_version('Gtk', '4.0'); from gi.repository import Gtk"
python3 "$project/tools/find-card.py"
"$project/tools/build.sh"
sudo "$project/tools/enable-unattended-probes.sh"
"$project/tools/run-probe.sh" --check
"$project/tools/install-app.sh"
"$project/tools/install-startup.sh" --boot
printf '\nInstallation complete; boot startup has been queued.\n'
printf 'The driver and RGB controls load without HDMI input; capture starts when a supported source is ready.\n'
printf 'Open GC573 Control from your application menu. Connect an active 1080p60 RGB8 SDR HDMI source.\n'
printf 'Check initialization: systemctl status gc573-native-boot.service --no-pager\n'
printf 'OBS video/audio setup is in README.md, step 5. Keep this checkout in its current location.\n'
