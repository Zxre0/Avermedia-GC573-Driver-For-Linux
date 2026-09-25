// SPDX-License-Identifier: GPL-2.0-only
/* HDMI OUT worker, isolated from host capture/DMA while in passthrough mode. */
#include "gc573_passthrough.h"
#include <linux/errno.h>
static int rxread(const struct gc573_block_io *io, struct gc573_passthrough_state *p,
		  unsigned int reg)
{
	if (reg == 0x4b || reg == 0x34 || (reg >= 0xc5 && reg <= 0xca))
		return gc573_splitter_edid_control_read(io, &p->last, reg);
	return gc573_splitter_passthrough_rx_read(io, &p->last, reg);
}
static int rxset(const struct gc573_block_io *io, struct gc573_passthrough_state *p,
		 unsigned int reg, unsigned int mask, unsigned int value)
{
	unsigned int target;
	int ret = rxread(io, p, reg);
	if (ret)
		return ret;
	target = (p->last.data[0] & ~mask) | (value & mask);
	if (reg == 0xf)
		p->bank_verified = 0;
	ret = gc573_splitter_passthrough_rx_write(io, &p->last, reg, target);
	p->writes += p->last.started;
	if (ret)
		return ret;
	ret = rxread(io, p, reg);
	if (ret)
		return ret;
	if ((p->last.data[0] & mask) != (target & mask))
		return -EIO;
	if (reg == 0xf) {
		p->bank = p->last.data[0];
		p->bank_verified = 1;
	}
	return 0;
}
int gc573_passthrough_program_edid(const struct gc573_block_io *io,
				   struct gc573_passthrough_state *p, const unsigned char edid[256])
{
	unsigned int i, j, sum;
	static const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
	int ret;
	for (i = 0; i < 8; i++)
		if (edid[i] != header[i])
			return -EBADMSG;
	if (edid[126] != 1)
		return -EINVAL;
	for (i = 0; i < 256; i += 128) {
		sum = 0;
		for (j = 0; j < 128; j++)
			sum += edid[i + j];
		if (sum & 255)
			return -EBADMSG;
	}
	/* Existing official HPD-low/DDC-disable sequence. SRAM only, never EEPROM. */
	static const unsigned char low[][3] = {{15, 255, 3},   {0xab, 255, 0x4a}, {0xab, 255, 0},
					       {0xac, 255, 0}, {15, 255, 0},	  {0x26, 255, 255},
					       {0x55, 255, 0}, {0x34, 1, 0},	  {0xc5, 1, 1}};
	for (i = 0; i < sizeof(low) / sizeof(low[0]); i++) {
		ret = rxset(io, p, low[i][0], low[i][1], low[i][2]);
		if (ret)
			return ret;
	}
	ret = rxread(io, p, 0x4b);
	if (ret)
		return ret;
	ret = gc573_splitter_edid_map(io, &p->last);
	p->writes += p->last.started;
	if (ret)
		return ret;
	ret = rxread(io, p, 0x4b);
	if (ret)
		return ret;
	if (p->last.data[0] != 0xd9)
		return -EIO;
	ret = rxread(io, p, 0xc5);
	if (ret)
		return ret;
	if ((p->last.data[0] & 0x13) != 3)
		return -EOPNOTSUPP;
	for (i = 0; i < 256; i += 4) {
		ret = gc573_splitter_edid_memory_read(io, &p->last, i);
		if (ret)
			return ret;
		if (!p->prior_valid)
			for (j = 0; j < 4; j++)
				p->prior_edid[i + j] = p->last.data[j];
	}
	if (!p->prior_valid) {
		for (i = 0; i < 256; i += 128) {
			sum = 0;
			for (j = 0; j < 127; j++)
				sum += p->prior_edid[i + j];
			p->prior_edid[i + 127] = -sum;
		}
		p->prior_valid = 1;
	}
	for (i = 0; i < 256; i++) {
		if (i == 127 || i == 255)
			continue; /* Hardware substitutes C9/CA on DDC. */
		ret = rxread(io, p, 0xc5);
		if (ret)
			return ret;
		if ((p->last.data[0] & 0x13) != 3)
			return -EIO;
		ret = gc573_splitter_edid_memory_write(io, &p->last, i, edid[i]);
		p->writes += p->last.started;
		if (ret)
			return ret;
		p->edid_written++;
	}
	for (i = 0; i < 256; i += 4) {
		ret = gc573_splitter_edid_memory_read(io, &p->last, i);
		if (ret)
			return ret;
		for (j = 0; j < 4; j++) {
			/* On this revision the SRAM checksum slots read as zero; the
			 * source sees the separately verified C9/CA checksum registers.
			 */
			if (i + j == 127 || i + j == 255) {
				if (p->last.data[j] && p->last.data[j] != edid[i + j])
					return -EIO;
			} else if (p->last.data[j] != edid[i + j])
				return -EIO;
		}
	}
	{
		const unsigned char controls[][3] = {{0xc6, 255, 0},
						     {0xc7, 255, 0},
						     {0xc8, 255, 255},
						     {0xc9, 255, edid[127]},
						     {0xca, 255, edid[255]},
						     {0xc5, 1, 0},
						     {0x34, 1, 1}};
		for (i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
			ret = rxset(io, p, controls[i][0], controls[i][1], controls[i][2]);
			if (ret)
				return ret;
		}
	}
	p->edid_verified = 1;
	io->sleep_ms(io->ctx, 500);
	{
		const unsigned char high[][3] = {{15, 255, 3},
						 {0xab, 255, 0xca},
						 {15, 255, 0},
						 {0x26, 255, 0},
						 {0x55, 255, 255}};
		for (i = 0; i < sizeof(high) / sizeof(high[0]); i++) {
			ret = rxset(io, p, high[i][0], high[i][1], high[i][2]);
			if (ret)
				return ret;
		}
	}
	return 0;
}
static int snapshot(const struct gc573_block_io *io, struct gc573_passthrough_state *p)
{
	unsigned int i, ht, vt;
	int ret = gc573_splitter_link_status(io, &p->identity, &p->link);
	if (ret)
		return ret;
	if (!(p->link.tx[2] & 1) || !(p->link.rx[8] & 0x10) || !(p->link.rx[11] & 0x80)) {
		p->active = p->configured = p->stable = 0;
		p->video.waiting_link = 0;
		p->measured.complete = p->scdc_status_valid = 0;
		return -EAGAIN;
	}
	ret = rxread(io, p, 0x98);
	if (ret)
		return ret;
	p->snapshot[0] = p->last.data[0];
	for (i = 1; i < 17; i++) {
		ret = rxread(io, p, 0x9a + i);
		if (ret)
			return ret;
		p->snapshot[i] = p->last.data[0];
	}
	ret = rxset(io, p, 15, 255, 2);
	if (ret)
		return ret;
	ret = rxread(io, p, 0x15);
	if (ret)
		return ret;
	p->snapshot[17] = p->last.data[0];
	ret = rxset(io, p, 15, 255, 0);
	if (ret)
		return ret;
	p->snapshot_valid = 1;
	for (i = 0; i < 18; i++)
		if (p->snapshot[i] != p->previous[i])
			break;
	if (i != 18) {
		for (i = 0; i < 18; i++)
			p->previous[i] = p->snapshot[i];
		p->active = p->configured = p->stable = 0;
		p->video.waiting_link = 0;
		p->measured.complete = p->scdc_status_valid = 0;
		return -EAGAIN;
	}
	p->width = p->snapshot[3] | ((p->snapshot[4] & 63) << 8);
	p->height = p->snapshot[10] | ((p->snapshot[11] & 63) << 8);
	ht = p->snapshot[1] | ((p->snapshot[2] & 63) << 8);
	vt = p->snapshot[8] | ((p->snapshot[9] & 63) << 8);
	if (!p->width || !p->height || p->width >= ht || p->height >= vt)
		return -EAGAIN;
	p->stable = 1;
	if (p->configured) {
		if (!p->measured.complete || (p->scaled && !(p->polls % 4))) {
			unsigned int previous_rate = p->measured.pixel_khz;

			ret = gc573_splitter_video_clock(io, &p->identity, &p->link, &p->measured,
							 0, 2);
			if (gc573_splitter_video_link_wait(&p->measured, ret)) {
				p->active = p->configured = 0;
				return -EAGAIN;
			}
			if (ret)
				return ret;
			p->millihz =
			    (unsigned long long)p->measured.pixel_khz * 1000000 / (ht * vt);
			/* Some 60/120 Hz timings share identical totals. Detect their
			 * clock change even when the timing-register snapshot is equal.
			 */
			if (p->scaled && previous_rate &&
			    (p->measured.pixel_khz > previous_rate * 110U / 100U ||
			     p->measured.pixel_khz < previous_rate * 90U / 100U)) {
				p->active = p->configured = 0;
				return -EAGAIN;
			}
			if (p->advertised.scdc) {
				ret = gc573_sink_scdc(io, &p->sink, 0x21, 0, &p->scdc_status);
				if (ret)
					return ret;
				ret = gc573_sink_scdc(io, &p->sink, 0x40, 0, &p->sink_lock);
				if (ret)
					return ret;
				p->scdc_status_valid = 1;
			}
		}
		return 0;
	}
	/* RGB8 SDR only, exactly the format advertised by our filtered EDID. */
	if ((p->snapshot[0] & 0xf2) || (p->snapshot[17] & 0x60)) {
		p->format_rejected = 1;
		p->format_waits++;
		return p->scaled ? -EAGAIN : -EOPNOTSUPP;
	}
	p->format_rejected = 0;
	ret = gc573_splitter_video_tx_read(io, &p->last, 2, 0x84);
	if (ret)
		return ret;
	i = p->last.data[0];
	ret = gc573_splitter_video_tx_read(io, &p->last, 2, 0x86);
	if (ret)
		return ret;
	if (i == 0xe4 && p->last.data[0] == 0) {
		ret = gc573_splitter_port_activate(io, &p->identity, &p->link, &p->hpd, 2);
		if (ret)
			return ret;
	} else if ((i & 0xe0) != 0x80 || !(p->last.data[0] & 8))
		return -EOPNOTSUPP;
	ret = p->video.waiting_link ?
		gc573_splitter_video_resume(io, &p->identity, &p->link, &p->video, 2, &p->advertised) :
		gc573_splitter_video_external(io, &p->identity, &p->link, &p->video, &p->advertised);
	if (ret == -EAGAIN)
		return ret;
	if (gc573_splitter_video_link_wait(&p->video, ret))
		return -EAGAIN;
	if (p->scaled && gc573_splitter_video_format_wait(&p->video, ret)) {
		p->format_rejected = 1;
		p->format_waits++;
		return -EAGAIN;
	}
	if (ret)
		return ret;
	p->millihz = (unsigned long long)p->video.pixel_khz * 1000000 / (ht * vt);
	p->format_rejected = 0;
	p->configured = p->active = 1;
	p->changes++;
	return 0;
}
int gc573_passthrough_poll(const struct gc573_block_io *io, struct gc573_passthrough_state *p)
{
	int ret;
	if (p->error)
		return p->error;
	p->polls++;
	switch (p->phase) {
	case 0:
		ret = gc573_sink_read(io, &p->sink);
		if (ret == -ENOLINK && !p->sink.writes)
			ret = -EAGAIN;
		if (!ret)
			ret = p->scaled ? gc573_scaled_edid(p->sink.edid, p->sink.bytes, &p->advertised) :
				gc573_passthrough_edid(p->sink.edid, p->sink.bytes, &p->advertised);
		break;
	case 1:
		ret = gc573_splitter_link_status(io, &p->identity, &p->link);
		if (ret)
			break;
		if (!(p->link.rx[8] & 1)) {
			ret = -EAGAIN;
			break;
		}
		if (p->link.rx[15] == 0xff && !p->link.rx[16] && (p->link.rx[17] & 0x13) == 3)
			ret = gc573_splitter_input_hpd(io, &p->identity, &p->link, &p->hpd);
		else if (p->link.rx[15] || p->link.rx[16] != 0xff || (p->link.rx[17] & 0x13) != 2)
			ret = -EOPNOTSUPP;
		break;
	case 2:
		ret = gc573_passthrough_program_edid(io, p, p->advertised.data);
		break;
	case 3:
		ret = snapshot(io, p);
		break;
	default:
		ret = -EINVAL;
	}
	p->waiting = ret == -EAGAIN;
	if (ret == -EAGAIN)
		return 0;
	if (ret) {
		p->active = 0;
		return p->error = ret;
	}
	if (p->phase < 3)
		p->phase++;
	return 0;
}

/* Restore the pre-test SRAM image on a normal mode switch. If a transport has
 * failed, leave the stopped state visible instead of replaying unknown writes.
 */
int gc573_passthrough_restore(const struct gc573_block_io *io, struct gc573_passthrough_state *p)
{
	unsigned int value;
	int ret;
	if (!p->prior_valid || !p->edid_written)
		return 0;
	if (p->error && io->read(io->ctx, GC573_BLOCK_STATUS) != GC573_BLOCK_READ_DONE &&
	    io->read(io->ctx, GC573_BLOCK_STATUS) != GC573_BLOCK_WRITE_DONE)
		return p->error;
	ret = gc573_passthrough_program_edid(io, p, p->prior_edid);
	if (ret)
		return ret;
	/* Both HDMI transmitters must leave the high TMDS ratio on a return
	 * to conservative capture. The internal receiver's SCDC is independent
	 * of the physical monitor's SCDC and survives a TX-only reset.
	 */
	for (unsigned int port = p->scaled ? 1 : 2; port <= 2; port++) {
		struct gc573_sink_result internal = {.port = 1};
		struct gc573_sink_result *sink = port == 1 ? &internal : &p->sink;

		if (port == 1 || p->advertised.scdc) {
			value = 0;
			ret = gc573_sink_scdc(io, sink, 0x20, 1, &value);
			if (ret) return ret;
		}
		ret = gc573_splitter_video_tx_read(io, &p->last, port, 0xc0);
		if (ret) return ret;
		value = p->last.data[0] & ~0x46U;
		ret = gc573_splitter_video_tx_write(io, &p->last, port, 0xc0, value);
		if (ret) return ret;
		ret = gc573_splitter_video_tx_read(io, &p->last, port, 0x83);
		if (ret) return ret;
		value = p->last.data[0] & ~8U;
		ret = gc573_splitter_video_tx_write(io, &p->last, port, 0x83, value);
		if (ret) return ret;
	}

	io->sleep_ms(io->ctx, 1000);
	return 0;
}
