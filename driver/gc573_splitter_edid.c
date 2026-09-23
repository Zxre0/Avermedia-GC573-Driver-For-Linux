// SPDX-License-Identifier: GPL-2.0-only
/* Inspect resident splitter EDID without enabling DDC or changing hotplug. */
#include <linux/errno.h>
#include "gc573_block.h"

const unsigned char gc573_splitter_edid_regs[GC573_SPLITTER_EDID_REGS] = {
	0x34, 0x4b, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
};

int gc573_splitter_edid_read(const struct gc573_block_io *io,
			    struct gc573_splitter_result *identity,
			    struct gc573_splitter_link_result *link,
			    struct gc573_splitter_edid_result *r)
{
	static const unsigned char header[] = { 0, 255, 255, 255, 255, 255, 255, 0 };
	unsigned long start;
	unsigned int i, j;
	int ret;

	*r = (struct gc573_splitter_edid_result) { 0 };
	if (!io->time_ms || !io->wait_write)
		return -EINVAL;
	start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_splitter_link_status(io, identity, link);
	r->prerequisite_error = ret;
	r->transactions = link->transactions;
	if (ret)
		return ret;
	r->phase = 2;
	for (i = 0; i < GC573_SPLITTER_EDID_REGS; i++) {
		if (io->time_ms(io->ctx) - start >= 15000)
			return -ETIMEDOUT;
		r->last_reg = gc573_splitter_edid_regs[i];
		r->transactions++;
		ret = gc573_splitter_edid_control_read(io, &r->last, r->last_reg);
		if (ret)
			return ret;
		r->control[i] = r->last.data[0];
		r->control_valid |= 1U << i;
	}
	/* Require DDC disabled before changing the local SRAM mapping. */
	if ((r->control[2] & 0x13) != 3)
		return -EOPNOTSUPP;
	r->last_reg = 0x4b;
	if (io->time_ms(io->ctx) - start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	ret = gc573_splitter_edid_map(io, &r->last);
	r->writes_started = r->last.started;
	if (ret)
		return ret;
	if (io->time_ms(io->ctx) - start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	ret = gc573_splitter_edid_control_read(io, &r->last, 0x4b);
	if (ret)
		return ret;
	r->mapping_after = r->last.data[0];
	if (r->mapping_after != 0xd9)
		return -EIO;
	r->mapping_verified = 1;
	r->phase = 3;
	for (i = 0; i < sizeof(r->data); i += 4) {
		if (io->time_ms(io->ctx) - start >= 15000)
			return -ETIMEDOUT;
		r->last_reg = i;
		r->transactions++;
		ret = gc573_splitter_edid_memory_read(io, &r->last, i);
		if (ret)
			return ret;
		for (j = 0; j < 4; j++)
			r->data[i + j] = r->last.data[j];
		r->bytes_read += 4;
	}
	r->header_matches = 1;
	for (i = 0; i < sizeof(header); i++)
		if (r->data[i] != header[i])
			r->header_matches = 0;
	for (i = 0; i < sizeof(r->data); i++)
		r->checksum[i / 128] += r->data[i];
	r->phase = 4;
	r->complete = 1;
	return 0;
}
