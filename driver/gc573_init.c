// SPDX-License-Identifier: GPL-2.0-only
/* Original bounded executor for the GC573 B1 receiver's static startup table. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/array_size.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"
#include "gc573_receiver_table.h"

#define GC573_INIT_BUDGET_MS 15000

static int gc573_init_expired(const struct gc573_block_io *io, unsigned long start)
{
	return io->time_ms(io->ctx) - start >= GC573_INIT_BUDGET_MS;
}

int gc573_receiver_init(const struct gc573_block_io *io,
			struct gc573_signal_result *signal,
			struct gc573_init_result *result)
{
	unsigned int i, bank = 0, value;
	unsigned long start;
	int ret;

	*result = (struct gc573_init_result) { 0 };
	*signal = (struct gc573_signal_result) { 0 };
	if (!io->time_ms || !io->wait_write || !io->sleep_ms)
		return -EINVAL;
	ret = gc573_receiver_status(io, signal);
	if (ret)
		return ret;
	if (signal->bank != 0 || signal->controls[0].data[0] != 0xb1)
		return -ENODEV;
	result->preflight_complete = 1;
	result->bank_verified = 1;
	start = io->time_ms(io->ctx);
	for (i = 0; i < ARRAY_SIZE(gc573_init_table); i++) {
		const struct gc573_init_entry *entry = &gc573_init_table[i];

		result->last_step = i;
		result->last_bank = bank;
		result->last_reg = entry->reg;
		result->last_value = 0;
		result->last = (struct gc573_block_result) { 0 };
		if (gc573_init_expired(io, start))
			return -ETIMEDOUT;
		/* The Windows table walker reads even for a full-byte mask. */
		ret = gc573_block_read_registers(io, &result->last, entry->reg, 1);
		if (ret)
			return ret;
		value = (result->last.data[0] & ~entry->mask) |
			(entry->value & entry->mask);
		result->last_value = value;
		if (gc573_init_expired(io, start))
			return -ETIMEDOUT;
		ret = gc573_block_write_byte(io, &result->last, entry->reg, value);
		result->writes_started += result->last.started;
		if (entry->reg == 0x0f && result->last.started)
			result->bank_verified = 0;
		if (ret)
			return ret;
		if (entry->reg == 0x0f) {
			if (gc573_init_expired(io, start))
				return -ETIMEDOUT;
			ret = gc573_block_read_registers(io, &result->last, 0x0f, 1);
			if (ret)
				return ret;
			if (result->last.data[0] != value)
				return -EIO;
			bank = value & 7;
			result->bank_verified = 1;
		}
		result->steps_completed++;
	}
	if (bank != 0)
		return -EIO;
	result->table_complete = 1;
	/* Diagnostic settling delay; remaining calibration/EDID work is absent. */
	io->sleep_ms(io->ctx, 100);
	result->post_attempted = 1;
	result->post_error = gc573_receiver_status(io, signal);
	return result->post_error;
}
