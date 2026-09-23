// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"
#include "receiver_fake.h"

static unsigned char resident[256];

static struct fake prepared(void)
{
	struct fake f = baseline();

	f.input_mode = f.clock_mode = f.edid_mode = 1;
	f.lock_after = 999;
	f.receiver[0][0x21] = 0x44;
	f.receiver[0][0x22] = 0x10;
	f.receiver[0][0x23] = f.receiver[0][0x2b] = 0xa0;
	f.receiver[0][0x2a] = f.receiver[0][0x32] = 1;
	f.receiver[0][0xc5] = f.receiver[4][0xc5] = 3;
	f.clock_data[1][0] = f.clock_data[1][1] = 255;
	f.clock_data[2][0] = 0x2e;
	f.clock_data[2][1] = 0xf6;
	f.clock_data[3][0] = 0x3a;
	f.clock_data[3][1] = 0xc0;
	memcpy(f.edid, resident, 256);
	return f;
}

static int run(struct fake *f, struct gc573_input_result *r)
{
	struct gc573_signal_result signal;
	struct gc573_clock_result clock;
	struct gc573_timing_result timing;
	struct gc573_edid_result edid;
	struct gc573_ddc_result ddc;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_receiver_input(&io, &signal, &clock, &timing, &edid, &ddc, r);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert((f->mmio[GC573_GPIO / 4] & ~4U) == 0x1f958);
	if (r->hpd_verified) {
		assert(ddc.complete && r->setup_complete && f->hpd_writes == 1);
		assert(r->steps_completed == 48 && r->writes_started == 48);
		assert(r->gpio_before == 0x1f958 && r->gpio_after == 0x1f95c);
		assert(!f->bank && !(f->receiver[0][0x35] & 1));
		assert(f->receiver[0][0x2d] == 255 && f->receiver[0][0x32] == 0x3e);
		assert(f->receiver[3][0xa8] & 8);
		assert(!(f->receiver[7][0xa8] & 8));
		assert(f->receiver[0][0x23] == 0xa0 && f->receiver[3][0x27] == 0x9f);
		assert(f->receiver[1][0xc5] == 255 && f->receiver[1][0xc6] == 255);
		assert(f->receiver[0][0xc9] == 0xe7 && f->receiver[0][0xca] == 0x79);
	}
	if (r->post_attempted && !r->post_error)
		assert(signal.valid == 0xfff && !signal.bank);
	return ret;
}

int main(void)
{
	struct gc573_input_result r;
	struct fake f;
	FILE *fixture = fopen("tests/receiver-edid.hex", "r");
	unsigned int i, byte, total;

	assert(fixture);
	for (i = 0; i < 256; i++) {
		assert(fscanf(fixture, "%x", &byte) == 1 && byte <= 255);
		resident[i] = byte;
	}
	fclose(fixture);
	f = prepared();
	assert(run(&f, &r) == 0 && r.hpd_verified && !r.lock_seen && r.samples == 20);
	assert(f.starts == 543 && f.writes == 124 && f.hpd_writes == 1);
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && f.starts == i);
		assert(f.hpd_writes == (i > 491));
	}
	f = prepared();
	f.lock_after = 3;
	assert(run(&f, &r) == 0 && r.lock_seen && r.samples == 3 && f.starts == 509);
	assert(r.poll[0][0] == 0x41 && r.poll[2][0] == 0x49 && r.poll[2][1] == 0xa0);
	f = prepared();
	f.hpd_drop = 1;
	assert(run(&f, &r) == -EIO && !r.hpd_verified && f.hpd_writes == 1 && !r.samples);
	f = prepared();
	f.wrong_bank_at = 77;
	assert(run(&f, &r) == -EIO && !r.bank_verified && !f.hpd_writes);
	f = prepared();
	f.wrong_value_at = 83;
	assert(run(&f, &r) == -EIO && !f.hpd_writes);
	f = prepared();
	f.slow_at = 346;
	assert(run(&f, &r) == -ETIMEDOUT && !r.writes_started && !f.hpd_writes);
	f = prepared();
	f.receiver[0][0x13] = 0;
	assert(run(&f, &r) == -ENOLINK && !r.writes_started && !f.hpd_writes);
	f = prepared();
	f.mmio[GC573_GPIO / 4] |= 4;
	assert(run(&f, &r) == -EBUSY && !f.starts && !f.hpd_writes);
	f = prepared();
	f.ddc_reset_selfclear = 1;
	assert(run(&f, &r) == 0 && r.hpd_verified);
	puts("PASS: port-zero setup, one masked HPD write, lock/no-lock polling,");
	puts("      543 transfer failure positions, no premature HPD, readback/budget/source gates");
	return 0;
}
