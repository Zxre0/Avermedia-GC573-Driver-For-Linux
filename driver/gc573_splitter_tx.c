// SPDX-License-Identifier: GPL-2.0-only
/* Windows 0x14004fa71..0x14004fab7, then diagnostic port reads only. */
#include <linux/errno.h>
#include "gc573_block.h"

struct tx_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_tx_result *r;
	unsigned long start;
};

static int tx_read(struct tx_context *c, unsigned int address, unsigned int reg)
{
	c->r->last_address = address;
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	if (address == 0x2c)
		return gc573_splitter_read(c->io, &c->r->last, reg);
	if (address == 0x38)
		return gc573_splitter_rx_finish_read(c->io, &c->r->last, reg);
	if (address == 0x4b) {
		if (reg == 0x20)
			return gc573_splitter_tx_reset_read(c->io, &c->r->last);
		return gc573_splitter_map_read(c->io, &c->r->last, reg);
	}
	if (address >= 0x34 && address <= 0x37)
		return gc573_splitter_tx_port_read(c->io, &c->r->last, address - 0x34, reg);
	return -EINVAL;
}

int gc573_splitter_tx_prepare(const struct gc573_block_io *io,
			     struct gc573_splitter_result *identity,
			     struct gc573_splitter_clock_result *clock,
			     struct gc573_splitter_timing_result *timing,
			     struct gc573_splitter_map_result *map,
			     struct gc573_splitter_cal_result *cal,
			     struct gc573_splitter_setup_result *setup,
			     struct gc573_splitter_finish_result *finish,
			     struct gc573_splitter_tx_result *r)
{
	static const unsigned char regs[] = { 0x03, 0x01, 0x84, 0x86, 0x88 };
	struct tx_context c = { .io = io, .r = r };
	unsigned int i, j, address, reg;
	int ret;

	*r = (struct gc573_splitter_tx_result) { 0 };
	*finish = (struct gc573_splitter_finish_result) { 0 };
	*setup = (struct gc573_splitter_setup_result) { 0 };
	*cal = (struct gc573_splitter_cal_result) { 0 };
	*map = (struct gc573_splitter_map_result) { 0 };
	*timing = (struct gc573_splitter_timing_result) { 0 };
	*clock = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_finish(io, identity, clock, timing, map, cal, setup, finish);
	r->prerequisite_error = ret;
	r->transactions = finish->transactions;
	if (ret)
		return ret;
	r->phase = 2;
	ret = tx_read(&c, 0x2c, 0xf1);
	if (ret)
		return ret;
	if (r->last.data[0] != 0x97)
		return -EOPNOTSUPP;
	r->common_mapping_verified = 1;
	/* Fresh-start state has c5b=1 and RX state[4]=0: set c5 bit zero. */
	for (i = 0; i < 3; i++) {
		address = i ? 0x4b : 0x38;
		reg = i ? 0x20 : 0xc5;
		ret = tx_read(&c, address, reg);
		if (ret)
			return ret;
		r->expected = i ? (i == 1 ? 2 : 0) : r->last.data[0] | 1;
		if (io->time_ms(io->ctx) - c.start >= 15000)
			return -ETIMEDOUT;
		r->transactions++;
		if (i)
			ret = gc573_splitter_tx_reset_write(io, &r->last, r->expected);
		else
			ret = gc573_splitter_rx_finish_write(io, &r->last, reg, r->expected);
		r->writes_started += r->last.started;
		if (ret)
			return ret;
		/* Assertion may self-clear; verify c5 and reset deassertion. */
		if (i != 1) {
			ret = tx_read(&c, address, reg);
			if (ret)
				return ret;
			r->observed = r->last.data[0];
			if (i ? r->observed != 0 : !(r->observed & 1))
				return -EIO;
			r->steps_verified++;
		}
	}
	r->reset_complete = 1;
	r->phase = 3;
	/* Require every port mapping after the common reset before any port read. */
	for (i = 0; i < 4; i++) {
		ret = tx_read(&c, 0x4b, 0x2c + i);
		if (ret)
			return ret;
		r->mapping[i] = r->last.data[0];
		r->mapping_valid |= 1U << i;
		if (r->mapping[i] != 0x69 + i * 2)
			return -EOPNOTSUPP;
	}
	r->phase = 4;
	for (i = 0; i < 4; i++) {
		for (j = 0; j < sizeof(regs); j++) {
			ret = tx_read(&c, 0x34 + i, regs[j]);
			if (ret)
				return ret;
			r->ports[i][j] = r->last.data[0];
			r->port_valid[i] |= 1U << j;
		}
	}
	r->phase = 5;
	for (i = 0; i < 2; i++) {
		ret = tx_read(&c, i ? 0x2c : 0x38, 0x0f);
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
		ret = tx_read(&c, 0x38, i ? 0x19 : 0x13);
		if (ret)
			return ret;
		r->status[i] = r->last.data[0];
		r->status_valid |= 1U << i;
	}
	r->complete = 1;
	return 0;
}
