// SPDX-License-Identifier: GPL-2.0-only
/* Windows 0x14004e52c reset prefix and one 0x14004dcc4 CAOF attempt. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/array_size.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct rx_op {
	unsigned char reg, mask, value;
};

static const struct rx_op reset[] = {
	{ 0x22, 0xff, 0x08 }, { 0x23, 0xff, 0x01 },
	{ 0x22, 0xff, 0x17 }, { 0x24, 0xff, 0xf8 },
	{ 0x23, 0xff, 0xa0 }, { 0x22, 0xff, 0x00 }, { 0x24, 0xff, 0x00 },
};

static const struct rx_op setup[] = {
	{ 0x0f, 0x03, 0x00 }, { 0x29, 0x01, 0x01 }, { 0x2a, 0x41, 0x41 },
	{ 0x0f, 0x03, 0x03 }, { 0x3a, 0x80, 0x00 }, { 0x3b, 0xc0, 0x00 },
	{ 0xa0, 0x80, 0x80 }, { 0xa1, 0x80, 0x80 }, { 0xa2, 0x80, 0x80 },
	{ 0xa7, 0x10, 0x10 }, { 0x48, 0x80, 0x80 }, { 0x0f, 0x03, 0x00 },
	{ 0x2a, 0x40, 0x00 }, { 0x24, 0x04, 0x04 }, { 0x25, 0xff, 0x00 },
	{ 0x26, 0xff, 0x00 }, { 0x27, 0xff, 0x00 }, { 0x28, 0xff, 0x00 },
	{ 0x3c, 0x10, 0x00 }, { 0x0f, 0x03, 0x03 }, { 0x3a, 0x80, 0x80 },
	{ 0x0f, 0x03, 0x00 },
};

/* Bank three is already selected for the result reads. */
static const struct rx_op cleanup[] = {
	{ 0x3a, 0x80, 0x00 }, { 0xa0, 0x80, 0x00 }, { 0xa1, 0x80, 0x00 },
	{ 0xa2, 0x80, 0x00 }, { 0x0f, 0x03, 0x00 }, { 0x08, 0x30, 0x30 },
	{ 0x29, 0x01, 0x00 }, { 0x24, 0x04, 0x00 }, { 0x3c, 0x10, 0x10 },
	{ 0xce, 0x20, 0x00 },
};

struct rx_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_cal_result *r;
	unsigned long start;
};

static int rx_read(struct rx_context *c, unsigned int reg)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	return gc573_splitter_rx_control_read(c->io, &c->r->last, reg);
}

static int rx_write(struct rx_context *c, const struct rx_op *op)
{
	struct gc573_splitter_cal_result *r = c->r;
	unsigned int value;
	int ret = rx_read(c, op->reg);

	if (ret)
		return ret;
	value = (r->last.data[0] & ~op->mask) | (op->value & op->mask);
	/* Write-one-to-clear: acknowledge only our two calibration flags. */
	if (op->reg == 0x08)
		value = 0x30;
	r->last_value = value;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	ret = gc573_splitter_rx_control_write(c->io, &r->last, op->reg, value);
	r->writes_started += r->last.started;
	if (r->last.started && (op->reg == 0x0f || op->reg == 0x22))
		r->bank_verified = 0;
	if (ret)
		return ret;
	if (op->reg == 0x0f) {
		ret = rx_read(c, 0x0f);
		if (ret)
			return ret;
		if (r->last.data[0] != value)
			return -EIO;
		r->bank = value & 3;
		r->bank_verified = 1;
	}
	r->steps_completed++;
	return 0;
}

int gc573_splitter_calibrate(const struct gc573_block_io *io,
			    struct gc573_splitter_result *identity,
			    struct gc573_splitter_clock_result *clock,
			    struct gc573_splitter_timing_result *timing,
			    struct gc573_splitter_map_result *map,
			    struct gc573_splitter_cal_result *r)
{
	static const unsigned char id[] = { 0x54, 0x49, 0x64, 0x66 };
	static const unsigned char result_regs[] = { 0x5a, 0x59, 0x59 };
	static const struct rx_op bank_three = { 0x0f, 0x03, 0x03 };
	static const struct rx_op verify[] = {
		{ 0x0f, 0xff, 0 }, { 0x29, 1, 0 }, { 0x24, 4, 0 },
		{ 0x3c, 0x10, 0x10 }, { 0xce, 0x20, 0 },
	};
	struct rx_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_cal_result) { 0 };
	*map = (struct gc573_splitter_map_result) { 0 };
	*timing = (struct gc573_splitter_timing_result) { 0 };
	*clock = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_map(io, identity, clock, timing, map);
	r->prerequisite_error = ret;
	r->transactions = map->transactions;
	if (ret)
		return ret;
	for (i = 0; i < sizeof(id); i++)
		if (map->rx_id[i] != id[i])
			return -ENODEV;
	r->preflight_complete = 1;
	r->bank_verified = 1;
	r->phase = 2;
	for (i = 0; i < ARRAY_SIZE(reset); i++) {
		ret = rx_write(&c, &reset[i]);
		if (ret)
			return ret;
		if (i == 3)
			io->sleep_ms(io->ctx, 10);
	}
	ret = rx_read(&c, 0x0f);
	if (ret)
		return ret;
	if (r->last.data[0] != 0)
		return -EIO;
	r->bank_verified = 1;
	r->reset_complete = 1;
	ret = rx_read(&c, 0x08);
	if (ret)
		return ret;
	r->initial_flags = r->last.data[0];
	r->flags_valid = 1;
	if (r->initial_flags & 0x30)
		return -EBUSY;
	r->phase = 3;
	for (i = 0; i < ARRAY_SIZE(setup); i++) {
		ret = rx_write(&c, &setup[i]);
		if (ret)
			return ret;
	}
	r->phase = 4;
	/* One attempt; omit the vendor's repeated 0x2a retrigger pulses. */
	for (i = 0; i < 32; i++) {
		ret = rx_read(&c, 0x08);
		if (ret)
			return ret;
		r->flags = r->last.data[0];
		r->flags_valid |= 2;
		r->poll_samples++;
		if (r->flags & 0x30) {
			r->completion_seen = 1;
			break;
		}
		io->sleep_ms(io->ctx, 1);
	}
	if (!r->completion_seen)
		r->poll_error = -ETIMEDOUT;
	r->phase = 5;
	ret = rx_write(&c, &bank_three);
	if (ret)
		return ret;
	for (i = 0; i < sizeof(result_regs); i++) {
		ret = rx_read(&c, result_regs[i]);
		if (ret)
			return ret;
		r->values[i] = r->last.data[0];
		r->values_valid |= 1U << i;
	}
	r->phase = 6;
	for (i = 0; i < ARRAY_SIZE(cleanup); i++) {
		ret = rx_write(&c, &cleanup[i]);
		if (ret)
			return ret;
	}
	r->bank_verified = 0;
	for (i = 0; i < ARRAY_SIZE(verify); i++) {
		ret = rx_read(&c, verify[i].reg);
		if (ret)
			return ret;
		r->last_value = verify[i].value;
		if ((r->last.data[0] & verify[i].mask) != verify[i].value)
			return -EIO;
		if (i == 0)
			r->bank_verified = 1;
	}
	r->cleanup_complete = 1;
	r->phase = 7;
	if (r->poll_error)
		return r->poll_error;
	r->complete = 1;
	return 0;
}
