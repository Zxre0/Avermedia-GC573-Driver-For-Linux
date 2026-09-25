# Validation

## Hardware verified

On one GC573 with FPGA `20201015`, board `57300102`, CachyOS 7.2.6:

- OBS recorded 901 frames at 1920×1080, 60 fps after the early audio-DMA enable
  error was corrected. Repeated V4L2 stop/restart tests returned intact frames.
- V4L2 compliance: 48 passed, zero failures, zero warnings (0.42.0).
- RGB controls changed rainbow/solid/off while 600 frames streamed at 60 fps.
  The read-only lighting keepalive preserved sequencer control state during a
  45-second OBS test. The user subsequently confirmed that RGB works great.
- ALSA stereo 48 kHz, S16_LE: 1800 periods over 18 seconds while video ran two
  successive 300-frame captures. Both streams stopped cleanly; video and audio
  guards remained intact. Audio samples were all zero.
- Version 0.40.3 also passed 900 video frames while ALSA was opened, captured
  four seconds, closed, and reopened twice. Both opens succeeded during active
  video; guards remained intact and audio samples were still all zero.
- An OBS test recorded 900 output frames plus a stereo 48 kHz AAC track; the
  user profile scaled the 1080p input to a 1440p recording. An audio track alone
  does not establish audible content.
- Builds and hardware-independent tests passed; new behavior must be retested
  on hardware before extending these claims.

Private recordings and raw logs stay in ignored `reports/`. They may contain
personal desktop content and are intentionally absent from this repository.

## Subsequent non-silent audio evidence

A local OBS recording at 18:59:54 contains 30.3 seconds of 48 kHz stereo audio
alongside video. Decoded channel peaks were -34.10 and -33.69 dBFS, RMS levels
-54.24 and -53.34 dBFS, and neither channel clipped. Audio ended at 30.314 s and
video at 30.333 s; codec priming gives the first audio packet a negative timestamp.
Track timing does not establish source-relative lip sync or correct left/right
channel labeling. Earlier all-zero recordings reflected the signal captured at
that time and are no longer the only audio evidence.

Version 0.41.1 compiles bounded 720p/1080p geometry and source-change events.
720p clock/output configuration passes the fake receiver harness; no matching
720p source has been captured on hardware yet. The host negotiates PCIe Gen 2
x2 despite the card's x4 capability; the immediate upstream port advertises x2.

## TX2 output investigation (0.42.0)

The user reported that HDMI OUT had no picture. A new splitter snapshot showed
TX1 `0x9f`, TX2 `0x17`, and TX0/TX3 `0x14`. The new port-2 activation and output
sequence succeeded: activation error 0, output error 0, 38 readbacks verified,
TX2 output enabled, and both TX1/TX2 subsequently `0x9f`. OBS video/audio streaming
resumed without capture errors or guard failures; saved lighting was restored.
Physical monitor picture/audio and latency are not inferred from those registers.
A subsequent reload reported `passthrough_startup_preserved=1` and
`passthrough_startup_enabled=1` with error 0. The loaded 0.42.0 module passed
48/48 V4L2 compliance checks and captured 180 frames alongside 300 ALSA periods
with no DMA guard failures. OBS was reopened with the user's settings.

The user subsequently confirmed that HDMI OUT video works (2026-09-22,
0.42.0, current 1080p60 source). The user also reports very good passthrough
latency in use; there is no numerical measurement. Output audio is unverified.

The fake harness exercises both ports, 285 transfer failures in TX2 activation
and output, and 327 failure positions in automatic startup. It checks that TX1
registers remain unchanged, absent sinks cause no chip writes, and already
active TX2 output is preserved without reprogramming.

## User confirmation (2026-09-22, 0.42.0)

The user reports that RGB and signal-loss recovery work great and passthrough
latency is really good. This confirms observed operation on the current setup.
The exact signal-loss method, individual lighting modes tested, and a numerical
latency measurement were not supplied. The report does not establish recovery
for every source resolution or external-display hotplug scenario.

## Reboot startup correction (2026-09-22)

The login unit ran after reboot but stopped at TX1 output preflight with
`splitter_video_error=-67`, phase 1, and zero chip writes. Activation had
completed, but RX19 briefly read `0x30` instead of a locked value. A later
read-only snapshot showed lock again. Continuing TX1 output, receiver output,
and capture registration restored video and the saved lighting.

Startup now waits after activation and records successful phases in a root-owned,
boot-specific checkpoint. Tests cover bounded status polling, resumption without
repeating calibration, stale checkpoints, and refusal to replay partial writes.
The system boot unit runs before login as the normal user, with temporary signal
waits retried after 10 seconds. It is enabled and started successfully on the
current machine. The user subsequently confirmed automatic loading works after
reboot with the corrected code. Full power-off/power-on testing remains separate.

## Still required

- Known reference stereo HDMI PCM for channel order, source-relative levels and A/V sync.
- Full power-off/startup followed by login, rather than a prepared-state restart.
- Documented cable/source-power recovery, source mode changes, additional formats and suspend/resume.
- HDMI OUT audio and numerical passthrough latency measurement.
- Independent testing on other boards and kernel distributions.

## Automated tests

### Deferred HDMI startup (0.43.0, 2026-09-24)

On the CachyOS 7.2.6 test PC, the new module registered `/dev/video0`, the
`GC573` ALSA card and RGB controls while FPGA input reported zero presence,
width and height. At that point `capture_started=0`, `capture_error=0`,
`hdmi_ready=0` and `audio_registered=1`. The worker then finished HDMI setup
(`hdmi_phase=19`, `hdmi_ready=1`, `hdmi_error=0`) and acquired 1080p60 without
reloading the module. The source was connected during this test; it validates
registration before video acquisition, not a fresh boot with the cable unplugged.

After acquisition, 120 V4L2 frames and 300 concurrent ALSA periods completed
with intact guards. The three-second PCM recording contained silence; this
test does not add a new claim of audible content. V4L2 compliance passed
48/48 with zero warnings. The boot service returned `active (exited)` and the
saved solid-green lighting was restored.

The real protocol fake tests cover repeated absent-source and receiver-power
waits without chip writes, later lock acquisition, recognized powered-TX
preservation, geometry waits, and failure injection across TX/RX output
transactions. A failed writing phase cannot replay itself. Python tests cover
cold-prefix handoff before any HDMI wait and service restart preserving a loaded
device while input is absent. A fresh physical unplugged boot and later cable
connection remain a separate hardware test.

### Receiver timing acquisition correction (0.43.1, 2026-09-24)

After a user-reported fresh boot with a PS5 and HDCP disabled, 0.43.0 had
registered video/audio but stopped at HDMI phase 16 with `hdmi_error=-34`
(`ERANGE`). OBS timed out waiting for frames and ALSA preparation returned
`ENOLINK`. The old status did not expose the inner receiver phase, so the exact
failed geometry or clock sample is unknown. A later receiver snapshot and the
unchanged guarded output operation both succeeded at 1920×1080.

0.43.1 retries inconsistent/invalid timing snapshots after verified bank
restoration, and out-of-range clock measurements after all five latch cycles
complete with readback verification. Output programming, transport failures,
unsupported formats and invalid configured reference clocks remain fatal.
The supported-mode and pixel-clock bounds are unchanged. New status fields
expose the inner receiver phase and measurement results for future diagnosis.

Protocol tests cover invalid geometry and a quantized clock reading outside
the limit, each followed by successful acquisition; they also check that missing
restoration or bank verification, partial output programming, invalid reference
clocks and unsupported formats cannot use this retry path. Existing transfer
failure injection still passes, as do all 32 Python tests and the kernel build.

On CachyOS kernel 7.2.6-1-cachyos, after recovering the receiver output, 0.43.1
captured 120 frames at 59.94 fps concurrently with three seconds of stereo
48 kHz PCM containing nonzero samples. Video and audio buffer guards passed.
OBS reopened the existing scene with 1080p59.94 video and stereo 48 kHz audio;
saved solid-green lighting was restored. This is a recovery test on the current
boot, not validation of another fresh boot with the fix installed.

### Test commands

`tools/test.sh` runs the C protocol harnesses with AddressSanitizer and
UndefinedBehaviorSanitizer, Python unit tests and shell syntax checks. The fake
I/O harnesses exercise failure at transfer boundaries, stale completion flags,
readback mismatches, timeouts, cleanup, bank/address guards, DMA-related bounds
and original LED-program generation. They do not simulate an entire HDMI source
or establish analog calibration quality.

Use `tools/build.sh` with matching headers to compile the kernel module. CI
compiles against distribution headers and runs these userspace tests. It has no
PCI card attached. Do not describe compilation or period interrupts as proof of
working video/audio content.

## Installation scripts

The install/uninstall tests run in a temporary filesystem with simulated package,
service and privilege boundaries. They cover build failure before helper activation,
installation order, absent login units, repeatable removal, busy-module refusal,
WirePlumber restoration on failure, settings retention, symlink targets and
preservation of recordings/backups. They do not uninstall the working test PC or
claim a fresh installation on every supported package manager.

## Experimental high-rate HDMI OUT (0.44.0, 2026-09-24)

Test platform: CachyOS, kernel `7.2.6-1-cachyos`, the same GC573 and PS5 with
HDCP disabled. The external display supplied 512 bytes of checksum-valid EDID.
Filtering retained 1440p120/144 detailed timings and CTA 4K60, limited to RGB8
SDR and at most 600 MHz. DisplayID-only/HDMI 2.1 modes were excluded.

Live source-facing SRAM programming verified all 254 data bytes. The two EDID
checksum slots read zero on this board; verified receiver C9/CA registers supply
the source-visible checksums. The PS5 changed to 3840×2160. TX2 output setup
completed, downstream SCDC TMDS_CONFIG read back 3, scrambler status read 1,
and sink status 0x4f reported clock detection and all three channel locks.
These observations prove negotiation/link status, not physical picture quality,
HDMI audio, numerical latency or game refresh. The clock estimate was roughly
545–549 MHz / 55 Hz instead of nominal 594 MHz / 60 Hz; its accuracy is unresolved.
1440p120 gameplay and 1080p240 have not been exercised.

A high-to-low switch required the checked splitter RX/TX startup sequence after
restoring the original source EDID. The existing internal receiver HPD GPIO bit
selects whether to preserve its prepared input or perform cold input setup.
The corrected mode switch reached HDMI phase 19, 1920×1080 at 59.944 fps, and
captured 60 V4L2 frames. A subsequent three-second stereo 48 kHz recording
contained nonzero PCM samples (peak 1444/32768). The final kernel build and full
C sanitizer suite passed, along with 38 Python tests and shell syntax checks.
No full PC reboot or physical unplug test was performed.

New fake-I/O tests cover external DDC reads, checksum/length rejection, EDID
filtering, SCDC, approximately 498/595 MHz output paths, source SRAM readback,
mode observation and return-profile restoration. Transfer failures stop at the
injected transaction; an unknown controller state prevents restoration writes.
The existing capture/HDMI tests remain in the full sanitizer suite. Python tests
cover both receiver handoff paths and refusal to reset a failed passthrough phase.

## Console preview and capture recovery (2026-09-24)

On the same CachyOS/7.2.6 GC573 and PS5, passthrough mode had stopped at a
phase-7 unsupported-format preflight after a console mode change. The return
to capture now accepts that specific rejection with bank-zero/read-completion
evidence, while still refusing transport errors and incomplete phase-8 writes.
The checked splitter restart and deferred capture setup reached phase 19 with
1920×1080 at 59.944 fps. Capture is again the saved startup preference.

The GTK4/GStreamer preview smoke run received 465 frames and rendered 460 in
approximately eight seconds. A live window screenshot showed the PS5 Settings
screen with correct geometry; video and stereo playback ran concurrently.
During playback, ALSA reported 815 periods, 1,393,104 nonzero bytes, no guard
errors and prepare_error=0. This verifies local decoded frames and nonzero
audio through the capture/playback pipeline, not a remote Discord viewer,
measured end-to-end latency or content-relative A/V synchronization.

Python tests cover rejecting passthrough/unsupported input for the preview,
bounded latest-frame buffering, matching audio to the same PCI card, cleanup
of both launchers, and the narrowly permitted mode-rejection recovery. The app
requires no root privileges, encoder or OBS process. It does not start a call
or broadcast. Simultaneous 1440p120 HDMI OUT plus 1080p60 capture remains pending.

## Scaled capture implementation (0.45.0, 2026-09-24)

On the same CachyOS 7.2.6-1-cachyos machine, with PS5 RGB8 1920×1080 at approximately
59.944 Hz and PCIe Gen2 x2:

- FPGA scaling delivered 120 1280×720 BGR24 frames at 59.94 fps. All 2443
  coefficient/phase/geometry/reset readbacks passed. DMA guard remained intact.
  The saved frame showed the full PS5 home screen with correct proportions/colors.
- Pacing before DMA delivered 300 scaled frames at approximately 30.00 fps while
  HDMI input remained 1080p59.94. Concurrent splitter monitoring completed without
  a capture error after accounting for the driver's owned video/audio IRQ bits.
- Combined-mode preview test received 819 frames, rendered 714, and reached the
  audio pipeline's playing state. This pipeline state alone is not audio proof.
- A separate direct ALSA recording contained 288000 interleaved samples over
  three seconds: 287539 nonzero, absolute peak 1014, no audio guard error.
- The actual monitor EDID filtered to 1440p59.95 and 1440p119.998 detailed timings;
  no 4K/144 Hz timing remained in the combined source profile.

**Not tested:** actual 1440p120 input → 1080p60 capture, high-rate physical
HDMI OUT picture/audio, 60↔120 gameplay transitions, source/display unplug,
combined-mode cold boot, numerical passthrough latency, or Discord viewer audio.
The PS5 remained at 1080p59.94 during these tests. Unit tests of dual-TTL writes
and phase tables do not establish real high-rate capture correctness.

The final 0.45.0 module also passed a switch from combined mode back to the
conservative profile (60 captured frames), then back to combined mode. The
live preview again showed the PS5 at 1080p59.94 with stereo audio and zero
capture/audio guard errors. Its status bar now distinguishes HDMI input from
preview output. Builds passed for all three documented CachyOS kernels; the
sanitizer suite and 46 Python tests passed. Boot mode is saved as `scaled` on
the test machine, but an actual reboot of this release has not been performed.


## 1440p scaled capture and negotiation recovery (0.45.1, 2026-09-24)

Test platform: PS5 with HDCP disabled, GC573 FPGA `20201015`, board `57300102`,
CachyOS `7.2.6-1-cachyos`, PCIe Gen2 x2. The live source was RGB8
2560×1440 at approximately 59.944 Hz. The user separately confirmed that
1440p physical passthrough worked; that report did not specify 120 Hz.

- Reproduced a stopped scaled worker (`-95`) after a source format transition.
  Added preflight validation and retries for restored, unsupported snapshots;
  transport failures and incomplete output writes remain stopped.
- Configured FPGA dual-pixel DDR packing. The horizontal timing counter read
  640 interface periods, corresponding to 2560 pixels. Hardware-scaled
  1920×1080 BGR24 showed the full PS5 home screen with correct colors.
- Captured 300 frames at 59.94 fps with all 2443 scaler readbacks verified,
  capture_error=0 and intact DMA guards. Stream close/reopen retained packing.
  The first single-frame test was blank; a subsequent capture after discarding
  30 startup frames and the live preview showed valid content.
- With the external monitor reporting RxSense inactive, internal capture
  continued while external setup waited. No physical monitor reconnect or
  simultaneous passthrough latency measurement was performed in that state.
- Final preview smoke test received 609 frames and rendered 608 in about ten
  seconds. Restored live Preview and Control windows showed HDMI 2560×1440 at
  59.94 Hz and 1920×1080 capture; saved RGB settings and WirePlumber were restored.
- Direct ALSA capture delivered 300 periods with no guard or prepare error.
  Receiver diagnostics indicated 48 kHz PCM. The initial recording and preview
  buffers were silent. During the subsequent live preview, audio became nonzero
  without another driver change: 10,452,871 nonzero bytes over 17,531 periods,
  with prepare_error=0 and zero audio guard errors. This establishes non-silent
  captured samples at 1440p59.94, not channel order or content A/V synchronization.
- All C sanitizer tests, 47 Python tests, and shell syntax checks passed.
  `W=1` module builds passed for 7.2.6-1-cachyos, cached 7.2.3-1-cachyos,
  and 6.18.52-1-cachyos-lts. Only 7.2.6 was loaded on hardware for this release.

Still pending: actual 1440p120 gameplay with 1080p60 capture, 60↔120 transitions,
audio at 120 Hz input, combined-mode cold boot and physical source/sink
hotplug. The measured 59.94 Hz result must not be presented as 120 Hz validation.

The code commit's GitHub Actions checks also passed, including an Ubuntu 24.04
kernel-header build. The restored live preview exceeded 9,500 error-free frames
while receiving nonzero PCM. Physical 120 Hz gameplay remains untested.


## PS5 120 Hz transition failure and continuation (0.45.2, 2026-09-24)

The user reported that 120 Hz did not work. The stopped 0.45.1 worker had
external_error=-67, external_video_phase=8 and last_reg=0x03 with a completed
I2C read. The internal receiver had previously measured 494080–496614 kHz,
but the final snapshot had no FPGA input. A bounded diagnostic read after both
workers/DMA stopped found TX1 C0=0x77/83=0x08 (high ratio), TX2 C0=0x31/83=0x00
(low ratio), and a capture receiver without SCDT. Both transmitters eventually
reported 0x9f, after the worker had already stopped. These observations identify
an unrecovered handshake failure, not successful 120 Hz capture.

0.45.2 records a pending link check only after output setup completes. Subsequent
polls remeasure the clock and validate the input before completing output enable;
a changed clock or TMDS ratio discards the pending configuration and starts a
fresh checked setup. Transport errors never enter this continuation. Tests cover
both transmitters, a late lock, RxSense loss, 120→60 clock change, and every
continuation transfer failure. All C sanitizer and 47 Python tests passed.

The patched module loaded on CachyOS 7.2.6-1-cachyos. Both transmitters reported
ready, external and combined errors cleared, and 1440p59.94 → 1080p59.94 live
preview recovered with intact guards over 3,500 frames. Module builds also
passed for 7.2.3-1-cachyos and 6.18.52-1-cachyos-lts. A live 120 Hz source has
not yet been supplied after this patch; successful 120 Hz capture and transitions
remain unverified.


## Missed I2C busy interval (0.45.3, 2026-09-24)

While observing 0.45.2 at 1440p59.94, a read-only link snapshot stopped with
-ETIMEDOUT at address 0x38, register 0x55. The saved transaction had initial,
prepared, final and cleanup status 4, completion_armed=0 and zero FIFO bytes.
A private read-only diagnostic, with capture/audio stopped, confirmed the
controller still reported 4. This is consistent with the CPU missing the short
busy interval; no evidence of a 120 Hz transition accompanied this failure.

0.45.3 protects START plus the first status sample against CPU interruption.
Regression tests model busy visible only in that first sample, stale DONE,
invalid BAR values, a true busy timeout, and immediate completion after cleared
preparation. All C sanitizer tests, 47 Python tests and shell checks passed.
W=1 builds passed for 7.2.6-1-cachyos, cached 7.2.3-1-cachyos and
6.18.52-1-cachyos-lts. The loaded 7.2.6 build restored HDMI, live scaled capture,
nonzero audio and saved RGB. The early sample now observes 0 followed by DONE 4.
Actual 1440p120 input and 60↔120 transitions still need a live retest.
