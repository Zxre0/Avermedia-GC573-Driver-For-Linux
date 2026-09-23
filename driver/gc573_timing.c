// SPDX-License-Identifier: GPL-2.0-only
/* B1 clock-dependent settings: GC573 Windows routine 0x14003bda0.
 * A fresh validated reference-clock read precedes all timing writes.
 */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct timing_context {
	const struct gc573_block_io *io;
	struct gc573_timing_result *r;
	unsigned long start;
};

int gc573_timing_compute(unsigned int khz, struct gc573_timing_values *v)
{
	*v = (struct gc573_timing_values) { 0 };
	if (khz < 28500 || khz > 47500)
		return -ERANGE;
	v->half_khz = khz / 2;
	v->adjusted_khz = v->half_khz + v->half_khz / 10;
	v->reg91 = (v->adjusted_khz / 1000) & 0x3f;
	v->reg92 = ((v->adjusted_khz % 1000) * 256) / 1000;
	v->regfd = v->half_khz / 100;
	v->reg45 = khz / 312;
	v->reg44 = (khz / 312) / 5;
	/* The reference's integer remainder terms are zero, not fractions. */
	v->reg46 = khz / 2320;
	v->reg47 = khz / 5312;
	/* All values fit their fields throughout the accepted clock range. */
	return 0;
}

static int timing_read(struct timing_context *c, unsigned int reg)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	return gc573_block_read_registers(c->io, &c->r->last, reg, 1);
}

static int timing_set(struct timing_context *c, unsigned int reg,
		      unsigned int mask, unsigned int value)
{
	int ret = timing_read(c, reg);

	if (ret)
		return ret;
	c->r->expected = (c->r->last.data[0] & ~mask) | (value & mask);
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	ret = gc573_block_write_byte(c->io, &c->r->last, reg, c->r->expected);
	c->r->writes_started += c->r->last.started;
	if (reg == 15 && c->r->last.started)
		c->r->bank_verified = 0;
	if (ret)
		return ret;
	ret = timing_read(c, reg);
	if (ret)
		return ret;
	c->r->observed = c->r->last.data[0];
	if (c->r->observed != c->r->expected)
		return -EIO;
	if (reg == 15) {
		c->r->bank = c->r->observed & 7;
		c->r->bank_verified = 1;
	}
	c->r->steps_verified++;
	return 0;
}

int gc573_receiver_timing(const struct gc573_block_io *io,
			  struct gc573_signal_result *signal,
			  struct gc573_clock_result *clock,
			  struct gc573_timing_result *r)
{
	struct timing_context c = { .io = io, .r = r };
	struct gc573_timing_values *v = &r->values;
	unsigned int i;
	int ret;

	*r = (struct gc573_timing_result) { 0 };
	r->clock_error = gc573_receiver_clock(io, signal, clock);
	if (r->clock_error)
		return r->clock_error;
	ret = gc573_timing_compute(clock->khz, v);
	if (ret)
		return ret;
	c.start = io->time_ms(io->ctx);
	r->bank_verified = 1;
	r->phase = 1;
	{
		const unsigned char ops[][3] = {
			{ 15, 7, 3 }, { 0xaa, 0x1f, 0x0c }, { 15, 7, 0 },
			{ 0x91, 0x3f, v->reg91 }, { 0x92, 255, v->reg92 },
			{ 15, 7, 1 }, { 0xfd, 255, v->regfd },
			{ 0xfe, 0x20, 0 }, { 0xfe, 0x0f, 0x0c },
			{ 0xfe, 0x10, 0x10 }, { 0xfe, 0x80, 0x80 },
			{ 15, 7, 0 }, { 0x45, 255, v->reg45 },
			{ 0x44, 255, v->reg44 }, { 0x46, 255, v->reg46 },
			{ 0x47, 255, v->reg47 }, { 15, 7, 0 },
		};

		for (i = 0; i < ARRAY_SIZE(ops); i++) {
			ret = timing_set(&c, ops[i][0], ops[i][1], ops[i][2]);
			if (ret)
				return ret;
		}
	}
	r->complete = 1;
	r->phase = 2;
	io->sleep_ms(io->ctx, 100);
	r->post_attempted = 1;
	r->post_error = gc573_receiver_status(io, signal);
	return r->post_error;
}
