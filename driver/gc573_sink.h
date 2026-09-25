/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_SINK_H
#define GC573_SINK_H
#include "gc573_block.h"
#define GC573_SINK_EDID_BYTES 512
struct gc573_sink_result {
	unsigned int phase, transactions, writes, bytes, blocks, complete;
	unsigned int status, polls, last_reg, saved_valid, restored, sink_present;
	int cleanup_error;
	unsigned char saved[3], edid[GC573_SINK_EDID_BYTES];
	struct gc573_block_result last;
};
int gc573_sink_read(const struct gc573_block_io *io, struct gc573_sink_result *r);
int gc573_sink_scdc(const struct gc573_block_io *io, struct gc573_sink_result *r, unsigned int reg,
		    int write, unsigned int *value);
#endif
