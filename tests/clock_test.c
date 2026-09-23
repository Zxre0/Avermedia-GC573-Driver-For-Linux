// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"
#include "receiver_fake.h"

static struct fake prepared(unsigned int rate, int scaled)
{
	struct fake f = baseline();
	unsigned int raw = scaled ? rate * 100 : rate;

	f.clock_mode = 1;
	f.receiver[0][0x21] = 0x44;
	f.receiver[0][0x22] = 0x10;
	f.receiver[0][0x23] = f.receiver[0][0x2b] = 0xa0;
	f.receiver[0][0x2a] = f.receiver[0][0x32] = 1;
	f.clock_data[0][0] = f.clock_data[0][1] = 255;
	f.clock_data[2][0] = raw & 255;
	f.clock_data[2][1] = (raw >> 8) & 255;
	f.clock_data[3][0] = (raw >> 16) & 255;
	f.clock_data[3][1] = scaled ? 0xc0 : 0;
	return f;
}

static int run(struct fake *f, struct gc573_clock_result *r)
{
	struct gc573_signal_result signal;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_receiver_clock(&io, &signal, r);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->mmio[GC573_GPIO / 4] == 0x1f958);
	if (r->cleanup_complete) {
		assert(f->bank == 0 && f->receiver[0][0xf8] == 0);
		assert(f->receiver[1][0x5f] == 0);
		assert(f->receiver[1][0xc5] == 255 && f->receiver[1][0xc6] == 255);
		assert(f->receiver[0][0x28] == 0x88);
	}
	if (r->post_attempted && !r->post_error)
		assert(signal.valid == 0xfff && signal.bank == 0);
	return ret;
}

int main(void)
{
	struct gc573_clock_result r;
	struct fake f;
	unsigned int i, branch, total, totals = 0;

	for (branch = 0; branch < 2; branch++) {
		f = prepared(38000, branch);
		f.ready_after = branch ? 3 : 0;
		assert(run(&f, &r) == 0 && r.clock_valid && r.khz == 38000);
		assert(r.raw_value == (branch ? 3800000 : 38000));
		assert(r.data_valid == 15 && r.cleanup_complete && r.ready);
		assert(r.recovery_used == branch && r.ready_samples == f.ready_after + 1);
		assert(f.bases[0] == 0 && f.bases[1] == 0);
		assert(f.bases[2] == 4 && f.bases[3] == 4);
		if (branch)
			assert(f.receiver[0][0xcf] & 1);
		total = f.starts;
		totals += total;
		for (i = 1; i <= total; i++) {
			f = prepared(38000, branch);
			f.ready_after = branch ? 3 : 0;
			f.fail_at = i;
			assert(run(&f, &r) == -ETIMEDOUT && f.starts == i);
		}
	}
	f = prepared(38000, 0);
	f.clock_data[0][0] = 0;
	assert(run(&f, &r) == 0 && r.selector_base == 0);
	assert(f.bases[2] == 0 && f.bases[3] == 0);
	for (i = 28499; i <= 28500; i++) {
		f = prepared(i, 0);
		assert(run(&f, &r) == (i == 28500 ? 0 : -ERANGE) && r.cleanup_complete);
	}
	for (i = 47500; i <= 47501; i++) {
		f = prepared(i, 0);
		assert(run(&f, &r) == (i == 47500 ? 0 : -ERANGE) && r.cleanup_complete);
	}
	f = prepared(0, 0);
	assert(run(&f, &r) == -ERANGE && !r.clock_valid && r.khz == 0);
	f = prepared(38000, 0);
	f.ready_after = 999;
	assert(run(&f, &r) == -ETIMEDOUT && !r.ready && r.ready_samples == 51);
	assert(r.cleanup_complete && r.data_valid == 0 && f.requests == 0);
	f = prepared(38000, 0);
	f.wrong_bank_at = 2;
	assert(run(&f, &r) == -EIO && f.writes == 2 && !r.bank_verified);
	f = prepared(38000, 0);
	f.slow_at = 13;
	assert(run(&f, &r) == -ETIMEDOUT && !f.writes);
	f = prepared(38000, 0);
	f.receiver[0][0x32] = 0;
	assert(run(&f, &r) == -EAGAIN && !f.writes);
	f = prepared(38000, 0);
	f.receiver[0][4] = 0xa0;
	assert(run(&f, &r) == -ENODEV && !f.writes);
	printf("PASS: clock selectors/encodings/range, ready/recovery, %u failure positions,\n", totals);
	puts("      timeout cleanup, bank checks, budget, prerequisites, no invented default");
	return 0;
}
