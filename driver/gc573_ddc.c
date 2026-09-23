// SPDX-License-Identifier: GPL-2.0-only
/* EDID checksum/address substitution and DDC reset from exact GC573 routines
 * 0x14003e5fc and 0x14003e2b0, using the resident EDID instead of replacing it.
 * Require HPD low; leave it low. No writes to the SRAM endpoint.
 */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

int gc573_ddc_plan(const unsigned char data[256], struct gc573_ddc_plan *p)
{
	static const unsigned char header[] = { 0, 255, 255, 255, 255, 255, 255, 0 };
	unsigned int i, end, length, found = 0, base = 0, extension = 0;

	*p = (struct gc573_ddc_plan) { 0 };
	for (i = 0; i < ARRAY_SIZE(header); i++)
		if (data[i] != header[i])
			return -EINVAL;
	/* Limit this experiment to EDID 1.3/1.4 with one CTA revision-3 block. */
	if (data[18] != 1 || (data[19] != 3 && data[19] != 4) ||
	    data[126] != 1 || data[128] != 2 || data[129] != 3 ||
	    data[130] < 4 || data[130] > 127)
		return -EINVAL;
	end = 128 + data[130];
	for (i = 132; i < end; i += length + 1) {
		length = data[i] & 31;
		if (i + length + 1 > end)
			return -EINVAL;
		if ((data[i] >> 5) != 3 || length < 3 ||
		    data[i + 1] != 3 || data[i + 2] != 12 || data[i + 3] != 0)
			continue;
		if (length < 5 || found)
			return -EINVAL;
		found = i + 4;
	}
	if (!found)
		return -EINVAL;
	for (i = 0; i < 127; i++) {
		base += data[i];
		extension += data[128 + i];
	}
	p->physical_offset = found;
	p->base_checksum = -base;
	/* Windows advertises 1.0.0.0 / 2.0.0.0, via separate override registers. */
	p->extension_checksum[0] = -extension + data[found] + data[found + 1] - 0x10;
	p->extension_checksum[1] = -extension + data[found] + data[found + 1] - 0x20;
	return 0;
}

struct ddc_context {
	const struct gc573_block_io *io;
	struct gc573_ddc_result *r;
	unsigned long start;
};

static int ddc_read(struct ddc_context *c, unsigned int reg, unsigned int count)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	return gc573_block_read_registers(c->io, &c->r->last, reg, count);
}

static int ddc_set(struct ddc_context *c, unsigned int reg,
		   unsigned int mask, unsigned int value)
{
	unsigned int verify_mask = mask;
	int ret = ddc_read(c, reg, 1);

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
	/* Do not require a reset request bit to remain asserted. */
	if (reg == 0xc5 && value == 0x10)
		verify_mask = 0;
	ret = ddc_read(c, reg, 1);
	if (ret)
		return ret;
	c->r->observed = c->r->last.data[0];
	if (((c->r->observed ^ c->r->expected) & verify_mask) ||
	    (reg == 15 && c->r->observed != c->r->expected))
		return -EIO;
	if (reg == 15) {
		c->r->bank = c->r->observed & 7;
		c->r->bank_verified = 1;
	}
	c->r->steps_completed++;
	return 0;
}

static int ddc_snapshot(struct ddc_context *c, unsigned char values[2][6],
			unsigned int *valid)
{
	unsigned int port, i;
	int ret;

	for (port = 0; port < 2; port++) {
		ret = ddc_set(c, 15, 7, port * 4);
		if (ret)
			return ret;
		ret = ddc_read(c, 0xc5, 4);
		if (ret)
			return ret;
		for (i = 0; i < 4; i++)
			values[port][i] = c->r->last.data[i];
		ret = ddc_read(c, 0xc9, 2);
		if (ret)
			return ret;
		for (i = 0; i < 2; i++)
			values[port][4 + i] = c->r->last.data[i];
		*valid |= 1U << port;
	}
	return ddc_set(c, 15, 7, 0);
}

int gc573_receiver_ddc(const struct gc573_block_io *io,
		       struct gc573_signal_result *signal,
		       struct gc573_clock_result *clock,
		       struct gc573_timing_result *timing,
		       struct gc573_edid_result *edid,
		       struct gc573_ddc_result *r)
{
	struct ddc_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_ddc_result) { 0 };
	*signal = (struct gc573_signal_result) { 0 };
	*clock = (struct gc573_clock_result) { 0 };
	*timing = (struct gc573_timing_result) { 0 };
	*edid = (struct gc573_edid_result) { 0 };
	r->gpio = io->read(io->ctx, GC573_GPIO);
	if (r->gpio == 0xffffffff || r->gpio == 0xeeeeeeee)
		return -ENODEV;
	if (r->gpio & 4)
		return -EBUSY;
	r->prerequisite_error = gc573_receiver_edid_read(io, signal, clock, timing, edid);
	if (r->prerequisite_error)
		return r->prerequisite_error;
	if (signal->port0 & 0x40)
		return -EBUSY;
	ret = gc573_ddc_plan(edid->data, &r->plan);
	if (ret)
		return ret;
	c.start = io->time_ms(io->ctx);
	r->bank_verified = 1;
	r->phase = 1;
	ret = ddc_snapshot(&c, r->before, &r->before_valid);
	if (ret)
		return ret;
	if ((r->before[0][0] | r->before[1][0]) & 0x10)
		return -EBUSY;
	r->phase = 2;
	{
		const unsigned char ops[][3] = {
			{ 0xc9, 255, r->plan.base_checksum }, { 15, 7, 4 },
			{ 0xc9, 255, r->plan.base_checksum }, { 15, 7, 0 },
			{ 0xc6, 255, r->plan.physical_offset }, { 0xc7, 255, 0x10 },
			{ 0xc8, 255, 0 }, { 0xca, 255, r->plan.extension_checksum[0] },
			{ 15, 7, 4 }, { 0xc7, 255, 0x20 }, { 0xc8, 255, 0 },
			{ 0xca, 255, r->plan.extension_checksum[1] }, { 15, 7, 0 },
			{ 0xc5, 1, 0 }, { 15, 7, 4 }, { 0xc5, 1, 0 }, { 15, 7, 0 },
			{ 0xc5, 0x10, 0x10 }, { 0xc5, 0x10, 0 }, { 15, 7, 4 },
			{ 0xc5, 0x10, 0x10 }, { 0xc5, 0x10, 0 }, { 15, 7, 0 },
		};

		for (i = 0; i < ARRAY_SIZE(ops); i++) {
			ret = ddc_set(&c, ops[i][0], ops[i][1], ops[i][2]);
			if (ret)
				return ret;
			if (ops[i][0] == 0xc5 && ops[i][2] == 0x10)
				io->sleep_ms(io->ctx, 1);
		}
	}
	r->phase = 3;
	ret = ddc_snapshot(&c, r->after, &r->after_valid);
	if (ret)
		return ret;
	for (i = 0; i < 2; i++)
		if ((r->after[i][0] & 0x11) || r->after[i][2] != (i + 1) * 0x10 ||
		    r->after[i][3] || r->after[i][4] != r->plan.base_checksum ||
		    r->after[i][5] != r->plan.extension_checksum[i])
			return -EIO;
	if (r->after[0][1] != r->plan.physical_offset)
		return -EIO;
	r->complete = 1;
	r->phase = 4;
	r->post_attempted = 1;
	r->post_error = gc573_receiver_status(io, signal);
	return r->post_error;
}
