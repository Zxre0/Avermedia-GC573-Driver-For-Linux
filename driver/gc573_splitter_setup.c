// SPDX-License-Identifier: GPL-2.0-only
/* Original register sequence from Windows 0x14004e284, after RX CAOF. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct setup_op {
	unsigned char reg, mask, value, verify;
};

/* 0x1400597a8 sets object+c5b=1 before the initialization caller. */
static const struct setup_op setup[] = {
	{ 0x56, 0xff, 0xff, 0 }, { 0x57, 0xff, 0xff, 0 },
	{ 0x0f, 0x03, 0x03, 1 }, { 0xa8, 0x08, 0x08, 1 },
	{ 0xa7, 0x40, 0x40, 1 }, { 0x26, 0x20, 0x00, 1 },
	{ 0x27, 0xff, 0x9f, 1 }, { 0x28, 0xff, 0x9f, 1 },
	{ 0x29, 0xff, 0x9f, 1 }, { 0x0f, 0x03, 0x00, 1 },
	{ 0x28, 0x59, 0x59, 1 }, { 0x2a, 0x01, 0x01, 1 },
	{ 0x43, 0x02, 0x00, 1 }, { 0x44, 0x3f, 0x19, 1 },
	{ 0x3c, 0x01, 0x00, 1 }, { 0x45, 0xff, 0xdf, 1 },
	{ 0x46, 0x3f, 0x15, 1 }, { 0x47, 0xff, 0x88, 1 },
	{ 0x49, 0xff, 0xe1, 1 }, { 0x23, 0xff, 0xa0, 1 },
	{ 0x53, 0xff, 0x0f, 1 }, { 0xe3, 0xff, 0x04, 1 },
	{ 0xce, 0x80, 0x00, 1 }, { 0x3c, 0x20, 0x00, 1 },
	{ 0x0f, 0x03, 0x03, 1 }, { 0xe3, 0x01, 0x01, 1 },
	/* Vendor value=03 with mask=06; preceding step already sets bit zero. */
	{ 0xe3, 0x06, 0x02, 1 }, { 0xf0, 0xff, 0xa0, 1 },
	{ 0x0f, 0x03, 0x00, 1 }, { 0x28, 0x88, 0x88, 1 },
	{ 0x3b, 0x20, 0x20, 1 }, { 0x26, 0xff, 0xff, 1 },
	{ 0x42, 0x20, 0x00, 1 },
};

struct setup_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_setup_result *r;
	unsigned long start;
};

static int setup_read(struct setup_context *c, unsigned int reg)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	return gc573_splitter_rx_setup_read(c->io, &c->r->last, reg);
}

int gc573_splitter_setup(const struct gc573_block_io *io,
			struct gc573_splitter_result *identity,
			struct gc573_splitter_clock_result *clock,
			struct gc573_splitter_timing_result *timing,
			struct gc573_splitter_map_result *map,
			struct gc573_splitter_cal_result *cal,
			struct gc573_splitter_setup_result *r)
{
	struct setup_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_setup_result) { 0 };
	*cal = (struct gc573_splitter_cal_result) { 0 };
	*map = (struct gc573_splitter_map_result) { 0 };
	*timing = (struct gc573_splitter_timing_result) { 0 };
	*clock = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_calibrate(io, identity, clock, timing, map, cal);
	r->prerequisite_error = ret;
	r->transactions = cal->transactions;
	if (ret)
		return ret;
	r->bank_verified = cal->bank_verified;
	r->phase = 2;
	for (i = 0; i < ARRAY_SIZE(setup); i++) {
		const struct setup_op *op = &setup[i];

		r->last_step = i;
		ret = setup_read(&c, op->reg);
		if (ret)
			return ret;
		r->expected = (r->last.data[0] & ~op->mask) | op->value;
		if (io->time_ms(io->ctx) - c.start >= 15000)
			return -ETIMEDOUT;
		r->transactions++;
		ret = gc573_splitter_rx_setup_write(io, &r->last, op->reg, r->expected);
		r->writes_started += r->last.started;
		if (op->reg == 0x0f && r->last.started)
			r->bank_verified = 0;
		if (ret)
			return ret;
		if (op->verify) {
			ret = setup_read(&c, op->reg);
			if (ret)
				return ret;
			r->observed = r->last.data[0];
			if ((r->observed & op->mask) != op->value)
				return -EIO;
			if (op->reg == 0x0f) {
				if (r->observed != r->expected)
					return -EIO;
				r->bank = r->observed & 3;
				r->bank_verified = 1;
			}
			r->steps_verified++;
		}
		r->steps_completed++;
	}
	r->phase = 3;
	r->bank_verified = 0;
	ret = setup_read(&c, 0x0f);
	if (ret)
		return ret;
	r->expected = 0;
	r->observed = r->last.data[0];
	if (r->observed)
		return -EIO;
	r->bank_verified = 1;
	r->complete = 1;
	return 0;
}
