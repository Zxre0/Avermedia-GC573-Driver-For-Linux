// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"
#include "receiver_fake.h"

static struct fake prepared(void)
{
	struct fake f = baseline();
	unsigned int i, sums[2] = { 0 };

	f.clock_mode = f.edid_mode = 1;
	f.receiver[0][0x21] = 0x44;
	f.receiver[0][0x22] = 0x10;
	f.receiver[0][0x23] = f.receiver[0][0x2b] = 0xa0;
	f.receiver[0][0x2a] = f.receiver[0][0x32] = 1;
	f.clock_data[1][0] = f.clock_data[1][1] = 255;
	f.clock_data[2][0] = 0x2e;
	f.clock_data[2][1] = 0xf6;
	f.clock_data[3][0] = 0x3a;
	f.clock_data[3][1] = 0xc0;
	/* Synthetic header/checksum fixture, not advertised display capabilities. */
	for (i = 1; i < 7; i++)
		f.edid[i] = 255;
	for (i = 8; i < 256; i++)
		f.edid[i] = i ^ 0x5a;
	f.edid[126] = 1;
	for (i = 0; i < 256; i++)
		if (i % 128 != 127)
			sums[i / 128] += f.edid[i];
	f.edid[127] = -sums[0];
	f.edid[255] = -sums[1];
	return f;
}

static const struct gc573_block_io callbacks = {
	.read = read_reg, .write = write_reg, .wait = wait_read,
	.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
};

static int run(struct fake *f, struct gc573_edid_result *r)
{
	struct gc573_signal_result signal;
	struct gc573_clock_result clock;
	struct gc573_timing_result timing;
	struct gc573_block_io io = callbacks;
	int ret;

	io.ctx = f;
	ret = gc573_receiver_edid_read(&io, &signal, &clock, &timing, r);
	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->mmio[GC573_GPIO / 4] == 0x1f958);
	assert(r->bytes_read == f->edid_reads);
	assert(!memcmp(r->data, f->edid, r->bytes_read));
	if (r->complete) {
		assert(r->bytes_read == 256 && r->mapping_verified && timing.complete);
		assert(clock.khz == 38641 && !f->bank);
		assert(f->receiver[1][0xc5] == 255 && f->receiver[1][0xc6] == 255);
	}
	if (r->post_attempted && !r->post_error)
		assert(signal.valid == 0xfff && !signal.bank);
	return ret;
}

int main(void)
{
	struct gc573_edid_result r;
	struct gc573_block_result transfer;
	struct gc573_block_io io = callbacks;
	struct fake f = prepared();
	unsigned int i, total;

	assert(run(&f, &r) == 0 && r.complete && r.phase == 3);
	assert(r.header_matches && !r.checksum[0] && !r.checksum[1]);
	assert(f.writes == 47 && f.starts == 238 && r.last_offset == 252);
	assert(f.log[46].bank == 0 && f.log[46].reg == 0x4b && f.log[46].value == 0xa9);
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && f.starts == i);
	}
	f = prepared();
	f.wrong_value_at = 47;
	assert(run(&f, &r) == -EIO && !r.mapping_verified && !f.edid_reads);
	f = prepared();
	f.slow_at = 160;
	assert(run(&f, &r) == -ETIMEDOUT && !r.mapping_written && !f.edid_reads);
	f = prepared();
	f.slow_at = 163;
	assert(run(&f, &r) == -ETIMEDOUT && r.bytes_read == 4 && f.starts == 163);
	f = prepared();
	memset(f.edid, 0, sizeof(f.edid));
	assert(run(&f, &r) == 0 && r.complete && !r.header_matches && !r.checksum[0]);
	f = prepared();
	f.edid[10]++;
	f.edid[200]++;
	assert(run(&f, &r) == 0 && r.header_matches && r.checksum[0] == 1 && r.checksum[1] == 1);
	f = prepared();
	f.receiver[0][4] = 0xb0;
	assert(run(&f, &r) == -ENODEV && !f.writes && !f.edid_reads);
	f = prepared();
	io.ctx = &f;
	assert(gc573_block_read_edid(&io, &transfer, 255, 2) == -EINVAL && !f.starts);
	assert(gc573_block_read_edid(&io, &transfer, 0, 5) == -EINVAL && !f.starts);
	assert(gc573_block_read_edid(&io, &transfer, 256, 1) == -EINVAL && !f.starts);
	assert(gc573_block_read_edid(&io, &transfer, 0, 0) == -EINVAL && !f.starts);
	puts("PASS: EDID fixed-address mapping/read, all 256 bytes, header/checksums,");
	puts("      238 failure positions, mapping mismatch, deadline, partial data, bounds");
	return 0;
}
