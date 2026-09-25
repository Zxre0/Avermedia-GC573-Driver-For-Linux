// SPDX-License-Identifier: GPL-2.0-only
/* GC573 FPGA's 4-pixel-per-clock Xilinx scaler, six taps and 64 phases.
 * Register layout corroborated against the official Xilinx VPSS driver.
 * Filter weights are generated from the Lanczos formula by our own tool.
 */
#include "gc573_scaler.h"
#include "gc573_scaler_coefficients.h"
#include <linux/errno.h>
#define H 0x40000U
#define V 0x60000U
#define RESET 0x50000U

static int set(const struct gc573_block_io *io, struct gc573_scaler_result *r, unsigned int reg,
	       unsigned int value)
{
	r->last_reg = reg;
	r->expected = value;
	io->write(io->ctx, reg, value);
	r->observed = io->read(io->ctx, reg);
	if (r->observed != value)
		return -EIO;
	r->verified++;
	return 0;
}

int gc573_scaler_configure(const struct gc573_block_io *io, struct gc573_scaler_result *r,
			   unsigned int iw, unsigned int ih, unsigned int ow, unsigned int oh)
{
	const short (*filter)[6];
	unsigned int p, t, group, lane, rate, offset = 0, index = 0, outputs = 0;
	int ret;

	*r = (struct gc573_scaler_result){
	    .input_width = iw, .input_height = ih, .width = ow, .height = oh};
	if (!io || !io->read || !io->write || !io->sleep_ms)
		return -EINVAL;
	if (!((iw == 1920 && ih == 1080) || (iw == 1280 && ih == 720) ||
	      (iw == 2560 && ih == 1440)) ||
	    !((ow == 1920 && oh == 1080) || (ow == 1280 && oh == 720)) || ow > iw)
		return -EINVAL;
	/* Never reconfigure a live capture engine. */
	if (io->read(io->ctx, 0x1000) & 1)
		return -EBUSY;
	if (iw == ow) {
		r->complete = 1;
		return 0;
	}
	filter = iw * 2 == ow * 3 ? filter_3_2 : iw * 3 == ow * 4 ? filter_4_3 : filter_2_1;
	ret = set(io, r, RESET, 0);
	if (ret)
		return ret;
	io->sleep_ms(io->ctx, 5);
	ret = set(io, r, RESET, 3);
	if (ret)
		return ret;
	io->sleep_ms(io->ctx, 2);
	/* Coefficient memory is readable; verify every programmed word. */
	for (p = 0; p < 64; p++) {
		for (t = 0; t < 3; t++) {
			unsigned int word =
			    (unsigned short)filter[p][t * 2] |
			    ((unsigned int)(unsigned short)filter[p][t * 2 + 1] << 16);
			ret = set(io, r, V + 0x800 + (p * 3 + t) * 4, word);
			if (ret)
				return ret;
			ret = set(io, r, H + 0x800 + (p * 3 + t) * 4, word);
			if (ret)
				return ret;
		}
	}
	rate = (iw << 16) / ow;
	/* Each 10-bit lane: six phase bits, three input index bits, output enable.
	 * Clear the unused tail as well so an old wider mode cannot leak through.
	 */
	for (group = 0; group < 1024; group++) {
		unsigned long long word = 0;
		if (group < iw / 4) {
			for (lane = 0; lane < 4; lane++) {
				unsigned int phase = (offset >> 10) & 63, emit = 0;
				if (offset >= 65536) {
					offset -= 65536;
					index++;
				}
				if (offset < 65536 && outputs < ow) {
					offset += rate;
					emit = 1;
					outputs++;
				}
				word |= (unsigned long long)(phase | (index << 6) | (emit << 9))
					<< (lane * 10);
			}
			index &= 3;
		}
		ret = set(io, r, H + 0x2000 + group * 8, (unsigned int)word);
		if (ret)
			return ret;
		ret = set(io, r, H + 0x2004 + group * 8, (unsigned int)(word >> 32));
		if (ret)
			return ret;
	}
	r->phase_outputs = outputs;
	if (outputs != ow)
		return -ERANGE;
#define SET(reg, val)                                                                              \
	do {                                                                                       \
		ret = set(io, r, (reg), (val));                                                    \
		if (ret)                                                                           \
			return ret;                                                                \
	} while (0)
	SET(V + 0x10, ih);
	SET(V + 0x18, iw);
	SET(V + 0x20, oh);
	SET(V + 0x28, (ih << 16) / oh);
	SET(H + 0x10, oh);
	SET(H + 0x18, iw);
	SET(H + 0x20, ow);
	SET(H + 0x28, 0);
	SET(H + 0x30, rate);
#undef SET
	/* AP_START may self-clear: only stable geometry/memory is compared. */
	io->write(io->ctx, V, 0x81);
	io->write(io->ctx, H, 0x81);
	r->enabled = 1;
	r->complete = 1;
	return 0;
}
