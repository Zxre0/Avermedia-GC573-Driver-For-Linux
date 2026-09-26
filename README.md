# GC573 Native — Linux driver and RGB control app

**AI-Slop development with GPT-6 Astra (200 million tokens used so far)**, with hardware testing on the project owner's
AVerMedia Live Gamer 4K GC573. This is an original native Linux driver and GTK4
control app, developed through hardware testing and research of AVerMedia's
official driver protocol. It does not install or link a community driver or
require a proprietary runtime binary.

**Version: 0.46.0 · Status: experimental · License: GPL-2.0-only**

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
| Native 1440p120 capture | Implemented in 0.46.0 for PCIe Gen2 ×4; live full-rate verification pending (see below) |
| 4K capture / 1440p144 capture / HDR | Not implemented |
| Hardware scaling | 1440p119.89 input → 1080p60 capture, 1440p59.94 → 1080p59.94, and 1080p → 720p verified |
| 1440p120 HDMI OUT + 1080p60 capture | Verified on a PS5 in 0.45.3: 119.89 Hz input, 60 fps capture, nonzero audio; user confirms 120 Hz output |
| High-rate HDMI OUT | Experimental RGB8 SDR mode: display-matched timings capped at 1080p240, 1440p144 and 4K60; host capture disabled; see limitations below |
| HDMI audio | Stereo S16_LE, 48 kHz; non-silent stereo audio recorded in OBS; channel order and content A/V sync still need a reference test |
| RGB | Generated rainbow, solid color, off and brightness; controls tested during capture; user confirms RGB works great |
| Control app | Live incoming resolution/rate, signal state, capture/audio state and saved lighting settings |
| Signal loss | User confirms recovery works in the current setup; detailed cable/source-mode test coverage pending |
| Automatic startup | Loads video/audio devices and RGB without HDMI video; finishes HDMI setup in the background when a supported source arrives |
| External HDMI passthrough | User confirms 1080p60 and 1440p120 video; user reports very good latency; output audio and numerical latency measurement pending |
| Suspend/resume, compressed or multichannel audio | Not supported |

The driver targets PCI `1461:0054`, subsystem `1461:5730`, FPGA `20201015`, board
`57300102`. It rejects unknown capture revisions. HDCP-protected content is not
supported.

The card is specified for PCIe Gen 2 ×4. The test PC now negotiates **Gen 2 ×4**;
the earlier 1080p60 capture tests used ×2. Native 1440p120 BGR24 transfers about
**1.33 GB/s** of video payload. The driver checks the narrowest upstream PCIe
link before offering the new format; a Gen2 ×2 connection keeps the 1080p60
capture limit. A wider connection alone does not enable 4K or HDR capture.
HDMI passthrough latency requires separate testing; PCIe bandwidth is not a measurement
of its latency.

## PS5 preview for Discord (no OBS required)

Open **GC573 Preview** from your application menu. It displays the native capture
stream in a normal window and plays stereo console audio through your desktop's
default output. Close OBS first: the preview and OBS cannot own the capture
stream simultaneously. The preview offers Pause/Resume, sound/volume controls,
fullscreen (F11; Escape to leave), and automatic recovery when a supported input
returns. It keeps only the newest video frame to avoid a growing preview delay.
This is a capture preview; play on HDMI OUT for the lowest latency.

For an existing installation, install the additional dependencies and launchers:

```sh
# Arch / CachyOS
sudo pacman -S --needed gstreamer gst-plugins-base gst-plugins-good
# Debian / Ubuntu alternative:
# sudo apt-get install gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 \
#   gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-alsa
./tools/install-app.sh
python3 tools/set-mode.py capture  # if passthrough-only mode was enabled
~/.local/bin/gc573-preview
```

The full installer includes these dependencies. You can also launch directly
with `python3 app/gc573_preview.py`; add `--mute` to start without local sound.

In your Discord call, choose **Share Your Screen** and select the **GC573 Preview**
window in the system picker. Enable sound/select the preview's audio stream if
your client offers that option. The app never joins calls or starts a broadcast.
Remote audio support depends on the Discord client and Linux audio/portal setup;
local playback does not prove that viewers hear it. See
[Discord's screen-sharing guide](https://support.discord.com/hc/en-us/articles/360040816151-Go-Live-and-Screen-Share).
An actual Discord call has not been tested by this project.

The preview works in **capture** and **scaled** modes with HDCP disabled. It
shows HDMI input resolution/rate separately from preview resolution and its
capture frame-rate limit. Passthrough-only mode does not provide host capture.

## Experimental native 1440p120 capture (0.46.0)

On PCIe Gen2 ×4, the driver now exposes **2560×1440 BGR24 at up to 120 fps**
to OBS and other V4L2 applications. It uses the existing combined HDMI profile
(`scaled` in the startup settings). Selecting native 1440p bypasses the FPGA
scaler; selecting 1080p or 720p still enables downscaling as before. HDMI OUT
keeps the source timing in either case.

Set the combined profile once, then open your capture application:

```sh
python3 tools/set-mode.py scaled
```

In OBS, choose the GC573 Video Capture Device (V4L2), **2560×1440**, **BGR3/BGR24**,
and **120 fps** (or 119.88 fps). Requests between 60 and 120 fps fall back to
60 fps. OBS's project/output frame rate is a separate
setting. For Preview, launch `gc573-preview`: it automatically selects native
1440p120 when the input and PCIe connection support it. To keep the lighter
1080p60 preview for Discord, use `gc573-preview --limit-1080p`.

The source must actually output 1440p120 RGB8 SDR with HDCP disabled. A 60 Hz
source produces 60 captured frames per second even when the capture limit is
120. A monitor refresh rate or a V4L2 format listing alone does not verify capture
throughput. Native 1440p60 frame delivery, a full-resolution image, audio, reopening and
1080p/720p fallback are verified on the test PC. The final four-slot DMA queue
still needs a sustained native 120 fps test. Earlier single-transfer tests
delivered only 60 fps with a 120 Hz input. Preview now displays the measured
capture rate separately from incoming HDMI timing.

Capture memory now holds 11,059,200 bytes per frame. Larger formats, 144 Hz
capture, YUV output and HDR remain unsupported. The previously verified
1440p120 passthrough **with 1080p60 capture** is documented separately below.

## Experimental 1440p passthrough with 1080p capture (0.45.3)

The new `scaled` mode keeps HDMI OUT at the console's input timing while the
FPGA scales the capture image to 1080p (or 720p when requested by V4L2). Capture
requests are paced before DMA, so unwanted source frames are not transferred
over PCIe. At 1080p60 BGR24 the video payload is about 373 MB/s; scaling happens
on the card, rather than receiving full-resolution 1440p120 on the host.

**1440p120 HDMI passthrough with 1080p60 capture is hardware verified on a
PS5 in 0.45.3.** The user confirmed 120 Hz output; the driver measured
2560×1440 at 119.89 Hz while delivering 1,200 1920×1080 frames over 20 seconds
(60.00 fps), with nonzero audio and intact video/audio DMA guards. A live
60→120 transition recovered automatically without reloading the driver.
1440p59.94 → 1080p59.94 capture and closing/reopening capture were also verified.

Version 0.45.1 fixes the scaled-mode `error -95` stop during format negotiation,
configures the FPGA for the receiver's dual-pixel DDR output, and restores that
configuration after capture stops. Unsupported formats now wait for a valid
RGB8 SDR signal. Capture can continue while the connected monitor's RxSense is
inactive. Transport failures still stop with a diagnostic error.

The PS5's 120 Hz mode test failed on 0.45.1: the worker stopped on a late HDMI
link-ready check and left the internal transmitter at the previous high TMDS
ratio. Version 0.45.2 adds a checked continuation and remeasures the source
before resuming. The subsequent 0.45.3 live retest confirmed working 120 Hz
input/passthrough and 1080p60 capture.
Version 0.45.3 also samples I2C status immediately after starting each read to
avoid missing the short busy interval under CPU load. Stale completion values
remain rejected.

At both 1440p59.94 and 1440p119.89, the live preview received nonzero 48 kHz stereo PCM with
no audio DMA guard errors. Channel order and content-relative A/V synchronization
still need a reference test.

Close OBS/GC573 Preview, then run as your desktop user:

```sh
python3 tools/set-mode.py scaled
~/.local/bin/gc573-preview
```

The selection is saved for the installed boot service. On a cold start, the
normal checked HDMI preparation runs first; the service then enables the
combined profile. Cold boot, repeated 60↔120 changes and physical hotplug in
this mode still need broader hardware validation. Use `python3 tools/set-mode.py capture` to return to
the conservative 1080p source profile.

On PS5, disable HDCP and HDR/VRR, select **1440p**, run **Test 1440p Output**, and
set **120 Hz Output** to **Automatic**. A compatible game must request 120 Hz;
the home screen may still run at 60 Hz. Use stereo Linear PCM audio. The driver
reads the connected monitor's EDID and offers only supported RGB8 SDR modes up
to 1440p120. It does not force the PS5 to change a manually selected resolution.
For downscaled OBS capture, choose 1920×1080, BGR3/BGR24 and 60 fps. Play on the HDMI OUT monitor;
share the preview/OBS window on Discord.

The monitor used for development advertises 2560×1440 at 59.95 and 119.998 Hz in
the combined profile. 144 Hz and 4K timings are omitted from this capture mode.
The separate passthrough-only mode below retains its broader timing list.


## Experimental high-rate HDMI passthrough (0.44.0)

HDMI OUT can now use a separate **RGB 8-bit SDR passthrough mode**. It reads your
connected monitor's EDID and advertises supported timings within the GC573's
HDMI 2.0 limits. It enables SCDC scrambling and the high-speed clock ratio above
340 MHz. Passthrough bypasses host capture, so PCIe ×2 does not impose the
1080p capture limit on HDMI OUT.

**OBS video and ALSA capture are disabled in this mode**, including when the
source sends 1080p. Use the experimental `scaled` mode above when host capture
is needed. RGB controls remain available. Close OBS and other capture applications, then run
as your normal desktop user after installing the helper:

```sh
python3 tools/set-mode.py passthrough
```

This builds/loads the driver and saves the mode for the installed boot/login
service. It briefly stops and restores WirePlumber when necessary to release
the ALSA device. HDMI negotiation continues in the background. To restore the
normal 1080p60 OBS video/audio mode and its boot preference:

```sh
python3 tools/set-mode.py capture
```

Keep the display connected to **HDMI OUT** and the console connected to **HDMI IN**.
For PS5, keep HDCP disabled, use SDR with HDR/VRR off, select 1440p, run the console's
1440p output test, and enable 120 Hz output. A compatible game must actually request
120 Hz; the console menu alone is not a 120 Hz test. The source chooses the mode;
the driver does not control console settings. See Sony's
[PS5 video-output guide](https://www.playstation.com/en-ca/support/hardware/ps5-4k-resolution-guide/).

On the test display, the filtered EDID includes **1440p120, 1440p144 and 4K60**.
The PS5 selected 3840×2160, and the monitor reported clock/channel lock and active
scrambling. **Physical high-rate picture/audio and 1440p120 gameplay are not yet
user-verified.** The clock-derived refresh estimate reads approximately 55 Hz
for the nominal 4K60 mode and needs correction; do not treat it as verified 60 Hz.

Limitations: HDR, deep color, YCbCr, HDMI 2.1 FRL, DSC and VRR are not advertised.
Only supported CTA modes and EDID detailed timings are retained; DisplayID-only
modes are not converted. 1080p240 requires a display-advertised timing within
600 MHz and has not been tested. This display's DisplayID 1080p240 timing exceeds
600 MHz, so it is excluded. Monitor replacement, suspend/resume and cold boot in
passthrough mode still need hardware validation. If initialization reports an
error, inspect diagnostics before retrying; failed hardware writes are not replayed.

## Easy install and uninstall

Clone or download this repository into a stable directory owned by your normal
desktop account. The driver can load with HDMI disconnected or the source off.
For the default capture mode, use **1080p60, RGB 8-bit, SDR** on HDMI IN with **48 kHz stereo PCM**
audio. To install, run:

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

If dependencies are already installed, or you use another distribution, see the
[non-Arch compilation tutorial](#compiling-on-non-arch-distributions) and then run:

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

To uninstall, close OBS, GC573 Preview and GC573 Control and run from the same checkout and
desktop account:

```sh
./uninstall.sh
```

This removes **all project installation components**: boot and login services,
the loaded kernel module, privileged helper and sudoers rule, app launchers and
menu entries, saved RGB preferences, runtime checkpoints, generated modules/test
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
sudo pacman -Syu --needed base-devel git clang llvm python python-gobject gtk4 gstreamer gst-plugins-base gst-plugins-good \
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
yay -Syu --needed base-devel git clang llvm python python-gobject gtk4 gstreamer gst-plugins-base gst-plugins-good \
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

For capture, connect an active, unencrypted **1920×1080, 60 Hz, SDR, RGB 8-bit** HDMI source to
**HDMI IN**. Use this verified mode for the first capture. Set source audio
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
Successful registration reports `capture_error=0` and `capture_video_registered=1`.
Since 0.43.0, video/audio devices and RGB can register before HDMI video is
available. HDMI setup continues inside the driver; `hdmi_ready=1` and a valid
input are required for frames. Unknown hardware states still stop HDMI setup.
Version 0.43.1 fixes startup stopping at `hdmi_phase=16`, `hdmi_error=-34`
when receiver timing is not yet valid. The driver now repeats safe timing
measurements until they settle; unsupported formats and partial programming
failures still stop setup. This addresses a PS5 startup failure with HDCP disabled.
A full power-off/power-on startup test remains pending.

### 4. Install the control and preview apps

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

`active (exited)` is normal: device registration has finished and the kernel
driver continues running. **An HDMI source is not required to load the driver.**
The video device, ALSA audio device and RGB controls remain available while a
background worker waits for HDMI power/lock. Connect or turn on a supported
source later; startup continues without unloading those devices. An open video
queue waits for matching supported input; audio capture requires a valid
48 kHz stereo PCM signal and may need reopening after the source arrives.

`hdmi_ready=0` with `hdmi_waiting=1` means HDMI setup is waiting; `hdmi_ready=1`
means setup finished. `hdmi_error` reports hardware/protocol failures separately
from device registration, and GC573 Control displays that state. Partial-write
failures stop the worker instead of replaying resets. Restarting the service
preserves an already registered card, including one waiting for input.

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

## Compiling on non-Arch distributions

These instructions cover conventional **Debian/Ubuntu, Fedora and openSUSE**
installations on an x86_64 PC. Compilation has passed in Ubuntu CI with
`6.8.0-142-generic` headers; hardware capture has only been tested on CachyOS.
The commands below do not establish compatibility with every distro/kernel
release. Older kernels and vendor-patched kernels may need code changes.

Build on the host that contains the PCIe card. A container's kernel headers or
an immutable distro's development container do not automatically prepare its
host to load this module. The startup installer requires systemd; the manual
build itself does not.

### A. Install your distribution's dependencies

Use **one** of the following sets of commands. They install compiler/build
tools, the control app's GTK4/Python dependencies, diagnostics and OBS.

#### Debian, Ubuntu and Linux Mint

```sh
sudo apt-get update
sudo apt-get install build-essential git clang llvm python3 python3-gi \
  gir1.2-gtk-4.0 kmod sudo util-linux pciutils v4l-utils alsa-utils obs-studio \
  "linux-headers-$(uname -r)"
```

Use headers matching the **running** kernel, including its flavor, such as
`generic`, `lowlatency` or `amd64`. If apt cannot find that exact package, update
the kernel through your distribution, reboot into it, and retry. For a custom
kernel, obtain its matching prepared build tree from its provider. See Debian's
[external-module guidance](https://www.debian.org/doc/manuals/debian-handbook/sect.kernel-compilation.en.html).

#### Fedora Workstation / conventional Fedora installations

```sh
sudo dnf install gcc make git clang llvm elfutils-libelf-devel openssl-devel \
  python3 python3-gobject gtk4 kmod sudo util-linux pciutils v4l-utils \
  alsa-utils obs-studio "kernel-devel-$(uname -r)"
```

Fedora's matching **`kernel-devel`** package supplies the module build tree;
`kernel-headers` alone is insufficient. If the running release is no longer
available in your enabled repositories, update the kernel, reboot, and install
the matching development package. A debug or custom kernel needs the corresponding
development package. See [Fedora's kernel-devel description](https://packages.fedoraproject.org/pkgs/kernel/kernel-devel/).

#### openSUSE Tumbleweed / Leap with the default kernel

```sh
sudo zypper refresh
sudo zypper install gcc make git clang llvm libelf-devel libopenssl-devel \
  kernel-devel kernel-default-devel python3 python3-gobject python3-gobject-Gdk \
  typelib-1_0-Gtk-4_0 libgtk-4-1 kmod sudo util-linux pciutils v4l-utils \
  alsa-utils obs-studio
```

This example uses `kernel-default`. Match the installed kernel flavor and
version with its development packages; reboot into the matching kernel after
an update. The [SUSE kernel module manual](https://documentation.suse.com/sbp/systems-management/pdf/SBP-KMP-Manual-SLE12SP2_en.pdf)
explains flavor-specific build dependencies. Package availability varies by
release and enabled repositories. If OBS is unavailable, install the remaining
dependencies first and obtain OBS separately through your distribution.

GTK/Python package names above follow the
[upstream PyGObject installation guide](https://pygobject.gnome.org/getting_started.html).
For another distro, install equivalent tools and a prepared external-module
build tree for your running kernel, then continue below.

### B. Check the headers and get the source

```sh
uname -r
cat "/lib/modules/$(uname -r)/build/include/config/kernel.release"
python3 -c "import gi; gi.require_version('Gtk', '4.0'); from gi.repository import Gtk"
```

The first two commands must report the same kernel release. A missing file or
mismatch means the kernel development packages need fixing before compilation.
The Python command should exit successfully without opening a window.

Clone as your normal desktop user into a stable location:

```sh
git clone https://github.com/Zxre0/Avermedia-GC573-Driver-For-Linux.git gc573-native
cd gc573-native
```

If you already downloaded the source, enter that directory instead. All remaining
commands run from the repository root.

### C. Compile the module

```sh
./tools/build.sh
modinfo ".build/modules/$(uname -r)/gc573_native.ko"
```

Build without sudo. The script uses `/lib/modules/<release>/build`, selects
Clang/LLVM when the kernel config requires it (otherwise GCC), and saves the
module under `.build/modules/<release>/gc573_native.ko`. Its `vermagic` must
begin with your running kernel release. Compilation alone does not load the card.
If the kernel requires a particular compiler version, install it; for GCC builds,
you can select an installed compiler using `CC=gcc-14 ./tools/build.sh`, for example.

### D. Load the driver and install the app

Connect an active, unencrypted **1080p60 RGB8 SDR** source to HDMI IN, with
**48 kHz stereo PCM** audio. Connect the passthrough display before loading if
you want HDMI OUT, then run:

```sh
sudo ./tools/load-probe.sh --start
./tools/install-app.sh
python3 app/gc573_control.py --status
```

Look for `capture_error=0` and `capture_video_registered=1` in the loader output.
Open **GC573 Control** from the application menu and follow the existing
[OBS video/audio instructions](#5-configure-obs-video-and-audio).

You can also run these commands without a source: devices/RGB register first,
and the driver waits for HDMI in the background. For automatic loading after reboot, follow
[step 6](#6-load-automatically-after-a-restart). Alternatively, after installing
the dependencies in section A, `./install.sh --skip-deps` performs sections C/D
and boot-service installation together. Debian/Ubuntu can also use `./install.sh`
to install dependencies automatically; Fedora/openSUSE need `--skip-deps`.

### E. Kernel updates and loading errors

After a kernel update, install its matching development packages and boot into
that kernel. For manual operation, repeat sections C/D. The optional boot service
rebuilds for the running kernel automatically when the matching headers and
compiler are available. This project does not provide DKMS or akmods integration.

If loading reports `Key was rejected by service` or a signature/lockdown error,
follow your distribution's module-signing and key-enrollment procedure. Signing
is not automated here; every rebuilt module needs signing again, including a
build made by the boot service. Manual signing once does not make the automatic
rebuild path ready for a system enforcing signatures.

For build failures, include `uname -r`, `/etc/os-release` and the compiler error
when reporting an issue. A successful build does not expand the supported video
modes listed above. To remove the installation, use `./uninstall.sh`, or follow
the [manual uninstall instructions](#manual-uninstall).

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
startup attempts the bounded RGB 8-bit, unencrypted output setup and
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
- **No video device / startup error:** inspect the service journal and reported
  error fields; a missing HDMI source alone should not prevent registration.
  Logs from the helper are stored locally under `reports/`.
- **Device present but no frames:** connect a supported 1080p60 RGB8 SDR source.
  Check `hdmi_ready`, `hdmi_waiting`, `hdmi_error` and `input_present` with the
  status command below. HDMI setup errors need diagnosis, not repeated resets.
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

V4L2 compliance on 0.43.0: **48 passed, zero failures, zero warnings**. Protocol
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

Remove both desktop launchers as your normal user:

```sh
rm -f ~/.local/bin/gc573-control ~/.local/bin/gc573-preview
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/gc573-control.desktop" \
  "${XDG_DATA_HOME:-$HOME/.local/share}/applications/gc573-preview.desktop"
```

After closing capture applications, `sudo rmmod gc573_native` unloads the driver
if nothing holds it open. Removal does not reset persistent card settings or
delete your checkout, recordings or saved preferences.

## Credits and license

The native driver, control app, tests and installation tooling were developed
with **AI assistance from GPT-6 Astra**, with iterative hardware testing on the
owner's GC573. (This is AI slop it works well in my experience)
AVerMedia's official driver packages were used to research the hardware protocol;
their binaries are not distributed with this source.
 

Licensed under [GPL-2.0-only](LICENSE). Private recordings, proprietary research
inputs, local logs, editor backups and generated build products are excluded
from Git and source exports.
