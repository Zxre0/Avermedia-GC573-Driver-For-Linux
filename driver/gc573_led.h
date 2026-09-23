/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_LED_H
#define GC573_LED_H
#include "gc573_block.h"
struct gc573_led_result {
	unsigned int before, after, divider, controls_verified, commands_written, complete;
};
int gc573_led_rgb(const struct gc573_block_io *io, struct gc573_led_result *r);
int gc573_led_set(const struct gc573_block_io *io, struct gc573_led_result *r,
		  unsigned int mode, unsigned int color, unsigned int brightness);
#endif
