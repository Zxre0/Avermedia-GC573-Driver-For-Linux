// SPDX-License-Identifier: GPL-2.0-only
/* Read receiver EDID SRAM after the exact-GC573 address-mapping write.
 * Windows 0x14003e2d1 writes 0x4b=0xa9, and 0x140041edd addresses 0xa8.
 * This diagnostic sends no SRAM writes and does not change HPD.
 */
#include <linux/errno.h>
#include "gc573_block.h"

int gc573_receiver_edid_read(const struct gc573_block_io *io,
			     struct gc573_signal_result *signal,
			     struct gc573_clock_result *clock,
			     struct gc573_timing_result *timing,
			     struct gc573_edid_result *r)
{
	static const unsigned char header[] = { 0, 255, 255, 255, 255, 255, 255, 0 };
	unsigned long start;
	unsigned int i, j;
	int ret;

	*r = (struct gc573_edid_result) { 0 };
	r->prerequisite_error = gc573_receiver_timing(io, signal, clock, timing);
	if (r->prerequisite_error)
		return r->prerequisite_error;
	start = io->time_ms(io->ctx);
	r->phase = 1;
	ret = gc573_block_read_registers(io, &r->last, 0x4b, 1);
	if (ret)
		return ret;
	r->mapping_before = r->last.data[0];
	if (io->time_ms(io->ctx) - start >= 15000)
		return -ETIMEDOUT;
	ret = gc573_block_write_byte(io, &r->last, 0x4b, 0xa9);
	r->mapping_written = r->last.started;
	if (ret)
		return ret;
	if (io->time_ms(io->ctx) - start >= 15000)
		return -ETIMEDOUT;
	ret = gc573_block_read_registers(io, &r->last, 0x4b, 1);
	if (ret)
		return ret;
	r->mapping_after = r->last.data[0];
	if (r->mapping_after != 0xa9)
		return -EIO;
	r->mapping_verified = 1;
	r->phase = 2;
	for (i = 0; i < sizeof(r->data); i += 4) {
		r->last_offset = i;
		if (io->time_ms(io->ctx) - start >= 15000)
			return -ETIMEDOUT;
		ret = gc573_block_read_edid(io, &r->last, i, 4);
		if (ret)
			return ret;
		for (j = 0; j < 4; j++)
			r->data[i + j] = r->last.data[j];
		r->bytes_read += 4;
	}
	r->complete = 1;
	r->header_matches = 1;
	for (i = 0; i < sizeof(header); i++)
		if (r->data[i] != header[i])
			r->header_matches = 0;
	for (i = 0; i < sizeof(r->data); i++)
		r->checksum[i / 128] += r->data[i];
	r->phase = 3;
	r->post_attempted = 1;
	r->post_error = gc573_receiver_status(io, signal);
	return r->post_error;
}
