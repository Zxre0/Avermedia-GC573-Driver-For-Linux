// SPDX-License-Identifier: GPL-2.0-only
#include "gc573_scaler.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static unsigned int memory[0x80000 / 4], writes, fault;
static unsigned int rd(void *ctx, unsigned int reg)
{
	(void)ctx;
	return memory[reg / 4] ^ (reg == fault ? 1 : 0);
}
static void wr(void *ctx, unsigned int reg, unsigned int value)
{
	(void)ctx;
	assert(reg == 0x50000 || (reg >= 0x40000 && reg <= 0x43ffc) ||
	       (reg >= 0x60000 && reg <= 0x60afc));
	memory[reg / 4] = value;
	writes++;
}
static void sleep_ms(void *ctx, unsigned int ms)
{
	(void)ctx;
	assert(ms <= 5);
}
static void check(unsigned int iw, unsigned int ih, unsigned int ow, unsigned int oh)
{
	struct gc573_block_io io = {.read = rd, .write = wr, .sleep_ms = sleep_ms};
	struct gc573_scaler_result r;
	unsigned int p, t, g, lane, outputs = 0, consumed = 0, last = 0;
	memset(memory, 0, sizeof(memory));
	fault = ~0U;
	assert(gc573_scaler_configure(&io, &r, iw, ih, ow, oh) == 0);
	assert(r.complete && r.enabled && r.phase_outputs == ow);
	assert(memory[0x60010 / 4] == ih && memory[0x40010 / 4] == oh);
	assert(memory[0x40018 / 4] == iw && memory[0x40020 / 4] == ow);
	for (p = 0; p < 64; p++) {
		int sum = 0;
		for (t = 0; t < 3; t++) {
			unsigned int v = memory[(0x40800 + (p * 3 + t) * 4) / 4];
			assert(v == memory[(0x60800 + (p * 3 + t) * 4) / 4]);
			sum += (short)v + (short)(v >> 16);
		}
		assert(sum == 4096);
	}
	for (g = 0; g < 1024; g++) {
		unsigned long long word = memory[(0x42000 + g * 8) / 4] |
					  ((unsigned long long)memory[(0x42004 + g * 8) / 4] << 32);
		if (g >= iw / 4) {
			assert(word == 0);
			continue;
		}
		for (lane = 0; lane < 4; lane++) {
			unsigned int slot = (word >> (lane * 10)) & 1023;
			unsigned int index = (slot >> 6) & 7;
			if (index < last)
				consumed += 4;
			if (slot & 512) {
				unsigned int source = (outputs * ((iw << 16) / ow)) >> 16;
				assert(consumed + index == source);
				outputs++;
			}
			last = index;
		}
	}
	assert(outputs == ow);
}
int main(void)
{
	struct gc573_block_io io = {.read = rd, .write = wr, .sleep_ms = sleep_ms};
	struct gc573_scaler_result r;
	check(1920, 1080, 1280, 720);
	check(2560, 1440, 1920, 1080);
	check(2560, 1440, 1280, 720);
	writes = 0;
	assert(gc573_scaler_configure(&io, &r, 2560, 1440, 2560, 1440) == 0);
	assert(r.complete && !r.enabled && !writes);
	assert(gc573_scaler_configure(&io, &r, 3840, 2160, 1920, 1080) == -EINVAL);
	assert(gc573_scaler_configure(&io, &r, 1280, 720, 1920, 1080) == -EINVAL);
	assert(writes == 0);
	memory[0x1000 / 4] = 1;
	assert(gc573_scaler_configure(&io, &r, 1920, 1080, 1280, 720) == -EBUSY);
	assert(writes == 0);
	memory[0x1000 / 4] = 0;
	fault = 0x40800;
	assert(gc573_scaler_configure(&io, &r, 1920, 1080, 1280, 720) == -EIO);
	assert(!r.complete && !r.enabled && r.last_reg == fault);
	puts("scaler tests passed");
	return 0;
}
