// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_audio.h"
#include "receiver_fake.h"

static struct fake prepared(void)
{
	struct fake f = baseline();

	f.receiver[0][0xb1] = 0x81;
	f.receiver[0][0xb2] = 0;
	f.receiver[0][0xb5] = 2;
	f.receiver[0][0xb6] = 0x0b;
	f.receiver[0][0x8c] = 8;
	f.receiver[0][0x81] = 0xe0;
	return f;
}

static int run(struct fake *f, struct gc573_audio_signal *r)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_audio_signal_read(&io, r);

	return ret ? ret : gc573_audio_signal_enable(&io, r);
}

int main(void)
{
	struct fake f = prepared();
	struct gc573_audio_signal r;
	unsigned int total, i;

	assert(!run(&f, &r) && r.valid && !f.bank);
	assert(!(f.receiver[0][0x8c] & 0x18));
	assert(f.receiver[0][0x86] & 1);
	assert(f.receiver[0][0x81] == 0xa0);
	total = f.starts;
	f = prepared(); f.mmio[GC573_IRQ_ENABLE / 4] = 0x22;
	assert(!run(&f, &r) && f.mmio[GC573_IRQ_ENABLE / 4] == 0x22);
	f = prepared(); f.mmio[GC573_IRQ_ENABLE / 4] = GC573_IRQ_I2C;
	assert(run(&f, &r) == -EBUSY && !f.writes);
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT);
	}
	f = prepared(); f.receiver[0][0xb1] = 0;
	assert(run(&f, &r) == -ENOLINK && f.receiver[0][0x8c] == 8);
	f = prepared(); f.receiver[0][0xb2] = 2;
	assert(run(&f, &r) == -EOPNOTSUPP && f.receiver[0][0x8c] == 8);
	f = prepared(); f.receiver[0][0xb5] = 0;
	assert(run(&f, &r) == -EOPNOTSUPP && f.receiver[0][0x8c] == 8);
	f = prepared(); f.receiver[0][0xb6] |= 0x40;
	assert(run(&f, &r) == -EOPNOTSUPP && f.receiver[0][0x8c] == 8);
	printf("PASS: HDMI PCM/rate guards, enable masks, %u transfer failures\n", total);
	return 0;
}
