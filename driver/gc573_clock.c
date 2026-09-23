// SPDX-License-Identifier: GPL-2.0-only
/* Reference-clock data read, GC573 B1 Windows routine 0x14003c1ac.
 * No default-clock substitution or subsequent timing programming.
 */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct clock_context {
	const struct gc573_block_io *io;
	struct gc573_clock_result *r;
	unsigned long start;
};

static int clock_read(struct clock_context *c, unsigned int reg)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	return gc573_block_read_registers(c->io, &c->r->last, reg, 1);
}

static int clock_set(struct clock_context *c, unsigned int reg,
		     unsigned int mask, unsigned int value)
{
	int ret = clock_read(c, reg);

	if (ret)
		return ret;
	value = (c->r->last.data[0] & ~mask) | (value & mask);
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	ret = gc573_block_write_byte(c->io, &c->r->last, reg, value);
	c->r->writes_started += c->r->last.started;
	if (reg == 15 && c->r->last.started)
		c->r->bank_verified = 0;
	if (ret)
		return ret;
	if (reg == 15) {
		ret = clock_read(c, 15);
		if (ret)
			return ret;
		if (c->r->last.data[0] != value)
			return -EIO;
		c->r->bank = value & 7;
		c->r->bank_verified = 1;
	}
	return 0;
}

static int clock_sequence(struct clock_context *c, const unsigned char ops[][3],
			  unsigned int count)
{
	unsigned int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = clock_set(c, ops[i][0], ops[i][1], ops[i][2]);
		if (ret)
			return ret;
	}
	return 0;
}

int gc573_receiver_clock(const struct gc573_block_io *io,
			 struct gc573_signal_result *signal,
			 struct gc573_clock_result *r)
{
	static const unsigned char setup[][3] = {
		{ 15, 7, 1 }, { 0xc5, 255, 255 }, { 0xc6, 255, 255 }, { 15, 7, 0 },
		{ 0xf8, 255, 0xc3 }, { 0xf8, 255, 0xa5 }, { 0x34, 255, 0 },
		{ 15, 7, 1 }, { 0x5f, 255, 4 }, { 0x5f, 255, 5 },
		{ 0x58, 255, 0x12 }, { 0x58, 255, 2 },
	};
	static const unsigned char recovery[][3] = {
		{ 0xf8, 255, 0xc3 }, { 0xf8, 255, 0xa5 }, { 0x5f, 255, 4 },
		{ 0x58, 255, 0x12 }, { 0x58, 255, 2 },
	};
	static const unsigned char recovery_end[][3] = {
		{ 15, 7, 0 }, { 0xcf, 1, 1 }, { 15, 7, 1 },
	};
	static const unsigned char cleanup[][3] = {
		{ 0x5f, 255, 0 }, { 15, 7, 0 }, { 0xf8, 255, 0 },
	};
	static const unsigned char selectors[] = { 0, 1, 0xb0, 0xb1 };
	struct clock_context c = { .io = io, .r = r };
	unsigned int i, j;
	int ret;

	*r = (struct gc573_clock_result) { 0 };
	*signal = (struct gc573_signal_result) { 0 };
	if (!io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	ret = gc573_receiver_status(io, signal);
	if (ret)
		return ret;
	if (signal->bank || signal->controls[0].data[0] != 0xb1)
		return -ENODEV;
	if (!(signal->controls[1].data[1] & 0x40) ||
	    signal->controls[1].data[2] != 0x10 || signal->controls[1].data[3] != 0xa0 ||
	    signal->controls[3].data[1] != 0xa0 ||
	    !(signal->controls[3].data[0] & 1) || !(signal->controls[5].data[0] & 1))
		return -EAGAIN;
	r->preflight_complete = 1;
	r->bank_verified = 1;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = clock_set(&c, 0x28, 255, 0x88);
	if (ret)
		return ret;
	ret = clock_read(&c, 0x98);
	if (ret)
		return ret;
	r->output_mode = r->last.data[0] & 0xf0;
	ret = clock_sequence(&c, setup, ARRAY_SIZE(setup));
	if (ret)
		return ret;
	r->phase = 2;
	ret = clock_read(&c, 0x60);
	if (ret)
		return ret;
	r->engine_status = r->last.data[0];
	r->ready_samples++;
	if (r->engine_status != 0x19) {
		r->recovery_used = 1;
		ret = clock_sequence(&c, recovery, ARRAY_SIZE(recovery));
		if (ret)
			return ret;
		for (i = 0; i < 50; i++) {
			ret = clock_read(&c, 0x60);
			if (ret)
				return ret;
			r->engine_status = r->last.data[0];
			r->ready_samples++;
			io->sleep_ms(io->ctx, 1);
			if (r->engine_status == 0x19)
				break;
		}
		if (r->engine_status != 0x19) {
			r->measurement_error = -ETIMEDOUT;
			goto close;
		}
		io->sleep_ms(io->ctx, 10);
		ret = clock_sequence(&c, recovery_end, ARRAY_SIZE(recovery_end));
		if (ret)
			return ret;
	}
	r->ready = 1;
	r->phase = 3;
	ret = clock_set(&c, 0x57, 255, 1);
	if (ret)
		return ret;
	for (i = 0; i < 4; i++) {
		if (i == 2 && r->data[0][0] == 255 && r->data[0][1] == 255 &&
		    r->data[1][0] == 0 && r->data[1][1] == 0)
			r->selector_base = 4;
		ret = clock_set(&c, 0x50, 255, i < 2 ? 0 : r->selector_base);
		if (ret)
			return ret;
		ret = clock_set(&c, 0x51, 255, selectors[i]);
		if (ret)
			return ret;
		ret = clock_set(&c, 0x54, 255, 4);
		if (ret)
			return ret;
		for (j = 0; j < 2; j++) {
			ret = clock_read(&c, 0x61 + j);
			if (ret)
				return ret;
			r->data[i][j] = r->last.data[0];
		}
		r->data_valid |= 1U << i;
	}
	r->raw_value = r->data[2][0] | (r->data[2][1] << 8) | (r->data[3][0] << 16);
	r->khz = (r->data[3][1] & 0xc0) == 0xc0 ? r->raw_value / 100 : r->raw_value;
	if (r->khz < 28500 || r->khz > 47500)
		r->measurement_error = -ERANGE;
	else
		r->clock_valid = 1;
close:
	r->phase = 4;
	ret = clock_sequence(&c, cleanup, ARRAY_SIZE(cleanup));
	if (ret)
		return ret;
	r->cleanup_complete = 1;
	r->phase = 5;
	r->post_attempted = 1;
	r->post_error = gc573_receiver_status(io, signal);
	return r->post_error ? r->post_error : r->measurement_error;
}
