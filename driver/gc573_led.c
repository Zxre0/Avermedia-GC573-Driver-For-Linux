// SPDX-License-Identifier: GPL-2.0-only
/* Original RGB sequencer and generated palette; no vendor profile data.
 * Protocol researched from official GC573 Windows 13200/1422c/14260.
 */
#include <linux/errno.h>
#include "gc573_led.h"

/* Thirty equally spaced positions on the RGB color wheel. */
static unsigned int wheel(unsigned int step, unsigned int channel)
{
	static const unsigned char corners[7][3] = {
		{255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255},
		{0, 0, 255}, {255, 0, 255}, {255, 0, 0},
	};
	unsigned int segment = step / 5, fraction = step % 5;

	return (corners[segment][channel] * (5 - fraction) +
		corners[segment + 1][channel] * fraction) / 5;
}

int gc573_led_set(const struct gc573_block_io *io, struct gc573_led_result *r,
		  unsigned int mode, unsigned int color, unsigned int brightness)
{
	unsigned int i, channel, step, start, end, value, count = mode == 0 ? 30 : 1;

	*r = (struct gc573_led_result) { 0 };
	if (mode > 2 || color > 0xffffff || brightness > 100)
		return -EINVAL;
	if (io->read(io->ctx, 0) != 0x20201015 ||
	    io->read(io->ctx, 0x64) != 0x57300102)
		return -ENODEV;
	r->before = io->read(io->ctx, 0x804);
	io->write(io->ctx, 0x804, 0);
	io->sleep_ms(io->ctx, 1);
	if (io->read(io->ctx, 0x804))
		return -EIO;
	io->write(io->ctx, 0x800, 0x3af);
	r->divider = io->read(io->ctx, 0x800);
	if (r->divider != 0x3af)
		return -EIO;
	for (i = 0; i < 15; i++) {
		start = 3 + (i % 3) * count;
		end = start + count - 1;
		io->write(io->ctx, 0x808 + i * 8, start);
		io->write(io->ctx, 0x80c + i * 8, end);
		if (io->read(io->ctx, 0x808 + i * 8) != start ||
		    io->read(io->ctx, 0x80c + i * 8) != end)
			return -EIO;
		r->controls_verified++;
	}
	/* Reserved zero command, then channel-local repeated hold commands. */
	io->write(io->ctx, 0x2008, 0);
	r->commands_written = 1;
	for (channel = 0; channel < 3; channel++) {
		for (step = 0; step < count; step++) {
			value = mode == 0 ? wheel(step, channel) :
				(color >> (16 - 8 * channel)) & 255;
			value = mode == 2 ? 0 : value * brightness / 100;
			io->write(io->ctx, 0x2008 + 4 * r->commands_written,
				  0x86300000 | (value << 8) | value);
			r->commands_written++;
		}
	}
	io->read(io->ctx, 0x800);
	io->write(io->ctx, 0x804, 0x1f);
	r->after = io->read(io->ctx, 0x804);
	if (r->after != 0x1f)
		return -EIO;
	r->complete = 1;
	return 0;
}

int gc573_led_rgb(const struct gc573_block_io *io, struct gc573_led_result *r)
{
	return gc573_led_set(io, r, 0, 0xffffff, 100);
}
