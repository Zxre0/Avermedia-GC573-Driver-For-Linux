/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "gc573_led.h"
struct fake { unsigned int regs[0x2500 / 4], writes, reads, fail, sleeps, expected_writes; };
static unsigned int rd(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;
	assert(!(offset & 3) && offset < sizeof(f->regs));
	if (++f->reads == f->fail)
		return f->regs[offset / 4] ^ 1;
	return f->regs[offset / 4];
}
static void wr(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;
	assert(!(offset & 3));
	assert((offset >= 0x800 && offset <= 0x87c) ||
	       (offset >= 0x2008 && offset < 0x2008 + 91 * 4));
	if (offset != 0x804)
		assert(f->regs[0x804 / 4] == 0 && f->sleeps == 1);
	if (offset == 0x804 && value)
		assert(f->writes == f->expected_writes - 1 && value == 31);
	f->regs[offset / 4] = value;
	f->writes++;
}
static void nap(void *ctx, unsigned int ms)
{
	struct fake *f = ctx;
	assert(ms == 1); f->sleeps++;
}
int main(void)
{
	struct fake f;
	struct gc573_led_result r;
	struct gc573_block_io io = { .ctx = &f, .read = rd, .write = wr, .sleep_ms = nap };
	unsigned int fail, total = 0;
	for (fail = 0; fail <= total; fail++) {
		memset(&f, 0, sizeof(f)); memset(&r, 0, sizeof(r));
		f.regs[0] = 0x20201015; f.regs[0x64/4] = 0x57300102;
		f.regs[0x804/4] = 31; f.fail = fail; f.expected_writes = 124;
		int ret = gc573_led_rgb(&io, &r);
		if (!fail) {
			assert(!ret && r.complete && r.controls_verified == 15 && r.commands_written == 91);
			assert(f.writes == 124 && f.regs[0x87c/4] == 92);
			total = f.reads;
		} else if (fail == 3 || fail == total - 1) {
			/* Informational previous-state read and posted-write flush. */
			assert(!ret);
		} else {
			assert(ret && !r.complete);
			if (fail <= 2) assert(!f.writes);
		}
	}
	memset(&f, 0, sizeof(f));
	f.regs[0] = 0x20201015; f.regs[0x64/4] = 0x57300102; f.expected_writes = 37;
	assert(!gc573_led_set(&io, &r, 1, 0x80ff00, 50));
	assert(f.regs[0x200c/4] == 0x86304040);
	assert(f.regs[0x2010/4] == 0x86307f7f);
	assert(f.regs[0x2014/4] == 0x86300000);
	assert(gc573_led_set(&io, &r, 3, 0, 100) == -EINVAL);
	assert(gc573_led_set(&io, &r, 0, 0, 101) == -EINVAL);
	assert(gc573_led_set(&io, &r, 0, 0x1000000, 100) == -EINVAL);
	printf("LED sequencer: fixed write bounds, %u read failure positions, generated rainbow passed\n", total);
}
