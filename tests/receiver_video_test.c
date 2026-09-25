// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"
#include "receiver_fake.h"

static struct fake prepared(void)
{
	struct fake f = baseline();

	f.receiver[0][0x13] = 0xbf;
	f.receiver[0][0x19] = 0xbe;
	f.receiver[0][0x98] = 0;
	f.receiver[0][0x9b] = 0x98;
	f.receiver[0][0x9c] = 8;
	f.receiver[0][0x9d] = 0x80;
	f.receiver[0][0x9e] = 7;
	f.receiver[0][0xa2] = 0x65;
	f.receiver[0][0xa3] = 4;
	f.receiver[0][0xa4] = 0x38;
	f.receiver[0][0xa5] = 4;
	f.receiver[0][0x99] = 67;
	f.receiver[0][0x9a] = 0x80;
	f.receiver[0][0xcf] = 0x80;
	f.receiver[2][0x15] = 2;
	f.receiver[1][0xc0] = 0x42;
	f.receiver[1][0xfd] = 193;
	return f;
}

static int run(struct fake *f, struct gc573_receiver_video_result *r, unsigned int enable)
{
	struct gc573_signal_result signal;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	unsigned int i;
	int ret = gc573_receiver_video(&io, &signal, r, enable);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2 && !f->hpd_writes);
	for (i = 0; i < f->writes; i++)
		assert(enable || f->log[i].reg == 15);
	if (!ret)
		assert(!f->bank && r->bank_verified);
	return ret;
}

int main(void)
{
	struct gc573_receiver_video_result r;
	struct fake f = prepared(), dual;
	unsigned int i, total;

	assert(!run(&f, &r, 0) && r.complete);
	assert(r.width == 1920 && r.height == 1080 && r.htotal == 2200 && r.vtotal == 1125);
	assert(!r.interlaced && r.timing_samples == 2 && r.writes_started == 3);
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r, 0) == -ETIMEDOUT && !r.complete && f.starts == i);
	}
	for (i = 1; i <= 3; i++) {
		f = prepared();
		f.wrong_bank_at = i;
		assert(run(&f, &r, 0) == -EIO && !r.complete && !r.bank_verified);
	}
	f = prepared();
	f.receiver[0][0x19] = 0x20;
	assert(run(&f, &r, 0) == -ENOLINK && !f.writes);
	f = prepared();
	f.receiver[0][0xa4] = f.receiver[0][0xa5] = 0;
	assert(run(&f, &r, 0) == -ERANGE && !r.complete && !f.bank);
	printf("PASS: receiver timing, bank-only writes, %u transfer failures and bank verification\n", total);
	f = prepared();
	assert(!run(&f, &r, 1) && r.complete && r.output_enabled);
	assert(!f.bank && f.receiver[1][0xc5] == 0x18);
	assert(r.pixel_min_khz == 147486 && r.pixel_max_khz == 148243);
	f = prepared();
	f.receiver[0][0x9d] = 0; f.receiver[0][0x9e] = 5;
	f.receiver[0][0xa4] = 0xd0; f.receiver[0][0xa5] = 2;
	f.receiver[0][0x99] = 133;
	assert(!run(&f, &r, 1) && r.output_enabled && r.width == 1280 && r.height == 720);
	assert(r.pixel_min_khz > 74000 && r.pixel_max_khz < 75000);
	f = prepared(); f.receiver[0][0x9d] = 1;
	assert(run(&f, &r, 1) == -EOPNOTSUPP && !r.output_enabled);
	f = prepared();
	assert(!run(&f, &r, 1));
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r, 1) == -ETIMEDOUT && !r.complete && !r.output_enabled);
	}
	printf("PASS: receiver RGB8 single TTL output, %u transfer failures\n", total);
	f = prepared();
	f.receiver[0][0x9b] = 0xa0; f.receiver[0][0x9c] = 0x0a;
	f.receiver[0][0x9d] = 0; f.receiver[0][0x9e] = 10;
	f.receiver[0][0xa2] = 0xc9; f.receiver[0][0xa3] = 5;
	f.receiver[0][0xa4] = 0xa0; f.receiver[0][0xa5] = 5;
	f.receiver[0][0x99] = 41;
	dual = f;
	assert(!run(&f, &r, 2) && r.output_enabled && r.width == 2560 && r.height == 1440);
	assert((f.receiver[1][0xc0] & 1) && (f.receiver[1][0xc1] & 2));
	assert((f.receiver[1][0xbd] & 0x30) == 0x10 && f.receiver[1][0xc4] == 0x20);
	assert((f.receiver[1][0xc5] & 1) && !f.receiver[1][0xc6]);
	assert(r.pixel_min_khz > 240000 && r.pixel_max_khz < 243000);
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = dual;
		f.fail_at = i;
		assert(run(&f, &r, 2) == -ETIMEDOUT && !r.complete && !r.output_enabled);
		assert(f.starts == i);
	}
	puts("PASS: 1440p dual TTL clock, lane selection, output controls and each transport failure");

	return 0;
}
