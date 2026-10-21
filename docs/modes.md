# Capture modes and passthrough

All modes are switched with `tools/set-mode.py` (close OBS and GC573 Preview
first); the choice is saved for the boot service. See the
[README summary](../README.md#capture-modes). Version-by-version test evidence
is in [testing.md](testing.md).

## GC573 Preview details

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

## Native 1440p120 capture

_Experimental, added in 0.46.1._

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
1080p/720p fallback are verified on the test PC. Version 0.46.0 still delivered
60 fps from a 120 Hz input because the FPGA capture timer retained its reset
default. Version 0.46.1 programs that timer on stream start and recovery, bounded
by the source rate. A 30-second recovered 1440p120 test delivered 119.87 native frames and DMA
completions per second, with intact guards and no capture errors.
Longer 120 fps runs and motion/frame-uniqueness checks remain pending. Preview
displays the measured capture rate separately from incoming HDMI timing.
Version 0.46.4 fixes capture stopping after a transient HDMI OUT display-status
read failure. A monitor-side NACK or timeout now leaves the locked capture path
running once the DDC engine is safely aborted and restored; status is checked
again on the normal polling schedule. Register-transfer and cleanup failures
still stop the worker.

Version 0.46.2 adds one automatic transmitter recovery attempt when an otherwise
completed HDMI setup remains unlocked through six same-rate checks. This fixes
a reproduced 60-to-120 Hz transition stall without replacing EDID or pulsing
source HPD. It does not retry failed register transfers indefinitely.

Capture memory now holds 11,059,200 bytes per frame. Larger formats, 144 Hz
capture, YUV output and HDR remain unsupported. The previously verified
1440p120 passthrough **with 1080p60 capture** is documented separately below.

## 1440p passthrough with 1080p capture

_Experimental, added in 0.45.3._

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

## High-rate HDMI passthrough

_Experimental, added in 0.44.0._

HDMI OUT can now use a separate **RGB 8-bit SDR passthrough mode**. It reads your
connected monitor's EDID and advertises supported timings within the GC573's
HDMI 2.0 limits. It enables SCDC scrambling and the high-speed clock ratio above
340 MHz. Passthrough bypasses host capture, so PCIe ×2 does not impose the
1080p capture limit on HDMI OUT.

**OBS video and ALSA capture are disabled in this mode**, including when the
source sends 1080p. Use the experimental `scaled` mode above when host capture
is needed. RGB controls remain available. Close OBS and other capture applications, then run
as your normal desktop user after installation (sudo is requested to reload the driver):

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

## Opt-in input-only 1440p144 with capture capped at 60 fps

`experimental_144hz=1` is an explicit, **default-off** input-only profile.
It requires `scaled_capture=1` and `capture_video=1`, but **does not require
anything connected to HDMI OUT**. This is intended for a PC desktop mirrored
to the capture card at 2560x1440 around 144 Hz. Capture remains paced at **at
most 60 fps**, even when an application requests 120 or 144 fps.

For an authorized temporary test on a cold, unbound card:

```bash
sudo python3 ./tools/start-capture.py --experimental-144hz 0000:04:00.0
```

Use the actual GC573 BDF. The checked cold prefix hands off at phase6; the
worker finishes the resident-EDID/internal-link initialization through
phase13, then uses the bounded TX1/receiver/FPGA profile rather than the
restricted legacy phase14 output. Interrupted writing phases remain stopped.
This option is not added to installation, automatic startup or saved modes.

The resident input EDID is retained, not replaced with a connected monitor's
EDID or a forced 1080p profile. Source display/mirroring settings are not
changed. The experimental path does not configure external TX2 output.
Progressive RGB8/SDR, supported geometry, the receiver's complete clock
interval <=600 MHz and FPGA timing (143-145 Hz for the added 1440p mode) gate
readiness and capture. Native 144-fps capture, HDR, YUV and 4K are not added.
The PCIe bandwidth requirement for 1440p capture is unchanged.

Without the opt-in, legacy input/capture/passthrough behavior is unchanged.
The explicit input profile reuses the existing dual-TTL/DDR register sequence;
it does not introduce new register programming tables.

A bounded native hardware test on CachyOS kernel `7.2.11-1-cachyos` verified
mirrored 2560x1440 input at 143.897 Hz with HDMI OUT unconnected. OBS 32.2.2
captured 1920x1080 BGR24 at 60 fps: 301 encoded frames over about 5.017 seconds,
321 returned capture frames/interrupts, and a visually checked useful desktop
image. Capture/HDMI errors stayed zero; audio DMA stayed off in all 124 status
samples. Module unload, unbound PCI state, enable zero and busmaster off were
read back after capture. Tested code commit: `d6c7903`.

This is not a long-duration, source-mode-transition, native 1440p60-output or
HDMI-OUT proof. The opt-in remains experimental and is not installed or
enabled automatically. Private desktop footage is not included in the repo.

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
