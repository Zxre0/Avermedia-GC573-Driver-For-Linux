// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <linux/errno.h>
#include "gc573_block.h"

struct fake {
	unsigned int regs[128], writes, starts, rx, fail_at, stale_at;
	unsigned int startup_mode, gpio_writes, sleeps, corrupt_gpio_at, drift_at;
	unsigned int resume_mode, settle_flip;
	unsigned char bank, id[4];
};

static unsigned int read_reg(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;

	assert(!(offset & 3) && offset / 4 < 128);
	if (offset == GC573_BLOCK_RX) {
		assert(f->regs[GC573_BLOCK_COMMAND / 4] == 0x10);
		assert(f->starts != f->fail_at && f->starts != f->stale_at);
		if (f->starts == 1) {
			assert(f->rx++ == 0);
			return f->bank;
		}
		assert(f->starts == 2 && f->rx < 4);
		return f->id[f->rx++];
	}
	return f->regs[offset / 4];
}

static void write_reg(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;

	if (offset == GC573_GPIO) {
		static const unsigned int masks[] = { 0x10, 0x40, 2, 0x20, 0x20, 0x20 };
		static const unsigned int values[] = { 0x10, 0x40, 0, 0x20, 0, 0x20 };
		static const unsigned int sleeps[] = { 1, 1, 2, 3, 4, 5 };
		unsigned int n = f->gpio_writes;

		assert(f->startup_mode && !f->resume_mode && n < 6 && !f->starts);
		assert(f->sleeps == sleeps[n]);
		assert(value == ((f->regs[offset / 4] & ~masks[n]) | values[n]));
		f->writes++;
		f->gpio_writes++;
		f->regs[offset / 4] = value;
		if (f->gpio_writes == f->corrupt_gpio_at)
			f->regs[offset / 4] ^= 0x100;
		return;
	}
	/* Never permit GPIO, TX FIFO, DMA, IRQ-enable or other MMIO writes. */
	assert(offset == GC573_BLOCK_DIVIDER || offset == GC573_BLOCK_ADDRESS ||
	       offset == GC573_BLOCK_SUBADDR_WIDTH || offset == GC573_BLOCK_SUBADDR ||
	       offset == GC573_BLOCK_FIFO_WIDTH || offset == GC573_BLOCK_LENGTH ||
	       offset == GC573_BLOCK_COMMAND);
	f->writes++;
	if (offset == GC573_BLOCK_COMMAND) {
		assert(value == 0x10 || value == 8);
		if (value == 8) {
			if (f->startup_mode) {
				assert(f->gpio_writes == (f->resume_mode ? 0 : 6));
				assert(f->sleeps == (f->resume_mode ? 1 : 7));
			}
			assert(f->starts < 2);
			assert(!f->fail_at || f->starts < f->fail_at);
			assert(!f->stale_at || f->starts < f->stale_at);
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] == 0x59);
			assert(f->regs[GC573_BLOCK_SUBADDR / 4] == (f->starts ? 0 : 0x0f));
			assert(f->regs[GC573_BLOCK_LENGTH / 4] == (f->starts ? 4 : 1));
			assert(f->regs[GC573_BLOCK_SUBADDR_WIDTH / 4] == 0);
			assert(f->regs[GC573_BLOCK_FIFO_WIDTH / 4] == 0);
			assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
			f->starts++;
			f->rx = 0;
		}
	}
	f->regs[offset / 4] = value;
}

static void sleep_ms(void *ctx, unsigned int ms)
{
	struct fake *f = ctx;
	static const unsigned int delays[] = { 200, 2, 2, 5, 10, 5, 100 };

	assert(f->startup_mode && f->sleeps < 7 && !f->starts);
	if (f->resume_mode) {
		assert(!f->sleeps && ms == 100);
		f->sleeps++;
	} else {
		assert(ms == delays[f->sleeps++]);
	}
	if (f->sleeps == f->drift_at)
		f->regs[GC573_GPIO / 4] ^= 4;
	if (ms == 100)
		f->regs[GC573_GPIO / 4] ^= f->settle_flip;
}

static int wait_read(void *ctx, unsigned int *status, unsigned int *armed)
{
	struct fake *f = ctx;

	if (f->starts == f->stale_at) {
		*status = 4;
		assert(!gc573_block_observe(*status, armed));
		return -ETIMEDOUT;
	}
	*status = 8;
	assert(!gc573_block_observe(*status, armed));
	if (f->starts == f->fail_at) {
		f->regs[GC573_BLOCK_STATUS / 4] = *status;
		return -ETIMEDOUT;
	}
	*status = 4;
	f->regs[GC573_BLOCK_STATUS / 4] = *status;
	assert(gc573_block_observe(*status, armed) == 1);
	return 0;
}

static struct fake baseline(void)
{
	struct fake f = { .id = { 0x54, 0x49, 0x64, 0x66 } };

	f.regs[0] = 0x20201015;
	f.regs[GC573_BOARD_ID / 4] = 0x57300102;
	f.regs[GC573_GPIO / 4] = 0x1f95c;
	f.regs[GC573_BLOCK_DIVIDER / 4] = 0x321;
	f.regs[GC573_BLOCK_STATUS / 4] = 4;
	return f;
}

static int run(struct fake *f, struct gc573_splitter_result *r)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
	};
	unsigned int gpio = f->regs[GC573_GPIO / 4];
	unsigned int divider = f->regs[GC573_BLOCK_DIVIDER / 4];
	int ret = gc573_splitter_identify(&io, r);

	assert(f->regs[GC573_GPIO / 4] == gpio);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == divider);
	assert(f->starts <= 2);
	return ret;
}

static int run_start(struct fake *f, struct gc573_splitter_start_result *s,
		     struct gc573_splitter_result *r)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.sleep_ms = sleep_ms,
	};
	unsigned int divider = f->regs[GC573_BLOCK_DIVIDER / 4];
	int ret;

	f->startup_mode = 1;
	f->resume_mode = !!(f->regs[GC573_GPIO / 4] & 0x20);
	ret = gc573_splitter_startup(&io, s, r);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == divider);
	assert(f->starts <= 2 && f->gpio_writes <= 6);
	return ret;
}

int main(void)
{
	struct gc573_splitter_result r;
	struct gc573_splitter_start_result s;
	struct fake f = baseline();
	unsigned int i;

	{
		struct gc573_block_io runtime = {.ctx = &f, .read = read_reg,
			.write = write_reg, .wait = wait_read, .owned_irq_mask = 0x22};
		f.regs[GC573_IRQ_ENABLE / 4] = 0x22;
		assert(!gc573_splitter_identify(&runtime, &r));
		f = baseline();
		f.regs[GC573_IRQ_ENABLE / 4] = 0x22;
		runtime.owned_irq_mask = 0;
		assert(gc573_splitter_identify(&runtime, &r) == -EBUSY && !f.writes);
		runtime.owned_irq_mask = 0xffffffff;
		f.regs[GC573_IRQ_ENABLE / 4] = GC573_IRQ_I2C;
		assert(gc573_splitter_identify(&runtime, &r) == -EBUSY && !f.writes);
		f = baseline();
	}

	assert(!run(&f, &r));
	assert(r.id_matches && r.id_valid && r.bank_valid && r.transactions == 2);
	assert(f.writes == 20 && f.rx == 4 && r.gpio == 0x1f95c);
	f = baseline();
	f.id[2] = 0x63;
	f.bank = 0x80;
	f.regs[GC573_GPIO / 4] = 0x1f958;
	assert(!run(&f, &r) && r.bank == 0x80);
	for (i = 0; i < 4; i++) {
		f = baseline();
		f.id[i] ^= 1;
		assert(run(&f, &r) == -ENODEV && r.id_valid && !r.id_matches);
	}
	f = baseline();
	f.bank = 1;
	assert(run(&f, &r) == -EOPNOTSUPP && r.bank_valid && !r.id_valid);
	assert(f.starts == 1 && f.writes == 10);
	for (i = 1; i <= 2; i++) {
		f = baseline();
		f.fail_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && !r.id_valid);
		assert(f.starts == i && !f.rx && !r.last.bytes_read);
		assert(r.last.cleanup_status == 8);
		f = baseline();
		f.stale_at = i;
		assert(run(&f, &r) == -ETIMEDOUT && !r.id_valid);
		assert(f.starts == i && !f.rx && !r.last.completion_armed);
	}
	for (i = 0; i < 10; i++) {
		f = baseline();
		switch (i) {
		case 0:
			f.regs[0] = 0;
			break;
		case 1:
			f.regs[GC573_BOARD_ID / 4] = 0;
			break;
		case 2:
			f.regs[GC573_GPIO / 4] = 0;
			break;
		case 3:
			f.regs[GC573_GPIO / 4] = 0xeeeeeeee;
			break;
		case 4:
			f.regs[GC573_GPIO / 4] = 0xffffffff;
			break;
		case 5:
			f.regs[GC573_IRQ_ENABLE / 4] = 1;
			break;
		case 6:
			f.regs[GC573_IRQ_STATUS / 4] = 1;
			break;
		case 7:
			f.regs[GC573_BLOCK_STATUS / 4] = 8;
			break;
		case 8:
			f.regs[GC573_BLOCK_STATUS / 4] = 0xffffffff;
			break;
		case 9:
			f.regs[GC573_BLOCK_DIVIDER / 4] = 0xeeeeeeee;
			break;
		}
		assert(run(&f, &r) < 0 && !f.writes && !f.starts && !r.id_valid);
	}
	/* Startup alone permits an initial 8 after its verified GPIO sequence. */
	for (i = 0; i < 4; i++) {
		static const unsigned int statuses[] = { 0, 1, 4, 8 };

		f = baseline();
		f.regs[GC573_BLOCK_STATUS / 4] = statuses[i];
		assert(!run_start(&f, &s, &r));
		assert(s.preflight_complete && s.complete && s.steps_completed == 6);
		assert(s.writes_started == 6 && f.writes == 26);
		assert(s.gpio_before == 0x1f95c && s.gpio_after == 0x1f97c);
		assert(r.id_matches && f.sleeps == 7 && r.gpio == 0x1f97c);
	}
	f = baseline();
	f.regs[GC573_GPIO / 4] = 0x1f958;
	assert(!run_start(&f, &s, &r) && s.gpio_after == 0x1f978);
	for (i = 1; i <= 6; i++) {
		f = baseline();
		f.corrupt_gpio_at = i;
		assert(run_start(&f, &s, &r) == -EIO && !f.starts && !s.complete);
		assert(s.steps_completed == i - 1 && s.writes_started == i);
		assert(f.writes == i);
	}
	for (i = 1; i <= 7; i++) {
		f = baseline();
		f.drift_at = i;
		assert(run_start(&f, &s, &r) == -EIO && !f.starts && !s.complete);
	}
	for (i = 1; i <= 2; i++) {
		f = baseline();
		f.fail_at = i;
		f.regs[GC573_BLOCK_STATUS / 4] = 8;
		assert(run_start(&f, &s, &r) == -ETIMEDOUT && s.complete);
		assert(f.starts == i && !f.rx && !r.id_valid && f.gpio_writes == 6);
		f = baseline();
		f.stale_at = i;
		assert(run_start(&f, &s, &r) == -ETIMEDOUT && s.complete);
		assert(f.starts == i && !f.rx && !r.id_valid && f.gpio_writes == 6);
	}
	f = baseline();
	f.bank = 1;
	assert(run_start(&f, &s, &r) == -EOPNOTSUPP && s.complete && f.starts == 1);
	f = baseline();
	f.id[0] = 0;
	assert(run_start(&f, &s, &r) == -ENODEV && s.complete && r.id_valid);
	/* No writes or delay on an unexpected board/GPIO/controller state. */
	for (i = 0; i < 10; i++) {
		f = baseline();
		switch (i) {
		case 0:
			f.regs[0] = 0;
			break;
		case 1:
			f.regs[GC573_BOARD_ID / 4] = 0;
			break;
		case 2:
			f.regs[GC573_GPIO / 4] ^= 0x80;
			break;
		case 3:
			f.regs[GC573_GPIO / 4] = 0xeeeeeeee;
			break;
		case 4:
			f.regs[GC573_GPIO / 4] ^= 2;
			break;
		case 5:
			f.regs[GC573_IRQ_ENABLE / 4] = 1;
			break;
		case 6:
			f.regs[GC573_IRQ_STATUS / 4] = 1;
			break;
		case 7:
			f.regs[GC573_BLOCK_STATUS / 4] = 2;
			break;
		case 8:
			f.regs[GC573_BLOCK_STATUS / 4] = 0xffffffff;
			break;
		case 9:
			f.regs[GC573_BLOCK_DIVIDER / 4] = 0xeeeeeeee;
			break;
		}
		assert(run_start(&f, &s, &r) < 0 && !f.writes && !f.starts && !f.sleeps);
		assert(!s.preflight_complete && !s.complete && !r.id_valid);
	}
	/* Reproduce the hardware's 0x1f97c -> 0x1fd7c after the final delay. */
	f = baseline();
	f.settle_flip = 0x400;
	f.regs[GC573_BLOCK_STATUS / 4] = 8;
	assert(!run_start(&f, &s, &r) && s.complete && !s.resumed);
	assert(s.gpio_after == 0x1fd7c && s.gpio_settle_changed == 0x400);
	assert(s.status_after_valid && s.status_after == 8 && f.writes == 26);
	/* Resume from each observed-state variant, without any GPIO writes. */
	for (i = 0; i < 16; i++) {
		static const unsigned int statuses[] = { 0, 1, 4, 8 };

		f = baseline();
		f.regs[GC573_GPIO / 4] = 0x1f978 | ((i & 1) ? 4 : 0) |
			((i & 2) ? 0x400 : 0);
		f.regs[GC573_BLOCK_STATUS / 4] = statuses[i / 4];
		assert(!run_start(&f, &s, &r) && s.complete && s.resumed);
		assert(!s.writes_started && !s.steps_completed && !f.gpio_writes);
		assert(s.gpio_before == s.gpio_after && f.writes == 20);
		assert(s.status_after_valid && s.status_after == statuses[i / 4]);
	}
	/* Bit 10 alone may vary in either direction; no other drift is allowed. */
	for (i = 0; i < 32; i++) {
		f = baseline();
		f.regs[GC573_GPIO / 4] = 0x1fd7c;
		f.settle_flip = 1U << i;
		if (i == 10) {
			assert(!run_start(&f, &s, &r) && s.complete && r.id_matches);
			assert(s.gpio_after == 0x1f97c);
		} else {
			assert(run_start(&f, &s, &r) == -EIO && !f.starts && !s.complete);
			assert(!f.writes);
		}
		assert(s.resumed && s.status_after_valid && !f.gpio_writes);
		assert(s.gpio_settle_changed == (1U << i));
	}
	for (i = 1; i <= 2; i++) {
		f = baseline();
		f.regs[GC573_GPIO / 4] = 0x1fd7c;
		f.fail_at = i;
		assert(run_start(&f, &s, &r) == -ETIMEDOUT && s.resumed);
		assert(!f.gpio_writes && !f.rx && f.starts == i && !r.id_valid);
		f = baseline();
		f.regs[GC573_GPIO / 4] = 0x1fd7c;
		f.stale_at = i;
		assert(run_start(&f, &s, &r) == -ETIMEDOUT && s.resumed);
		assert(!f.gpio_writes && !f.rx && f.starts == i && !r.id_valid);
	}
	puts("PASS: splitter address/read scope, identities, bank gate, HPD preservation,");
	puts("      timeout/stale completion stops without FIFO reads or retry,");
	puts("      board/GPIO/IRQ/status gates and divider restoration");
	puts("      startup GPIO order/delays/readback, HPD preserved, no retries,");
	puts("      failure at each GPIO/transfer, drift, explicit status-8 recovery");
	puts("      asynchronous bit-10 settling, resume without GPIO writes,");
	puts("      all other settling-bit changes rejected, measured-status validity");
	return 0;
}
