// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <linux/errno.h>
#include "gc573_block.h"

struct fake {
	unsigned int regs[128], starts, fifo_reads, index, fail_at;
	unsigned int command;
	unsigned char bank, port0, port1, sync;
	unsigned char revision;
	unsigned int allow_write, write_starts, tx_bytes, write_fault, bad_verify;
	int bad_id;
};

static struct fake baseline(void)
{
	struct fake f = { .port0 = 9, .port1 = 0, .sync = 0x80, .revision = 0xb1 };

	f.regs[0] = 0x20201015;
	f.regs[GC573_BOARD_ID / 4] = 0x57300102;
	f.regs[GC573_GPIO / 4] = 0x1f958;
	f.regs[GC573_BLOCK_DIVIDER / 4] = 0x4e2;
	f.regs[GC573_BLOCK_STATUS / 4] = 4;
	return f;
}

static unsigned int fake_read(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;
	static const unsigned char id[] = { 0x54, 0x49, 5, 0x68 };

	assert(!(offset & 3) && offset / 4 < 128);
	if (offset != GC573_BLOCK_RX)
		return f->regs[offset / 4];
	assert(f->command == 0x10 && f->starts != f->fail_at);
	assert(f->index < f->regs[GC573_BLOCK_LENGTH / 4]);
	f->fifo_reads++;
	f->index++;
	switch (f->regs[GC573_BLOCK_SUBADDR / 4]) {
	case 0: return f->bad_id ? 0xff : id[f->index - 1];
	case 0x0f: return f->write_starts && f->bad_verify ? 2 : f->bank;
	case 0x13: return f->port0;
	case 0x16: return f->port1;
	case 0x19: return f->sync;
	case 0x04: return f->revision;
	case 0x20:
	case 0x25:
	case 0x2a:
	case 0x2d:
	case 0x32:
	case 0x35:
		/* Distinct bytes expose incorrect multi-byte storage/order. */
		return f->regs[GC573_BLOCK_SUBADDR / 4] + f->index - 1;
	default: assert(!"unexpected receiver register"); return 0;
	}
}

static void fake_write(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;
	static const unsigned int subaddr[] = {
		0x0f, 0, 0x13, 0x16, 0x19, 4, 0x20, 0x25, 0x2a, 0x2d, 0x32, 0x35,
	};
	static const unsigned int lengths[] = { 1, 4, 1, 1, 1, 1, 4, 3, 2, 3, 1, 1 };

	/* No GPIO, IRQ enable or reset writes. TX allowed only in write tests. */
	switch (offset) {
	case GC573_BLOCK_DIVIDER:
	case GC573_BLOCK_ADDRESS:
	case GC573_BLOCK_SUBADDR_WIDTH:
	case GC573_BLOCK_FIFO_WIDTH:
	case GC573_BLOCK_SUBADDR:
	case GC573_BLOCK_LENGTH:
		f->regs[offset / 4] = value;
		break;
	case GC573_BLOCK_COMMAND:
		assert(value == 0x10 || value == 8 || (f->allow_write && value == 4));
		f->command = value;
		if (value == 0x10 && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x90 &&
		    f->write_fault == 3)
			f->regs[GC573_BLOCK_STATUS / 4] = 1;
		if (value == 4) {
			assert(f->starts == 12 && f->tx_bytes == 1 && !f->write_starts);
			assert(f->regs[GC573_BLOCK_SUBADDR / 4] == 0x0f);
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] == 0x90);
			assert(f->regs[GC573_BLOCK_LENGTH / 4] == 1);
			f->write_starts++;
		}
		if (value == 8) {
			if (f->starts == 12) {
				assert(f->allow_write && f->write_starts == 1);
				assert(!f->write_fault);
				assert(f->regs[GC573_BLOCK_SUBADDR / 4] == 0x0f);
				assert(f->regs[GC573_BLOCK_LENGTH / 4] == 1);
			} else {
				assert(f->starts < 12);
				assert(f->regs[GC573_BLOCK_SUBADDR / 4] == subaddr[f->starts]);
				assert(f->regs[GC573_BLOCK_LENGTH / 4] == lengths[f->starts]);
			}
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] == 0x91);
			f->starts++;
			f->index = 0;
		}
		break;
	case GC573_BLOCK_TX:
		assert(f->allow_write && value == 0 && f->command == 0x10);
		assert(f->regs[GC573_BLOCK_SUBADDR / 4] == 0x0f);
		assert(f->regs[GC573_BLOCK_ADDRESS / 4] == 0x90);
		assert(f->regs[GC573_BLOCK_SUBADDR_WIDTH / 4] == 0);
		assert(f->regs[GC573_BLOCK_FIFO_WIDTH / 4] == 0);
		f->tx_bytes++;
		assert(f->tx_bytes == 1);
		break;
	default:
		assert(!"unexpected MMIO write");
	}
}

static int fake_wait(void *ctx, unsigned int *status, unsigned int *armed)
{
	struct fake *f = ctx;

	*status = 8;
	f->regs[GC573_BLOCK_STATUS / 4] = *status;
	assert(gc573_block_observe(*status, armed) == 0);
	if (f->starts == f->fail_at)
		return -ETIMEDOUT;
	*status = 4;
	f->regs[GC573_BLOCK_STATUS / 4] = *status;
	assert(gc573_block_observe(*status, armed) == 1);
	return 0;
}

static int fake_wait_write(void *ctx, unsigned int *status, unsigned int *armed)
{
	struct fake *f = ctx;
	int observed;

	assert(f->write_starts == 1 && f->tx_bytes == 1);
	*status = f->write_fault == 1 ? 8 : f->write_fault == 2 ? 4 :
		f->write_fault == 4 ? 0xffffffff : 1;
	f->regs[GC573_BLOCK_STATUS / 4] = *status;
	observed = gc573_block_observe_write(*status, armed);
	if (observed < 0)
		return observed;
	return observed == 1 ? 0 : -ETIMEDOUT;
}

static int run_write(struct fake *f, struct gc573_write_test_result *result)
{
	struct gc573_signal_result signal;
	const struct gc573_block_io io = {
		.ctx = f, .read = fake_read, .write = fake_write, .wait = fake_wait,
		.wait_write = fake_wait_write,
	};
	int ret;

	f->allow_write = 1;
	ret = gc573_receiver_write_test(&io, &signal, result);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->regs[GC573_GPIO / 4] == 0x1f958);
	return ret;
}

static int run(struct fake *f, struct gc573_signal_result *result)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = fake_read, .write = fake_write, .wait = fake_wait,
	};
	int ret = gc573_receiver_status(&io, result);

	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
	assert(f->regs[GC573_GPIO / 4] == 0x1f958);
	return ret;
}

int main(void)
{
	struct gc573_signal_result result;
	struct gc573_write_test_result write_result;
	struct fake f = baseline();
	unsigned int i, j;

	assert(run(&f, &result) == 0 && result.valid == 0xfff);
	assert(f.starts == 12 && f.fifo_reads == 23);
	assert(result.port0 == 9 && result.port1 == 0 && result.sync == 0x80);
	assert(result.controls[0].subaddr == 4 && result.controls[0].length == 1);
	assert(result.controls[1].subaddr == 0x20 && result.controls[1].length == 4);
	assert(result.controls[6].subaddr == 0x35 && result.controls[6].length == 1);
	assert(result.controls[0].data[0] == 0xb1);
	for (i = 1; i < GC573_CONTROL_READS; i++)
		for (j = 0; j < result.controls[i].length; j++)
			assert(result.controls[i].data[j] == result.controls[i].subaddr + j);
	f = baseline();
	f.port0 = f.port1 = f.sync = 0;
	assert(run(&f, &result) == 0 && result.valid == 0xfff && !result.sync);
	f = baseline();
	f.bank = 1;
	assert(run(&f, &result) == -EOPNOTSUPP && f.starts == 1);
	assert(result.valid == 1);
	f = baseline();
	f.bad_id = 1;
	assert(run(&f, &result) == -ENODEV && f.starts == 2);
	assert(result.valid == 3);
	for (i = 1; i <= 12; i++) {
		f = baseline();
		f.fail_at = i;
		assert(run(&f, &result) == -ETIMEDOUT && f.starts == i);
		assert(result.valid == (1U << (i - 1)) - 1);
		for (j = i > 5 ? i - 6 : 0; j < GC573_CONTROL_READS; j++)
			assert(result.controls[j].length == 0);
	}
	puts("PASS: fixed receiver reads/lengths, bank and ID gates, status bytes,");
	puts("      timeout stops sequence, validity mask, no GPIO/config/reset writes");
	f = baseline();
	assert(run_write(&f, &write_result) == 0 && write_result.bank_verified);
	assert(f.write_starts == 1 && f.starts == 13 && f.fifo_reads == 24);
	for (i = 1; i <= 4; i++) {
		f = baseline();
		f.write_fault = i;
		assert(run_write(&f, &write_result) == (i == 4 ? -ENODEV : -ETIMEDOUT));
		assert(f.write_starts == 1 && f.starts == 12 && !write_result.bank_verified);
	}
	f = baseline();
	f.bad_verify = 1;
	assert(run_write(&f, &write_result) == -EIO && !write_result.bank_verified);
	f = baseline();
	f.fail_at = 13;
	assert(run_write(&f, &write_result) == -ETIMEDOUT && !write_result.bank_verified);
	f = baseline();
	f.revision = 0xa0;
	assert(run_write(&f, &write_result) == -ENODEV && !f.tx_bytes);
	f = baseline();
	f.bank = 0x80;
	assert(run_write(&f, &write_result) == -ENODEV && !f.tx_bytes);
	f = baseline();
	f.bad_id = 1;
	assert(run_write(&f, &write_result) == -ENODEV && !f.tx_bytes);
	for (i = 1; i <= 12; i++) {
		f = baseline();
		f.fail_at = i;
		assert(run_write(&f, &write_result) == -ETIMEDOUT && !f.tx_bytes);
	}
	puts("PASS: one bank-zero write, revision/full-bank/identity gates, distinct");
	puts("      write completion, stale completion/timeout/invalid status refusal,");
	puts("      no retry/read after failed write, fresh readback verification");
	return 0;
}
