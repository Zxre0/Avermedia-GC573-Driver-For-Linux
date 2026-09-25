/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_SCALER_H
#define GC573_SCALER_H
#include "gc573_block.h"
struct gc573_scaler_result {
	unsigned int complete, enabled, verified, phase_outputs;
	unsigned int input_width, input_height, width, height;
	unsigned int last_reg, expected, observed;
};
/* Only fixed 16:9 RGB downscale paths; no DMA or HDMI register writes. */
int gc573_scaler_configure(const struct gc573_block_io *io, struct gc573_scaler_result *r,
			   unsigned int iw, unsigned int ih, unsigned int ow, unsigned int oh);
#endif
