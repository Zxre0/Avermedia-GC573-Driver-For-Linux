#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
case ${1:-} in
    --help|-h)
        echo "Usage: $0 [--skip-deps]"
        echo 'Install dependencies, build the driver, install GC573 Control/Preview and enable boot startup.'
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
        packages=(base-devel git clang llvm python python-gobject gtk4 gstreamer gst-plugins-base gst-plugins-good kmod sudo util-linux v4l-utils alsa-utils obs-studio)
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
            gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 gstreamer1.0-plugins-base \
            gstreamer1.0-plugins-good gstreamer1.0-alsa \
            kmod sudo util-linux v4l-utils alsa-utils obs-studio "linux-headers-$kernel"
    elif command -v dnf >/dev/null; then
        packages=(gcc make git clang llvm elfutils-libelf-devel openssl-devel python3 python3-gobject gtk4
                  gstreamer1 gstreamer1-plugins-base gstreamer1-plugins-good
                  kmod sudo util-linux v4l-utils alsa-utils obs-studio)
        if [[ ! -f /lib/modules/$kernel/build/Makefile ]]; then
            # Variant kernels such as 7.2.11-200.fc44.x86_64+debug ship kernel-debug-devel.
            if [[ $kernel == *+* ]]; then
                packages+=("kernel-${kernel##*+}-devel-${kernel%+*}")
            else
                packages+=("kernel-devel-$kernel")
            fi
        fi
        # Secure Boot only loads signed modules; the boot service signs with the
        # akmods key (tools/prepare-module.sh), so create and enroll one if missing.
        lockdown=$(cat /sys/kernel/security/lockdown 2>/dev/null || true)
        if [[ ! -e /etc/pki/akmods/certs/public_key.der &&
              ( $(cat /sys/module/module/parameters/sig_enforce 2>/dev/null) == Y ||
                $lockdown == *'[integrity]'* || $lockdown == *'[confidentiality]'* ) ]]; then
            packages+=(akmods mokutil)
            enroll=1
        fi
        sudo dnf install "${packages[@]}"
        if [[ -n ${enroll:-} ]]; then
            sudo kmodgenca -a
            echo 'Secure Boot: choose a one-time password; you enter it once in the blue MOK screen on the next boot.'
            sudo mokutil --import /etc/pki/akmods/certs/public_key.der
        fi
    else
        echo 'Automatic dependencies support Arch/CachyOS, Debian/Ubuntu and Fedora.' >&2
        echo 'Install the README dependencies for your distro, then run ./install.sh --skip-deps.' >&2
        exit 1
    fi
fi

# Validate everything we can before any system change or enabling startup.
python3 -c "import gi; gi.require_version('Gtk', '4.0'); from gi.repository import Gtk"
python3 - <<'PY'
import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstApp', '1.0')
gi.require_version('GstVideo', '1.0')
from gi.repository import Gst
Gst.init(None)
missing = [name for name in ('v4l2src', 'appsink', 'alsasrc', 'pulsesink',
                             'audioconvert', 'audioresample', 'volume')
           if not Gst.ElementFactory.find(name)]
if missing:
    raise SystemExit('Missing preview plugins: ' + ', '.join(missing))
PY
python3 "$project/tools/find-card.py"
"$project/tools/build.sh"
"$project/tools/install-app.sh"
# Boot startup runs from a root-owned copy; no password-free sudo rule.
# Older installations used a login unit and a password-free helper: retire both.
"$project/tools/install-startup.sh" --remove
sudo "$project/tools/install-system-boot.sh"
if [[ -e /usr/local/sbin/gc573-codex-probe || -e /etc/sudoers.d/gc573-codex ]]; then
    sudo "$project/tools/enable-unattended-probes.sh" --remove
fi
if [[ -n ${enroll:-} ]]; then
    # The module is signed with a key the firmware does not trust until reboot.
    printf '\nInstallation complete. Reboot now and choose "Enroll MOK" in the blue screen,\n'
    printf 'then enter the password you just chose; the driver loads automatically afterwards.\n'
else
    sudo systemctl start --no-block gc573-native-system.service
    printf '\nInstallation complete; boot startup has been queued.\n'
fi
printf 'The driver and RGB controls load without HDMI input; capture starts when a supported source is ready.\n'
printf 'Open GC573 Control from your application menu. Connect an active 1080p60 RGB8 SDR HDMI source.\n'
printf 'For a simple console window to share in Discord, open GC573 Preview with OBS closed.\n'
printf 'Check initialization: systemctl status gc573-native-system.service --no-pager\n'
printf 'OBS video/audio setup is in README.md, under "Using the card".\n'
printf 'After updating or editing this checkout, rerun ./install.sh --skip-deps to refresh boot startup.\n'
