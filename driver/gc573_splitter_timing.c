// SPDX-License-Identifier: GPL-2.0-only
/* Original clock-dependent writes from Windows 0x14004db60. */
#include <linux/errno.h>
#include "gc573_block.h"

int gc573_splitter_timing_compute(unsigned int khz,
				 struct gc573_splitter_timing_values *values)
{
	*values = (struct gc573_splitter_timing_values) { 0 };
	/* Refuse invalid reference data or overflow of the 18-bit timer. */
	if (khz < 10000 || khz > 34000 || khz * 10 > 0x3ffff)
		return -ERANGE;
	values->ticks = khz * 10;
	values->bytes[0] = values->ticks & 255;
	values->bytes[1] = (values->ticks >> 8) & 255;
	values->bytes[2] = (values->ticks >> 16) & 3;
	values->bytes[3] = (khz / 1000) & 63;
	values->bytes[4] = ((khz % 1000) * 256) / 1000;
	return 0;
}

struct timing_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_timing_result *r;
	unsigned long start;
};

static int timing_read(struct timing_context *c, unsigned int address,
		       unsigned int reg)
{
	c->r->last_address = address;
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	if (address == 0x4b)
		return gc573_splitter_timer_read(c->io, &c->r->last, reg);
	return gc573_splitter_read(c->io, &c->r->last, reg);
}

int gc573_splitter_timing(const struct gc573_block_io *io,
			 struct gc573_splitter_result *identity,
			 struct gc573_splitter_clock_result *clock,
			 struct gc573_splitter_timing_result *r)
{
	static const unsigned char registers[] = { 0x11, 0x12, 0x13, 0x1e, 0x1f };
	static const unsigned char masks[] = { 255, 255, 3, 63, 255 };
	struct timing_context c = { .io = io, .r = r };
	unsigned int i, address, value;
	int ret;

	*r = (struct gc573_splitter_timing_result) { 0 };
	*clock = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_clock(io, identity, clock);
	r->clock_error = ret;
	r->transactions = clock->ops.transactions;
	if (ret)
		return ret;
	r->bank_verified = clock->ops.bank_verified;
	r->phase = 2;
	ret = gc573_splitter_timing_compute(clock->khz, &r->values);
	if (ret)
		return ret;
	/* Recheck the mapping after clock-access cleanup before address 0x4b. */
	ret = timing_read(&c, 0x2c, 0xf1);
	if (ret)
		return ret;
	if (r->last.data[0] != 0x97)
		return -EOPNOTSUPP;
	r->mapping_verified = 1;
	r->phase = 3;
	for (i = 0; i < sizeof(registers); i++) {
		address = i < 3 ? 0x4b : 0x2c;
		ret = timing_read(&c, address, registers[i]);
		if (ret)
			return ret;
		r->before[i] = r->last.data[0];
		r->before_valid |= 1U << i;
		value = (r->before[i] & ~masks[i]) | r->values.bytes[i];
		r->expected = value;
		if (io->time_ms(io->ctx) - c.start >= 15000)
			return -ETIMEDOUT;
		r->transactions++;
		if (i < 3)
			ret = gc573_splitter_timer_write(io, &r->last, registers[i], value);
		else
			ret = gc573_splitter_write(io, &r->last, registers[i], value);
		r->writes_started += r->last.started;
		if (ret)
			return ret;
		ret = timing_read(&c, address, registers[i]);
		if (ret)
			return ret;
		r->after[i] = r->last.data[0];
		r->after_valid |= 1U << i;
		r->observed = r->after[i];
		if ((r->observed & masks[i]) != r->values.bytes[i])
			return -EIO;
		r->steps_verified++;
	}
	r->phase = 4;
	r->bank_verified = 0;
	ret = timing_read(&c, 0x2c, 0x0f);
	if (ret)
		return ret;
	r->expected = 0;
	r->observed = r->last.data[0];
	if (r->observed != 0)
		return -EIO;
	r->bank_verified = 1;
	r->complete = 1;
	return 0;
}
