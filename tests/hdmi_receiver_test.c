// SPDX-License-Identifier: GPL-2.0-only
#define main receiver_suite_main
#include "receiver_video_test.c"
#undef main
#include "gc573_hdmi.h"

static unsigned int fpga[3];
static unsigned int hdmi_read(void *ctx, unsigned int offset)
{
	if (offset >= 0x1004 && offset <= 0x100c)
		return fpga[(offset - 0x1004) / 4];
	return read_reg(ctx, offset);
}

static int tick(struct fake *f, struct gc573_hdmi *h)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = hdmi_read, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	return gc573_hdmi_poll(&io, h);
}

int main(void)
{
	struct fake f = prepared();
	struct gc573_hdmi h = { .phase = 6 };
	unsigned int i, count;

	f.receiver[0][0x13] = 0;
	for (i = 0; i < 100; i++)
		assert(!tick(&f, &h) && h.waiting && h.phase == 6 && !f.writes);
	f.receiver[0][0x13] = 1;
	assert(!tick(&f, &h) && h.phase == 7 && !h.waiting && !f.writes);

	h = (struct gc573_hdmi) { .phase = 15 };
	f.receiver[0][0x19] = 0x20;
	assert(!tick(&f, &h) && h.waiting && !f.writes);
	f.receiver[0][0x13] = 0xbf;
	f.receiver[0][0x19] = 0xb0;
	assert(!tick(&f, &h) && h.phase == 16 && !h.waiting);
	assert(!tick(&f, &h) && h.phase == 17 && h.receiver_video.output_enabled);
	assert(!tick(&f, &h) && h.waiting && h.phase == 17 && !h.complete);
	fpga[0] = 1; fpga[1] = 3840; fpga[2] = 2160;
	assert(!tick(&f, &h) && h.waiting && !h.complete);
	fpga[1] = 1920; fpga[2] = 1080;
	assert(!tick(&f, &h) && h.phase == 18 && !h.waiting);

	f = prepared();
	h = (struct gc573_hdmi) { .phase = 16 };
	assert(!tick(&f, &h));
	count = f.starts;
	for (i = 1; i <= count; i++) {
		f = prepared();
		f.fail_at = i;
		h = (struct gc573_hdmi) { .phase = 16 };
		assert(tick(&f, &h) == -ETIMEDOUT && !h.complete);
		assert(tick(&f, &h) == -ETIMEDOUT && f.starts == i);
	}
	puts("PASS: deferred receiver power/lock/geometry waits and output failure replay guards");
	return 0;
}
