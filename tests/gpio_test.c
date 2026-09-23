// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <linux/errno.h>
#include "gc573_block.h"

struct fake {
	unsigned int gpio, status, fpga_id, board_id, irq_enable, irq_status;
	unsigned int writes, sleeps, elapsed_ms;
	int dropped_write, completes;
};

static struct fake baseline(void)
{
	return (struct fake) {
		.gpio = 0x1f850, .status = 8,
		.fpga_id = 0x20201015, .board_id = 0x57300102,
	};
}

static unsigned int fake_read(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;

	switch (offset) {
	case 0: return f->fpga_id;
	case GC573_BOARD_ID: return f->board_id;
	case GC573_GPIO: return f->gpio;
	case GC573_BLOCK_STATUS: return f->status;
	case GC573_IRQ_ENABLE: return f->irq_enable;
	case GC573_IRQ_STATUS: return f->irq_status;
	default: assert(!"unexpected register/FIFO read"); return 0;
	}
}

static void fake_write(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;

	assert(offset == GC573_GPIO && f->writes == 0);
	assert((value ^ f->gpio) == GC573_GPIO_PREPARE);
	f->writes++;
	if (!f->dropped_write)
		f->gpio = value;
}

static void fake_sleep(void *ctx, unsigned int milliseconds)
{
	struct fake *f = ctx;

	assert(f->sleeps < 2);
	assert(milliseconds == (f->sleeps ? 1900 : 100));
	f->sleeps++;
	f->elapsed_ms += milliseconds;
	if (f->completes) {
		f->status = 4;
		f->irq_status = GC573_IRQ_I2C;
	}
}

static int run(struct fake *f, struct gc573_gpio_result *result)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = fake_read, .write = fake_write,
		.sleep_ms = fake_sleep,
	};

	return gc573_block_prepare_gpio(&io, result);
}

int main(void)
{
	struct gc573_gpio_result result;
	struct fake f = baseline();
	unsigned int i;

	assert(run(&f, &result) == 0);
	assert(f.writes == 1 && f.gpio == 0x1f858 && f.elapsed_ms == 2000);
	assert(result.changed == 1 && result.samples == 2);
	assert(result.status_before == 8 && result.status_100ms == 8);
	assert(result.status_2000ms == 8 && result.irq_after == 0);
	/* Repeating preparation must not toggle an already-set bit. */
	f.writes = f.sleeps = f.elapsed_ms = 0;
	assert(run(&f, &result) == 0 && !f.writes && !result.changed);
	f = baseline();
	f.completes = 1;
	assert(run(&f, &result) == 0 && f.writes == 1);
	assert(result.status_100ms == 4 && result.status_2000ms == 4);
	assert(result.irq_after == GC573_IRQ_I2C && f.irq_status == GC573_IRQ_I2C);
	f = baseline();
	f.dropped_write = 1;
	assert(run(&f, &result) == -EIO && f.writes == 1);
	assert(result.samples == 1 && f.elapsed_ms == 100);
	for (i = 0; i < 7; i++) {
		f = baseline();
		switch (i) {
		case 0: f.fpga_id = 0; break;
		case 1: f.board_id = 0; break;
		case 2: f.gpio = 0xeeeeeeee; break;
		case 3: f.gpio = 0xffffffff; break;
		case 4: f.status = 4; break;
		case 5: f.irq_enable = 1; break;
		case 6: f.irq_status = GC573_IRQ_I2C; break;
		}
		assert(run(&f, &result) < 0 && !f.writes && !f.sleeps);
		assert(!result.samples);
	}
	puts("PASS: GPIO-only write, unrelated bits preserved, no automatic retry,");
	puts("      no command/FIFO/IRQ ACK, status observations, readback failure,");
	puts("      identity/state refusal before writes, already-set bit unchanged");
	return 0;
}
