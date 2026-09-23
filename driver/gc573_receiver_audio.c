// SPDX-License-Identifier: GPL-2.0-only
/* Receiver audio observations, independently implemented from vendor protocol. */
#include <linux/errno.h>
#include "gc573_audio.h"

static int audio_bank(const struct gc573_block_io *io, unsigned int bank)
{
	struct gc573_block_result r;
	unsigned int value;
	int ret = gc573_block_read_registers(io, &r, 15, 1);

	if (ret)
		return ret;
	value = (r.data[0] & ~7U) | bank;
	ret = gc573_block_write_byte(io, &r, 15, value);
	if (ret)
		return ret;
	ret = gc573_block_read_registers(io, &r, 15, 1);
	return ret ? ret : r.data[0] == value ? 0 : -EIO;
}

int gc573_audio_signal_read(const struct gc573_block_io *io, struct gc573_audio_signal *r)
{
	struct gc573_block_result b;
	unsigned int i;
	int ret = audio_bank(io, 0);

	*r = (struct gc573_audio_signal) { 0 };
	if (ret)
		return ret;
	for (i = 0; i < 16; i++) {
		ret = gc573_block_read_registers(io, &b, 0xb0 + i, 1);
		if (ret)
			return ret;
		r->status[i] = b.data[0];
		ret = gc573_block_read_registers(io, &b, 0x80 + i, 1);
		if (ret)
			return ret;
		r->controls[i] = b.data[0];
	}
	ret = audio_bank(io, 2);
	if (ret)
		return ret;
	for (i = 0; i < 5; i++) {
		ret = gc573_block_read_registers(io, &b, 0xbe + i, 1);
		if (ret)
			goto restore;
		r->clock[i] = b.data[0];
	}
	ret = audio_bank(io, 1);
	if (ret)
		goto restore;
	ret = gc573_block_read_registers(io, &b, 0xc7, 1);
	if (!ret) {
		r->output = b.data[0];
		r->valid = 1;
	}
restore:
	{
		int cleanup = audio_bank(io, 0);

		return ret ? ret : cleanup;
	}
}

int gc573_audio_signal_enable(const struct gc573_block_io *io, struct gc573_audio_signal *r)
{
	/* aud_chg(1), Enable_Audio_Output and the PCM branch of aud_fsm. */
	static const unsigned char ops[][3] = {
		{ 0x8c, 0x10, 0x10 }, { 0x8c, 0x18, 0 },
		{ 0x86, 1, 1 }, { 0x81, 0x40, 0 },
	};
	struct gc573_block_result b;
	unsigned int i, value;
	int ret;

	if (!r->valid || !(r->status[1] & 0x80))
		return -ENOLINK;
	if ((r->status[2] & 2) || ((r->status[5] & 15) | ((r->status[6] & 0xc0) >> 2)) != 2)
		return -EOPNOTSUPP;
	for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
		ret = gc573_block_read_registers(io, &b, ops[i][0], 1);
		if (ret)
			return ret;
		value = (b.data[0] & ~ops[i][1]) | ops[i][2];
		ret = gc573_block_write_byte(io, &b, ops[i][0], value);
		if (ret)
			return ret;
	}
	return gc573_audio_signal_read(io, r);
}
