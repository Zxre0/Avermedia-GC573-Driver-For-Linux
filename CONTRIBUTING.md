# Contributing

This is an experimental, independently implemented driver for the AVerMedia
GC573. Keep hardware changes narrowly scoped and cite the register evidence in
`docs/protocol.md`. Do not add proprietary binaries, vendor source, captures,
EDID serial numbers or personal logs to commits.

Run `tools/test.sh` for userspace protocol tests and `tools/build.sh` with
matching kernel headers. Hardware testing is separate: a passing build or fake
I/O harness is not evidence that HDMI video or sound actually works. Include
kernel version, sanitized PCI IDs, test modes, observed frame/audio rates,
DMA guard status and kernel errors in hardware reports.

Never submit guessed DMA addresses. DMA addresses must come from the kernel
DMA API; validate descriptor lengths, allocate before enabling, and stop/drain
before freeing. Preserve the ability to stop a test and leave unrelated devices
alone. Do not bypass protected HDMI content.

Open a pull request with the problem, resulting behavior, test evidence and
remaining limitations. Hardware fixes should preserve working video, audio and
RGB while applications open/close the device or the HDMI signal changes.
