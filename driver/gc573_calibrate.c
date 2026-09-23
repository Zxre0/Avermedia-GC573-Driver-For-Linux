// SPDX-License-Identifier: GPL-2.0-only
/* One CAOF attempt, from the GC573 B1 Windows startup; no fallback/retries. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"
#include "gc573_calibration_steps.h"

struct gc573_cal_context {
	const struct gc573_block_io *io;
	struct gc573_cal_result *result;
	unsigned long start;
};

static int cal_expired(struct gc573_cal_context *c)
{
	return c->io->time_ms(c->io->ctx) - c->start >= 15000;
}

static int cal_read(struct gc573_cal_context *c, unsigned int reg, unsigned int len)
{
	c->result->last_reg = reg;
	if (cal_expired(c))
		return -ETIMEDOUT;
	return gc573_block_read_registers(c->io, &c->result->last, reg, len);
}

static int cal_op(struct gc573_cal_context *c, const struct gc573_cal_op *op)
{
	struct gc573_cal_result *r = c->result;
	unsigned int value;
	int ret;

	if (cal_expired(c))
		return -ETIMEDOUT;
	if (op->delay_ms) {
		c->io->sleep_ms(c->io->ctx, op->delay_ms);
		r->steps++;
		return 0;
	}
	ret = cal_read(c, op->reg, 1);
	if (ret)
		return ret;
	value = (r->last.data[0] & ~op->mask) | (op->value & op->mask);
	r->last_value = value;
	if (cal_expired(c))
		return -ETIMEDOUT;
	ret = gc573_block_write_byte(c->io, &r->last, op->reg, value);
	r->writes_started += r->last.started;
	if (op->reg == 15 && r->last.started)
		r->bank_verified = 0;
	if (ret)
		return ret;
	if (op->reg == 15) {
		ret = cal_read(c, 15, 1);
		if (ret)
			return ret;
		if (r->last.data[0] != value)
			return -EIO;
		r->bank = value & 7;
		r->bank_verified = 1;
	}
	r->steps++;
	return 0;
}

int gc573_receiver_calibrate(const struct gc573_block_io *io,
			     struct gc573_signal_result *signal,
			     struct gc573_cal_result *r)
{
	struct gc573_cal_context c = { .io = io, .result = r };
	const struct gc573_cal_op result_banks[] = { { 15, 7, 3, 0 }, { 15, 7, 7, 0 } };
	unsigned int i, port;
	int ret;

	*r = (struct gc573_cal_result) { 0 };
	*signal = (struct gc573_signal_result) { 0 };
	if (!io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	ret = gc573_receiver_status(io, signal);
	if (ret)
		return ret;
	if (signal->bank || signal->controls[0].data[0] != 0xb1)
		return -ENODEV;
	/* Require the observed static-table markers before analog setup. */
	if (!(signal->controls[1].data[1] & 0x40) ||
	    signal->controls[1].data[2] != 0x10 ||
	    signal->controls[1].data[3] != 0xa0 || signal->controls[3].data[1] != 0xa0)
		return -EAGAIN;
	c.start = io->time_ms(io->ctx);
	r->bank_verified = 1;
	for (port = 0; port < 2; port++) {
		ret = cal_read(&c, port ? 0x0d : 0x08, 1);
		if (ret)
			return ret;
		r->initial_flags[port] = r->last.data[0];
		r->flags_valid |= 1U << port;
	}
	/* Never accept a prior calibration's sticky result as this attempt. */
	if ((r->initial_flags[0] | r->initial_flags[1]) & 0x30)
		return -EBUSY;
	r->preflight_complete = 1;
	r->phase = 1;
	for (i = 0; i < ARRAY_SIZE(gc573_cal_setup); i++) {
		ret = cal_op(&c, &gc573_cal_setup[i]);
		if (ret)
			return ret;
	}
	r->phase = 2;
	for (i = 0; i < 50; i++) {
		for (port = 0; port < 2; port++) {
			ret = cal_read(&c, port ? 0x0d : 0x08, 1);
			if (ret)
				return ret;
			r->flags[port] = r->last.data[0];
			r->flags_valid |= 1U << (port + 2);
		}
		r->poll_samples++;
		if ((r->flags[0] & 0x30) && (r->flags[1] & 0x30)) {
			r->completion_seen = 1;
			break;
		}
		io->sleep_ms(io->ctx, 10);
	}
	if (!r->completion_seen)
		r->poll_error = -ETIMEDOUT;
	r->phase = 3;
	/* Preserve the reference's read order, including the repeated 0x59. */
	for (port = 0; port < 2; port++) {
		ret = cal_op(&c, &result_banks[port]);
		if (ret)
			return ret;
		ret = cal_read(&c, 0x5a, 1);
		if (ret)
			return ret;
		r->values[port][1] = r->last.data[0];
		ret = cal_read(&c, 0x59, 1);
		if (ret)
			return ret;
		r->values[port][0] = r->last.data[0];
		ret = cal_read(&c, 0x59, 1);
		if (ret)
			return ret;
		r->values[port][2] = r->last.data[0];
		r->results_valid |= 1U << port;
	}
	r->phase = 4;
	/* Normal teardown also runs after no completion, if the bus still works. */
	for (i = 0; i < ARRAY_SIZE(gc573_cal_cleanup); i++) {
		ret = cal_op(&c, &gc573_cal_cleanup[i]);
		if (ret)
			return ret;
	}
	r->cleanup_complete = 1;
	r->phase = 5;
	r->post_attempted = 1;
	r->post_error = gc573_receiver_status(io, signal);
	return r->post_error ? r->post_error : r->poll_error;
}
