// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"

#include "receiver_fake.h"

static int run(struct fake *f, struct gc573_init_result *result)
{
	struct gc573_signal_result signal;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_receiver_init(&io, &signal, result);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->mmio[GC573_GPIO / 4] == 0x1f958);
	if (!ret)
		assert(signal.valid == 0xfff && signal.bank == 0);
	return ret;
}

int main(void)
{
	struct fake f = baseline();
	struct gc573_init_result result;
	unsigned int i, total;
	const unsigned char reset_regs[] = { 0x0f, 0x22, 0x22, 0x23, 0x2b, 0x24,
		0x22, 0x23, 0x2b, 0x24 };
	const unsigned char reset_values[] = { 0, 8, 0x17, 0x1f, 0x1f, 0xf8,
		0x10, 0xa0, 0xa0, 0 };

	assert(run(&f, &result) == 0);
	assert(result.table_complete && result.steps_completed == 116);
	assert(result.writes_started == 116 && result.bank_verified && f.sleeps == 1);
	assert(result.post_attempted && !result.post_error);
	for (i = 0; i < sizeof(reset_regs); i++) {
		assert(f.log[i].bank == 0 && f.log[i].reg == reset_regs[i]);
		assert(f.log[i].value == reset_values[i]);
	}
	assert(f.receiver[0][0x21] == 0x44); /* Preserve observed bit 2. */
	assert(f.receiver[1][0xc4] == 2); /* Preserve bits outside masks. */
	assert(f.receiver[0][0xce] == 0x82 && f.receiver[4][0xce] == 0x82);
	assert(f.receiver[3][0xa7] == 0xd2 && f.receiver[7][0xa7] == 0xd2);
	assert(f.receiver[0][0x22] == 0x10 && f.receiver[0][0x23] == 0xa0);
	total = f.starts;
	/* Every transaction: no retry or subsequent START after its timeout. */
	for (i = 1; i <= total; i++) {
		f = baseline();
		f.fail_at = i;
		assert(run(&f, &result) == -ETIMEDOUT && f.starts == i);
		assert(i > total - 12 ? result.table_complete : !result.table_complete);
	}
	f = baseline();
	f.wrong_bank_at = 12; /* First switch to bank 3, table index 11. */
	assert(run(&f, &result) == -EIO && f.writes == 12);
	assert(!result.bank_verified && result.steps_completed == 11);
	f = baseline();
	f.slow_at = 13; /* Deadline expires after first table read, before TX. */
	assert(run(&f, &result) == -ETIMEDOUT && !f.writes);
	f = baseline();
	f.receiver[0][4] = 0xa0;
	assert(run(&f, &result) == -ENODEV && !f.writes);
	f = baseline();
	f.bank = 0x80;
	assert(run(&f, &result) == -ENODEV && !f.writes);
	f = baseline();
	f.receiver[0][0] = 0;
	assert(run(&f, &result) == -ENODEV && !f.writes);
	printf("PASS: 116 ordered writes, masks/banks/reset sequence, %u transaction failures,\n", total);
	puts("      bank mismatch, deadline, ID/revision gates, post-snapshot, no GPIO/DMA writes");
	return 0;
}
