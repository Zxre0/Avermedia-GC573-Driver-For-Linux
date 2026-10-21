# GC573 Native — Linux driver and RGB control app

**AI-Slop development with Opus 5.5 & GPT-6 Astra (275 million tokens used so far)**, with hardware testing on the project owner's
AVerMedia Live Gamer 4K GC573. This is an original native Linux driver and GTK4
control app, developed through hardware testing and research of AVerMedia's
official driver protocol. It does not install or link a community driver or
require a proprietary runtime binary.

**Version: 0.46.5 · Status: experimental · License: GPL-2.0-only**

Native **1080p60 capture works in OBS**. The driver also exposes HDMI audio through
ALSA, RGB lighting controls, and live incoming resolution/frame-rate information.
This project is not affiliated with or endorsed by AVerMedia or OpenAI.

**Contents:** [Tested systems](#tested-systems) ·
[What works](#what-works) · [Install](#install) · [Secure Boot](#secure-boot) ·
[Using the card](#using-the-card) · [Capture modes](#capture-modes) ·
[Manual install](#manual-install) · [Troubleshooting](#troubleshooting) ·
[Uninstall](#uninstall) · [Development](#development)

## Tested systems

| Distro | Kernel | Result |
| --- | --- | --- |
| CachyOS (x86_64) | `7.2.6-1-cachyos` | Capture, OBS, ALSA and RGB tested on hardware |
| CachyOS | `7.2.3-1-cachyos`, `6.18.52-1-cachyos-lts` | Compiles; not run on hardware |
| Fedora 44 (x86_64) | `7.2.11-200.fc44.x86_64` | Works, using the [manual install](#manual-install) |
| Ubuntu | CI headers | Compiles in GitHub Actions; not a hardware test |

Other distributions and board revisions still need testing. Exact test
package versions are in [docs/testing.md](docs/testing.md).

## What works

| Feature | Status |
| --- | --- |
| 1080p60 capture | Hardware verified in OBS (RGB 8-bit input → V4L2 BGR24) |
| 720p and lower-rate 1080p | Implemented; more source modes need testing |
| Native 1440p120 capture | Experimental; needs PCIe Gen2 ×4 ([details](docs/modes.md#native-1440p120-capture)) |
| 1440p120 HDMI OUT + 1080p60 capture | Verified on a PS5 ([details](docs/modes.md#1440p-passthrough-with-1080p-capture)) |
| High-rate HDMI OUT (up to 1440p144 / 4K60) | Experimental; disables capture ([details](docs/modes.md#high-rate-hdmi-passthrough)) |
| HDMI audio | Stereo 48 kHz recorded in OBS; channel order and A/V sync not yet verified |
| RGB lighting | Rainbow, solid, off and brightness; works during capture |
| HDMI passthrough | 1080p60 and 1440p120 confirmed; latency reported as very good but not measured |
| Startup without a source | Devices and RGB load first; HDMI setup finishes when a source appears |
| 4K / 1440p144 capture, HDR, suspend/resume, multichannel audio | **Not supported** |

The card's [official specifications](https://www.avermedia.com/product-detail/GC573)
(1080p240, 1440p144, 4K60 HDR) describe the hardware, not this driver.
The driver targets PCI `1461:0054`, subsystem `1461:5730`, FPGA `20201015`, board
`57300102`, and rejects unknown revisions. HDCP-protected content is not supported.

## Install

Clone the repository into a stable folder owned by your normal desktop account,
then run the installer **without sudo** (it asks for sudo when needed):

```sh
git clone https://github.com/Zxre0/Avermedia-GC573-Driver-For-Linux.git gc573-native
cd gc573-native
./install.sh
```

The installer:

- installs build tools, matching kernel headers, GTK/GStreamer and OBS
  (pacman on Arch/CachyOS, apt on Debian/Ubuntu, dnf on Fedora);
- builds the driver and installs **GC573 Control** and **GC573 Preview** in your app menu;
- enables a boot service that loads the driver from a root-owned copy in
  `/usr/local/lib/gc573-native` and restores your RGB settings.

> **Fedora: the automatic installer is untested.** Fedora 44 works with the
> [manual install](#manual-install), so we recommend that instead. If you try
> `./install.sh` anyway, please report the result.

**Requirements:** sudo access, systemd, internet access, and exactly one GC573.
With Secure Boot enabled, read [Secure Boot](#secure-boot) first. If you
already installed the dependencies yourself, or use another distro, run
`./install.sh --skip-deps`.

**After updating or editing the source, rerun `./install.sh --skip-deps`**;
otherwise boot keeps loading the old copy. Check startup with
`systemctl status gc573-native-system.service --no-pager`.

## Secure Boot

With Secure Boot enabled, most distribution kernels only load modules signed by
a trusted key. An unsigned module fails with `Key was rejected by service`.
This is a one-time setup: once a key is enrolled, the boot service signs every
module it builds, including rebuilds after kernel updates.

**1. Check whether you need it.**

```sh
mokutil --sb-state
cat /sys/module/module/parameters/sig_enforce
cat /sys/kernel/security/lockdown
```

You need a key if Secure Boot is enabled **and** `sig_enforce` is `Y` or lockdown
shows `[integrity]` or `[confidentiality]` (usual on Fedora, Ubuntu and Debian).
If `sig_enforce` is `N` and lockdown is `[none]` (common on Arch/CachyOS with
`sbctl`), skip this section.

**2. Create and enroll a key.** The boot service signs with the key in
`/etc/pki/akmods`, the same key Fedora uses for akmods drivers such as NVIDIA.
If you already enrolled one, skip to step 3.

```sh
# Fedora
sudo dnf install akmods mokutil
sudo kmodgenca -a
sudo mokutil --import /etc/pki/akmods/certs/public_key.der
```

```sh
# Ubuntu, Debian and other distros that boot through shim
sudo install -d -m 0755 /etc/pki/akmods/certs
sudo install -d -m 0700 /etc/pki/akmods/private
sudo openssl req -new -x509 -newkey rsa:2048 -nodes -days 36500 \
  -subj "/CN=GC573 module signing/" -outform DER \
  -keyout /etc/pki/akmods/private/private_key.priv \
  -out /etc/pki/akmods/certs/public_key.der
sudo mokutil --import /etc/pki/akmods/certs/public_key.der
```

`mokutil --import` asks for a one-time password. Reboot, choose **Enroll MOK →
Continue → Yes** in the blue screen, enter the password and reboot again.
Confirm with `mokutil --test-key /etc/pki/akmods/certs/public_key.der`
("is already enrolled"). This needs the shim boot loader; with your own
Secure Boot keys (e.g. `sbctl`), add the certificate to the firmware `db` instead.

**3. Install.** The boot service now signs (and on Fedora, SELinux-labels) each
build automatically. If you load the driver manually, sign after every build:

```sh
sudo ./tools/prepare-module.sh ".build/modules/$(uname -r)/gc573_native.ko" "$(uname -r)"
```

Keep `/etc/pki/akmods/private` readable only by root: anyone with that key can
sign modules your kernel trusts.

## Using the card

For the default mode, send **1080p60, RGB 8-bit, SDR** with HDCP off and
**48 kHz stereo PCM** audio to **HDMI IN**. Connect your display to **HDMI OUT**
before the driver starts if you want passthrough.

### GC573 Control

Open **GC573 Control** from the app menu to set Rainbow, Solid or Off lighting
and brightness, and to see the incoming resolution, frame rate and signal state.
Settings are saved in `~/.config/gc573-control/settings.json`. No sudo needed.

### OBS

1. Add **Video Capture Device (V4L2)** → **AVerMedia GC573 Native**.
2. Select **1920×1080**, **BGR3/BGR24**, **60 fps**, **Full** color range.
3. Add **Audio Capture Device (ALSA)** → **Custom** → `hw:GC573,0`, **48000 Hz**.
   Set OBS audio to 48 kHz stereo too.

The audio device is exclusive: close other programs using it first. For a stable
path, use the card's entry in `/dev/v4l/by-path/` rather than `/dev/video0`.

### GC573 Preview (Discord, no OBS)

**GC573 Preview** shows the capture in a normal window with console audio,
Pause/Resume, volume and fullscreen (F11). Close OBS first; they can't share
the device. In Discord, choose **Share Your Screen** and pick the preview
window. Discord calls themselves have not been tested. For the lowest latency,
play on HDMI OUT.

## Capture modes

Switch modes with `tools/set-mode.py` (close OBS and Preview first). The choice
is saved for the boot service.

| Mode | Command | What it does |
| --- | --- | --- |
| `capture` (default) | `python3 tools/set-mode.py capture` | 1080p60 source, capture + passthrough |
| `scaled` | `python3 tools/set-mode.py scaled` | Passthrough at source timing (up to 1440p120); capture at 1080p/720p, or native 1440p120 on Gen2 ×4 |
| `passthrough` | `python3 tools/set-mode.py passthrough` | High-rate HDMI OUT only (up to 1440p144 / 4K60); **no capture or audio** |

These modes are experimental. PS5 settings, limitations and test results are in
[docs/modes.md](docs/modes.md), along with the opt-in 1440p144 input profile.

## Manual install

Use this on Fedora or openSUSE, or if you prefer individual steps. Build on the
machine that has the card (not in a container).

### 1. Dependencies

```sh
# Arch / CachyOS (use linux-cachyos-lts-headers etc. for other kernels)
sudo pacman -Syu --needed base-devel git clang llvm python python-gobject gtk4 \
  gstreamer gst-plugins-base gst-plugins-good kmod sudo util-linux v4l-utils \
  alsa-utils obs-studio linux-cachyos-headers

# Debian / Ubuntu / Mint
sudo apt-get update
sudo apt-get install build-essential git clang llvm python3 python3-gi \
  gir1.2-gtk-4.0 gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-alsa \
  kmod sudo util-linux pciutils v4l-utils alsa-utils obs-studio "linux-headers-$(uname -r)"

# Fedora (tested on Fedora 44)
sudo dnf install gcc make git clang llvm elfutils-libelf-devel openssl-devel \
  python3 python3-gobject gtk4 gstreamer1 gstreamer1-plugins-base gstreamer1-plugins-good \
  kmod sudo util-linux pciutils v4l-utils alsa-utils obs-studio "kernel-devel-$(uname -r)"

# openSUSE (default kernel)
sudo zypper install gcc make git clang llvm libelf-devel libopenssl-devel \
  kernel-devel kernel-default-devel python3 python3-gobject python3-gobject-Gdk \
  typelib-1_0-Gtk-4_0 libgtk-4-1 kmod sudo util-linux pciutils v4l-utils \
  alsa-utils obs-studio
```

The kernel headers must match the **running** kernel. If your package manager
can't find them, update, reboot into the new kernel and retry. Check:

```sh
uname -r
cat "/lib/modules/$(uname -r)/build/include/config/kernel.release"   # must match
```

### 2. Build and load

```sh
git clone https://github.com/Zxre0/Avermedia-GC573-Driver-For-Linux.git gc573-native
cd gc573-native
./tools/build.sh                      # no sudo; picks Clang or GCC from the kernel config
sudo ./tools/load-probe.sh --start    # add a PCI address if you have several cards
./tools/install-app.sh
```

Look for `capture_error=0` and `capture_video_registered=1`. With Secure Boot,
sign the module between the build and load steps ([Secure Boot](#secure-boot)).

### 3. Load at boot

```sh
sudo ./tools/install-system-boot.sh
sudo systemctl start gc573-native-system.service   # optional: start now
```

This copies the driver into root-owned `/usr/local/lib/gc573-native` and
enables `gc573-native-system.service`, which rebuilds for new kernels, loads
your saved mode and restores RGB. It never runs files from your checkout, so
**rerun it after updating the source**. (`./install.sh --skip-deps` does
steps 2–3 in one go.) A home directory unlocked only at login needs the
[login-only setup](docs/unattended-probes.md#login-only-startup) instead.

## Troubleshooting

- **Headers missing or mismatched:** install headers for `uname -r`, or reboot into
  the kernel matching your headers, then rebuild.
- **`Key was rejected by service`:** see [Secure Boot](#secure-boot).
- **Module in use:** close OBS and other capture apps. WirePlumber can also hold
  the audio device. Don't force-remove the module.
- **No video device / startup error:** check
  `journalctl -u gc573-native-system.service -b --no-pager`. A missing HDMI
  source alone should not prevent loading.
- **Device present but no frames:** use a supported 1080p60 RGB8 SDR source with
  HDCP off, and check `hdmi_ready`, `hdmi_waiting`, `hdmi_error` and
  `input_present` in the status output below.
- **No audio:** set the source to 48 kHz stereo PCM over HDMI, check its volume,
  and use `hw:GC573,0` in OBS. Compressed and multichannel audio aren't supported.

Diagnostics (read-only):

```sh
python3 app/gc573_control.py --status
python3 tools/collect.py
```

When reporting a build failure, include `uname -r`, `/etc/os-release` and the
compiler error.

## Uninstall

Close OBS, GC573 Preview and GC573 Control, then from the same checkout and account:

```sh
./uninstall.sh                  # or --keep-settings to keep lighting preferences
```

This removes the boot/login services, the loaded module, helper and sudoers rule,
app launchers, saved settings and generated build files. It keeps shared packages
(OBS, GTK, headers), OBS configuration, recordings and the source checkout.
Card registers are not reset.

## Development

```sh
./tools/test.sh                         # fault-injection, Python and shell tests
./tools/build.sh 6.18.52-1-cachyos-lts  # build against another installed kernel
v4l2-compliance -d /dev/video0          # close OBS first
```

V4L2 compliance on 0.43.0: 48 passed, zero failures, zero warnings. See
[test evidence and version history](docs/testing.md), [capture modes](docs/modes.md),
[protocol notes](docs/protocol.md), [development boot helper](docs/unattended-probes.md),
[contributing](CONTRIBUTING.md) and [TODO](TODO.md).

## Credits and license

The native driver, control app, tests and installation tooling were developed
with **AI assistance from GPT-6 Astra**, with iterative hardware testing on the
owner's GC573. (This is AI slop it works well in my experience)
AVerMedia's official driver packages were used to research the hardware protocol;
their binaries are not distributed with this source.

Licensed under [GPL-2.0-only](LICENSE). Private recordings, proprietary research
inputs, local logs, editor backups and generated build products are excluded
from Git and source exports.
