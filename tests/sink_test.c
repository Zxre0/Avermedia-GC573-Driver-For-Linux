// SPDX-License-Identifier: GPL-2.0-only
#define main splitter_suite_main
#include "splitter_prepare_test.c"
#undef main
#include "gc573_sink.h"
static unsigned char display_edid[512], scdc[256];
static unsigned int cursor, ddc_fault;
static unsigned int sink_read(void *ctx, unsigned int off)
{
	struct fake *f = ctx;
	unsigned int port = (f->regs[GC573_BLOCK_ADDRESS / 4] - 0x68) / 2;
	if (off == GC573_BLOCK_RX && (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x6b || f->regs[GC573_BLOCK_ADDRESS / 4] == 0x6d) &&
	    f->regs[GC573_BLOCK_SUBADDR / 4] == 0x30) {
		assert(cursor < sizeof(display_edid));
		f->tx_ports[port][0x30] = f->tx_ports[port][0x29] == 0xa8 ? scdc[f->tx_ports[port][0x2a]]
								    : display_edid[cursor++];
	}
	return read_reg(ctx, off);
}
static int sink_wait_write(void *ctx, unsigned int *status, unsigned int *armed)
{
	struct fake *f = ctx;
	unsigned int port = (f->regs[GC573_BLOCK_ADDRESS / 4] - 0x68) / 2;
	int ret = wait_write(ctx, status, armed);
	if (!ret && (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x6a || f->regs[GC573_BLOCK_ADDRESS / 4] == 0x6c) &&
	    f->regs[GC573_BLOCK_SUBADDR / 4] == 0x2e) {
		unsigned int cmd = f->regs[GC573_BLOCK_TX / 4];
		if (cmd == 9)
			f->tx_ports[port][0x2f] = 0;
		if (cmd == 3) {
			cursor = f->tx_ports[port][0x2a] + 256 * f->tx_ports[port][0x2d];
			f->tx_ports[port][0x2f] = ddc_fault ? ddc_fault & 255 : 0x80;
		}
		if (cmd == 15)
			f->tx_ports[port][0x2f] = 0x80;
		if (cmd == 0 || cmd == 1) {
			assert(f->tx_ports[port][0x29] == 0xa8);
			if (cmd == 1)
				scdc[f->tx_ports[port][0x2a]] = f->tx_ports[port][0x30];
			f->tx_ports[port][0x2f] = ddc_fault ? ddc_fault & 255 : 0x80;
		}
	}
	return ret;
}
static void delay(void *ctx, unsigned int ms) { ((struct fake *)ctx)->ms += ms; }
static struct fake setup(unsigned int extensions)
{
	struct fake f = video_baseline();
	unsigned int i, j;
	f.selected_port = 2;
	f.tx_ports[2][3] = 0x9f;
	f.tx_ports[2][0x19] = 0xa1;
	f.tx_ports[2][0x1d] = 0x42;
	f.tx_ports[2][0x28] = 0x80;
	cursor = ddc_fault = 0;
	memset(scdc, 0, sizeof(scdc));
	scdc[1] = 1;
	memset(display_edid, 0, sizeof(display_edid));
	memset(display_edid + 1, 255, 6);
	display_edid[126] = extensions;
	for (i = 0; i < 4; i++) {
		unsigned int sum = 0;
		for (j = 0; j < 127; j++)
			sum += display_edid[i * 128 + j];
		display_edid[i * 128 + 127] = -sum;
	}
	return f;
}
static int run_sink(struct fake *f, struct gc573_sink_result *r)
{
	const struct gc573_block_io io = {.ctx = f,
					  .read = sink_read,
					  .write = write_reg,
					  .wait = wait_read,
					  .wait_write = sink_wait_write,
					  .sleep_ms = delay,
					  .time_ms = time_ms};
	return gc573_sink_read(&io, r);
}
#ifndef GC573_SINK_TEST_NO_MAIN
int main(void)
{
	struct gc573_sink_result r;
	struct fake f = setup(3);
	unsigned int i, n;
	assert(!run_sink(&f, &r) && r.complete && r.restored && r.bytes == 512 && r.blocks == 4);
	assert(!memcmp(r.edid, display_edid, 512));
	assert(f.tx_ports[2][0x28] == 0x80 && f.tx_ports[2][0x19] == 0xa1 &&
	       f.tx_ports[2][0x1d] == 0x42);
	for (i = 0; i < f.writes; i++)
		assert(f.trace[i][0] == 0x36);
	n = f.transactions;
	for (i = 1; i <= n; i++) {
		f = setup(3);
		f.fail_at = i;
		assert(run_sink(&f, &r) == -ETIMEDOUT && !r.complete && f.transactions == i);
	}
	f = setup(1);
	display_edid[25] ^= 1;
	assert(run_sink(&f, &r) == -EBADMSG && r.restored && !r.complete);
	f = setup(4);
	assert(run_sink(&f, &r) == -E2BIG && r.restored && r.bytes == 128);
	f = setup(1);
	f.tx_ports[2][3] = 0;
	assert(run_sink(&f, &r) == -ENOLINK && !f.writes);
	f = setup(1);
	ddc_fault = 0x20;
	assert(run_sink(&f, &r) == -EIO && r.restored && !r.bytes);
	f = setup(1);
	ddc_fault = 0x100;
	assert(run_sink(&f, &r) == -ETIMEDOUT && r.restored && r.polls == 20);
	puts("PASS: external EDID, all bus failures, checksum, size, unplug, NACK, "
	     "bounded timeout and control restoration");
}

#endif
