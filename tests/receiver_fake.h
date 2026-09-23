/* SPDX-License-Identifier: GPL-2.0-only */
struct fake {
	unsigned int mmio[128], starts, writes, index, tx;
	unsigned int fail_at, failure_seen, wrong_bank_at, slow_at, sleeps, caof, no_completion;
	unsigned int wrong_value_at;
	unsigned int edid_mode, edid_reads;
	unsigned int ddc_reset_selfclear;
	unsigned int input_mode, hpd_writes, hpd_drop, lock_after, lock_reads;
	unsigned char edid[256];
	unsigned int clock_mode, ready_after, ready_reads, requests, bases[4];
	unsigned char clock_data[4][2];
	unsigned long now;
	unsigned char receiver[8][256], bank;
	struct { unsigned int bank, reg, value; } log[256];
};

static struct fake baseline(void)
{
	struct fake f = { 0 };

	memset(f.receiver, 0x92, sizeof(f.receiver));
	f.receiver[0][0] = 0x54;
	f.receiver[0][1] = 0x49;
	f.receiver[0][2] = 5;
	f.receiver[0][3] = 0x68;
	f.receiver[0][4] = 0xb1;
	f.receiver[0][0x13] = 1;
	f.receiver[0][0x16] = 0;
	f.receiver[0][0x19] = 0x20;
	f.receiver[0][0x21] = 4;
	f.mmio[0] = 0x20201015;
	f.mmio[GC573_BOARD_ID / 4] = 0x57300102;
	f.mmio[GC573_GPIO / 4] = 0x1f958;
	f.mmio[GC573_BLOCK_DIVIDER / 4] = 0x4e2;
	f.mmio[GC573_BLOCK_STATUS / 4] = 4;
	return f;
}

static unsigned int read_reg(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;
	unsigned int reg;

	assert(!(offset & 3) && offset / 4 < 128);
	if (offset != GC573_BLOCK_RX)
		return f->mmio[offset / 4];
	assert(!f->failure_seen && f->mmio[GC573_BLOCK_STATUS / 4] == 4);
	assert(f->index < f->mmio[GC573_BLOCK_LENGTH / 4]);
	reg = f->mmio[GC573_BLOCK_SUBADDR / 4] + f->index++;
	assert(reg <= 255);
	if (f->mmio[GC573_BLOCK_ADDRESS / 4] == 0xa9) {
		assert(f->edid_mode && f->receiver[0][0x4b] == 0xa9);
		f->edid_reads++;
		return f->edid[reg];
	}
	if (f->input_mode && f->bank == 0 && f->hpd_writes) {
		if (reg == 0x13) {
			f->lock_reads++;
			return 0x41 | (f->lock_reads >= f->lock_after ? 8 : 0);
		}
		if (reg == 0x19)
			return f->lock_reads >= f->lock_after ? 0xa0 : 0x20;
	}
	if (f->clock_mode && f->bank == 1 && reg == 0x60)
		return ++f->ready_reads > f->ready_after ? 0x19 : 0;
	if (reg == 0x0f)
		return f->wrong_bank_at && f->writes == f->wrong_bank_at ?
			f->bank ^ 1 : f->bank;
	if (f->wrong_value_at && f->writes == f->wrong_value_at)
		return f->receiver[f->bank & 7][reg] ^ 1;
	return f->receiver[f->bank & 7][reg];
}

static void write_reg(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;
	unsigned int reg = f->mmio[GC573_BLOCK_SUBADDR / 4];

	switch (offset) {
	case GC573_GPIO:
		assert(f->input_mode && !f->failure_seen && !f->hpd_writes);
		assert(value == (f->mmio[offset / 4] | 4));
		f->hpd_writes++;
		if (!f->hpd_drop)
			f->mmio[offset / 4] = value;
		break;
	case GC573_BLOCK_DIVIDER:
	case GC573_BLOCK_ADDRESS:
	case GC573_BLOCK_SUBADDR_WIDTH:
	case GC573_BLOCK_SUBADDR:
	case GC573_BLOCK_FIFO_WIDTH:
	case GC573_BLOCK_LENGTH:
		f->mmio[offset / 4] = value;
		break;
	case GC573_BLOCK_TX:
		assert(!f->failure_seen && value <= 255);
		f->tx = value;
		break;
	case GC573_BLOCK_COMMAND:
		assert(value == 0x10 || value == 8 || value == 4);
		if (value == 0x10)
			break;
		assert(!f->failure_seen);
		assert(!f->mmio[GC573_BLOCK_SUBADDR_WIDTH / 4]);
		assert(!f->mmio[GC573_BLOCK_FIFO_WIDTH / 4]);
		f->starts++;
		f->index = 0;
		f->mmio[GC573_BLOCK_STATUS / 4] = 0;
		if (value == 8) {
			assert(f->mmio[GC573_BLOCK_ADDRESS / 4] == 0x91 ||
			       (f->edid_mode && f->mmio[GC573_BLOCK_ADDRESS / 4] == 0xa9));
			break;
		}
		assert(f->mmio[GC573_BLOCK_ADDRESS / 4] == 0x90);
		assert(f->mmio[GC573_BLOCK_LENGTH / 4] == 1);
		assert(f->writes < 256 && reg <= 255);
		f->log[f->writes].bank = f->bank & 7;
		f->log[f->writes].reg = reg;
		f->log[f->writes++].value = f->tx;
		/* A timed-out transfer may already have changed the receiver. */
		if (reg == 0x0f)
			f->bank = f->tx;
		else if (f->caof && !f->bank && (reg == 8 || reg == 13))
			f->receiver[0][reg] &= ~f->tx;
		else
			f->receiver[f->bank & 7][reg] = f->tx;
		if (f->ddc_reset_selfclear && (f->bank == 0 || f->bank == 4) && reg == 0xc5)
			f->receiver[f->bank][reg] &= ~0x10;
		if (f->input_mode && f->bank == 0 && reg == 7)
			f->receiver[0][7] = 0; /* W1C acknowledgement. */
		if (f->caof && f->no_completion != 1 && reg == 0x3a && (f->tx & 0x80)) {
			if (f->bank == 3)
				f->receiver[0][8] |= 0x10;
			if (f->bank == 7 && f->no_completion != 2)
				f->receiver[0][13] |= 0x20;
		}
		if (f->clock_mode && f->bank == 1 && reg == 0x54) {
			unsigned int selector = f->receiver[1][0x51];
			unsigned int slot = selector < 2 ? selector : selector - 0xb0 + 2;

			assert(f->tx == 4 && slot < 4 && slot == f->requests);
			f->bases[f->requests++] = f->receiver[1][0x50];
			f->receiver[1][0x61] = f->clock_data[slot][0];
			f->receiver[1][0x62] = f->clock_data[slot][1];
		}
		break;
	default:
		assert(!"unexpected GPIO/IRQ/reset/DMA MMIO write");
	}
}

static int wait_common(void *ctx, unsigned int *status, unsigned int *armed, int write)
{
	struct fake *f = ctx;
	int (*observe)(unsigned int, unsigned int *) = write ?
		gc573_block_observe_write : gc573_block_observe;

	f->now += f->slow_at == f->starts ? 15000 : 1;
	assert(observe(0, armed) == 0);
	if (f->starts == f->fail_at) {
		*status = f->mmio[GC573_BLOCK_STATUS / 4] = 8;
		f->failure_seen = 1;
		return -ETIMEDOUT;
	}
	*status = f->mmio[GC573_BLOCK_STATUS / 4] = write ? 1 : 4;
	assert(observe(*status, armed) == 1);
	return 0;
}

static int wait_read(void *ctx, unsigned int *status, unsigned int *armed)
{
	return wait_common(ctx, status, armed, 0);
}

static int wait_write(void *ctx, unsigned int *status, unsigned int *armed)
{
	return wait_common(ctx, status, armed, 1);
}

static void sleep_ms(void *ctx, unsigned int ms)
{
	struct fake *f = ctx;

	assert(!f->failure_seen && (ms == 3 || ms == 20 || ms == 100 || ms == 10 || ms == 2 || ms == 1));
	f->now += ms;
	f->sleeps++;
}

static unsigned long time_ms(void *ctx)
{
	return ((struct fake *)ctx)->now;
}
