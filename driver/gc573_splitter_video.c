// SPDX-License-Identifier: GPL-2.0-only
/* TX1/TX2 clock, analog setup and bounded RGB8 output from the official driver. */
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "gc573_block.h"
#include "gc573_passthrough.h"

struct video_context {
	const struct gc573_block_io *io;
	struct gc573_splitter_video_result *r;
	unsigned long start;
	unsigned int port;
	const struct gc573_passthrough_edid *sink;
};

static int video_read(struct video_context *c, unsigned int address, unsigned int reg)
{
	struct gc573_splitter_video_result *r = c->r;

	r->last_address = address;
	r->last_reg = reg;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	if (address == 0x4b)
		return gc573_splitter_timer_read(c->io, &r->last, reg);
	if (address == 0x38)
		return gc573_splitter_video_rx_read(c->io, &r->last, reg);
	return gc573_splitter_video_tx_read(c->io, &r->last, c->port, reg);
}

static int video_set(struct video_context *c, unsigned int reg, unsigned int mask,
		     unsigned int value, unsigned int verify)
{
	struct gc573_splitter_video_result *r = c->r;
	int ret = video_read(c, 0x34 + c->port, reg);

	if (ret)
		return ret;
	r->expected = (r->last.data[0] & ~mask) | (value & mask);
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	ret = gc573_splitter_video_tx_write(c->io, &r->last, c->port, reg, r->expected);
	r->writes_started += r->last.started;
	if (ret || !verify)
		return ret;
	ret = video_read(c, 0x34 + c->port, reg);
	if (ret)
		return ret;
	r->observed = r->last.data[0];
	if ((r->observed & verify) != (r->expected & verify))
		return -EIO;
	r->steps_verified++;
	return 0;
}

static int video_counter(struct video_context *c, unsigned int *count)
{
	unsigned int lo;
	int ret = video_read(c, 0x34 + c->port, 6);

	if (ret)
		return ret;
	lo = c->r->last.data[0];
	ret = video_read(c, 0x34 + c->port, 7);
	if (ret)
		return ret;
	*count = 2 * (lo | ((c->r->last.data[0] & 15) << 8));
	return 0;
}

int gc573_splitter_video_rate(unsigned int reference_khz, unsigned int sum,
			       unsigned int exponent, unsigned int depth,
			       unsigned int *pixel_khz, unsigned int *link_khz)
{
	unsigned int denominator, rate;

	*pixel_khz = *link_khz = 0;
	if (reference_khz < 10000 || reference_khz > 26214 || exponent > 7 || depth > 15)
		return -ERANGE;
	denominator = sum / (10U << exponent);
	if (!denominator)
		return -ERANGE;
	rate = (reference_khz << 12) / denominator;
	*pixel_khz = rate;
	if ((depth & 3) == 1)
		rate = rate * 5 / 4;
	else if ((depth & 3) == 2)
		rate = rate * 3 / 2;
	if (!rate || rate >= 621000)
		return -ERANGE;
	*link_khz = rate;
	return 0;
}

static int video_rx_bank(struct video_context *c, unsigned int bank)
{
	struct gc573_splitter_video_result *r = c->r;
	int ret;

	r->last_address = 0x38;
	r->last_reg = 0x0f;
	if (c->io->time_ms(c->io->ctx) - c->start >= 15000)
		return -ETIMEDOUT;
	r->transactions++;
	ret = gc573_splitter_rx_control_write(c->io, &r->last, 0x0f, bank);
	r->writes_started += r->last.started;
	if (r->last.started)
		r->bank_verified = 0;
	if (ret)
		return ret;
	ret = video_read(c, 0x38, 0x0f);
	if (ret)
		return ret;
	r->expected = bank;
	r->observed = r->last.data[0];
	if (r->observed != bank)
		return -EIO;
	r->bank = bank;
	r->bank_verified = 1;
	r->steps_verified++;
	return 0;
}

/* Port-local HDMI 2.0 negotiation from 58830, bounded to the display's EDID.
 * Sink protocol version and TMDS_CONFIG are read before output enable.
 */
static int video_scdc(struct video_context *c)
{
	struct gc573_sink_result ddc={0};
	unsigned int high=c->r->link_khz>340000, value;
	int ret;
	if(!c->sink || c->port!=2) return -EINVAL;
	ret=video_set(c,0x83,8,high ? 8 : 0,8); if(ret) return ret;
	/* On this GC573 C0 bits 6/2 read as link status during negotiation:
     * writing 0x77 produced 0x33 before SCDC was configured. Verify the
     * writable enable bit here, then verify TMDS_CONFIG at the actual sink.
     */
	ret=video_set(c,0xc0,0x46,high ? 0x46 : 0,0x02); if(ret) return ret;
	c->io->sleep_ms(c->io->ctx,50);
	ret=video_set(c,0x3a,3,0,3); if(ret) return ret;
	if(!c->sink->scdc) return high ? -EOPNOTSUPP : 0;
	if(high) {
		value=0;
		ret=gc573_sink_scdc(c->io,&ddc,1,0,&value); if(ret) goto done;
		if(!value) { ret=-EOPNOTSUPP; goto done; }
		value=1;
		ret=gc573_sink_scdc(c->io,&ddc,2,1,&value); if(ret) goto done;
	}
	value=high ? 3 : 0;
	ret=gc573_sink_scdc(c->io,&ddc,0x20,1,&value); if(ret) goto done;
	value=0xff;
	ret=gc573_sink_scdc(c->io,&ddc,0x20,0,&value);
	if(!ret && (value&3)!=(high ? 3U : 0U)) ret=-EIO;
 done:
	c->r->transactions+=ddc.transactions;
	c->r->writes_started+=ddc.writes;
	return ret;
}

/* Restricted 8-bit RGB HDMI pass-through: 5415c/53e5c/552b8/5836a.
 * The display-matched path adds HDMI 2.0 scrambling; format conversion and
 * encrypted input remain unsupported.
 */
static int video_output(struct video_context *c)
{
	struct gc573_splitter_video_result *r = c->r;
	static const unsigned char ops[][4] = {
		{ 0xc0, 1, 1, 1 }, { 0xc1, 0xf0, 0, 0xb0 }, { 0xc1, 4, 0, 4 },
		{ 0x18, 0x0c, 0x0c, 0x0c }, { 0x85, 255, 0x19, 255 },
		{ 0x1a, 0x0b, 0x0b, 0x0b }, { 0xc0, 2, 0, 2 },
		{ 0xc1, 8, 0, 8 }, { 0xc1, 8, 8, 8 },
		{ 0xc2, 0x80, 0x80, 0x80 }, { 0xc3, 0x30, 0x30, 0x30 },
		{ 0x88, 3, 0, 3 },
	};
	unsigned int i;
	int ret;

	r->phase = 7;
	ret = video_rx_bank(c, 2);
	if (ret)
		return ret;
	ret = video_read(c, 0x38, 0x15);
	if (ret)
		return ret;
	r->avi_color = r->last.data[0];
	ret = video_rx_bank(c, 0);
	if (ret)
		return ret;
	ret = video_read(c, 0x38, 0xcf);
	if (ret)
		return ret;
	r->rx_cf = r->last.data[0];
	ret = video_read(c, 0x38, 0x13);
	if (ret)
		return ret;
	r->rx13 = r->last.data[0];
	r->format_valid = 1;
	if ((r->avi_color & 0x60) || (r->depth & 3) || (r->rx_cf & 0x20) ||
	    (r->rx13 & 0x13) != 0x13 ||
			(c->sink ? (r->link_khz > c->sink->max_tmds_khz ||
						(r->link_khz > 340000 && !c->sink->scdc)) : r->link_khz >= 150000))
		return -EOPNOTSUPP;
	if (c->sink) {
		unsigned char t[16];
		unsigned int w,h,ht,vt,limit,millihz;
		for(i=0;i<16;i++) {
			ret=video_read(c,0x38,0x9b+i); if(ret) return ret;
			t[i]=r->last.data[0];
		}
		w=t[2]|((t[3]&63)<<8); h=t[9]|((t[10]&63)<<8);
		ht=t[0]|((t[1]&63)<<8); vt=t[7]|((t[8]&63)<<8);
		if(!w || !h || w>=ht || h>=vt) return -ERANGE;
		limit=(w==640 && h==480) || (w==720 && (h==480 || h==576)) ? 60 :
			w==1280 && h==720 ? 120 : w==1920 && h==1080 ? 240 :
			w==2560 && h==1440 ? 144 : w==3840 && h==2160 ? 60 : 0;
		millihz=(unsigned long long)r->pixel_khz*1000000/(ht*vt);
		if(!limit || millihz>limit*1010U) return -EOPNOTSUPP;
	}
	r->phase = 8;
	for (i = 0; i < ARRAY_SIZE(ops); i++) {
		if (c->sink && i == 6)
			ret = video_scdc(c);
		else
			ret = video_set(c, ops[i][0], ops[i][1], ops[i][2], ops[i][3]);
		if (ret)
			return ret;
	}
	c->io->sleep_ms(c->io->ctx, 100);
	if (c->sink) { ret=video_scdc(c); if(ret) return ret; }
	ret = video_read(c, 0x34 + c->port, 3);
	if (ret)
		return ret;
	r->tx_status = r->last.data[0];
	if ((r->tx_status & 15) != 15)
		return -ENOLINK;
	/* The unencrypted runtime branch clears these two output-control bits. */
	ret = video_set(c, 0x91, 0x10, 0, 0x10);
	if (ret)
		return ret;
	ret = video_set(c, 0xc1, 1, 0, 1);
	if (ret)
		return ret;
	r->output_enabled = 1;
	return 0;
}

static int video_clock(const struct gc573_block_io *io,
			       struct gc573_splitter_result *identity,
			       struct gc573_splitter_link_result *link,
			       struct gc573_splitter_video_result *r, unsigned int configure, unsigned int port,
                               const struct gc573_passthrough_edid *sink)
{
	struct video_context c = { .io = io, .r = r, .port = port, .sink = sink };
	unsigned int i, exponent, initial, count, sum = 0;
	int ret;

	*r = (struct gc573_splitter_video_result) { 0 };
	if ((port != 1 && port != 2) || configure > 2 || (sink && (port != 2 || !sink->max_tmds_khz || sink->max_tmds_khz > 600000)) ||
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
	    !(link->rx[11] & 0x80) || (link->rx[17] & 0x13) != 2)
		return -ENOLINK;
	/* Recover the configured reference from the previously verified 10ms timer.
	 * This is not a new oscillator measurement and is never a fallback constant.
	 */
	for (i = 0; i < 3; i++) {
		ret = video_read(&c, 0x4b, 0x11 + i);
		if (ret)
			return ret;
		if (i == 2 && (r->last.data[0] & ~3U))
			return -EOPNOTSUPP;
		r->reference_ticks |= r->last.data[0] << (i * 8);
	}
	if (r->reference_ticks % 10 || r->reference_ticks < 100000)
		return -ERANGE;
	r->reference_khz = r->reference_ticks / 10;
	for (i = 0; i < 2; i++) {
		ret = video_read(&c, 0x34 + port, i ? 0x86 : 0x84);
		if (ret)
			return ret;
		if ((r->last.data[0] & (i ? 8 : 0xe0)) != (i ? 8 : 0x80))
			return -EOPNOTSUPP;
	}
	r->phase = 2;
	if (configure) {
		/* 575d0's first-SCDT branch, before its clock and analog helpers. */
		for (i = 0; i < 6; i++) {
			ret = video_read(&c, 0x34 + port, 0x10 + i);
			if (ret)
				return ret;
			r->irq_before[i] = r->last.data[0];
			r->irq_valid |= 1U << i;
			ret = video_set(&c, 0x10 + i, 255, i ? 255 : 0xc0, 0);
			if (ret)
				return ret;
		}
		ret = video_set(&c, 1, 255, 0x24, 0);
		if (ret)
			return ret;
		ret = video_set(&c, 1, 255, 0, 255);
		if (ret)
			return ret;
	}
	ret = video_set(&c, 7, 0x80, 0x80, 0);
	if (ret)
		return ret;
	io->sleep_ms(io->ctx, 1);
	ret = video_set(&c, 7, 0x80, 0, 0x80);
	if (ret)
		return ret;
	ret = video_counter(&c, &initial);
	if (ret)
		return ret;
	r->initial_count = initial;
	for (exponent = 7; exponent && initial >= (1U << (11 - exponent)); exponent--)
		;
	r->exponent = exponent;
	r->phase = 3;
	for (i = 0; i < 10; i++) {
		ret = video_set(&c, 7, 0xf0, 0x80 | (exponent << 4), 0);
		if (ret)
			return ret;
		ret = video_set(&c, 7, 0xf0, exponent << 4, 0xf0);
		if (ret)
			return ret;
		ret = video_counter(&c, &count);
		if (ret)
			return ret;
		r->counts[r->samples++] = count;
		sum += count;
	}
	ret = video_read(&c, 0x38, 0x98);
	if (ret)
		return ret;
	r->depth = r->last.data[0] >> 4;
	r->phase = 4;
	ret = gc573_splitter_video_rate(r->reference_khz, sum, exponent, r->depth,
				       &r->pixel_khz, &r->link_khz);
	if (ret)
		return ret;
	r->rate_valid = 1;
	ret = video_set(&c, 0xaf, 0xc0, 0, 0xc0);
	if (ret)
		return ret;
	if (configure) {
		unsigned int rate = r->link_khz;
		unsigned char analog87, analog89, analog8b;

		if (rate > 375000) {
			analog87 = 14;
			analog8b = 13;
			analog89 = 0x25;
		} else if (rate > 310000) {
			analog87 = 13;
			analog8b = 11;
			analog89 = 0x25;
		} else if (rate > 150000) {
			analog87 = analog8b = 9;
			analog89 = 0x21;
		} else {
			analog87 = analog8b = 3;
			analog89 = 0x80;
		}
		{
			const unsigned char ops[][4] = {
				{ 0x84, 7, rate > 100000 ? 4 : 3, 7 },
				{ 0x88, 4, rate > 162000 ? 4 : 0, 4 },
				{ 0x87, 31, analog87, 31 }, { 0x89, 0xbf, analog89, 0xbf },
				{ 0x8a, 15, 0, 15 }, { 0x8b, 15, analog8b, 15 },
			};

			r->phase = 5;
			for (i = 0; i < ARRAY_SIZE(ops); i++) {
				ret = video_set(&c, ops[i][0], ops[i][1], ops[i][2], ops[i][3]);
				if (ret)
					return ret;
			}
		}
		r->analog_complete = 1;
		io->sleep_ms(io->ctx, 100);
		r->phase = 6;
		{
			const unsigned char ops[][4] = {
				{ 1, 255, 6, 0 }, { 0x94, 1, 1, 0 }, { 0x94, 1, 0, 1 },
				{ 1, 255, 4, 0 }, { 1, 255, 0, 255 }, { 0x18, 0x80, 0x80, 0x80 },
			};

			for (i = 0; i < ARRAY_SIZE(ops); i++) {
				ret = video_set(&c, ops[i][0], ops[i][1], ops[i][2], ops[i][3]);
				if (ret)
					return ret;
			}
		}
		r->output_setup_complete = 1;
	}
	if (configure == 2) {
		ret = video_output(&c);
		if (ret)
			return ret;
	}
	r->complete = 1;
	return 0;
}

int gc573_splitter_video_clock(const struct gc573_block_io *io,
	struct gc573_splitter_result *identity, struct gc573_splitter_link_result *link,
	struct gc573_splitter_video_result *r, unsigned int configure, unsigned int port)
{ return video_clock(io,identity,link,r,configure,port,0); }

int gc573_splitter_video_external(const struct gc573_block_io *io,
	struct gc573_splitter_result *identity, struct gc573_splitter_link_result *link,
	struct gc573_splitter_video_result *r, const struct gc573_passthrough_edid *sink)
{ return sink ? video_clock(io,identity,link,r,2,2,sink) : -EINVAL; }

/* TX2 is the newly observed external sink; TX1 must remain untouched.
 * Called once before capture/audio registration, so the I2C engine is idle.
 * Unknown states fail closed, and an absent sink is not a capture error.
 */
int gc573_splitter_passthrough(const struct gc573_block_io *io,
			      struct gc573_passthrough_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	struct gc573_splitter_hpd_result activation;
	struct gc573_splitter_video_result video;
	struct gc573_block_result last;
	static const unsigned char regs[] = { 0x84, 0x86, 0xc0, 0xc1, 0x88, 0x91 };
	unsigned char values[ARRAY_SIZE(regs)];
	unsigned int i;
	int ret;

	*r = (struct gc573_passthrough_result) { .phase = 1 };
	ret = gc573_splitter_link_status(io, &identity, &link);
	if (ret)
		return ret;
	r->tx_status = link.tx[2];
	r->sink_present = !!(link.tx[2] & 1);
	if (!r->sink_present)
		return 0;
	if ((link.tx[2] & 7) != 7)
		return -ENOLINK;
	r->phase = 2;
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = gc573_splitter_video_tx_read(io, &last, 2, regs[i]);
		if (ret)
			return ret;
		values[i] = last.data[0];
	}
	if ((link.tx[2] & 15) == 15 && (values[0] & 0xe0) == 0x80 &&
	    (values[1] & 8) && (values[2] & 3) == 1 &&
	    (values[3] & 0xbd) == 8 && !(values[4] & 3) && !(values[5] & 0x10)) {
		r->preserved = r->enabled = 1;
		return 0;
	}
	r->phase = 3;
	if (values[0] == 0xe4 && !values[1]) {
		ret = gc573_splitter_port_activate(io, &identity, &link, &activation, 2);
		if (ret)
			return ret;
	} else if ((values[0] & 0xe0) != 0x80 || !(values[1] & 8)) {
		return -EOPNOTSUPP;
	}
	r->phase = 4;
	ret = gc573_splitter_video_clock(io, &identity, &link, &video, 2, 2);
	if (ret)
		return ret;
	r->tx_status = video.tx_status;
	r->enabled = video.output_enabled;
	return 0;
}
