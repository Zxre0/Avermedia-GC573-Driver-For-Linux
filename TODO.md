# GC573 driver work

Current driver: 0.42.0. Hardware validation and implementation are tracked
separately; an unchecked item has not been demonstrated complete.

- [x] Deliver live native OBS video: 1920×1080 at 60 fps verified.
- [x] Implement RGB control: original generated rainbow, solid color, off,
  brightness, reset restoration, and read-only keepalive. Controls pass while
  video streams, and saved app settings are restored at login.
- [x] Confirm physical RGB operation. The user reports that RGB works great
  on 0.42.0 (2026-09-22).
- [x] Install automatic login startup and portable setup scripts. Prepared-state
  startup preserves an open working device; builds select the kernel compiler.
- [ ] Verify startup from a full power cycle. No full reboot performed yet.
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
- [ ] Implement and validate the advertised 1080p240, 1440p144 and 4K60 modes.
  Current capture uses RGB24 and a <=150 MHz single-TTL path. The host currently
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
