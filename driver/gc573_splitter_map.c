// SPDX-License-Identifier: GPL-2.0-only
/* Original TX address setup following Windows 0x14004db60, plus RX reads. */
#include <linux/errno.h>
#include "gc573_block.h"

struct map_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_map_result *r;
	unsigned long start;
};

static int map_read(struct map_context *c, unsigned int address,
		    unsigned int reg, unsigned int length)
{
	c->r->last_address = address;
	c->r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	c->r->transactions++;
	if (address == 0x4b)
		return gc573_splitter_map_read(c->io, &c->r->last, reg);
	if (address == 0x38)
		return gc573_splitter_rx_read(c->io, &c->r->last, reg, length);
	return gc573_splitter_read(c->io, &c->r->last, reg);
}

int gc573_splitter_map(const struct gc573_block_io *io,
		      struct gc573_splitter_result *identity,
		      struct gc573_splitter_clock_result *clock,
		      struct gc573_splitter_timing_result *timing,
		      struct gc573_splitter_map_result *r)
{
	static const unsigned char regs[] = { 0x50, 0x2c, 0x2d, 0x2e, 0x2f };
	static const unsigned char values[] = { 0, 0x69, 0x6b, 0x6d, 0x6f };
	static const unsigned char rx_regs[] = { 0x0f, 0x00, 0x22, 0xc5 };
	static const unsigned char rx_lengths[] = { 1, 4, 3, 1 };
	struct map_context c = { .io = io, .r = r };
	unsigned int i, j, mask;
	int ret;

	*r = (struct gc573_splitter_map_result) { 0 };
	*timing = (struct gc573_splitter_timing_result) { 0 };
	*clock = (struct gc573_splitter_clock_result) { 0 };
	*identity = (struct gc573_splitter_result) { 0 };
	if (!io->time_ms)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_timing(io, identity, clock, timing);
	r->timing_error = ret;
	r->transactions = timing->transactions;
	if (ret)
		return ret;
	r->phase = 2;
	ret = map_read(&c, 0x2c, 0xf1, 1);
	if (ret)
		return ret;
	if (r->last.data[0] != 0x97)
		return -EOPNOTSUPP;
	r->tx_mapping_verified = 1;
	for (i = 0; i < sizeof(regs); i++) {
		ret = map_read(&c, 0x4b, regs[i], 1);
		if (ret)
			return ret;
		r->before[i] = r->last.data[0];
		r->before_valid |= 1U << i;
		mask = i ? 255 : 4;
		r->expected = (r->before[i] & ~mask) | values[i];
		if (io->time_ms(io->ctx) - c.start >= 15000)
			return -ETIMEDOUT;
		r->transactions++;
		ret = gc573_splitter_map_write(io, &r->last, regs[i], r->expected);
		r->writes_started += r->last.started;
		if (ret)
			return ret;
		ret = map_read(&c, 0x4b, regs[i], 1);
		if (ret)
			return ret;
		r->after[i] = r->last.data[0];
		r->after_valid |= 1U << i;
		r->observed = r->after[i];
		if ((r->observed & mask) != values[i])
			return -EIO;
		r->steps_verified++;
	}
	r->phase = 3;
	ret = map_read(&c, 0x2c, 0xf0, 1);
	if (ret)
		return ret;
	if (r->last.data[0] != 0x71)
		return -EOPNOTSUPP;
	r->rx_mapping_verified = 1;
	/* Snapshot only: do not reset or write the newly reached RX endpoint. */
	for (i = 0; i < sizeof(rx_regs); i++) {
		ret = map_read(&c, 0x38, rx_regs[i], rx_lengths[i]);
		if (ret)
			return ret;
		r->rx_valid |= 1U << i;
		switch (i) {
		case 0:
			r->rx_bank = r->last.data[0];
			if (r->rx_bank != 0)
				return -EOPNOTSUPP;
			r->rx_bank_verified = 1;
			break;
		case 1:
			for (j = 0; j < 4; j++)
				r->rx_id[j] = r->last.data[j];
			break;
		case 2:
			for (j = 0; j < 3; j++)
				r->rx_reset[j] = r->last.data[j];
			break;
		case 3:
			r->rx_c5 = r->last.data[0];
			break;
		}
	}
	r->phase = 4;
	ret = map_read(&c, 0x2c, 0x0f, 1);
	if (ret)
		return ret;
	r->expected = 0;
	r->observed = r->last.data[0];
	if (r->observed)
		return -EIO;
	r->bank_verified = 1;
	r->complete = 1;
	return 0;
}
