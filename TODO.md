# GC573 driver work

Current driver: 0.45.0. Hardware validation and implementation are tracked
separately; an unchecked item has not been demonstrated complete.

- [x] Deliver live native OBS video: 1920×1080 at 60 fps verified.
- [x] Implement RGB control: original generated rainbow, solid color, off,
  brightness, reset restoration, and read-only keepalive. Controls pass while
  video streams, and saved app settings are restored at login.
- [x] Confirm physical RGB operation. The user reports that RGB works great
  on 0.42.0 (2026-09-22).
- [x] Install system boot startup with builds as the normal user, saved RGB
  restoration, bounded HDMI-lock waits and checkpoints for temporary retries.
  The boot unit is enabled and has started successfully; the old login unit is disabled.
- [x] Verify automatic loading after reboot with the startup fix. The user
  confirmed it works; delayed-lock/resume/failure cases also pass startup tests.
- [ ] Verify startup after a full power-off/power-on cycle, separately from reboot.
- [x] Register capture/audio devices and RGB before HDMI input is available;
  finish checked HDMI startup in the background without reloading the module.
  Hardware registration with zero FPGA input followed by 1080p60 acquisition passed.
- [ ] Verify a fresh boot with HDMI physically unplugged, then connect the source.
- [x] Retry restored receiver timing measurements during deferred startup instead
  of stopping permanently on a transient range error. PS5 capture recovered;
  simulated invalid geometry/clock followed by valid timing passes.
- [ ] Verify another fresh boot with the 0.43.1 receiver-timing retry fix.
- [x] Implement HDMI audio through dedicated guarded DMA buffers and an ALSA
  stereo 48 kHz S16_LE device. Add GC573 HDMI Audio to the local OBS scene.
  Audio/video run together; either stream can stop/restart independently.
- [x] Verify non-silent stereo HDMI audio reaches an OBS recording. The local
  30-second 18:59:54 test contains audio in both channels, peaks -34.1/-33.7 dBFS,
  no clipped samples, and audio/video track endpoints within 19 ms. This proves
  recorded samples, not content synchronization or channel labeling.
- [ ] Verify left/right channel order, source-relative levels and content A/V
  synchronization with a known reference stimulus.
- [x] Implement queue recovery when the same supported FPGA input disappears
  and returns. DMA stays stopped while the expected input is absent.
- [x] Implement bounded 720p geometry and source-timed 1080p frame intervals,
  dynamic descriptor lengths/clip geometry, and V4L2 source-change events.
- [ ] Validate 720p and additional 1080p rates with matching source signals.
- [x] Confirm signal-loss recovery in the current setup. The user reports it
  works great on 0.42.0 (2026-09-22); the exact loss/reconnect method was not specified.
- [ ] Document separate HDMI cable, source power and resolution-change recovery
  tests, including splitter/receiver relocking.
- [x] Add an experimental HDMI OUT mode with display EDID filtering, up to
  600 MHz RGB8 SDR output and SCDC configuration; keep host DMA disabled.
  Live PS5 3840×2160 negotiation reached monitor clock/channel lock and scrambling.
- [x] Add persistent capture/passthrough selection and checked return to capture.
- [ ] Validate physical 1440p120/144 and 1080p240 passthrough, output audio, and
  nominal 4K60 refresh. Correct the high-rate clock estimate before calling it measured 60 Hz.
- [ ] Implement HDR/deep-color passthrough and validate source/sink hotplug and
  cold startup in passthrough mode.
- [ ] Implement and validate native capture at 1080p240, 1440p144 and 4K60.
  Current host output uses RGB24 at up to 1080p60; higher-rate input uses an
  experimental dual-TTL path. The host currently
  negotiates PCIe Gen 2 x2 (upstream maximum x2); high-rate RGB24 exceeds this
  link's capacity. Lower-bandwidth formats and/or a suitable x4 slot are needed.
- [x] Add TX2 activation and bounded RGB8 output setup alongside internal TX1.
  Hardware changed TX2 status from 0x17 to 0x9f; 38 output readbacks passed.
  Capture registration attempts TX2 setup and preserves an already enabled port.
- [x] Confirm HDMI OUT video after the TX2 fix. The user confirmed working
  physical output on 2026-09-22 with driver 0.42.0 and the current 1080p60 source.
- [x] Confirm acceptable passthrough latency in use. The user reports that
  latency is really good on 0.42.0 (2026-09-22).
- [ ] Verify HDMI OUT audio and measure passthrough latency independently of
  OBS preview. No numerical latency measurement has been recorded.
- [ ] Implement and verify external-output hotplug recovery after driver startup.
- [x] Add GC573 Preview: a shareable GTK4 console window with bounded video
  buffering, stereo audio playback, volume, pause and fullscreen controls.
  Live 1080p59.94 PS5 video and nonzero stereo audio observed locally.
- [ ] Verify a Discord viewer receives both the preview picture and console audio.
- [x] Implement FPGA scaling with original generated six-tap filters, independent
  input/output geometry, and capture pacing before DMA. Real 1080p→720p capture
  and 60→30 fps pacing passed with intact guards and inspected picture.
- [x] Implement experimental combined `scaled` mode: monitor-derived EDID up to
  1440p120, separate TX1/TX2 setup, dual-TTL receiver output, shared I2C locking,
  persistent boot selection, and separate input/capture information in the apps.
- [ ] Validate simultaneous **actual 1440p120 input/passthrough and 1080p60 capture**.
  PS5 was still sending 1080p59.94 during implementation. Scaling register tests
  and lower-rate live capture do not establish high-rate video correctness.
- [ ] Validate combined-mode 60↔120 transitions, physical HDMI OUT picture/audio,
  cold boot and source/display hotplug. Check latency independently of preview.
- [x] Build and install a GTK4 desktop app with RGB controls, live measured
  resolution/frame rate, capture/audio state and disconnected/error feedback.
- [x] Prepare source for GitHub: portable paths and PCI discovery, GPL-2.0-only,
  contributor/protocol/testing documentation, CI, and clean-checkout tests.
  Source repository: https://github.com/Zxre0/Avermedia-GC573-Driver-For-Linux.
  Private captures,
  research binaries and generated files are excluded from the staged source.

The remaining hardware checks require reference playback, controlled
source/cable changes, latency measurements or a full power cycle. Passing registers, compilation and
silent recordings cannot substitute for those checks.
