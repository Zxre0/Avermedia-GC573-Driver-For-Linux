// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"
#include "receiver_fake.h"

static struct fake prepared(void)
{
	struct fake f = baseline();

	f.clock_mode = 1;
	f.receiver[0][0x21] = 0x44;
	f.receiver[0][0x22] = 0x10;
	f.receiver[0][0x23] = f.receiver[0][0x2b] = 0xa0;
	f.receiver[0][0x2a] = f.receiver[0][0x32] = 1;
	/* Actual board data: 3864110 / 100 -> 38641 kHz. */
	f.clock_data[1][0] = f.clock_data[1][1] = 255;
	f.clock_data[2][0] = 0x2e;
	f.clock_data[2][1] = 0xf6;
	f.clock_data[3][0] = 0x3a;
	f.clock_data[3][1] = 0xc0;
	return f;
}

static int run(struct fake *f, struct gc573_timing_result *r)
{
	struct gc573_signal_result signal;
	struct gc573_clock_result clock;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_receiver_timing(&io, &signal, &clock, r);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->mmio[GC573_GPIO / 4] == 0x1f958);
	if (r->complete) {
		assert(r->steps_verified == 17 && r->writes_started == 17);
		assert(r->bank_verified && !r->bank && !f->bank);
		assert(clock.khz == 38641 && clock.clock_valid && clock.cleanup_complete);
		assert(f->receiver[1][0xc5] == 255 && f->receiver[1][0xc6] == 255);
	}
	if (r->post_attempted && !r->post_error)
		assert(signal.valid == 0xfff && !signal.bank);
	return ret;
}

static unsigned int high_product(unsigned int x, unsigned int multiplier)
{
	return ((uint64_t)x * multiplier) >> 32;
}

/* Independent arithmetic transcription of the x64 instructions, retaining
 * multiply/shift sequences, remainder terms, and saturation branches.
 */
static void arithmetic(void)
{
	struct gc573_timing_values v;
	unsigned int x, half, adjusted, a, b, r, value;

	for (x = 28500; x <= 47500; x++) {
		assert(gc573_timing_compute(x, &v) == 0);
		half = x >> 1;
		adjusted = half + (high_product(half, 0xcccccccd) >> 3);
		assert(v.half_khz == half && v.adjusted_khz == adjusted);
		a = high_product(adjusted, 0x10624dd3) >> 6;
		assert(v.reg91 == (a & 0x3f));
		b = (adjusted - a * 1000) << 8;
		assert(v.reg92 == (high_product(b, 0x10624dd3) >> 6));
		assert(v.regfd == (half >= 0x6400 ? 255 :
			(high_product(half, 0x51eb851f) >> 5)));
		a = high_product(x, 0xa41a41a5);
		b = (((x - a) >> 1) + a) >> 8;
		assert(v.reg45 == (b > 255 ? 255 : b));
		a = high_product(b, 0xcccccccd) >> 2;
		assert(v.reg44 == (a > 255 ? 255 : a));
		a = high_product(x, 0xe1fc780f) >> 11;
		b = high_product(x, 0x3159721f) >> 10;
		r = a * 100 - (high_product(a * 100, 0x51eb851f) >> 5) * 100;
		value = a + ((high_product(r, 0x51eb851f) >> 4) << 6);
		assert(v.reg46 == (value > 255 ? 255 : value));
		r = b * 100 - (high_product(b * 100, 0x51eb851f) >> 5) * 100;
		value = b + ((high_product(r, 0x51eb851f) >> 3) << 6);
		assert(v.reg47 == (value > 255 ? 255 : value));
	}
	assert(gc573_timing_compute(28499, &v) == -ERANGE && !v.half_khz);
	assert(gc573_timing_compute(47501, &v) == -ERANGE && !v.half_khz);
	assert(gc573_timing_compute(0, &v) == -ERANGE);
	assert(gc573_timing_compute(UINT32_MAX, &v) == -ERANGE);
}

int main(void)
{
	/* Expected complete B1 write trace after the clock read, seeded with 0x92
	 * in unrelated bits. Preserve register 0x91[7:6], 0xaa[7:5], 0xfe[6].
	 */
	static const unsigned int trace[][3] = {
		{ 0, 15, 3 }, { 3, 0xaa, 0x8c }, { 3, 15, 0 },
		{ 0, 0x91, 0x95 }, { 0, 0x92, 0x40 }, { 0, 15, 1 },
		{ 1, 0xfd, 0xc1 }, { 1, 0xfe, 0x92 }, { 1, 0xfe, 0x9c },
		{ 1, 0xfe, 0x9c }, { 1, 0xfe, 0x9c }, { 1, 15, 0 },
		{ 0, 0x45, 0x7b }, { 0, 0x44, 0x18 }, { 0, 0x46, 0x10 },
		{ 0, 0x47, 7 }, { 0, 15, 0 },
	};
	struct gc573_timing_result r;
	struct fake f = prepared();
	unsigned int i, total;

	arithmetic();
	assert(run(&f, &r) == 0 && r.complete && r.phase == 2);
	assert(f.writes == 46 && f.starts == 159);
	for (i = 0; i < 17; i++) {
		assert(f.log[i + 29].bank == trace[i][0]);
		assert(f.log[i + 29].reg == trace[i][1]);
		assert(f.log[i + 29].value == trace[i][2]);
	}
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && f.starts == i);
	}
	for (i = 0; i < 17; i++) {
		f = prepared();
		if (trace[i][1] == 15)
			f.wrong_bank_at = 30 + i;
		else
			f.wrong_value_at = 30 + i;
		assert(run(&f, &r) == -EIO && f.writes == 30 + i);
		assert(r.steps_verified == i && !r.complete);
	}
	f = prepared();
	f.receiver[0][0x91] = 0xff;
	f.receiver[3][0xaa] = 0xff;
	f.receiver[1][0xfe] = 0xff;
	assert(run(&f, &r) == 0);
	assert(f.receiver[0][0x91] == 0xd5 && f.receiver[3][0xaa] == 0xec);
	assert(f.receiver[1][0xfe] == 0xdc);
	f = prepared();
	f.wrong_bank_at = 30;
	assert(run(&f, &r) == -EIO && f.writes == 30 && !r.bank_verified);
	f = prepared();
	f.wrong_value_at = 31;
	assert(run(&f, &r) == -EIO && f.writes == 31 && r.steps_verified == 1);
	f = prepared();
	f.slow_at = 97;
	assert(run(&f, &r) == -ETIMEDOUT && !r.writes_started && f.writes == 29);
	f = prepared();
	memset(f.clock_data[2], 0, 2);
	memset(f.clock_data[3], 0, 2);
	assert(run(&f, &r) == -ERANGE && !r.writes_started && r.clock_error == -ERANGE);
	f = prepared();
	f.receiver[0][4] = 0xb0;
	assert(run(&f, &r) == -ENODEV && !f.writes);
	puts("PASS: timing arithmetic across all 19001 accepted rates, B1 write trace,");
	puts("      159 failure positions, readback/bank/budget guards, invalid-clock gate");
	return 0;
}
