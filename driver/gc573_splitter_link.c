// SPDX-License-Identifier: GPL-2.0-only
/* Read-only event/status snapshot before implementing Windows runtime handlers. */
#include <linux/errno.h>
#include "gc573_block.h"

const unsigned char gc573_splitter_link_regs[GC573_SPLITTER_LINK_REGS] = {
	0x05, 0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x15, 0x19, 0x1a, 0x1b, 0x1d, 0x26, 0x55, 0xc5,
};

struct link_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_link_result *r;
	unsigned long start;
};

static int link_read(struct link_context *c, unsigned int address, unsigned int reg)
{
	struct gc573_splitter_link_result *r = c->r;

	r->last_address = address;
	r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	if (address == 0x2c)
		return gc573_splitter_read(c->io, &r->last, reg);
	if (address == 0x4b)
		return gc573_splitter_map_read(c->io, &r->last, reg);
	if (address == 0x38) {
		if (!reg || reg == 0x0f)
			return gc573_splitter_rx_read(c->io, &r->last, reg, reg ? 1 : 4);
		return gc573_splitter_rx_event_read(c->io, &r->last, reg);
	}
	return gc573_splitter_tx_port_read(c->io, &r->last, address - 0x34, reg);
}

int gc573_splitter_link_status(const struct gc573_block_io *io,
			       struct gc573_splitter_result *identity,
			       struct gc573_splitter_link_result *r)
{
	struct link_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_link_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_identify(io, identity);
	r->prerequisite_error = ret;
	r->transactions = identity->transactions;
	if (ret)
		return ret;
	if (identity->bank || identity->id[2] != 0x63 ||
	    (identity->gpio & ~0x404U) != 0x1f978)
		return -ENODEV;
	r->phase = 2;
	for (i = 0; i < 6; i++) {
		ret = link_read(&c, i < 2 ? 0x2c : 0x4b, i < 2 ? 0xf0 + i : 0x2a + i);
		if (ret)
			return ret;
		if (r->last.data[0] != (i < 2 ? (i ? 0x97 : 0x71) : 0x65 + i * 2))
			return -EOPNOTSUPP;
	}
	r->mapping_verified = 1;
	ret = link_read(&c, 0x38, 0x0f);
	if (ret)
		return ret;
	if (r->last.data[0])
		return -EOPNOTSUPP;
	ret = link_read(&c, 0x38, 0);
	if (ret)
		return ret;
	if (r->last.data[0] != 0x54 || r->last.data[1] != 0x49 ||
	    r->last.data[2] != 0x64 || r->last.data[3] != 0x66)
		return -ENODEV;
	r->rx_identity_verified = 1;
	r->phase = 3;
	ret = link_read(&c, 0x2c, 5);
	if (ret)
		return ret;
	r->irq_before = r->last.data[0];
	r->irq_valid = 1;
	for (i = 0; i < GC573_SPLITTER_LINK_REGS; i++) {
		ret = link_read(&c, 0x38, gc573_splitter_link_regs[i]);
		if (ret)
			return ret;
		r->rx[i] = r->last.data[0];
		r->rx_valid |= 1U << i;
	}
	for (i = 0; i < 4; i++) {
		ret = link_read(&c, 0x34 + i, 3);
		if (ret)
			return ret;
		r->tx[i] = r->last.data[0];
		r->tx_valid |= 1U << i;
	}
	ret = link_read(&c, 0x2c, 5);
	if (ret)
		return ret;
	r->irq_after = r->last.data[0];
	r->irq_valid |= 2;
	r->phase = 4;
	for (i = 0; i < 2; i++) {
		ret = link_read(&c, i ? 0x2c : 0x38, 0x0f);
		if (ret)
			return ret;
		if (r->last.data[0])
			return -EIO;
		r->banks_verified |= 1U << i;
	}
	r->complete = 1;
	return 0;
}
