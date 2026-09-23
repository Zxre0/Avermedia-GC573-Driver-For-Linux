// SPDX-License-Identifier: GPL-2.0-only
/* Fresh-state TX paths: 0x14004fad0, 0x14004ed14, 0x140057158, 0x14004ef50. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/array_size.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct port_op {
	unsigned char reg, mask, value, verify;
};

static const struct port_op prefix[] = {
	{ 0x18, 0xdc, 0, 1 }, { 0x19, 7, 0, 1 }, { 0x1a, 255, 0, 1 },
	{ 0x1b, 255, 0, 1 }, { 0x1c, 255, 0, 1 }, { 0x84, 0x60, 0x60, 1 },
	{ 0x86, 8, 0, 1 }, { 0x88, 3, 3, 1 }, { 0x01, 0x26, 0x26, 0 },
};

static const struct port_op no_sink[] = {
	{ 0x1a, 2, 0, 1 }, { 0x19, 255, 0x0f, 0 }, { 0x1d, 255, 0xaf, 1 },
	{ 0x19, 255, 0x3f, 0 }, { 0x07, 255, 255, 0 }, { 0x1a, 2, 2, 1 },
};

static const struct port_op reset[] = {
	{ 0x01, 255, 6, 0 }, { 0x94, 1, 1, 0 }, { 0x94, 1, 0, 1 },
	{ 0x01, 255, 4, 0 }, { 0x01, 255, 0, 1 },
	{ 0x01, 0x20, 0x20, 0 }, { 0x01, 0x20, 0, 1 },
};

static const struct port_op suffix[] = {
	{ 0x18, 0x80, 0, 1 }, { 0x19, 255, 0, 1 }, { 0x1a, 255, 0, 1 },
	{ 0x1b, 255, 0, 1 }, { 0x1c, 255, 0, 1 },
	{ 0x35, 0x10, 0x10, 0 }, { 0x35, 0x10, 0, 1 },
};

/* Port 1/2 reset and static configuration, in vendor call order. */
static const struct port_op setup12[] = {
	{ 0xc1, 1, 1, 1 }, { 0x01, 1, 1, 0 }, { 0x01, 1, 0, 1 },
	{ 0x08, 0x1c, 0, 1 }, { 0x02, 2, 2, 1 }, { 0x41, 1, 0, 1 },
	{ 0xc0, 1, 1, 1 }, { 0x34, 0xc0, 0x80, 1 }, { 0x35, 3, 0, 1 },
	{ 0x3a, 0xfc, 0x90, 1 }, { 0x93, 255, 0x40, 1 },
	{ 0x94, 0x3e, 0x26, 1 }, { 0xc0, 0x10, 0x10, 1 },
	{ 0xc1, 4, 0, 1 }, { 0xc3, 15, 1, 1 }, { 0x18, 3, 3, 1 },
	{ 0x19, 255, 7, 1 }, { 0x1a, 255, 3, 1 },
	{ 0x1b, 255, 255, 1 }, { 0x1c, 255, 3, 1 },
	{ 0x88, 255, 0x54, 1 }, { 0x8a, 255, 0, 1 }, { 0x8b, 255, 7, 1 },
};

/* 0x14004f622..0x14004f84a: the shared tail after all four port routines. */
static const struct port_op tail_ports03[] = {
	{ 0x84, 255, 0x60, 1 }, { 0x86, 255, 0, 1 }, { 0x88, 255, 0x0b, 1 },
};

static const struct port_op tail_control[] = {
	{ 0x08, 15, 15, 0 }, { 0x0d, 255, 0, 1 }, { 0x6b, 0x3c, 0, 1 },
	{ 0x6c, 0x38, 0x20, 1 }, { 0x18, 0x10, 0x10, 1 },
	{ 0x0f, 1, 1, 1 },
	/* Bank one. */
	{ 0x10, 0x49, 0x41, 1 }, { 0x1d, 0x80, 0x80, 1 },
	{ 0x20, 0x78, 0x78, 1 }, { 0x0f, 1, 0, 1 },
	/* Bank zero: don't assume status/command writes retain their value. */
	{ 0x19, 0x3f, 0x0f, 0 }, { 0x2b, 255, 255, 0 },
	{ 0x2d, 255, 15, 0 }, { 0x2e, 255, 255, 0 }, { 0x30, 255, 15, 0 },
	{ 0x6d, 0x30, 0, 1 },
};

static const struct port_op tail_each_port[] = {
	{ 0x41, 1, 0, 1 }, { 0xc1, 1, 1, 1 }, { 0x88, 1, 1, 1 },
};

struct port_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_ports_result *r;
	unsigned long start;
};

static int port_read(struct port_context *c, unsigned int address, unsigned int reg)
{
	c->r->last_address = address;
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	if (address == 0x2c)
		return gc573_splitter_read(c->io, &c->r->last, reg);
	if (address == 0x4b && reg == 0x15)
		return gc573_splitter_tx_route_read(c->io, &c->r->last);
	if (address == 0x4b)
		return gc573_splitter_map_read(c->io, &c->r->last, reg);
	if (address == 0x38)
		return gc573_splitter_rx_finish_read(c->io, &c->r->last, reg);
	if (reg == 3)
		return gc573_splitter_tx_port_read(c->io, &c->r->last, address - 0x34, reg);
	return gc573_splitter_tx_control_read(c->io, &c->r->last, address - 0x34, reg);
}

static int port_write(struct port_context *c, unsigned int address,
		      const struct port_op *op)
{
	struct gc573_splitter_ports_result *r = c->r;
	int ret = port_read(c, address, op->reg);

	if (ret)
		return ret;
	r->expected = (r->last.data[0] & ~op->mask) | op->value;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	if (address == 0x2c)
		ret = gc573_splitter_write(c->io, &r->last, op->reg, r->expected);
	else if (address == 0x4b && op->reg == 0x15)
		ret = gc573_splitter_tx_route_write(c->io, &r->last, r->expected);
	else
		ret = gc573_splitter_tx_control_write(c->io, &r->last, address - 0x34,
						    op->reg, r->expected);
	r->writes_started += r->last.started;
	if (ret)
		return ret;
	if (op->verify) {
		ret = port_read(c, address, op->reg);
		if (ret)
			return ret;
		r->observed = r->last.data[0];
		if ((r->observed & op->mask) != op->value)
			return -EIO;
		r->steps_verified++;
	}
	r->steps_completed++;
	return 0;
}

static int port_ops(struct port_context *c, unsigned int address,
		    const struct port_op *ops, unsigned int count)
{
	unsigned int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = port_write(c, address, &ops[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static int port_reset_tail(struct port_context *c, unsigned int port)
{
	struct port_op op = { 0x0c, 255, 1U << (port + 4), 0 };
	int ret = port_ops(c, 0x34 + port, reset, ARRAY_SIZE(reset));

	if (ret)
		return ret;
	ret = port_write(c, 0x2c, &op);
	if (ret)
		return ret;
	op.value = 0;
	op.verify = 1;
	ret = port_write(c, 0x2c, &op);
	if (ret)
		return ret;
	return port_ops(c, 0x34 + port, suffix, ARRAY_SIZE(suffix));
}

int gc573_splitter_tx_ports(const struct gc573_block_io *io,
			   struct gc573_splitter_result *identity,
			   struct gc573_splitter_clock_result *clock,
			   struct gc573_splitter_timing_result *timing,
			   struct gc573_splitter_map_result *map,
			   struct gc573_splitter_cal_result *cal,
			   struct gc573_splitter_setup_result *setup,
			   struct gc573_splitter_finish_result *finish,
			   struct gc573_splitter_tx_result *tx,
			   struct gc573_splitter_ports_result *r, unsigned int extent)
{
	static const unsigned char regs[] = { 3, 1, 0x84, 0x86, 0x88 };
	static const unsigned char order[] = { 0, 3, 1, 2 };
	struct port_context c = { .io = io, .r = r };
	struct port_op op;
	unsigned int i, j, port, address;
	int ret;

	*r = (struct gc573_splitter_ports_result) { 0 };
	*tx = (struct gc573_splitter_tx_result) { 0 };
	if (!io->time_ms || extent > 2)
		return -EINVAL;
	r->port_count = extent ? 4 : 2;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_tx_prepare(io, identity, clock, timing, map, cal, setup, finish, tx);
	r->prerequisite_error = ret;
	r->transactions = tx->transactions;
	if (ret)
		return ret;
	r->phase = 2;
	ret = port_read(&c, 0x2c, 0xf1);
	if (ret)
		return ret;
	if (r->last.data[0] != 0x97)
		return -EOPNOTSUPP;
	for (i = 0; i < 4; i++) {
		ret = port_read(&c, 0x4b, 0x2c + i);
		if (ret)
			return ret;
		if (r->last.data[0] != 0x69 + i * 2)
			return -EOPNOTSUPP;
	}
	r->mapping_verified = 1;
	for (i = 0; i < r->port_count; i++) {
		port = order[i];
		address = 0x34 + port;
		r->port = port;
		if (i >= 2) {
			r->phase = 7;
			ret = port_ops(&c, address, setup12, ARRAY_SIZE(setup12));
			if (ret)
				return ret;
			/* 4ed14 changes flags, not TX state[port]; cold state stays zero. */
			ret = port_reset_tail(&c, port);
			if (ret)
				return ret;
		}
		r->phase = 3;
		op = (struct port_op) { 8, 1U << port, 0, 0 };
		ret = port_write(&c, 0x2c, &op);
		if (ret)
			return ret;
		ret = port_ops(&c, address, prefix, ARRAY_SIZE(prefix));
		if (ret)
			return ret;
		/* Fresh software port states are zero: skip the state-2/3 reset branches. */
		op = (struct port_op) { 0x0d, 3U << (port * 2), 0, 1 };
		ret = port_write(&c, 0x2c, &op);
		if (ret)
			return ret;
		r->phase = 4;
		for (j = 0; j < 2; j++) {
			ret = port_read(&c, 0x35 + j, 3);
			if (ret)
				return ret;
			r->sink[i][j] = r->last.data[0];
			r->sink_valid[i] |= 1U << j;
		}
		if (!((r->sink[i][0] | r->sink[i][1]) & 1)) {
			r->no_sink_mask |= 1U << port;
			ret = port_ops(&c, 0x2c, no_sink, ARRAY_SIZE(no_sink));
			if (ret)
				return ret;
		}
		r->phase = 5;
		ret = port_reset_tail(&c, port);
		if (ret)
			return ret;
		r->ports_complete |= 1U << port;
	}
	if (extent) {
		r->phase = 8;
		op = (struct port_op) { 0x15, 8, 8, 1 };
		ret = port_write(&c, 0x4b, &op);
		if (ret)
			return ret;
		r->common_enabled = 1;
	}
	if (extent == 2) {
		r->phase = 9;
		op = (struct port_op) { 3, 255, 3, 0 };
		ret = port_write(&c, 0x34, &op);
		if (ret)
			return ret;
		for (i = 0; i < 2; i++) {
			ret = port_ops(&c, i ? 0x37 : 0x34, tail_ports03,
				       ARRAY_SIZE(tail_ports03));
			if (ret)
				return ret;
		}
		ret = port_ops(&c, 0x2c, tail_control, ARRAY_SIZE(tail_control));
		if (ret)
			return ret;
		io->sleep_ms(io->ctx, 10);
		for (i = 0; i < 4; i++) {
			r->port = i;
			ret = port_ops(&c, 0x34 + i, tail_each_port, ARRAY_SIZE(tail_each_port));
			if (ret)
				return ret;
		}
		r->tail_complete = 1;
	}
	r->phase = 6;
	for (i = 0; i < r->port_count; i++) {
		for (j = 0; j < sizeof(regs); j++) {
			ret = port_read(&c, 0x34 + order[i], regs[j]);
			if (ret)
				return ret;
			r->snapshot[i][j] = r->last.data[0];
			r->snapshot_valid[i] |= 1U << j;
		}
	}
	for (i = 0; i < 2; i++) {
		ret = port_read(&c, i ? 0x2c : 0x38, 0x0f);
		if (ret)
			return ret;
		r->expected = 0;
		r->observed = r->last.data[0];
		if (r->observed)
			return -EIO;
		if (i)
			r->control_bank_verified = 1;
		else
			r->bank_verified = 1;
	}
	for (i = 0; i < 2; i++) {
		ret = port_read(&c, 0x38, i ? 0x19 : 0x13);
		if (ret)
			return ret;
		r->status[i] = r->last.data[0];
		r->status_valid |= 1U << i;
	}
	r->complete = 1;
	return 0;
}
