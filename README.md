# GC573 Native — Linux driver and RGB control app

**(AI assisted by GPT-6 Astra)**, with hardware testing on the project owner's
AVerMedia Live Gamer 4K GC573. This is an original native Linux driver and GTK4
control app, developed through hardware testing and research of AVerMedia's
official driver protocol. It does not install or link a community driver or
require a proprietary runtime binary.

**Version: 0.42.0 · Status: experimental · License: GPL-2.0-only**

Native **1080p60 capture works in OBS**. The driver also exposes HDMI audio through
ALSA, RGB lighting controls, and live incoming resolution/frame-rate information.
This project is not affiliated with or endorsed by AVerMedia or OpenAI.

## Tested distro and kernels

The hardware test system runs **CachyOS Linux, rolling release, x86_64**.

| Kernel release | Validation |
| --- | --- |
| `7.2.6-1-cachyos` | Running kernel: GC573 hardware capture, OBS, ALSA and RGB controls tested |
| `7.2.3-1-cachyos` | Module compilation tested; no runtime claim for this kernel |
| `6.18.52-1-cachyos-lts` | Module compilation tested with GCC; no runtime claim for this kernel |

The test system uses OBS Studio **32.2.2**, Python **3.14.7**, PyGObject **3.56.3**,
GTK **4.22.5**, and v4l-utils **1.32.0**. These are recorded test versions, not a
requirement to downgrade or pin those packages. The main kernel build uses
Clang/LLVM **22.1.8**; the build script selects Clang or GCC from the kernel config.

GitHub Actions is configured to build with Ubuntu distribution headers and run
hardware-independent tests. That is not a claim of hardware validation on Ubuntu.
Other distributions and board revisions still need testing.

## Hardware capabilities versus current driver support

AVerMedia's [current official GC573 specifications](https://www.avermedia.com/product-detail/GC573)
list these maximum modes for both capture and HDMI passthrough:

| Resolution | Advertised maximum rate |
| --- | --- |
| 1920×1080 | 240 fps / Hz |
| 2560×1440 | 144 fps / Hz |
| 3840×2160 | 60 fps / Hz, including HDR support in the official software |

Those are **card specifications**, not features already working in this driver.

| Feature | Current native driver status |
| --- | --- |
| 1080p60 video | Hardware verified in OBS; RGB 8-bit HDMI input → V4L2 BGR24 |
| 720p and lower-rate 1080p | Bounded support implemented; additional source modes need hardware validation |
| 1080p240 / 1440p144 / 4K60 / HDR | Not implemented; do not select these expecting working native capture |
| HDMI audio | Stereo S16_LE, 48 kHz; non-silent stereo audio recorded in OBS; channel order and content A/V sync still need a reference test |
| RGB | Generated rainbow, solid color, off and brightness; controls tested during capture; user confirms RGB works great |
| Control app | Live incoming resolution/rate, signal state, capture/audio state and saved lighting settings |
| Signal loss | User confirms recovery works in the current setup; detailed cable/source-mode test coverage pending |
| Automatic startup | System boot service with HDMI-lock waits and resumable startup; user confirms automatic loading after reboot |
| External HDMI passthrough | 1080p60 video confirmed; user reports very good latency; output audio and numerical latency measurement pending |
| Suspend/resume, compressed or multichannel audio | Not supported |

The driver targets PCI `1461:0054`, subsystem `1461:5730`, FPGA `20201015`, board
`57300102`. It rejects unknown capture revisions. HDCP-protected content is not
supported.

The card is specified for PCIe Gen 2 ×4. Our test PC currently negotiates **Gen 2
×2**, with an immediate upstream port limited to ×2. This matters for high-rate
capture: uncompressed RGB24 at 1080p240 or 4K60 is about **1.49 GB/s**, above that
link's theoretical payload capacity before PCIe transaction overhead. Lower-bandwidth
formats and/or a suitable ×4 connection are needed for those capture modes.
HDMI passthrough latency requires separate testing; PCIe bandwidth is not a measurement
of its latency.

## Easy install and uninstall

Clone or download this repository into a stable directory owned by your normal
desktop account. With an active **1080p60, RGB 8-bit, SDR** source connected to
HDMI IN (audio: **48 kHz stereo PCM**), run:

```sh
git clone https://github.com/Zxre0/Avermedia-GC573-Driver-For-Linux.git gc573-native
cd gc573-native
./install.sh
```

Run the script **without sudo**; it requests sudo for system changes and lets
the package manager show its normal confirmation prompts. It installs the
build tools, matching kernel headers, GTK/Python dependencies, video/audio
utilities and OBS; builds the driver; installs **GC573 Control** in your app
menu; and enables automatic loading at boot with saved RGB restoration.
It uses pacman on Arch/CachyOS and apt on Debian/Ubuntu. Hardware testing remains
limited to CachyOS; Ubuntu compilation in CI is not a hardware test.

You need administrator access through sudo, a running systemd system, internet
access for missing packages, and exactly one GC573 for automatic discovery.
Keep your distribution updated. If repository headers no longer match your
running kernel, update/reboot and rerun the installer. Secure Boot signing is
not automated. A checkout/home directory unlocked only at login needs the
[manual login-only setup](#6-load-automatically-after-a-restart).

If dependencies are already installed, or you use another distribution:

```sh
./install.sh --skip-deps
```

The installer enables the privileged helper described in
[step 6](#6-load-automatically-after-a-restart): this trusts the checkout and its
module with root/kernel execution. Keep the checkout in the same location.
Installation queues initialization; check its result with
`systemctl status gc573-native-boot.service --no-pager`. A late HDMI source is
handled by the startup service. Open **GC573 Control** from the app menu, then
follow [step 5](#5-configure-obs-video-and-audio) to add video and audio in OBS.
The installer does not overwrite your OBS scenes or profiles.

To uninstall, close OBS and GC573 Control and run from the same checkout and
desktop account:

```sh
./uninstall.sh
```

This removes **all project installation components**: boot and login services,
the loaded kernel module, privileged helper and sudoers rule, app launcher and
menu entry, saved RGB preferences, runtime checkpoints, generated modules/test
binaries, Python caches, and automatic startup logs. It also works after a
partial or manual installation. If WirePlumber holds the audio device, removal
briefly pauses it and restores it afterward; desktop audio may pause. A driver
still held by another application is not forcibly unloaded: the script exits
with an error and can be rerun after closing that application.

To keep your lighting preferences instead:

```sh
./uninstall.sh --keep-settings
```

Shared distribution packages (including OBS, GTK and kernel headers), OBS
configuration, recordings, the source checkout, research/test evidence and
backups are preserved. System journal entries remain under systemd's retention
policy. The driver is built and loaded from this checkout; it does not install a
DKMS package or copy a module into `/lib/modules`. Removal does not reset card
registers. Use the same `XDG_CONFIG_HOME`/`XDG_DATA_HOME` values as at installation
if you customize these directories.

The complete manual method follows for users who prefer individual steps.

## Manual installation tutorial — CachyOS

### 1. Install dependencies and matching kernel headers

For the regular `linux-cachyos` kernel:

```sh
sudo pacman -Syu --needed base-devel git clang llvm python python-gobject gtk4 \
  kmod sudo util-linux v4l-utils alsa-utils obs-studio linux-cachyos-headers
```

#### Using yay instead

You can use `yay` for the dependency installation above. Check whether it is
already installed:

```sh
yay --version
```

If the command is missing, build and install it using the
[upstream yay installation instructions](https://github.com/Jguer/yay#installation):

```sh
sudo pacman -Syu --needed git base-devel
mkdir -p ~/src
cd ~/src
git clone https://aur.archlinux.org/yay.git
cd yay
makepkg -si
cd ..
```

Run `makepkg` and `yay` as your normal user; they request sudo when needed.
Install the driver dependencies with:

```sh
yay -Syu --needed base-devel git clang llvm python python-gobject gtk4 \
  kmod sudo util-linux v4l-utils alsa-utils obs-studio linux-cachyos-headers
```

`-Syu` updates the system and installs the listed packages; `--needed` skips
reinstalling packages already current. Review the proposed changes and confirm
the installation prompts. Choose either this command or the pacman command above.

This project does not currently publish an AUR package. After installing the
dependencies, check the headers below and continue with **steps 2–6** to build
the driver, install the control app and configure OBS. Future `yay -Syu` updates
do not rebuild this source-built module automatically; rebuild it for the new
running kernel after a kernel update.

#### Check the kernel headers

For CachyOS LTS, use `linux-cachyos-lts-headers` instead. Other kernel variants
need their corresponding header package. If the update installs a new kernel,
boot that kernel before building. Confirm the running release and header tree:

```sh
uname -r
cat /lib/modules/"$(uname -r)"/build/include/config/kernel.release
```

The two release strings must match. Module signing must also satisfy any Secure
Boot/kernel lockdown policy enforced on your machine; signing is not automated
by this project.

### 2. Get the source

Download and extract this repository, or clone it:

```sh
git clone https://github.com/Zxre0/Avermedia-GC573-Driver-For-Linux.git gc573-native
cd gc573-native
```

All commands below run from the project directory. Keep the checkout in a stable
location: the optional helper, desktop launcher and startup service reference it.

### 3. Build and start the driver

Connect an active, unencrypted **1920×1080, 60 Hz, SDR, RGB 8-bit** HDMI source to
**HDMI IN**. Use this verified mode for the first installation. Set source audio
to **48 kHz stereo PCM**.

```sh
./tools/build.sh
sudo ./tools/load-probe.sh --start
```

The loader discovers a single GC573 and checks its identity and kernel/module
version. With multiple cards, select a PCI address explicitly:

```sh
lspci -nn | grep -i avermedia
sudo ./tools/load-probe.sh --start 0000:05:00.0
```

The address above is an example; use the address shown on your system.
Successful module loading alone is not proof of HDMI capture: the output must
report `capture_error=0` and `capture_video_registered=1`. Startup stops on
unknown hardware states. Initial HDMI bring-up currently requires an active
supported source, and full power-cycle startup has not yet been verified.

### 4. Install the control app

Run this as your normal desktop user:

```sh
./tools/install-app.sh
```

Open **GC573 Control** from your application menu, or run:

```sh
~/.local/bin/gc573-control
```

Choose Rainbow, Solid or Off, adjust brightness, and click **Apply lighting**.
The app shows the actual incoming resolution and measured frame rate. Lighting
controls work while OBS captures. Preferences are stored under
`~/.config/gc573-control/settings.json` (or `$XDG_CONFIG_HOME`). The app uses normal
video-device permissions and does not require sudo.

### 5. Configure OBS video and audio

1. Add **Video Capture Device (V4L2)** and choose **AVerMedia GC573 Native**.
2. Select **1920×1080**, **BGR3/BGR24**, **60 fps**, and **Full** color range.
3. Add **Audio Capture Device (ALSA)**, choose **Custom**, enter `hw:GC573,0`,
   and select **48000 Hz**. Set OBS audio to 48 kHz stereo as well.
4. Play known sound on the HDMI source and check the OBS meter and a recording.
   Source volume affects the captured level. Content synchronization and channel
   order still need verification; do not infer them from an active audio meter.

The ALSA device is exclusive: close other programs using it before opening it
in OBS. For a stable video-device path, use the card's entry in
`/dev/v4l/by-path/` rather than assuming it will always be `/dev/video0`.

### 6. Load automatically after a restart

Install the system boot service once, from your normal account in the checkout:

```sh
sudo ./tools/enable-unattended-probes.sh
./tools/run-probe.sh --check
./tools/install-startup.sh --boot
```

The last command enables and starts `gc573-native-boot.service`, and disables
an older login-only service if installed. The card will initialize at system
boot, including before desktop login. The service builds the module as the
checkout owner, loads it through the helper, and restores that account's saved
RGB settings. Keep this checkout in its current location and install matching
kernel headers after kernel updates. The checkout and the user's home directory
must be available at boot; a home directory unlocked only at login requires
the login-only option below.

The helper grants root/kernel execution of this **user-writable checkout and
its module**. Enable it only for source you trust. The boot service gives its
process access to the video/audio groups; it does not add those groups to your
account globally.

Check startup with:

```sh
systemctl is-enabled gc573-native-boot.service
systemctl status gc573-native-boot.service --no-pager
journalctl -u gc573-native-boot.service -b --no-pager
```

`active (exited)` is normal: initialization has finished and the kernel driver
continues running. If the HDMI source is late, startup waits for power and link
lock, then retries after 10 seconds using a checkpoint from the current boot.
Keep a supported HDMI source active for initial bring-up. While waiting, the
service may show `activating (auto-restart)` and the capture device may not yet
exist. Hardware errors after partial writes stop startup instead of replaying
resets. The original reboot failure was a transient link loss immediately after
TX1 activation; startup now waits again at that point.

To restart the service manually after correcting a problem:

```sh
sudo systemctl restart gc573-native-boot.service
```

To remove boot startup:

```sh
./tools/install-startup.sh --remove-boot
```

For **login-only startup**, remove the boot service first if installed, then:

```sh
./tools/install-startup.sh
systemctl --user start gc573-native.service
```

See [unattended setup and removal](docs/unattended-probes.md) for the helper's
scope and troubleshooting. If you move the checkout, reinstall the helper,
app and selected startup service. The user has confirmed successful automatic
loading after reboot with this fix. A full power-off/power-on test remains separate.

## Passthrough and latency

Connect the gaming display to the card's **HDMI OUT** when testing physical
passthrough. An OBS preview goes through capture, PCIe, application processing
and display presentation; its delay does not establish HDMI passthrough delay.

Version 0.42.0 adds startup for splitter TX2, the external output
identified when a second sink was connected. Previously only TX1, feeding the
internal capture receiver, was enabled. TX2 now reports a connected sink and
locked output; its analog setup and output-enable controls passed readback.
The user confirmed working HDMI OUT video on 2026-09-22 with the current
1080p60 source. HDMI OUT audio remains unverified.

Connect and power the HDMI OUT display before starting the driver. Capture
registration attempts the bounded RGB 8-bit, unencrypted output setup and
preserves an already enabled TX2. An absent display does not prevent capture.
The `passthrough_startup_*` status fields describe that startup attempt, not a
live display check. Automatic output hotplug recovery is not implemented yet.

The user reports very good passthrough latency in use on 0.42.0. No end-to-end
latency number has been measured for this driver.
Validation must compare a direct display reference with HDMI OUT at the same
mode, then repeat with OBS closed and with video/audio capture active.

## Troubleshooting

- **Missing headers / version mismatch:** install headers matching `uname -r`,
  or reboot into the kernel matching your installed headers, then rebuild.
- **Module is in use:** close OBS and other capture applications. Desktop audio
  services can also hold the ALSA control device open. Do not force module removal.
- **No video device / startup error:** keep the source active at 1080p60 RGB8 SDR,
  inspect the reported error fields, and save the output. Logs from the helper
  are stored locally under `reports/`.
- **No audio:** select HDMI as the source's sound output, choose 48 kHz stereo PCM,
  check volume/mute, and select `hw:GC573,0` in OBS. Compressed bitstreams and
  multichannel capture are not implemented.
- **App says access denied:** check your desktop session's video-device access.
  Running the whole GUI as root is not required.

Read live diagnostics without changing hardware:

```sh
python3 app/gc573_control.py --status
python3 tools/collect.py
```

## Tests and development

```sh
./tools/test.sh                       # protocol fault injection, Python, shell syntax
./tools/build.sh                      # running kernel
./tools/build.sh 6.18.52-1-cachyos-lts  # another installed header tree
v4l2-compliance -d /dev/video0         # close OBS first; select your actual node
```

V4L2 compliance on 0.42.0: **48 passed, zero failures, zero warnings**. Protocol
harnesses use AddressSanitizer and UndefinedBehaviorSanitizer. Hardware captures
and raw reports remain local; CI and fake-I/O tests cannot prove HDMI operation.

See [test evidence](docs/testing.md), [protocol notes](docs/protocol.md),
[contributing](CONTRIBUTING.md), and the remaining [TODO](TODO.md).

## Manual uninstall

For complete removal, use `./uninstall.sh` as described above. To remove the
components individually while keeping preferences and build files:

If automatic startup and the helper were installed:

```sh
./tools/install-startup.sh --remove-boot
./tools/install-startup.sh --remove
sudo ./tools/enable-unattended-probes.sh --remove
```

Remove the desktop launcher as your normal user:

```sh
rm -f ~/.local/bin/gc573-control
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/gc573-control.desktop"
```

After closing capture applications, `sudo rmmod gc573_native` unloads the driver
if nothing holds it open. Removal does not reset persistent card settings or
delete your checkout, recordings or saved preferences.

## Credits and license

**Astra (OpenAI Codex)** wrote the native driver, control app, tests and
installation tooling, with iterative hardware testing on the owner's GC573.
AVerMedia's official driver packages were used to research the hardware protocol;
their binaries are not distributed with this source.

Licensed under [GPL-2.0-only](LICENSE). Private recordings, proprietary research
inputs, local logs, editor backups and generated build products are excluded
from Git and source exports.
