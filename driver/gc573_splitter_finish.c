// SPDX-License-Identifier: GPL-2.0-only
/* Fresh-start path: 0x14004e5c7 -> 0x140053b2c -> 0x14005071c(0). */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct finish_op {
	unsigned char address, reg, mask, value, verify, delay_ms;
};

static const struct finish_op prefix[] = {
	{ 0x38, 0x0f, 0xff, 3, 1, 0 }, { 0x38, 0xab, 0xff, 0x4a, 1, 0 },
	{ 0x38, 0xac, 0xff, 0x40, 1, 0 }, { 0x38, 0x0f, 0xff, 0, 1, 0 },
	{ 0x38, 0x53, 0xe0, 0, 1, 0 }, { 0x38, 0x54, 0xff, 0, 1, 0 },
	{ 0x38, 0x55, 0x07, 0, 1, 0 }, { 0x38, 0x57, 0x0f, 0, 1, 0 },
	{ 0x38, 0xc5, 0x10, 0x10, 0, 0 }, { 0x38, 0xc5, 0x10, 0, 1, 0 },
	{ 0x2c, 0x0a, 0x04, 0x04, 0, 1 }, { 0x2c, 0x0a, 0x04, 0, 1, 0 },
	{ 0x38, 0x0f, 0x03, 3, 1, 0 },
};

static const struct finish_op ca_branch[] = {
	{ 0x38, 0xab, 0xff, 0x4a, 1, 0 },
	{ 0x38, 0xab, 0xff, 0, 1, 0 }, { 0x38, 0xac, 0xff, 0, 1, 0 },
};

static const struct finish_op suffix[] = {
	{ 0x38, 0x0f, 0x03, 0, 1, 0 }, { 0x38, 0x26, 0xff, 0xff, 1, 0 },
	{ 0x38, 0x55, 0xff, 0, 1, 0 }, { 0x38, 0x0f, 0x03, 3, 1, 0 },
	{ 0x38, 0x27, 0xff, 0x9f, 1, 0 }, { 0x38, 0x28, 0xff, 0x9f, 1, 0 },
	{ 0x38, 0x29, 0xff, 0x9f, 1, 0 }, { 0x38, 0x20, 0xff, 0x1b, 1, 0 },
	{ 0x38, 0x21, 0xff, 3, 1, 0 }, { 0x38, 0x0f, 0x03, 0, 1, 0 },
};

struct finish_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_finish_result *r;
	unsigned long start;
};

static int finish_read(struct finish_context *c, unsigned int address, unsigned int reg)
{
	c->r->last_address = address;
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	if (address == 0x2c)
		return gc573_splitter_read(c->io, &c->r->last, reg);
	return gc573_splitter_rx_finish_read(c->io, &c->r->last, reg);
}

static int finish_ops(struct finish_context *c, const struct finish_op *ops,
		      unsigned int count)
{
	struct gc573_splitter_finish_result *r = c->r;
	unsigned int i;
	int ret;

	for (i = 0; i < count; i++) {
		const struct finish_op *op = &ops[i];

		ret = finish_read(c, op->address, op->reg);
		if (ret)
			return ret;
		r->expected = (r->last.data[0] & ~op->mask) | op->value;
		if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
			return -ETIMEDOUT;
		r->transactions++;
		if (op->address == 0x2c)
			ret = gc573_splitter_write(c->io, &r->last, op->reg, r->expected);
		else
			ret = gc573_splitter_rx_finish_write(c->io, &r->last, op->reg,
							   r->expected);
		r->writes_started += r->last.started;
		if (op->address == 0x38 && op->reg == 0x0f && r->last.started)
			r->bank_verified = 0;
		if (ret)
			return ret;
		if (op->delay_ms)
			c->io->sleep_ms(c->io->ctx, op->delay_ms);
		if (op->verify) {
			ret = finish_read(c, op->address, op->reg);
			if (ret)
				return ret;
			r->observed = r->last.data[0];
			if ((r->observed & op->mask) != op->value)
				return -EIO;
			if (op->address == 0x38 && op->reg == 0x0f) {
				if (r->observed != r->expected)
					return -EIO;
				r->bank = r->observed & 3;
				r->bank_verified = 1;
			}
			r->steps_verified++;
		}
		r->steps_completed++;
	}
	return 0;
}

int gc573_splitter_finish(const struct gc573_block_io *io,
			 struct gc573_splitter_result *identity,
			 struct gc573_splitter_clock_result *clock,
			 struct gc573_splitter_timing_result *timing,
			 struct gc573_splitter_map_result *map,
			 struct gc573_splitter_cal_result *cal,
			 struct gc573_splitter_setup_result *setup,
			 struct gc573_splitter_finish_result *r)
{
	static const unsigned char status_regs[] = { 0x13, 0x19 };
	struct finish_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_finish_result) { 0 };
	*setup = (struct gc573_splitter_setup_result) { 0 };
	*cal = (struct gc573_splitter_cal_result) { 0 };
	*map = (struct gc573_splitter_map_result) { 0 };
	*timing = (struct gc573_splitter_timing_result) { 0 };
	*clock = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms || !io->sleep_ms)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_setup(io, identity, clock, timing, map, cal, setup);
	r->prerequisite_error = ret;
	r->transactions = setup->transactions;
	if (ret)
		return ret;
	r->bank_verified = setup->bank_verified;
	r->phase = 2;
	ret = finish_ops(&c, prefix, ARRAY_SIZE(prefix));
	if (ret)
		return ret;
	r->phase = 3;
	ret = finish_read(&c, 0x38, 0xab);
	if (ret)
		return ret;
	r->ab_before = r->last.data[0];
	r->ab_valid = 1;
	if (r->ab_before == 0xca) {
		r->ab_ca_branch = 1;
		ret = finish_ops(&c, ca_branch, ARRAY_SIZE(ca_branch));
		if (ret)
			return ret;
	}
	r->phase = 4;
	ret = finish_ops(&c, suffix, ARRAY_SIZE(suffix));
	if (ret)
		return ret;
	r->phase = 5;
	r->bank_verified = 0;
	ret = finish_read(&c, 0x38, 0x0f);
	if (ret)
		return ret;
	r->expected = 0;
	r->observed = r->last.data[0];
	if (r->observed)
		return -EIO;
	r->bank_verified = 1;
	ret = finish_read(&c, 0x2c, 0x0f);
	if (ret)
		return ret;
	r->observed = r->last.data[0];
	if (r->observed)
		return -EIO;
	r->control_bank_verified = 1;
	for (i = 0; i < ARRAY_SIZE(status_regs); i++) {
		ret = finish_read(&c, 0x38, status_regs[i]);
		if (ret)
			return ret;
		r->status[i] = r->last.data[0];
		r->status_valid |= 1U << i;
	}
	r->complete = 1;
	return 0;
}
