// SPDX-License-Identifier: GPL-2.0-only
/* Bounded cold input path from 50f0f, 4e0d0 and 5071c(1), without IRQ ACKs. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/array_size.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"

struct hpd_op {
	unsigned char address, reg, mask, value, verify;
};

static const struct hpd_op input_ops[] = {
	{ 0x2c, 0x0c, 4, 4, 0 }, { 0x2c, 0x10, 0x40, 0, 1 },
	{ 0x2c, 0x10, 0x40, 0x40, 1 }, { 0x2c, 0x0c, 4, 0, 1 },
	{ 0x38, 0x0f, 3, 0, 1 },
	{ 0x38, 0x0f, 3, 3, 1 }, { 0x38, 0x3a, 6, 2, 1 },
	{ 0x38, 0x0f, 3, 0, 1 }, { 0x38, 0x29, 1, 0, 1 },
	{ 0x38, 0x26, 0x0c, 0, 1 }, { 0x2c, 0x69, 0x2f, 0, 1 },
};

struct hpd_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_hpd_result *r;
	unsigned long start;
};

static int hpd_read(struct hpd_context *c, unsigned int address, unsigned int reg)
{
	struct gc573_splitter_hpd_result *r = c->r;

	r->last_address = address;
	r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	if (address == 0x35 || address == 0x36)
		return gc573_splitter_tx_control_read(c->io, &r->last, address - 0x34, reg);
	if (address == 0x2c)
		return gc573_splitter_read(c->io, &r->last, reg);
	if (reg == 0x34 || (reg >= 0xc5 && reg <= 0xca))
		return gc573_splitter_edid_control_read(c->io, &r->last, reg);
	if (reg == 0x3a)
		return gc573_splitter_rx_control_read(c->io, &r->last, reg);
	return gc573_splitter_rx_finish_read(c->io, &r->last, reg);
}

static int hpd_write(struct hpd_context *c, const struct hpd_op *op)
{
	struct gc573_splitter_hpd_result *r = c->r;
	unsigned int verify_mask = op->mask;
	int ret = hpd_read(c, op->address, op->reg);

	if (ret)
		return ret;
	r->expected = (r->last.data[0] & ~op->mask) | op->value;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	if (op->address == 0x35 || op->address == 0x36)
		ret = gc573_splitter_tx_control_write(c->io, &r->last, op->address - 0x34, op->reg, r->expected);
	else if (op->address == 0x2c)
		ret = gc573_splitter_write(c->io, &r->last, op->reg, r->expected);
	else if (op->reg == 0x34 || (op->reg >= 0xc5 && op->reg <= 0xca))
		ret = gc573_splitter_edid_control_write(c->io, &r->last, op->reg, r->expected);
	else if (op->reg == 0x3a)
		ret = gc573_splitter_rx_control_write(c->io, &r->last, op->reg, r->expected);
	else
		ret = gc573_splitter_rx_finish_write(c->io, &r->last, op->reg, r->expected);
	r->writes_started += r->last.started;
	if (op->address == 0x38 && op->reg == 0x0f && r->last.started)
		r->bank_verified = 0;
	if (ret)
		return ret;
	if (op->verify) {
		ret = hpd_read(c, op->address, op->reg);
		if (ret)
			return ret;
		r->observed = r->last.data[0];
		/* Hardware returned C1=c1 after the exact 575d0 write of 81.
		 * Bit 6 is not assumed to retain writes; its meaning is unconfirmed.
		 * Keep checking bits 7/5/4 and report the full observed byte.
		 */
		if ((op->address == 0x35 || op->address == 0x36) && op->reg == 0xc1 && op->mask == 0xf0) {
			verify_mask &= ~0x40U;
			r->c1_readback = r->observed;
			r->c1_valid = 1;
		}
		if ((r->observed & verify_mask) != (op->value & verify_mask) ||
		    (op->address == 0x38 && op->reg == 0x0f && r->observed != op->value))
			return -EIO;
		r->steps_verified++;
		if (op->address == 0x38 && op->reg == 0x0f) {
			r->bank = r->observed;
			r->bank_verified = 1;
		}
	}
	r->steps_completed++;
	return 0;
}

int gc573_splitter_input_hpd(const struct gc573_block_io *io,
			    struct gc573_splitter_result *identity,
			    struct gc573_splitter_link_result *link,
			    struct gc573_splitter_hpd_result *r)
{
	struct hpd_context c = { .io = io, .r = r };
	struct hpd_op op;
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_hpd_result) { 0 };
	if (!io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_link_status(io, identity, link);
	r->prerequisite_error = ret;
	r->transactions = link->transactions;
	if (ret)
		return ret;
	/* Require the observed cold HPD-low state and at least one downstream sink. */
	r->sink_mask = ((link->tx[1] & 1) ? 2 : 0) | ((link->tx[2] & 1) ? 4 : 0);
	if (!r->sink_mask || (link->rx[8] & 0x41) != 1 ||
	    link->rx[15] != 0xff || link->rx[16] || (link->rx[17] & 0x13) != 3)
		return -EOPNOTSUPP;
	r->bank_verified = 1;
	r->phase = 2;
	for (i = 0; i < ARRAY_SIZE(input_ops); i++) {
		ret = hpd_write(&c, &input_ops[i]);
		if (ret)
			return ret;
		/* 4e0d0 selects the HDMI branch from the live bank-zero RX13 value. */
		if (i == 4) {
			ret = hpd_read(&c, 0x38, 0x13);
			if (ret)
				return ret;
			if ((r->last.data[0] & 0x41) != 1)
				return -ENOLINK;
		}
	}
	r->phase = 3;
	ret = hpd_read(&c, 0x38, 0x13);
	if (ret)
		return ret;
	if (!(r->last.data[0] & 1))
		return -ENOLINK;
	op = (struct hpd_op) { 0x38, 0x0f, 3, 3, 1 };
	ret = hpd_write(&c, &op);
	if (ret)
		return ret;
	ret = hpd_read(&c, 0x38, 0xab);
	if (ret)
		return ret;
	r->ab_before = r->last.data[0];
	r->ab_valid = 1;
	if (r->ab_before != 0xca) {
		op = (struct hpd_op) { 0x38, 0xab, 255, 0xca, 1 };
		ret = hpd_write(&c, &op);
		if (ret)
			return ret;
	}
	op = (struct hpd_op) { 0x38, 0x0f, 3, 0, 1 };
	ret = hpd_write(&c, &op);
	if (ret)
		return ret;
	op = (struct hpd_op) { 0x38, 0x26, 255, 0, 1 };
	ret = hpd_write(&c, &op);
	if (ret)
		return ret;
	op = (struct hpd_op) { 0x38, 0x55, 255, 255, 1 };
	ret = hpd_write(&c, &op);
	if (ret)
		return ret;
	r->setup_complete = 1;
	r->phase = 4;
	for (i = 0; i < GC573_SPLITTER_HPD_SAMPLES; i++) {
		io->sleep_ms(io->ctx, 100);
		ret = hpd_read(&c, 0x38, 0x13);
		if (ret)
			return ret;
		r->poll[i][0] = r->last.data[0];
		ret = hpd_read(&c, 0x38, 0x19);
		if (ret)
			return ret;
		r->poll[i][1] = r->last.data[0];
		r->samples++;
		if ((r->poll[i][0] & 0x10) && (r->poll[i][1] & 0x80)) {
			r->lock_seen = 1;
			break;
		}
	}
	return 0;
}

/* Use the resident image with the official compose/copy checksum controls.
 * No SRAM replacement, TX activation, HDCP change or interrupt acknowledgement.
 */
int gc573_splitter_edid_enable(const struct gc573_block_io *io,
			       struct gc573_splitter_result *identity,
			       struct gc573_splitter_link_result *link,
			       struct gc573_splitter_edid_result *edid,
			       struct gc573_splitter_hpd_result *r)
{
	struct hpd_context c = { .io = io, .r = r };
	struct gc573_ddc_plan plan;
	struct hpd_op op = { 0x38, 0x0f, 3, 3, 1 };
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_hpd_result) { 0 };
	if (!io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_edid_read(io, identity, link, edid);
	r->prerequisite_error = ret;
	r->transactions = edid->transactions;
	if (ret)
		return ret;
	ret = gc573_ddc_plan(edid->data, &plan);
	if (ret)
		return ret;
	r->sink_mask = ((link->tx[1] & 1) ? 2 : 0) | ((link->tx[2] & 1) ? 4 : 0);
	if (!r->sink_mask || (link->rx[8] & 0x41) != 1 ||
	    link->rx[15] || link->rx[16] != 0xff || edid->control[0] != 1)
		return -EOPNOTSUPP;
	/* Keep the resident physical address; disable substitution as in 492db. */
	r->edid_checksum[0] = edid->data[127] - edid->checksum[0];
	r->edid_checksum[1] = edid->data[255] - edid->checksum[1];
	r->bank_verified = 1;
	r->phase = 2;
	ret = hpd_write(&c, &op);
	if (ret)
		return ret;
	ret = hpd_read(&c, 0x38, 0xab);
	if (ret)
		return ret;
	r->ab_before = r->last.data[0];
	r->ab_valid = 1;
	if (r->ab_before != 0xca) {
		op.value = 0;
		ret = hpd_write(&c, &op);
		return ret ? ret : -EOPNOTSUPP;
	}
	{
		const struct hpd_op ops[] = {
			{ 0x38, 0xab, 255, 0x4a, 1 }, { 0x38, 0xab, 255, 0, 1 },
			{ 0x38, 0xac, 255, 0, 1 }, { 0x38, 0x0f, 3, 0, 1 },
			{ 0x38, 0x26, 255, 255, 1 }, { 0x38, 0x55, 255, 0, 1 },
			{ 0x38, 0x34, 1, 0, 1 }, { 0x38, 0xc6, 255, 0, 1 },
			{ 0x38, 0xc7, 255, 0, 1 }, { 0x38, 0xc8, 255, 255, 1 },
			{ 0x38, 0xc9, 255, r->edid_checksum[0], 1 },
			{ 0x38, 0xca, 255, r->edid_checksum[1], 1 },
			{ 0x38, 0xc5, 1, 0, 1 }, { 0x38, 0x34, 1, 1, 1 },
		};

		for (i = 0; i < ARRAY_SIZE(ops); i++) {
			ret = hpd_write(&c, &ops[i]);
			if (ret)
				return ret;
		}
	}
	r->edid_configured = 1;
	io->sleep_ms(io->ctx, 500);
	r->phase = 3;
	ret = hpd_read(&c, 0x38, 0x13);
	if (ret)
		return ret;
	if ((r->last.data[0] & 0x41) != 1)
		return -ENOLINK;
	{
		const struct hpd_op ops[] = {
			{ 0x38, 0x0f, 3, 3, 1 }, { 0x38, 0xab, 255, 0xca, 1 },
			{ 0x38, 0x0f, 3, 0, 1 }, { 0x38, 0x26, 255, 0, 1 },
			{ 0x38, 0x55, 255, 255, 1 },
		};

		for (i = 0; i < ARRAY_SIZE(ops); i++) {
			ret = hpd_write(&c, &ops[i]);
			if (ret)
				return ret;
		}
	}
	r->setup_complete = 1;
	r->phase = 4;
	for (i = 0; i < GC573_SPLITTER_HPD_SAMPLES; i++) {
		io->sleep_ms(io->ctx, 100);
		ret = hpd_read(&c, 0x38, 0x13);
		if (ret)
			return ret;
		r->poll[i][0] = r->last.data[0];
		ret = hpd_read(&c, 0x38, 0x19);
		if (ret)
			return ret;
		r->poll[i][1] = r->last.data[0];
		r->samples++;
		if ((r->poll[i][0] & 0x10) && (r->poll[i][1] & 0x80)) {
			r->lock_seen = 1;
			break;
		}
	}
	return 0;
}

/* Unconditional six-write prefix of 575d0 for connected transmitter port 1.
 * The software-dependent continuation and runtime state machine remain separate.
 */
int gc573_splitter_port_activate(const struct gc573_block_io *io,
				 struct gc573_splitter_result *identity,
				 struct gc573_splitter_link_result *link,
				 struct gc573_splitter_hpd_result *r, unsigned int port)
{
	const struct hpd_op ops[] = {
		{ 0x2c, 0x08, port == 2 ? 4 : 2, port == 2 ? 4 : 2, 0 }, { 0x34 + port, 0xc1, 0xf0, 0x80, 1 },
		{ 0x34 + port, 0x84, 0xe0, 0x80, 1 }, { 0x34 + port, 0x86, 8, 8, 1 },
		{ 0x34 + port, 0x02, 1, 0, 1 }, { 0x34 + port, 0x19, 7, 7, 1 },
	};
	struct hpd_context c = { .io = io, .r = r };
	unsigned int i;
	int ret;

	*r = (struct gc573_splitter_hpd_result) { 0 };
	if ((port != 1 && port != 2) ||
	    !io->time_ms || !io->sleep_ms || !io->wait_write)
		return -EINVAL;
	c.start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_link_status(io, identity, link);
	r->prerequisite_error = ret;
	r->transactions = link->transactions;
	if (ret)
		return ret;
	if ((link->tx[port] & 7) != 7 || !(link->rx[8] & 0x10) ||
	    !(link->rx[11] & 0x80) || link->rx[15] || link->rx[16] != 0xff ||
	    (link->rx[17] & 0x13) != 2)
		return -EOPNOTSUPP;
	r->sink_mask = 1U << port;
	r->bank_verified = 1;
	/* Guard this cold activation against an already-running/unknown TX state. */
	for (i = 0; i < 2; i++) {
		ret = hpd_read(&c, 0x34 + port, i ? 0x86 : 0x84);
		if (ret)
			return ret;
		if (r->last.data[0] != (i ? 0 : 0xe4))
			return -EOPNOTSUPP;
	}
	r->phase = 2;
	for (i = 0; i < ARRAY_SIZE(ops); i++) {
		ret = hpd_write(&c, &ops[i]);
		if (ret)
			return ret;
	}
	r->setup_complete = 1;
	io->sleep_ms(io->ctx, 100);
	r->phase = 3;
	ret = hpd_read(&c, 0x38, 0x13);
	if (ret)
		return ret;
	r->poll[0][0] = r->last.data[0];
	ret = hpd_read(&c, 0x38, 0x19);
	if (ret)
		return ret;
	r->poll[0][1] = r->last.data[0];
	r->samples = 1;
	r->lock_seen = !!((r->poll[0][0] & 0x10) && (r->poll[0][1] & 0x80));
	return 0;
}
