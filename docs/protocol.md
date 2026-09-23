# Implemented protocol

This is an original implementation derived from observations of the target
GC573 and analysis of AVerMedia's official drivers. Proprietary binaries and
private captures are excluded from the source distribution. No community driver
is installed, linked, or used as a source dependency.

Target: PCI `1461:0054`, subsystem `1461:5730`, FPGA ID `20201015`, board ID
`57300102`, BAR0 size `0x80000`. Other revisions are rejected by capture setup.
All addresses below are byte offsets. Driver modules contain bounded protocol
operations, with a userspace fake-I/O harness for receiver/splitter transactions.

## HDMI path

The board has a separate capture receiver at I²C address `0x48` (identity `54 49 05 68`)
and a splitter control endpoint at `0x2c` (`54 49 63 66`). The splitter's receiver
maps to `0x38` (`54 49 64 66`). Splitter transmitter 1 feeds that capture receiver.
The implemented path is RGB 8-bit, unencrypted 1080p60, with no scaler or color
conversion. Initialization includes GPIO sequencing, reference-clock measurements,
clock-dependent timers, EDID/DDC configuration, calibration and transmitter output.

The block I²C engine lives at `0x180` onward; `0x1a4` is its status. Completion
is armed and observed per transaction rather than accepting stale status. Tests
cover timeout, partial transfer, bank restoration and readback failures. The old
byte-register layout at `0x108` is not this board's active I²C engine.

## External output startup (0.42.0)

TX2 at I²C `0x36` reported a newly connected sink (`reg03=0x17`) while TX1
remained `0x9f`. The official driver's port-indexed activation, measurement,
analog setup and RGB8 output sequence applies to both ports. Our implementation
now accepts ports 1 and 2 explicitly, with the same bounded register allowlists.
TX2 activation sets only its bit in shared control `0x08`; transmitter register
writes target `0x36`. TX1 is neither reset nor reconfigured.

The hardware attempt completed six activation writes followed by 59 output
writes with 38 readback checks. TX2 then reported `0x9f`, with `C1=0x08` after
clearing the output-disable bit. These are electrical/control observations;
the user subsequently confirmed physical HDMI OUT video on 0.42.0. Output
audio and numerical latency remain separate validation steps; the user reports
very good passthrough latency in use.

Before registering capture, a one-shot helper checks the sink and TX2 controls.
It preserves an active output without chip writes, initializes the known cold
state, or completes a previously activated state. Unknown states stop the
passthrough attempt; missing sinks do not prevent V4L2 registration. No worker
polls or reconfigures TX2 during capture, avoiding unsynchronized access to the
I²C engine shared with ALSA preparation. Output hotplug remains pending.

## Video DMA

The official GC573 Windows driver's descriptor layout and target hardware tests
establish 16-byte descriptors: low/high DMA address, length in DWORDs, control
`0x80008000`. Descriptors cover at most 4096 bytes each, using coherent 64 KiB
allocations. Only DMA API addresses are submitted. The last allocation has an
untouched guard region checked after each frame.

Four descriptor slots begin at `0x308`, 12 bytes apart. `0x304` arms an individual
slot. Completion comes from interrupt-status `0x10` bit 1 and slot status
`0x300 & 7`. `0x1000` selects RGB24 and enables video. Input geometry is at
`0x1008/0x100c`; `0x1010` is the frame period in 100 MHz clock ticks. Video reset
uses `0x0c` and completion bit 3. Global `0x08` bit 1 is **audio DMA**.

The driver exports BGR24 with bounded 1920×1080 and 1280×720 geometry.
Descriptor count, queue payload and clip dimensions follow the selected mode.
1080p60 is hardware verified; additional modes still need source testing.
Frame intervals report source timing, and the app reads the measured input rate.
Stopping video preserves an active audio engine. The worker waits without DMA
when its expected FPGA input is lost and resumes after that input returns.
The user confirms signal-loss recovery on the current setup; detailed cable,
source-power and mode-change coverage remains pending. Module teardown disables both
engines and drains PCI transactions before releasing DMA allocations; failed
draining retains memory instead of risking DMA into freed allocations.

## Audio DMA

The official target driver's audio initializer configures format at `0x200`,
period length in DWORDs at `0x204`, sample count at `0x21c`, and two DMA addresses
at `0x208–0x214`. This driver implements stereo S16_LE, 48 kHz, 480 frames per
period. Each 1920-byte payload has a guard region inside its owned 4096-byte
allocation. Audio uses global enable `0x08` bit 1, interrupt bit 5 and completion
slot `0x14 & 3`. ALSA receives an independent software ring.

Receiver PCM/rate observations and output enable precede capture. The ALSA
prepare callback rechecks the source format before enabling its DMA engine. DMA and
ALSA period delivery are verified on hardware, including simultaneous video.
Initial samples were zero; a subsequent OBS recording contains non-silent
stereo audio. Content synchronization and channel labeling are still unverified.
Period interrupts alone do not establish working sound.

## Lighting

The LED sequencer divider is `0x800`, control `0x804`; fifteen start/end pairs
begin at `0x808`, and program memory begins at `0x2000`. The implementation
creates its own 30-step RGB palette, and supports solid color and off. It does
not redistribute the vendor's factory animation table.

Video reset restores the blinking-red default on the observed FPGA. The driver
reapplies lighting after reset. An empirical read-only keepalive reads LED control
registers every 250 ms; a two-second read gap previously caused red fallback.
The write-only program memory cannot be verified through readback. Control
persistence has been tested; physical color appearance needs direct observation.

Private V4L2 controls: `0x00982900` mode (rainbow/solid/off), `0x00982901` RGB24
color, `0x00982902` brightness (0–100). They are available during streaming.

## References

Protocol research used the official GC573 Windows package, driver version
2.2802.64.107, and related official AVerMedia CL511 driver objects. These are
research inputs, not runtime dependencies or files for redistribution.

Linux interface references:
- [V4L2 controls](https://www.kernel.org/doc/html/next/driver-api/media/v4l2-controls.html)
- [Writing an ALSA driver](https://www.kernel.org/doc/html/next/sound/kernel-api/writing-an-alsa-driver.html)
- [DMA API](https://www.kernel.org/doc/html/next/core-api/dma-api.html)
