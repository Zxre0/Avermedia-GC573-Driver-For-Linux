// SPDX-License-Identifier: GPL-2.0-only
/* External TX2 DDC EDID reads: official GC573 4cfd8/4d288/5503c.
 * EDID reads preserve source/video controls. The separate SCDC operation
 * below writes only HDMI 2.0 source version and TMDS configuration.
 */
#include "gc573_sink.h"
#include <linux/errno.h>

static int rd(const struct gc573_block_io *io, struct gc573_sink_result *r, unsigned int reg)
{
	r->last_reg = reg;
	r->transactions++;
	return gc573_splitter_ddc_read(io, &r->last, reg);
}
static int wr(const struct gc573_block_io *io, struct gc573_sink_result *r, unsigned int reg,
	      unsigned int value)
{
	int ret;
	/* This transport deliberately requires a completed read before each write.
	 * TX status is safe to read even when restoring after sink removal.
	 */
	ret = rd(io, r, 3);
	if (ret)
		return ret;
	r->last_reg = reg;
	r->transactions++;
	ret = gc573_splitter_ddc_write(io, &r->last, reg, value);
	r->writes += r->last.started;
	return ret;
}
static int set(const struct gc573_block_io *io, struct gc573_sink_result *r, unsigned int reg,
	       unsigned int value)
{
	int ret = wr(io, r, reg, value);
	if (ret)
		return ret;
	ret = rd(io, r, reg);
	return ret ? ret : r->last.data[0] == value ? 0 : -EIO;
}

int gc573_sink_read(const struct gc573_block_io *io, struct gc573_sink_result *r)
{
	static const unsigned char controls[] = {0x28, 0x19, 0x1d};
	static const unsigned char masks[] = {1, 4, 8};
	static const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	unsigned int i, j, chunk, limit = 128, sum;
	int ret;

	*r = (struct gc573_sink_result){.phase = 1};
	if (!io->sleep_ms || !io->wait_write || !io->time_ms)
		return -EINVAL;
	ret = gc573_splitter_link_status(io, &identity, &link);
	r->transactions = link.transactions;
	if (ret)
		return ret;
	r->sink_present = !!(link.tx[2] & 1);
	if (!r->sink_present)
		return -ENOLINK;
	for (i = 0; i < 3; i++) {
		ret = rd(io, r, controls[i]);
		if (ret)
			return ret;
		r->saved[i] = r->last.data[0];
		r->saved_valid |= 1U << i;
	}
	r->phase = 2;
	for (i = 0; i < 3; i++) {
		ret = set(io, r, controls[i], r->saved[i] | masks[i]);
		if (ret)
			return ret; /* Do not send cleanup transactions after a bus failure. */
	}
	r->phase = 3;
	for (chunk = 0; chunk < limit; chunk += 32) {
		const unsigned char commands[][2] = {
		    {0x2e, 9},	{0x29, 0xa0}, {0x2a, chunk & 255},
		    {0x2b, 32}, {0x2c, 0},    {0x2d, chunk >> 8},
		};
		ret = rd(io, r, 3);
		if (ret)
			return ret;
		if (!(r->last.data[0] & 1)) {
			ret = -ENOLINK;
			goto restore;
		}
		for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
			ret = wr(io, r, commands[i][0], commands[i][1]);
			if (ret)
				return ret;
		}
		ret = wr(io, r, 0x2e, 3);
		if (ret)
			return ret;
		for (i = 0; i < 20; i++) {
			io->sleep_ms(io->ctx, 15);
			ret = rd(io, r, 0x2f);
			if (ret)
				return ret;
			r->polls++;
			r->status = r->last.data[0];
			if (r->status & 0x38) {
				ret = -EIO;
				goto abort;
			}
			if (r->status & 0x80)
				break;
		}
		if (i == 20) {
			ret = -ETIMEDOUT;
			goto abort;
		}
		for (i = 0; i < 32; i++) {
			ret = rd(io, r, 0x30);
			if (ret)
				return ret;
			r->edid[r->bytes++] = r->last.data[0];
		}
		if (r->bytes % 128)
			continue;
		sum = 0;
		for (j = r->bytes - 128; j < r->bytes; j++)
			sum += r->edid[j];
		if (sum & 255) {
			ret = -EBADMSG;
			goto restore;
		}
		r->blocks++;
		if (r->bytes == 128) {
			for (j = 0; j < 8; j++)
				if (r->edid[j] != header[j]) {
					ret = -EBADMSG;
					goto restore;
				}
			limit = (r->edid[126] + 1) * 128;
			if (limit > sizeof(r->edid)) {
				ret = -E2BIG;
				goto restore;
			}
		}
	}
	ret = 0;
	goto restore;
abort:
	/* The documented abort command is bounded; never reset either video path. */
	r->cleanup_error = wr(io, r, 0x2e, 15);
	if (r->cleanup_error)
		return ret;
	for (i = 0; i < 20; i++) {
		io->sleep_ms(io->ctx, 1);
		r->cleanup_error = rd(io, r, 0x2f);
		if (r->cleanup_error)
			return ret;
		if (r->last.data[0] & 0xb8)
			break;
	}
	if (i == 20) {
		r->cleanup_error = -ETIMEDOUT;
		return ret;
	}
restore:
	r->phase = 4;
	for (i = 3; i; i--) {
		r->cleanup_error = set(io, r, controls[i - 1], r->saved[i - 1]);
		if (r->cleanup_error)
			return ret ? ret : r->cleanup_error;
	}
	r->restored = 1;
	r->complete = !ret;
	return ret;
}

/* Official 585d4/58704 SCDC transactions. Only version/configuration is
 * exposed; these HDMI 2.0 link controls are unrelated to HDCP authentication.
 */
int gc573_sink_scdc(const struct gc573_block_io *io, struct gc573_sink_result *r, unsigned int reg,
		    int write, unsigned int *value)
{
	unsigned int saved, i;
	int ret;
	if (!io->sleep_ms || !value || (write != 0 && write != 1) ||
	    (reg != 1 && reg != 2 && reg != 0x20 && reg != 0x21 && reg != 0x40) ||
	    (write && ((reg == 2 && *value != 1) || (reg == 0x20 && *value != 0 && *value != 3) ||
		       (reg != 2 && reg != 0x20))))
		return -EINVAL;
	ret = rd(io, r, 3);
	if (ret)
		return ret;
	if (!(r->last.data[0] & 1))
		return -ENOLINK;
	ret = rd(io, r, 0x28);
	if (ret)
		return ret;
	saved = r->last.data[0];
	ret = set(io, r, 0x28, saved & ~1U);
	if (ret)
		return ret;
	{
		const unsigned char ops[][2] = {
		    {0x2e, 9}, {0x29, 0xa8}, {0x2a, reg}, {0x2b, 1}, {0x2c, 0}};
		for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
			ret = wr(io, r, ops[i][0], ops[i][1]);
			if (ret)
				return ret;
		}
	}
	if (write) {
		ret = wr(io, r, 0x30, *value);
		if (ret)
			return ret;
	}
	ret = wr(io, r, 0x2e, write ? 1 : 0);
	if (ret)
		return ret;
	for (i = 0; i < 20; i++) {
		io->sleep_ms(io->ctx, 15);
		ret = rd(io, r, 0x2f);
		if (ret)
			return ret;
		r->status = r->last.data[0];
		r->polls++;
		if (r->status & 0x38) {
			ret = -EIO;
			break;
		}
		if (r->status & 0x80)
			break;
	}
	if (i == 20)
		ret = -ETIMEDOUT;
	if (ret) {
		r->cleanup_error = wr(io, r, 0x2e, 15);
		if (r->cleanup_error)
			return ret;
		for (i = 0; i < 20; i++) {
			io->sleep_ms(io->ctx, 1);
			r->cleanup_error = rd(io, r, 0x2f);
			if (r->cleanup_error)
				return ret;
			if (r->last.data[0] & 0xb8)
				break;
		}
		if (i == 20) {
			r->cleanup_error = -ETIMEDOUT;
			return ret;
		}
	} else if (!write) {
		ret = rd(io, r, 0x30);
		if (ret)
			return ret;
		*value = r->last.data[0];
	}
	r->cleanup_error = set(io, r, 0x28, saved);
	return ret ? ret : r->cleanup_error;
}
