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

	f.caof = 1;
	f.receiver[0][8] = f.receiver[0][13] = 0;
	f.receiver[0][0x21] = 0x44;
	f.receiver[0][0x22] = 0x10;
	f.receiver[0][0x23] = f.receiver[0][0x2b] = 0xa0;
	return f;
}

static int run(struct fake *f, struct gc573_cal_result *result)
{
	struct gc573_signal_result signal;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_receiver_calibrate(&io, &signal, result);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->mmio[GC573_GPIO / 4] == 0x1f958);
	if (result->post_attempted && !result->post_error)
		assert(signal.valid == 0xfff && signal.bank == 0);
	return ret;
}

static void check_cleanup(struct fake *f)
{
	unsigned int bank, reg;

	assert(!f->bank && !(f->receiver[0][0x29] & 1));
	assert(!(f->receiver[0][0x24] & 4) && !(f->receiver[0][0x2c] & 4));
	assert(f->receiver[0][0x3c] & 0x10);
	assert(f->receiver[4][0x3c] & 0x10);
	assert(!(f->receiver[0][8] & 0x30) && !(f->receiver[0][13] & 0x30));
	for (bank = 3; bank <= 7; bank += 4) {
		assert(!(f->receiver[bank][0x3a] & 0x80));
		for (reg = 0xa0; reg <= 0xa2; reg++)
			assert(f->receiver[bank][reg] == 0x12); /* Other bits preserved. */
	}
}

int main(void)
{
	struct fake f = prepared();
	struct gc573_cal_result r;
	unsigned int total, i;

	assert(run(&f, &r) == 0);
	assert(r.preflight_complete && r.completion_seen && r.cleanup_complete);
	assert(r.post_attempted && r.flags_valid == 15 && r.results_valid == 3);
	assert(r.flags[0] == 0x10 && r.flags[1] == 0x20);
	assert(r.writes_started == 65 && f.writes == 65 && r.steps == 67);
	assert(r.poll_samples == 1 && f.sleeps == 2);
	assert(r.values[0][0] == 0x92 && r.values[1][1] == 0x92);
	check_cleanup(&f);
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && f.starts == i);
	}
	f = prepared();
	f.no_completion = 1;
	assert(run(&f, &r) == -ETIMEDOUT);
	assert(r.poll_samples == 50 && !r.completion_seen && r.cleanup_complete);
	assert(r.poll_error == -ETIMEDOUT && r.post_attempted && !r.post_error);
	assert(f.writes == 65); /* No repeated trigger/reset sequence. */
	check_cleanup(&f);
	f = prepared();
	f.no_completion = 2;
	assert(run(&f, &r) == -ETIMEDOUT && !r.completion_seen && r.cleanup_complete);
	assert(r.flags[0] == 0x10 && !r.flags[1]);
	check_cleanup(&f);
	f = prepared();
	f.wrong_bank_at = 1;
	assert(run(&f, &r) == -EIO && f.writes == 1 && !r.bank_verified);
	f = prepared();
	f.slow_at = 15;
	assert(run(&f, &r) == -ETIMEDOUT && !f.writes);
	for (i = 0; i < 2; i++) {
		f = prepared();
		f.receiver[0][i ? 13 : 8] = 0x10;
		assert(run(&f, &r) == -EBUSY && !f.writes);
	}
	f = prepared();
	f.receiver[0][0x22] = 0;
	assert(run(&f, &r) == -EAGAIN && !f.writes);
	f = prepared();
	f.receiver[0][4] = 0xa0;
	assert(run(&f, &r) == -ENODEV && !f.writes);
	printf("PASS: CAOF setup/teardown, masks, both-port completion, %u failure points,\n", total);
	puts("      bounded no-completion teardown, stale flags, bank mismatch, deadline, gates");
	return 0;
}
