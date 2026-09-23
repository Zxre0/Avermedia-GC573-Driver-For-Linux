// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"
#include "receiver_fake.h"

static unsigned char resident[256];

static struct fake prepared(void)
{
	struct fake f = baseline();

	f.clock_mode = f.edid_mode = 1;
	f.receiver[0][0x21] = 0x44;
	f.receiver[0][0x22] = 0x10;
	f.receiver[0][0x23] = f.receiver[0][0x2b] = 0xa0;
	f.receiver[0][0x2a] = f.receiver[0][0x32] = 1;
	f.receiver[0][0xc5] = f.receiver[4][0xc5] = 0x41;
	f.clock_data[1][0] = f.clock_data[1][1] = 255;
	f.clock_data[2][0] = 0x2e;
	f.clock_data[2][1] = 0xf6;
	f.clock_data[3][0] = 0x3a;
	f.clock_data[3][1] = 0xc0;
	memcpy(f.edid, resident, 256);
	return f;
}

static int run(struct fake *f, struct gc573_ddc_result *r)
{
	struct gc573_signal_result signal;
	struct gc573_clock_result clock;
	struct gc573_timing_result timing;
	struct gc573_edid_result edid;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	unsigned int gpio = f->mmio[GC573_GPIO / 4];
	int ret = gc573_receiver_ddc(&io, &signal, &clock, &timing, &edid, r);

	assert(f->mmio[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->mmio[GC573_GPIO / 4] == gpio);
	if (r->complete) {
		assert(r->writes_started == 29 && r->steps_completed == 29);
		assert(r->bank_verified && !r->bank && !f->bank);
		assert(r->before_valid == 3 && r->after_valid == 3);
		assert(f->receiver[0][0xc5] == 0x40 && f->receiver[4][0xc5] == 0x40);
		assert(f->receiver[0][0xc6] == 0x95);
		assert(f->receiver[0][0xc7] == 0x10 && f->receiver[4][0xc7] == 0x20);
		assert(!f->receiver[0][0xc8] && !f->receiver[4][0xc8]);
		assert(f->receiver[0][0xc9] == 0xe7 && f->receiver[4][0xc9] == 0xe7);
		assert(f->receiver[0][0xca] == 0x79 && f->receiver[4][0xca] == 0x69);
		assert(!memcmp(f->edid, resident, 256));
	}
	if (r->post_attempted && !r->post_error)
		assert(signal.valid == 0xfff && !signal.bank);
	return ret;
}

static void parser(void)
{
	struct gc573_ddc_plan p;
	unsigned char data[256];
	unsigned int i;

	assert(gc573_ddc_plan(resident, &p) == 0);
	assert(p.physical_offset == 0x95 && p.base_checksum == 0xe7);
	assert(p.extension_checksum[0] == 0x79 && p.extension_checksum[1] == 0x69);
	memcpy(data, resident, 256);
	data[127] = data[255] = 255;
	assert(gc573_ddc_plan(data, &p) == 0 && p.base_checksum == 0xe7);
	assert(p.extension_checksum[0] == 0x79);
	data[0x95] = 0x30;
	data[0x96] = 0x10;
	assert(gc573_ddc_plan(data, &p) == 0 && p.extension_checksum[0] == 0x79);
	for (i = 0; i < 256; i++) {
		memcpy(data, resident, 256);
		data[130] = i;
		/* Every possible DTD offset must be bounded under ASan. */
		(void)gc573_ddc_plan(data, &p);
		data[0x91] = i;
		(void)gc573_ddc_plan(data, &p);
	}
	memcpy(data, resident, 256);
	data[126] = 2;
	assert(gc573_ddc_plan(data, &p) == -EINVAL);
	memset(data, 0, 256);
	assert(gc573_ddc_plan(data, &p) == -EINVAL);
	memcpy(data, resident, 256);
	data[0x91] = 0x63;
	assert(gc573_ddc_plan(data, &p) == -EINVAL);
	memcpy(data, resident, 256);
	data[130] = 0x1e;
	data[0x99] = 0x65;
	data[0x9a] = 3; data[0x9b] = 12; data[0x9c] = 0;
	assert(gc573_ddc_plan(data, &p) == -EINVAL);
}

int main(void)
{
	struct gc573_ddc_result r;
	struct fake f;
	FILE *fixture = fopen("tests/receiver-edid.hex", "r");
	unsigned int i, byte, total, before;

	assert(fixture);
	for (i = 0; i < 256; i++) {
		assert(fscanf(fixture, "%x", &byte) == 1 && byte <= 255);
		resident[i] = byte;
	}
	fclose(fixture);
	parser();
	f = prepared();
	assert(run(&f, &r) == 0 && r.complete && f.starts == 345 && f.writes == 76);
	assert(f.sleeps == 3 && f.now == 447); /* 100 ms setup + two 1 ms pulses. */
	assert(f.log[67].bank == 0 && f.log[67].reg == 0xc5 && f.log[67].value == 0x50);
	assert(f.log[68].bank == 0 && f.log[68].reg == 0xc5 && f.log[68].value == 0x40);
	assert(f.log[70].bank == 4 && f.log[70].reg == 0xc5 && f.log[70].value == 0x50);
	assert(f.log[71].bank == 4 && f.log[71].reg == 0xc5 && f.log[71].value == 0x40);
	total = f.starts;
	for (i = 1; i <= total; i++) {
		f = prepared();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && f.starts == i);
	}
	f = prepared();
	f.ddc_reset_selfclear = 1;
	assert(run(&f, &r) == 0 && r.complete);
	f = prepared();
	f.wrong_bank_at = 48;
	assert(run(&f, &r) == -EIO && !r.bank_verified && f.writes == 48);
	f = prepared();
	f.wrong_value_at = 51;
	assert(run(&f, &r) == -EIO && f.writes == 51 && !r.complete);
	f = prepared();
	f.slow_at = 239;
	assert(run(&f, &r) == -ETIMEDOUT && !r.writes_started);
	f = prepared();
	f.mmio[GC573_GPIO / 4] |= 4;
	assert(run(&f, &r) == -EBUSY && !f.starts && !f.writes);
	f = prepared();
	f.receiver[0][0x13] |= 0x40;
	assert(run(&f, &r) == -EBUSY && !r.writes_started);
	f = prepared();
	f.receiver[4][0xc5] |= 0x10;
	before = f.receiver[0][0xc9];
	assert(run(&f, &r) == -EBUSY && r.writes_started == 3);
	assert(f.receiver[0][0xc9] == before);
	f = prepared();
	f.edid[126] = 0;
	assert(run(&f, &r) == -EINVAL && !r.writes_started);
	puts("PASS: resident EDID checksum/address plan, two-port DDC configuration,");
	puts("      345 failure positions, HPD/reset gates, readback/budget, bounded parser");
	return 0;
}
