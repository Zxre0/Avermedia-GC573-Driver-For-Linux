// SPDX-License-Identifier: GPL-2.0-only
/* B1 port-zero startup and one HPD assertion, derived from exact GC573
 * Windows 3d374, 439e8, 40dbc, 45b5c(state 2), 3d1e4/38d80.
 * No streaming state machine, capture output, IRQ enable or DMA.
 */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/array_size.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct input_context {
	const struct gc573_block_io *io;
	struct gc573_input_result *r;
	unsigned long start;
};

static int input_read(struct input_context *c, unsigned int reg)
{
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	return gc573_block_read_registers(c->io, &c->r->last, reg, 1);
}

static int input_set(struct input_context *c, const unsigned char op[5])
{
	unsigned int reg = op[0];
	int ret = input_read(c, reg);

	if (ret)
		return ret;
	c->r->expected = (c->r->last.data[0] & ~op[1]) | (op[2] & op[1]);
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	ret = gc573_block_write_byte(c->io, &c->r->last, reg, c->r->expected);
	c->r->writes_started += c->r->last.started;
	if (reg == 15 && c->r->last.started)
		c->r->bank_verified = 0;
	if (ret)
		return ret;
	if (op[4])
		c->io->sleep_ms(c->io->ctx, op[4]);
	ret = input_read(c, reg);
	if (ret)
		return ret;
	c->r->observed = c->r->last.data[0];
	if ((c->r->observed ^ c->r->expected) & op[3])
		return -EIO;
	if (reg == 15) {
		c->r->bank = c->r->observed & 7;
		c->r->bank_verified = 1;
	}
	c->r->steps_completed++;
	return 0;
}

int gc573_receiver_input(const struct gc573_block_io *io,
			 struct gc573_signal_result *signal,
			 struct gc573_clock_result *clock,
			 struct gc573_timing_result *timing,
			 struct gc573_edid_result *edid,
			 struct gc573_ddc_result *ddc,
			 struct gc573_input_result *r)
{
	/* reg, write mask, value, verification mask, delay after write (ms). */
	static const unsigned char ops[][5] = {
		/* Startup policy 3d374: fields 1f4=1, 1f8=0, 1fc=4 (no 6b write). */
		{ 15, 7, 1, 255, 0 }, { 0xc0, 6, 2, 6, 0 },
		{ 0xc1, 2, 2, 2, 0 }, { 0xc1, 0x20, 0, 0x20, 0 }, { 15, 7, 0, 255, 0 },
		/* Port-zero branch of 439e8; other-port HPD helper has no HW access. */
		{ 15, 7, 0, 255, 0 }, { 0x35, 1, 0, 1, 0 }, { 15, 7, 0, 255, 0 },
		{ 15, 7, 3, 255, 0 }, { 0xe5, 0x1c, 0, 0x1c, 0 },
		{ 15, 7, 7, 255, 0 }, { 0xe5, 0x1c, 0, 0x1c, 0 },
		{ 15, 7, 0, 255, 0 }, { 15, 7, 0, 255, 0 },
		{ 0x25, 255, 0, 255, 0 }, { 0x26, 255, 0, 255, 0 },
		{ 0x27, 255, 0, 255, 0 }, { 0x2a, 255, 1, 255, 0 },
		{ 0x2d, 255, 255, 255, 0 }, { 0x2e, 255, 255, 255, 0 },
		{ 0x2f, 255, 255, 255, 0 }, { 0x32, 255, 0x3e, 255, 0 },
		{ 15, 7, 3, 255, 0 }, { 0xa8, 8, 8, 8, 0 },
		{ 15, 7, 7, 255, 0 }, { 0xa8, 8, 0, 8, 0 }, { 15, 7, 0, 255, 0 },
		{ 0xc5, 0x10, 0x10, 0, 1 }, { 0xc5, 0x10, 0, 0x10, 0 },
		/* EQ state-zero reset 40dbc, using startup default field 1cd=1. */
		{ 15, 7, 3, 255, 0 }, { 0x2c, 255, 0, 255, 0 },
		{ 0x2d, 255, 7, 255, 0 }, { 15, 7, 0, 255, 0 },
		{ 7, 255, 255, 0, 0 }, /* Interrupt acknowledgement, not storage. */
		{ 0x23, 255, 0xb0, 0xef, 1 }, { 0x23, 255, 0xa0, 255, 0 },
		{ 15, 7, 3, 255, 0 }, { 0x27, 255, 0x9f, 255, 0 },
		{ 0x28, 255, 0x9f, 255, 0 }, { 0x29, 255, 0x9f, 255, 0 },
		{ 0x22, 255, 0, 255, 0 }, { 0x4b, 0x80, 0, 0x80, 0 },
		{ 15, 7, 0, 255, 0 }, { 15, 7, 0, 255, 0 },
		/* Source-present state 2 disables receiver outputs before HPD. */
		{ 15, 7, 1, 255, 0 }, { 0xc5, 255, 255, 255, 0 },
		{ 0xc6, 255, 255, 255, 0 }, { 15, 7, 0, 255, 0 },
	};
	struct input_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_input_result) { 0 };
	r->prerequisite_error = gc573_receiver_ddc(io, signal, clock, timing, edid, ddc);
	if (r->prerequisite_error)
		return r->prerequisite_error;
	if (!(signal->port0 & 1))
		return -ENOLINK;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	r->bank_verified = 1;
	for (i = 0; i < ARRAY_SIZE(ops); i++) {
		ret = input_set(&c, ops[i]);
		if (ret)
			return ret;
	}
	r->setup_complete = 1;
	r->phase = 2;
	/* Confirm selected input and source power immediately before HPD. */
	ret = input_read(&c, 0x35);
	if (ret)
		return ret;
	if (r->last.data[0] & 1)
		return -EIO;
	ret = input_read(&c, 0x13);
	if (ret)
		return ret;
	if (!(r->last.data[0] & 1) || (r->last.data[0] & 0x40))
		return -ENOLINK;
	r->gpio_before = io->read(io->ctx, GC573_GPIO);
	if (r->gpio_before == 0xffffffff || r->gpio_before == 0xeeeeeeee ||
	    (r->gpio_before & 0x10c) != 0x108)
		return -EIO;
	if (io->time_ms(io->ctx) - c.start >= 15000)
		return -ETIMEDOUT;
	io->write(io->ctx, GC573_GPIO, r->gpio_before | 4);
	r->hpd_written = 1;
	r->gpio_after = io->read(io->ctx, GC573_GPIO);
	if (r->gpio_after != (r->gpio_before | 4))
		return -EIO;
	r->hpd_verified = 1;
	io->sleep_ms(io->ctx, 2);
	r->phase = 3;
	/* Observation only after assertion; no adaptive receiver programming. */
	c.start = io->time_ms(io->ctx);
	for (i = 0; i < ARRAY_SIZE(r->poll); i++) {
		io->sleep_ms(io->ctx, 100);
		ret = input_read(&c, 0x13);
		if (ret)
			return ret;
		r->poll[i][0] = r->last.data[0];
		ret = input_read(&c, 0x19);
		if (ret)
			return ret;
		r->poll[i][1] = r->last.data[0];
		r->samples++;
		if ((r->poll[i][0] & 9) == 9 && (r->poll[i][1] & 0x80)) {
			r->lock_seen = 1;
			break;
		}
	}
	r->phase = 4;
	r->post_attempted = 1;
	r->post_error = gc573_receiver_status(io, signal);
	return r->post_error;
}
