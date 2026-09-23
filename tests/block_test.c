// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <linux/errno.h>
#include "gc573_block.h"

struct fake {
	unsigned int regs[128];
	unsigned int writes, fifo_reads, waits, starts, acknowledgements;
	unsigned int last_command, completion, stale_status;
	unsigned int gpio_writes, sleeps, drop_gpio_at;
	int timeout, sticky_status, progress;
};

static unsigned int fake_read(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;
	static const unsigned char bytes[] = { 0x54, 0x49, 0x05, 0x68 };

	assert(!(offset & 3) && offset / 4 < 128);
	if (offset == GC573_BLOCK_RX) {
		assert(f->last_command == 0x10);
		assert(f->fifo_reads < sizeof(bytes));
		return bytes[f->fifo_reads++];
	}
	if (offset == GC573_BLOCK_STATUS && f->writes && !f->starts && f->stale_status)
		return f->stale_status;
	return f->regs[offset / 4];
}

static void fake_write(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;

	assert(!(offset & 3) && offset / 4 < 128);
	f->writes++;
	if (offset == GC573_GPIO) {
		static const unsigned int values[] = { 0x1f858, 0x1f958, 0x1f858, 0x1f958 };

		assert(f->gpio_writes < 4 && !f->starts);
		assert(value == values[f->gpio_writes++]);
		if (f->drop_gpio_at == f->gpio_writes)
			return;
	}
	if (offset == GC573_BLOCK_COMMAND) {
		assert(value == 0x10 || value == 0x08);
		f->last_command = value;
		if (value == 0x08) {
			if (f->gpio_writes)
				assert(f->gpio_writes == 4 && f->sleeps == 5);
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] == 0x91);
			assert(f->regs[GC573_BLOCK_LENGTH / 4] == 4);
			assert(f->regs[GC573_BLOCK_DIVIDER / 4] == GC573_BLOCK_TEST_DIVIDER);
			assert(f->regs[GC573_BLOCK_SUBADDR / 4] == 0);
			f->starts++;
		}
		if (value == 0x10 && !f->sticky_status)
			f->regs[GC573_BLOCK_STATUS / 4] = 0;
	}
	if (offset == GC573_IRQ_STATUS) {
		assert(value == GC573_IRQ_I2C);
		f->acknowledgements++;
		f->regs[offset / 4] &= ~value;
	} else {
		f->regs[offset / 4] = value;
	}
}

static void fake_sleep(void *ctx, unsigned int milliseconds)
{
	struct fake *f = ctx;
	static const unsigned int delays[] = { 100, 2, 2, 2, 100 };

	assert(f->sleeps < 5 && milliseconds == delays[f->sleeps++]);
	assert(!f->starts && !f->fifo_reads);
}

static int fake_wait(void *ctx, unsigned int *status, unsigned int *saw_clear)
{
	struct fake *f = ctx;
	unsigned int i;
	int observed;

	assert(f->starts == 1);
	f->waits++;
	for (i = 0; i < 3; i++) {
		*status = f->progress && !i ? 8 : f->completion;
		f->regs[GC573_BLOCK_STATUS / 4] = *status;
		observed = gc573_block_observe(*status, saw_clear);
		if (observed < 0)
			return observed;
		if (observed > 0)
			return f->timeout ? -ETIMEDOUT : 0;
	}
	return -ETIMEDOUT;
}

static int run(struct fake *f, struct gc573_block_result *result)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = fake_read, .write = fake_write, .wait = fake_wait,
	};
	unsigned int saved = f->regs[GC573_BLOCK_DIVIDER / 4];
	int ret = gc573_block_identify(&io, result);

	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == saved);
	assert(f->starts <= 1 && f->waits <= 1);
	return ret;
}

static struct fake receiver_baseline(void)
{
	struct fake f = { .completion = 4, .sticky_status = 1 };

	f.regs[0] = 0x20201015;
	f.regs[GC573_BOARD_ID / 4] = 0x57300102;
	f.regs[GC573_GPIO / 4] = 0x1f850;
	f.regs[GC573_BLOCK_STATUS / 4] = 8;
	f.regs[GC573_BLOCK_DIVIDER / 4] = 0x4e2;
	return f;
}

static int run_receiver(struct fake *f, struct gc573_receiver_result *receiver,
			struct gc573_block_result *block)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = fake_read, .write = fake_write,
		.wait = fake_wait, .sleep_ms = fake_sleep,
	};
	unsigned int divider = f->regs[GC573_BLOCK_DIVIDER / 4];
	int ret = gc573_receiver_identify(&io, receiver, block);

	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == divider);
	assert(f->starts <= 1 && f->waits <= 1);
	return ret;
}

int main(void)
{
	struct gc573_block_result result;
	struct gc573_receiver_result receiver;
	struct fake f = { .completion = 4 };
	unsigned int i;

	f.regs[GC573_IRQ_STATUS / 4] = 0x802;
	f.regs[GC573_BLOCK_DIVIDER / 4] = 0x99;
	assert(run(&f, &result) == 0);
	assert(result.bytes_read == 4 && result.data[3] == 0x68);
	assert(f.acknowledgements == 1 && f.regs[GC573_IRQ_STATUS / 4] == 2);
	f = (struct fake) { .completion = 1 };
	assert(run(&f, &result) == -ETIMEDOUT && !f.fifo_reads && !result.bytes_read);
	f = (struct fake) { .completion = 8, .timeout = 1, .sticky_status = 1 };
	assert(run(&f, &result) == -ETIMEDOUT && !f.fifo_reads);
	assert(result.status == 8 && result.cleanup_status == 8 && !result.bytes_read);
	f = (struct fake) { .timeout = 1 };
	assert(run(&f, &result) == -ETIMEDOUT && !f.fifo_reads);
	assert(f.last_command == 0x10);
	/* A sticky previous result must never release old FIFO bytes. */
	f = (struct fake) { .completion = 4, .sticky_status = 1 };
	f.regs[GC573_BLOCK_STATUS / 4] = 4;
	assert(run(&f, &result) == -ETIMEDOUT && !f.fifo_reads);
	assert(result.prepared_status == 4 && !result.completion_armed);
	assert(result.started == 1);
	/* With an old result, observing 4 -> 8 -> 4 establishes freshness. */
	f = (struct fake) { .completion = 4, .sticky_status = 1, .progress = 1 };
	f.regs[GC573_BLOCK_STATUS / 4] = 4;
	assert(run(&f, &result) == 0 && result.bytes_read == 4);
	assert(result.prepared_status == 4 && result.completion_armed);
	/* A cleared preparation permits an immediately completed fresh read. */
	f = (struct fake) { .completion = 4 };
	f.regs[GC573_BLOCK_STATUS / 4] = 4;
	assert(run(&f, &result) == 0 && result.bytes_read == 4);
	assert(result.prepared_status == 0 && result.completion_armed);
	f = (struct fake) { .stale_status = 8 };
	assert(run(&f, &result) == -EIO && !f.starts && !f.fifo_reads);
	f = (struct fake) { .completion = 0xffffffff };
	assert(run(&f, &result) == -ENODEV && !f.fifo_reads);
	f = (struct fake) { .completion = 0xeeeeeeee };
	assert(run(&f, &result) == -ENODEV && !f.fifo_reads);
	f = (struct fake) { .completion = 0 };
	assert(run(&f, &result) == -ETIMEDOUT && !f.fifo_reads);
	f = (struct fake) { 0 };
	f.regs[GC573_BLOCK_STATUS / 4] = 0xeeeeeeee;
	assert(run(&f, &result) == -ENODEV && !f.writes);
	f = (struct fake) { 0 };
	f.regs[GC573_BLOCK_STATUS / 4] = 2;
	assert(run(&f, &result) == -EBUSY && !f.writes);
	f = (struct fake) { 0 };
	f.regs[GC573_BLOCK_STATUS / 4] = 8;
	assert(run(&f, &result) == -EBUSY && !f.writes);
	f = (struct fake) { 0 };
	f.regs[GC573_IRQ_ENABLE / 4] = GC573_IRQ_I2C;
	assert(run(&f, &result) == -EBUSY && !f.writes);
	f = receiver_baseline();
	assert(run_receiver(&f, &receiver, &result) == 0);
	assert(receiver.steps == 4 && receiver.setup_complete);
	assert(receiver.status_after == 8 && result.initial_status == 8);
	assert(result.bytes_read == 4 && result.started == 1);
	assert(f.regs[GC573_GPIO / 4] == 0x1f958);
	f = receiver_baseline();
	f.completion = 8;
	assert(run_receiver(&f, &receiver, &result) == -ETIMEDOUT);
	assert(receiver.setup_complete && result.started && !f.fifo_reads);
	for (i = 1; i <= 4; i++) {
		f = receiver_baseline();
		f.drop_gpio_at = i;
		assert(run_receiver(&f, &receiver, &result) == -EIO);
		assert(!f.starts && !f.fifo_reads && !receiver.setup_complete);
		assert(receiver.steps == i - 1);
	}
	for (i = 0; i < 6; i++) {
		f = receiver_baseline();
		switch (i) {
		case 0: f.regs[0] = 0; break;
		case 1: f.regs[GC573_BOARD_ID / 4] = 0; break;
		case 2: f.regs[GC573_GPIO / 4] = 0; break;
		case 3: f.regs[GC573_BLOCK_STATUS / 4] = 2; break;
		case 4: f.regs[GC573_IRQ_ENABLE / 4] = 1; break;
		case 5: f.regs[GC573_IRQ_STATUS / 4] = 1; break;
		}
		assert(run_receiver(&f, &receiver, &result) < 0 && !f.writes);
	}
	puts("PASS: block transaction ordering, timeout/no FIFO read, stale status,");
	puts("      read-only completion bit, invalid status, IRQ/busy refusal,");
	puts("      exact IRQ ACK, divider restoration, sticky old result rejected,");
	puts("      fresh 4 -> 8 -> 4 and 4 -> 0 -> 4 sequences accepted");
	puts("      receiver GPIO scope/order/delays, startup failure prevents read,");
	puts("      explicit startup from status 8, timeout without retry/FIFO read");
	return 0;
}
