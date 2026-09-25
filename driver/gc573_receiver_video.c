// SPDX-License-Identifier: GPL-2.0-only
/* Fixed receiver video snapshot: official 3adf0 and 3b09c. */
#include <linux/errno.h>
#include "gc573_block.h"
#include "gc573_modes.h"

static int rv_read(const struct gc573_block_io *io,
		   struct gc573_receiver_video_result *r, unsigned int reg)
{
	r->last_reg = reg;
	r->transactions++;
	return gc573_block_read_registers(io, &r->last, reg, 1);
}

static int rv_bank(const struct gc573_block_io *io,
		   struct gc573_receiver_video_result *r, unsigned int bank)
{
	unsigned int value;
	int ret = rv_read(io, r, 15);

	if (ret)
		return ret;
	value = (r->last.data[0] & ~7U) | bank;
	r->transactions++;
	ret = gc573_block_write_byte(io, &r->last, 15, value);
	r->writes_started += r->last.started;
	if (r->last.started)
		r->bank_verified = 0;
	if (ret)
		return ret;
	ret = rv_read(io, r, 15);
	if (ret)
		return ret;
	if (r->last.data[0] != value)
		return -EIO;
	r->bank = bank;
	r->bank_verified = 1;
	return 0;
}

/* Masked writes are confined to this fixed, traced table. */
static int rv_set(const struct gc573_block_io *io,
		  struct gc573_receiver_video_result *r, const unsigned char op[5])
{
	unsigned int value;
	int ret = rv_read(io, r, op[0]);

	if (ret)
		return ret;
	value = (r->last.data[0] & ~op[1]) | (op[2] & op[1]);
	r->expected = value;
	r->transactions++;
	ret = gc573_block_write_byte(io, &r->last, op[0], value);
	r->writes_started += r->last.started;
	if (op[0] == 15 && r->last.started)
		r->bank_verified = 0;
	if (ret)
		return ret;
	if (op[4])
		io->sleep_ms(io->ctx, op[4]);
	ret = rv_read(io, r, op[0]);
	if (ret)
		return ret;
	r->observed = r->last.data[0];
	if ((r->observed ^ value) & op[3])
		return -EIO;
	if (op[0] == 15) {
		r->bank = r->observed & 7;
		r->bank_verified = 1;
	}
	r->steps_verified++;
	return 0;
}

static int rv_output(const struct gc573_block_io *io,
		     struct gc573_receiver_video_result *r)
{
	/* 42134: RGB, <=1920 pixels, <=150 MHz; 3d374/3d280 single TTL.
	 * 42cac: original RGB pass-through policy, no colorspace conversion.
	 * 3c768/3d458: video reset then output enable, with mute released last.
	 */
	static const unsigned char ops[][5] = {
		{ 0x64, 4, 0, 4, 0 }, { 0x64, 2, 2, 0, 0 }, { 0x64, 2, 0, 2, 0 },
		{ 15, 7, 5, 255, 0 }, { 0x20, 0x40, 0x40, 0x40, 0 },
		{ 15, 7, 1, 255, 0 }, { 0xc0, 1, 0, 1, 0 },
		{ 0xc0, 6, 2, 6, 0 }, { 0xc1, 2, 0, 2, 0 }, { 0xc1, 0x20, 0, 0x20, 0 },
		{ 15, 7, 5, 255, 0 }, { 0xd1, 1, 0, 1, 0 },
		{ 0xd1, 12, 4, 12, 0 }, { 0xda, 0x10, 0, 0x10, 0 }, { 0xd0, 255, 0xf3, 255, 0 },
		{ 15, 7, 1, 255, 0 }, { 0xbd, 0x30, 0, 0x30, 0 }, { 0xbe, 255, 0, 255, 0 },
		{ 0xfe, 0x10, 0x10, 0x10, 0 }, { 0xc4, 255, 0, 255, 0 },
		{ 15, 7, 0, 255, 0 }, { 0x64, 4, 0, 4, 0 },
		{ 0x64, 2, 2, 0, 0 }, { 0x64, 2, 0, 2, 0 },
		{ 15, 7, 1, 255, 0 }, { 0xb0, 1, 0, 1, 0 },
		{ 15, 7, 0, 255, 0 }, { 0x6b, 0x3f, 0, 0x3f, 0 },
		{ 0x6e, 255, 0xa0, 255, 0 }, { 15, 7, 1, 255, 0 },
		{ 0x86, 255, 0, 255, 0 }, { 15, 7, 0, 255, 0 },
		{ 0x6c, 3, 0, 3, 0 }, { 15, 7, 1, 255, 0 },
		{ 0x85, 255, 0, 255, 0 }, { 15, 7, 0, 255, 0 },
		{ 0x6b, 3, 0, 3, 0 },
		/* Mute while resetting video logic; do not enable audio. */
		{ 0x4f, 0xa0, 0xa0, 0xa0, 0 }, { 0x22, 1, 1, 0, 1 }, { 0x22, 1, 0, 1, 0 },
		{ 0x10, 2, 2, 0, 0 }, { 0x12, 0x80, 0x80, 0, 0 },
		{ 15, 7, 1, 255, 0 }, { 0xc5, 0x80, 0, 0x80, 0 },
		{ 0xc6, 0x80, 0, 0x80, 0 }, { 0xc5, 255, 0x18, 255, 0 },
		{ 15, 7, 0, 255, 0 },
		/* 3c99c(false), unencrypted single-TTL branch. */
		{ 15, 7, 1, 255, 0 }, { 0xc5, 1, 1, 0, 0 }, { 0xc5, 1, 0, 1, 0 },
		{ 15, 7, 0, 255, 0 }, { 0x4f, 0xa0, 0xa0, 0xa0, 0 },
		{ 0x4f, 0xa0, 0x80, 0xa0, 0 },
	};
	unsigned int i, ref, counter, sum = 0;
	int ret;

	r->phase = 5;
	if (!gc573_mode_supported(r->width, r->height) || r->interlaced ||
	    (r->avi[0] & 0x60) || (r->timing[1][0] & 0xf0) ||
	    (r->extra[3] & 0x20) || (r->output[0] & 0xc0) != 0x40)
		return -EOPNOTSUPP;
	/* FD=floor(reference_half_kHz / 100), set by the verified B1 timing path.
	 * Use its full uncertainty interval to guard the <=150MHz branch.
	 */
	ret = rv_bank(io, r, 1);
	if (ret)
		return ret;
	ret = rv_read(io, r, 0xfd);
	if (ret)
		return ret;
	ref = r->last.data[0] * 100;
	r->reference_half_khz = ref;
	ret = rv_bank(io, r, 0);
	if (ret)
		return ret;
	/* 3aedc latches five samples; a single 66/67-tick reading is too coarse. */
	for (i = 0; i < 5; i++) {
		const unsigned char clear[] = { 0x9a, 0x80, 0, 0x80, 0 };
		const unsigned char set[] = { 0x9a, 0x80, 0x80, 0x80, 0 };

		io->sleep_ms(io->ctx, 3);
		ret = rv_set(io, r, clear);
		if (ret)
			return ret;
		ret = rv_read(io, r, 0x9a);
		if (ret)
			return ret;
		counter = (r->last.data[0] & 7) << 8;
		ret = rv_read(io, r, 0x99);
		if (ret)
			return ret;
		counter |= r->last.data[0];
		r->counts[i] = counter;
		r->clock_samples++;
		sum += counter;
		ret = rv_set(io, r, set);
		if (ret)
			return ret;
	}
	r->measurement_restored = 1;
	if (!sum || ref < 14000 || ref > 24000)
		return -ERANGE;
	r->pixel_min_khz = ref * 5 * 512 / sum;
	r->pixel_max_khz = (ref + 99) * 5 * 512 / sum;
	if (r->pixel_min_khz < 70000 || r->pixel_max_khz > 150000)
		return -ERANGE;
	r->phase = 6;
	for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
		ret = rv_set(io, r, ops[i]);
		if (ret)
			return ret;
	}
	r->output_enabled = 1;
	return 0;
}

/* Bank selection and the clock latch are safe to repeat only after their
 * readbacks confirm restoration. This never retries output/reset programming
 * (phase 6), bus failures, unsupported formats or a bad reference clock.
 */
int gc573_receiver_video_retryable(const struct gc573_receiver_video_result *r, int error)
{
	if (error == -ENOLINK && r->phase == 1 && !r->writes_started)
		return 1;
	if (!r->bank_verified || r->bank)
		return 0;
	if (r->phase == 4 && (error == -EAGAIN || error == -ERANGE))
		return 1;
	return error == -ERANGE && r->phase == 5 && r->measurement_restored &&
		r->clock_samples == 5 && r->reference_half_khz >= 14000 &&
		r->reference_half_khz <= 24000;
}

int gc573_receiver_video(const struct gc573_block_io *io,
			 struct gc573_signal_result *signal,
			 struct gc573_receiver_video_result *r, unsigned int enable)
{
	unsigned int i, j;
	static const unsigned char regs[] = { 0x43, 0x48, 0x1b, 0xcf };
	int ret;

	*r = (struct gc573_receiver_video_result) { 0 };
	r->phase = 1;
	ret = gc573_receiver_status(io, signal);
	r->transactions = signal->transactions;
	if (ret)
		return ret;
	if ((signal->port0 & 0x11) != 0x11 || !(signal->sync & 0x80))
		return -ENOLINK;
	r->bank_verified = 1;
	r->phase = 2;
	/* Repeated timing snapshots detect a source changing modes mid-read. */
	for (j = 0; j < 2; j++) {
		for (i = 0; i < 19; i++) {
			ret = rv_read(io, r, 0x98 + i);
			if (ret)
				return ret;
			r->timing[j][i] = r->last.data[0];
		}
		r->timing_samples++;
		io->sleep_ms(io->ctx, 20);
	}
	for (i = 0; i < 4; i++) {
		ret = rv_read(io, r, regs[i]);
		if (ret)
			return ret;
		r->extra[i] = r->last.data[0];
	}
	r->phase = 3;
	ret = rv_bank(io, r, 2);
	if (ret)
		return ret;
	for (i = 0; i < 5; i++) {
		ret = rv_read(io, r, 0x15 + i);
		if (ret)
			return ret;
		r->avi[i] = r->last.data[0];
	}
	ret = rv_bank(io, r, 1);
	if (ret)
		return ret;
	for (i = 0; i < 7; i++) {
		ret = rv_read(io, r, 0xc0 + i);
		if (ret)
			return ret;
		r->output[i] = r->last.data[0];
	}
	ret = rv_bank(io, r, 0);
	if (ret)
		return ret;
	r->phase = 4;
	for (i = 3; i < 19; i++)
		if (r->timing[0][i] != r->timing[1][i])
			return -EAGAIN;
	r->htotal = r->timing[1][3] | ((r->timing[1][4] & 63) << 8);
	r->width = r->timing[1][5] | ((r->timing[1][6] & 63) << 8);
	r->vtotal = r->timing[1][10] | ((r->timing[1][11] & 63) << 8);
	r->height = r->timing[1][12] | ((r->timing[1][13] & 63) << 8);
	r->interlaced = (r->timing[1][0] >> 1) & 1;
	if (!r->width || !r->height || r->width >= r->htotal || r->height >= r->vtotal)
		return -ERANGE;
	if (enable) {
		ret = rv_output(io, r);
		if (ret)
			return ret;
	}
	r->complete = 1;
	return 0;
}
