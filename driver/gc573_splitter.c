// SPDX-License-Identifier: GPL-2.0-only
/* Original bounded prefix of Windows splitter initialization 0x14004f87c.
 * Stop before reference-clock calibration and downstream endpoint accesses.
 */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct splitter_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_prepare_result *r;
	unsigned long start;
};

static int splitter_read(struct splitter_context *c, unsigned int reg)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	return gc573_splitter_read(c->io, &c->r->last, reg);
}

static int splitter_set(struct splitter_context *c, unsigned int reg,
			unsigned int mask, unsigned int value, int verify)
{
	int ret = splitter_read(c, reg);

	if (ret)
		return ret;
	value = (c->r->last.data[0] & ~mask) | (value & mask);
	c->r->expected = value;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	ret = gc573_splitter_write(c->io, &c->r->last, reg, value);
	c->r->writes_started += c->r->last.started;
	if ((reg == 0x0f || reg == 0x0a) && c->r->last.started)
		c->r->bank_verified = 0;
	if (ret || !verify)
		return ret;
	ret = splitter_read(c, reg);
	if (ret)
		return ret;
	c->r->observed = c->r->last.data[0];
	if ((c->r->observed & mask) != (value & mask))
		return -EIO;
	if (reg == 0x0f) {
		c->r->bank = c->r->observed & 1;
		c->r->bank_verified = 1;
	}
	return 0;
}

int gc573_splitter_prepare(const struct gc573_block_io *io,
			  struct gc573_splitter_result *identity,
			  struct gc573_splitter_prepare_result *r)
{
	/* Command/key writes: completion is required, not stored readback. */
	static const unsigned char recovery[][2] = {
		{ 0xff, 0xc3 }, { 0xff, 0xa5 }, { 0x5f, 4 },
		{ 0x58, 0x12 }, { 0x58, 2 }, { 0x5f, 0 }, { 0xff, 0xff },
	};
	static const unsigned char base[][4] = {
		{ 0x10, 255, 0x6e, 1 }, { 0xf0, 255, 0x71, 1 },
		{ 0x0e, 7, 0, 1 }, { 0x08, 15, 15, 0 },
		{ 0xf0, 255, 0x71, 1 }, { 0xf1, 255, 0x97, 1 },
	};
	struct splitter_context c = { .io = io, .r = r };
	unsigned int i, gpio;
	int ret;

	*r = (struct gc573_splitter_prepare_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_identify(io, identity);
	r->prerequisite_error = ret;
	r->transactions = identity->transactions;
	if (ret)
		return ret;
	/* Restrict the first configuration experiment to the observed 6663 ID. */
	if (identity->bank != 0 || identity->id[2] != 0x63)
		return -ENODEV;
	gpio = io->read(io->ctx, GC573_GPIO);
	if ((gpio & ~0x404U) != 0x1f978)
		return -ENODEV;
	r->bank_verified = 1;
	r->preflight_complete = 1;
	r->phase = 2;
	ret = splitter_set(&c, 0x0a, 255, 1, 0);
	if (ret)
		return ret;
	ret = splitter_set(&c, 0x0a, 255, 0, 1);
	if (ret)
		return ret;
	ret = splitter_read(&c, 0x0f);
	if (ret)
		return ret;
	r->expected = 0;
	r->observed = r->last.data[0];
	if (r->observed != 0)
		return -EIO;
	r->bank_verified = 1;
	ret = splitter_read(&c, 0x60);
	if (ret)
		return ret;
	r->engine_before = r->last.data[0];
	r->engine_status = r->engine_before;
	r->engine_valid = 1;
	r->phase = 3;
	if (r->engine_status != 0x19) {
		r->recovery_used = 1;
		for (i = 0; i < ARRAY_SIZE(recovery); i++) {
			ret = splitter_set(&c, recovery[i][0], 255, recovery[i][1], 0);
			if (ret)
				return ret;
		}
		for (i = 0; i < 50; i++) {
			io->sleep_ms(io->ctx, 1);
			ret = splitter_read(&c, 0x60);
			if (ret)
				return ret;
			r->poll_samples++;
			r->engine_status = r->last.data[0];
			if (r->engine_status == 0x19)
				break;
		}
		if (r->engine_status != 0x19)
			return -ETIMEDOUT;
		r->ready = 1;
		io->sleep_ms(io->ctx, 10);
		ret = splitter_set(&c, 0x0f, 1, 1, 1);
		if (ret)
			return ret;
		ret = splitter_set(&c, 0x73, 4, 4, 1);
		if (ret)
			return ret;
		ret = splitter_set(&c, 0x0f, 1, 0, 1);
		if (ret)
			return ret;
	}
	r->ready = 1;
	r->phase = 4;
	for (i = 0; i < ARRAY_SIZE(base); i++) {
		ret = splitter_set(&c, base[i][0], base[i][1], base[i][2], base[i][3]);
		if (ret)
			return ret;
	}
	ret = splitter_read(&c, 0x0f);
	if (ret)
		return ret;
	r->expected = 0;
	r->observed = r->last.data[0];
	r->bank = r->last.data[0] & 1;
	r->bank_verified = !r->bank;
	if (!r->bank_verified)
		return -EIO;
	r->phase = 5;
	r->complete = 1;
	return 0;
}

int gc573_splitter_clock_decode(unsigned int low, unsigned int high,
			       unsigned int *raw, unsigned int *khz)
{
	*raw = 0;
	*khz = 0;
	if (low > 0xffff || high > 0xffff)
		return -EINVAL;
	*raw = low | ((high & 255) << 16);
	*khz = (high & 0xc000) == 0xc000 ? *raw / 100 : *raw;
	/* Vendor accepts 22000 +/- 12000 kHz; do not substitute its default. */
	return *khz >= 10000 && *khz <= 34000 ? 0 : -ERANGE;
}

/* Windows 0x14004d368: internal reference data, not live HDMI pixel clock. */
int gc573_splitter_clock(const struct gc573_block_io *io,
			struct gc573_splitter_result *identity,
			struct gc573_splitter_clock_result *result)
{
	static const unsigned char checks[][3] = {
		{ 0x60, 255, 0x19 }, { 0x10, 255, 0x6e },
		{ 0xf0, 255, 0x71 }, { 0xf1, 255, 0x97 }, { 0x0e, 7, 0 },
	};
	static const unsigned char setup[][2] = {
		{ 0xff, 0xc3 }, { 0xff, 0xa5 }, { 0x0f, 0 },
		{ 0x5f, 4 }, { 0x5f, 5 }, { 0x58, 0x12 }, { 0x58, 2 }, { 0x57, 1 },
	};
	struct gc573_splitter_prepare_result *r = &result->ops;
	struct splitter_context c = { .io = io, .r = r };
	unsigned int i, selector, low, gpio;
	int ret;

	*result = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms || !io->wait_write)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_identify(io, identity);
	r->prerequisite_error = ret;
	r->transactions = identity->transactions;
	if (ret)
		return ret;
	if (identity->bank != 0 || identity->id[2] != 0x63)
		return -ENODEV;
	gpio = io->read(io->ctx, GC573_GPIO);
	if ((gpio & ~0x404U) != 0x1f978)
		return -ENODEV;
	r->bank_verified = 1;
	for (i = 0; i < ARRAY_SIZE(checks); i++) {
		ret = splitter_read(&c, checks[i][0]);
		if (ret)
			return ret;
		r->expected = checks[i][2];
		r->observed = r->last.data[0];
		if ((r->observed & checks[i][1]) != r->expected)
			return -EOPNOTSUPP;
	}
	r->preflight_complete = 1;
	r->phase = 2;
	for (i = 0; i < ARRAY_SIZE(setup); i++) {
		ret = splitter_set(&c, setup[i][0], 255, setup[i][1], setup[i][0] == 15);
		if (ret)
			return ret;
	}
	r->phase = 3;
	for (i = 0; i < 4; i++) {
		if (i == 2)
			result->selector_base = result->words[0] == 0xffff &&
				result->words[1] == 0 ? 0x4b0 : 0xb0;
		selector = i < 2 ? i : result->selector_base + i - 2;
		ret = splitter_set(&c, 0x50, 255, (selector >> 8) & 15, 0);
		if (ret)
			return ret;
		ret = splitter_set(&c, 0x51, 255, selector & 255, 0);
		if (ret)
			return ret;
		ret = splitter_set(&c, 0x54, 255, 4, 0);
		if (ret)
			return ret;
		ret = splitter_read(&c, 0x61);
		if (ret)
			return ret;
		low = r->last.data[0];
		ret = splitter_read(&c, 0x62);
		if (ret)
			return ret;
		result->words[i] = low | (r->last.data[0] << 8);
		result->data_valid |= 1U << i;
	}
	result->measurement_error = gc573_splitter_clock_decode(result->words[2],
			result->words[3], &result->raw, &result->khz);
	result->valid = !result->measurement_error;
	r->phase = 4;
	ret = splitter_set(&c, 0x5f, 255, 0, 1);
	if (ret)
		return ret;
	ret = splitter_set(&c, 0x0f, 255, 0, 1);
	if (ret)
		return ret;
	ret = splitter_set(&c, 0xff, 255, 0xff, 0);
	if (ret)
		return ret;
	result->cleanup_complete = 1;
	if (result->measurement_error)
		return result->measurement_error;
	r->phase = 5;
	r->complete = 1;
	return 0;
}
