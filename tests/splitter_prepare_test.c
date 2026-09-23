// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <linux/errno.h>
#include "gc573_block.h"

struct fake {
	unsigned int regs[128], bank, transactions, writes, fifo;
	unsigned int fail_at, wrong_at, slow_at, ready_after, polls, pending;
	unsigned int clock_mode, words[4], data_reads;
	unsigned int timing_mode, map_mode, wrong_mask;
	unsigned int cal_mode, setup_mode, finish_mode, force_ab_ca, pulse_delays, rx_bank, cal_running, cal_ready_after, cal_polls;
	unsigned int tx_mode, ports_mode, all_ports, link_mode, hpd_mode, hpd_polls, lock_after, port_reads, lose_tx_mapping;
	unsigned int edid_mode, edid_enable_mode, activate_mode, c1_status;
	unsigned int video_mode, video_raw, video_setup, selected_port;
	unsigned char edid[256];
	unsigned char tx_ports[4][256];
	unsigned char common[256], rx[4][256];
	unsigned char timer[3];
	unsigned long ms;
	unsigned char chip[2][256], trace[512][3];
};

static unsigned int read_reg(void *ctx, unsigned int offset)
{
	struct fake *f = ctx;
	unsigned int reg, value;

	assert(!(offset & 3) && offset / 4 < 128);
	if (offset != GC573_BLOCK_RX)
		return f->regs[offset / 4];
	assert(f->transactions != f->fail_at);
	assert(f->regs[GC573_BLOCK_COMMAND / 4] == 0x10);
	assert(f->fifo < f->regs[GC573_BLOCK_LENGTH / 4]);
	reg = f->regs[GC573_BLOCK_SUBADDR / 4] + f->fifo++;
	value = reg == 15 ? f->bank : f->chip[f->bank & 1][reg];
	if (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x97) {
		if (reg >= 0x11 && reg <= 0x13) {
			assert(f->timing_mode);
			value = f->timer[reg - 0x11];
		} else {
			assert(f->map_mode && (reg == 0x50 || (reg >= 0x2c && reg <= 0x2f) || (f->tx_mode && reg == 0x20) || (f->all_ports && reg == 0x15)));
			value = f->common[reg];
		}
	}
	if (f->regs[GC573_BLOCK_ADDRESS / 4] >= 0x69 &&
	    f->regs[GC573_BLOCK_ADDRESS / 4] <= 0x6f) {
		unsigned int port = (f->regs[GC573_BLOCK_ADDRESS / 4] - 0x69) / 2;

		assert(f->tx_mode && (f->writes == 132 || f->ports_mode));
		value = f->tx_ports[port][reg];
		if (f->activate_mode && reg == 0xc1 && f->writes >= 2)
			value |= f->c1_status;
		if (f->video_mode && port == f->selected_port && (reg == 6 || reg == 7)) {
			unsigned int raw = f->video_raw << ((f->tx_ports[f->selected_port][7] >> 4) & 7);

			assert(raw < 4096);
			value = reg == 6 ? raw & 255 : (f->tx_ports[f->selected_port][7] & 0xf0) | (raw >> 8);
		}
		f->port_reads++;
	}
	if (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x71) {
		value = reg == 0x0f ? f->rx_bank : f->rx[f->rx_bank][reg];
		if (f->hpd_mode && !f->rx_bank && f->rx[0][0x55] == 0xff) {
			if (reg == 0x13)
				f->hpd_polls++;
			if (f->lock_after && f->hpd_polls >= f->lock_after &&
			    (!f->edid_enable_mode || !(f->rx[0][0xc5] & 1))) {
				if (reg == 0x13)
					value |= 0x10;
				if (reg == 0x19)
					value |= 0x80;
			}
		}
		if (reg == 0x08 && !f->rx_bank && f->cal_running) {
			f->cal_polls++;
			if (f->cal_ready_after && f->cal_polls >= f->cal_ready_after)
				f->rx[0][0x08] |= 0x10;
			value = f->rx[0][0x08];
		}
	}
	if (f->regs[GC573_BLOCK_ADDRESS / 4] == 0xd9)
		value = f->edid[reg];
	if (f->clock_mode && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x59 &&
	    (reg == 0x61 || reg == 0x62)) {
		unsigned int n = f->data_reads / 2;
		unsigned int base = f->words[0] == 0xffff && !f->words[1] ? 0x4b0 : 0xb0;
		unsigned int selector = f->chip[0][0x50] * 256 + f->chip[0][0x51];

		assert(!f->bank && f->data_reads < 8);
		assert(reg == 0x61 + f->data_reads % 2);
		assert(selector == (n < 2 ? n : base + n - 2));
		assert(f->writes && f->trace[f->writes - 1][1] == 0x54);
		assert(f->trace[f->writes - 1][2] == 4);
		value = (f->words[n] >> (8 * (f->data_reads++ % 2))) & 255;
	}
	if (reg == 0x60 && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x59 && f->writes >= 9) {
		f->polls++;
		value = f->ready_after && f->polls >= f->ready_after ? 0x19 : 0;
	}
	if (f->transactions == f->wrong_at)
		value ^= f->wrong_mask ? f->wrong_mask : 1;
	return value;
}

static void write_reg(void *ctx, unsigned int offset, unsigned int value)
{
	struct fake *f = ctx;

	assert(offset == GC573_BLOCK_DIVIDER || offset == GC573_BLOCK_ADDRESS ||
	       offset == GC573_BLOCK_SUBADDR_WIDTH || offset == GC573_BLOCK_SUBADDR ||
	       offset == GC573_BLOCK_FIFO_WIDTH || offset == GC573_BLOCK_LENGTH ||
	       offset == GC573_BLOCK_COMMAND || offset == GC573_BLOCK_TX);
	if (offset == GC573_BLOCK_COMMAND && value != 0x10) {
		assert(value == 4 || value == 8);
		assert(!f->fail_at || f->transactions < f->fail_at);
		if (f->edid_mode && f->regs[GC573_BLOCK_ADDRESS / 4] == 0xd9) {
			assert(value == 8 && f->rx[0][0x4b] == 0xd9);
			assert(f->regs[GC573_BLOCK_LENGTH / 4] == 4);
		} else if (f->timing_mode && f->regs[GC573_BLOCK_ADDRESS / 4] >= 0x96) {
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] == (value == 4 ? 0x96 : 0x97));
			unsigned int reg = f->regs[GC573_BLOCK_SUBADDR / 4];

			assert((reg >= 0x11 && reg <= 0x13) ||
			       (f->map_mode && (reg == 0x50 || (reg >= 0x2c && reg <= 0x2f) || (f->tx_mode && reg == 0x20) || (f->all_ports && reg == 0x15))));
			assert(f->writes >= 23 || f->link_mode);
		} else if (f->ports_mode && f->regs[GC573_BLOCK_ADDRESS / 4] >= 0x68 &&
			   f->regs[GC573_BLOCK_ADDRESS / 4] <= 0x6e &&
			   !(f->regs[GC573_BLOCK_ADDRESS / 4] & 1)) {
			assert(value == 4 && (f->writes >= 132 || f->activate_mode));
			assert(f->all_ports || (f->activate_mode && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x68 + 2 * f->selected_port) ||
			       f->regs[GC573_BLOCK_ADDRESS / 4] == 0x68 ||
			       f->regs[GC573_BLOCK_ADDRESS / 4] == 0x6e);
		} else if (f->regs[GC573_BLOCK_ADDRESS / 4] >= 0x69 &&
			   f->regs[GC573_BLOCK_ADDRESS / 4] <= 0x6f) {
			unsigned int reg = f->regs[GC573_BLOCK_SUBADDR / 4];

			assert(f->tx_mode && value == 8 && (f->writes == 132 || f->ports_mode));
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] & 1);
			assert(f->ports_mode || reg == 3 || reg == 1 || reg == 0x84 || reg == 0x86 || reg == 0x88);
		} else if (f->cal_mode && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x70) {
			assert(value == 4 && (f->writes >= 33 || f->hpd_mode));
		} else if (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x71) {
			unsigned int reg = f->regs[GC573_BLOCK_SUBADDR / 4];

			assert(f->map_mode && value == 8 && (f->writes == 33 || f->cal_mode));
			assert(f->cal_mode || reg == 0x0f || reg == 0 || reg == 0x22 || reg == 0xc5);
		} else {
			assert(f->regs[GC573_BLOCK_ADDRESS / 4] == (value == 4 ? 0x58 : 0x59));
		}
		assert(!f->regs[GC573_BLOCK_SUBADDR_WIDTH / 4]);
		assert(!f->regs[GC573_BLOCK_FIFO_WIDTH / 4]);
		assert((f->map_mode && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x71 &&
		       ((f->regs[GC573_BLOCK_SUBADDR / 4] == 0 &&
			 f->regs[GC573_BLOCK_LENGTH / 4] == 4) ||
			(f->regs[GC573_BLOCK_SUBADDR / 4] == 0x22 &&
			 f->regs[GC573_BLOCK_LENGTH / 4] == 3))) ||
		       f->regs[GC573_BLOCK_LENGTH / 4] == 1 ||
		       (f->edid_mode && f->regs[GC573_BLOCK_ADDRESS / 4] == 0xd9 &&
			f->regs[GC573_BLOCK_LENGTH / 4] == 4) ||
		       (value == 8 && f->regs[GC573_BLOCK_ADDRESS / 4] == 0x59 &&
			!f->regs[GC573_BLOCK_SUBADDR / 4] &&
			f->regs[GC573_BLOCK_LENGTH / 4] == 4));
		assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x4e2);
		f->transactions++;
		f->fifo = 0;
		f->pending = value;
	}
	f->regs[offset / 4] = value;
}

static int wait_io(struct fake *f, unsigned int *status, unsigned int *armed,
		   int write)
{
	assert(f->pending == (write ? 4U : 8U));
	*status = 8;
	if (write)
		assert(!gc573_block_observe_write(*status, armed));
	else
		assert(!gc573_block_observe(*status, armed));
	if (f->transactions == f->fail_at) {
		f->regs[GC573_BLOCK_STATUS / 4] = 8;
		return -ETIMEDOUT;
	}
	if (write) {
		unsigned int reg = f->regs[GC573_BLOCK_SUBADDR / 4];
		unsigned int value = f->regs[GC573_BLOCK_TX / 4];

		assert(f->writes < 512);
		f->trace[f->writes][0] = f->bank;
		f->trace[f->writes][1] = reg;
		f->trace[f->writes++][2] = value;
		if (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x96) {
			f->trace[f->writes - 1][0] = 0x4b;
			if (reg >= 0x11 && reg <= 0x13) {
				assert(f->timing_mode);
				f->timer[reg - 0x11] = value;
			} else {
				assert(f->map_mode && (reg == 0x50 || (reg >= 0x2c && reg <= 0x2f) || (f->tx_mode && reg == 0x20) || (f->all_ports && reg == 0x15)));
				f->common[reg] = f->tx_mode && reg == 0x20 ? 0 : value;
				if (f->tx_mode && reg == 0x20 && value == 2 && f->lose_tx_mapping)
					f->common[0x2c] = 0x98;
			}
		} else if (f->ports_mode && f->regs[GC573_BLOCK_ADDRESS / 4] >= 0x68 &&
			   f->regs[GC573_BLOCK_ADDRESS / 4] <= 0x6e &&
			   !(f->regs[GC573_BLOCK_ADDRESS / 4] & 1)) {
			unsigned int port = (f->regs[GC573_BLOCK_ADDRESS / 4] - 0x68) / 2;

			f->trace[f->writes - 1][0] = 0x34 + port;
			f->tx_ports[port][reg] = reg == 3 ? f->tx_ports[port][reg] & ~value :
				reg == 1 ? value & ~0x27U :
				reg == 0x94 ? value & ~1U : reg == 0x35 ? value & ~0x10U : value;
		} else if (f->regs[GC573_BLOCK_ADDRESS / 4] == 0x70) {
			assert(f->cal_mode);
			f->trace[f->writes - 1][0] = 0x38 + f->rx_bank;
			if (reg == 0x0f) {
				assert(value == 0 || value == 3 || (f->video_mode && value == 2));
				f->rx_bank = value;
			} else if (!f->rx_bank && reg == 0x08) {
				assert(value == 0x30);
				f->rx[0][reg] &= ~value;
			} else if (f->finish_mode && !f->rx_bank && reg == 0xc5) {
				f->rx[0][reg] = value & ~0x10U;
			} else if (f->setup_mode && !f->rx_bank && (reg == 0x56 || reg == 0x57)) {
				/* Do not assume these unverified writes retain their value. */
				f->rx[0][reg] = 0;
			} else {
				f->rx[f->rx_bank][reg] = value;
				if (f->edid_enable_mode && !f->rx_bank && reg == 0x55 && !value)
					f->hpd_polls = 0;
				if (f->rx_bank == 3 && reg == 0x3a)
					f->cal_running = !!(value & 0x80);
			}
		} else if (reg == 15) {
			f->bank = value;
		} else if (f->all_ports == 2 && !f->bank &&
			   (reg == 0x2b || reg == 0x2d || reg == 0x2e || reg == 0x30)) {
			f->chip[0][reg] &= ~value;
		} else if (f->ports_mode && (reg == 0x0c || reg == 0x07)) {
			f->chip[0][reg] = 0;
		} else if (f->finish_mode && reg == 0x0a && (value & 4)) {
			f->chip[0][reg] = value & ~4U;
			if (f->force_ab_ca)
				f->rx[3][0xab] = 0xca;
		} else {
			f->chip[f->bank & 1][reg] = reg == 0x08 || reg == 0xff ||
				(reg == 0x0a && value == 1) ? 0 : value;
		}
	}
	*status = write ? 1 : 4;
	f->regs[GC573_BLOCK_STATUS / 4] = *status;
	if (write)
		assert(gc573_block_observe_write(*status, armed) == 1);
	else
		assert(gc573_block_observe(*status, armed) == 1);
	if (f->transactions == f->slow_at)
		f->ms += 15000;
	return 0;
}

static int wait_read(void *ctx, unsigned int *status, unsigned int *armed)
{
	return wait_io(ctx, status, armed, 0);
}

static int wait_write(void *ctx, unsigned int *status, unsigned int *armed)
{
	return wait_io(ctx, status, armed, 1);
}

static unsigned long time_ms(void *ctx)
{
	return ((struct fake *)ctx)->ms;
}

static void sleep_ms(void *ctx, unsigned int ms)
{
	struct fake *f = ctx;

	assert((f->edid_enable_mode && ms == 500) || ms == 1 || ms == 10 || (f->hpd_mode && ms == 100));
	if (f->all_ports == 2 && ms == 10 && f->writes != 37) {
		assert(f->trace[f->writes - 1][1] == 0x6d);
		assert(!f->bank && (f->common[0x15] & 8));
	} else if (f->cal_mode && ms == 10) {
		assert(f->writes == 37);
		assert(f->trace[36][1] == 0x24 && f->trace[36][2] == 0xf8);
	}
	if (f->finish_mode && ms == 1 && f->writes >= 117) {
		assert(f->writes == 117 && f->trace[116][0] == 0);
		assert(f->trace[116][1] == 0x0a && (f->trace[116][2] & 4));
		f->pulse_delays++;
	}
	f->ms += ms;
}

static struct fake baseline(int recovery)
{
	struct fake f = { .ready_after = 1 };

	f.regs[0] = 0x20201015;
	f.regs[GC573_BOARD_ID / 4] = 0x57300102;
	f.regs[GC573_GPIO / 4] = 0x1fd7c;
	f.regs[GC573_BLOCK_STATUS / 4] = 4;
	f.regs[GC573_BLOCK_DIVIDER / 4] = 0x321;
	f.chip[0][0] = 0x54;
	f.chip[0][1] = 0x49;
	f.chip[0][2] = 0x63;
	f.chip[0][3] = 0x66;
	f.chip[0][0x60] = recovery ? 0 : 0x19;
	f.chip[0][0x0e] = 0xb7;
	f.chip[1][0x73] = 0xa1;
	return f;
}

static int run(struct fake *f, struct gc573_splitter_prepare_result *r)
{
	struct gc573_splitter_result identity;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	unsigned int gpio = f->regs[GC573_GPIO / 4];
	int ret = gc573_splitter_prepare(&io, &identity, r);

	assert(f->regs[GC573_GPIO / 4] == gpio);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake clock_baseline(void)
{
	struct fake f = baseline(0);

	f.clock_mode = 1;
	f.chip[0][0x10] = 0x6e;
	f.chip[0][0xf0] = 0x71;
	f.chip[0][0xf1] = 0x97;
	f.chip[0][0x0e] = 0xb0;
	f.words[0] = 0xffff;
	f.words[1] = 0;
	f.words[2] = 0x91c0;
	f.words[3] = 0xc021;
	return f;
}

static int run_clock(struct fake *f, struct gc573_splitter_clock_result *r)
{
	struct gc573_splitter_result identity;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_clock(&io, &identity, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake timing_baseline(void)
{
	struct fake f = clock_baseline();

	f.timing_mode = 1;
	f.words[0] = 0x0101;
	f.words[1] = 0x0101;
	f.words[2] = 0xecf4;
	f.words[3] = 0xc01f;
	f.timer[2] = 0xb0;
	f.chip[0][0x1e] = 0xc0;
	return f;
}

static int run_timing(struct fake *f, struct gc573_splitter_timing_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_timing(&io, &identity, &clock, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake map_baseline(void)
{
	struct fake f = timing_baseline();

	f.map_mode = 1;
	f.common[0x50] = 0xa5;
	/* Arbitrary snapshot fixture: hardware RX identity is not known yet. */
	f.rx[0][0] = 0x54;
	f.rx[0][1] = 0x49;
	f.rx[0][2] = 0x12;
	f.rx[0][3] = 0x34;
	f.rx[0][0x22] = 0xab;
	f.rx[0][0x23] = 0xcd;
	f.rx[0][0x24] = 0xef;
	f.rx[0][0xc5] = 0x55;
	return f;
}

static int run_map(struct fake *f, struct gc573_splitter_map_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_map(&io, &identity, &clock, &timing, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake cal_baseline(void)
{
	struct fake f = map_baseline();

	f.cal_mode = 1;
	f.cal_ready_after = 1;
	f.rx[0][2] = 0x64;
	f.rx[0][3] = 0x66;
	f.rx[0][0x08] = 0x40;
	f.rx[3][0x59] = 0x8f;
	f.rx[3][0x5a] = 0xff;
	f.rx[3][0xa0] = 5;
	f.rx[3][0xa1] = 6;
	f.rx[3][0xa2] = 7;
	return f;
}

static int run_cal(struct fake *f, struct gc573_splitter_cal_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	struct gc573_splitter_map_result map;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_calibrate(&io, &identity, &clock, &timing, &map, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake setup_baseline(void)
{
	struct fake f = cal_baseline();

	f.setup_mode = 1;
	f.rx[0][0x44] = 0xc0;
	f.rx[0][0x46] = 0x80;
	f.rx[0][0xce] = 0xff;
	f.rx[0][0x3c] = 0xff;
	f.rx[3][0x26] = 0xff;
	f.rx[3][0xe3] = 0xc0;
	return f;
}

static int run_setup(struct fake *f, struct gc573_splitter_setup_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	struct gc573_splitter_map_result map;
	struct gc573_splitter_cal_result cal;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_setup(&io, &identity, &clock, &timing, &map, &cal, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake finish_baseline(unsigned int ca)
{
	struct fake f = setup_baseline();

	f.finish_mode = 1;
	f.force_ab_ca = ca;
	f.rx[0][0xc5] = 3;
	f.rx[0][0x13] = 1;
	f.rx[0][0x19] = 0x20;
	f.chip[0][0x0a] = 0x80;
	return f;
}

static int run_finish(struct fake *f, struct gc573_splitter_finish_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	struct gc573_splitter_map_result map;
	struct gc573_splitter_cal_result cal;
	struct gc573_splitter_setup_result setup;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_finish(&io, &identity, &clock, &timing, &map,
				      &cal, &setup, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake tx_baseline(void)
{
	struct fake f = finish_baseline(0);
	unsigned int i, j;

	f.tx_mode = 1;
	f.rx[0][0xc5] = 2;
	f.common[0x20] = 0x55;
	for (i = 0; i < 4; i++)
		for (j = 0; j < 256; j++)
			f.tx_ports[i][j] = (i * 0x10) ^ j;
	return f;
}

static int run_tx(struct fake *f, struct gc573_splitter_tx_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	struct gc573_splitter_map_result map;
	struct gc573_splitter_cal_result cal;
	struct gc573_splitter_setup_result setup;
	struct gc573_splitter_finish_result finish;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_tx_prepare(&io, &identity, &clock, &timing, &map,
					  &cal, &setup, &finish, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake ports_baseline(unsigned int sink)
{
	struct fake f = tx_baseline();
	unsigned int i;

	f.ports_mode = 1;
	for (i = 0; i < 4; i++) {
		f.tx_ports[i][3] = (i == 1 || i == 2) ? sink : 0;
		f.tx_ports[i][1] = 6;
		f.tx_ports[i][0x84] = 0x84;
		f.tx_ports[i][0x86] = 8;
		f.tx_ports[i][0x88] = 0x11;
		f.tx_ports[i][0x18] = 0xff;
		f.tx_ports[i][0x94] = 0xaa;
		f.tx_ports[i][0x35] = 0xf5;
	}
	f.chip[0][0x0d] = 0xff;
	return f;
}

static int run_ports(struct fake *f, struct gc573_splitter_ports_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	struct gc573_splitter_map_result map;
	struct gc573_splitter_cal_result cal;
	struct gc573_splitter_setup_result setup;
	struct gc573_splitter_finish_result finish;
	struct gc573_splitter_tx_result tx;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_tx_ports(&io, &identity, &clock, &timing, &map,
					&cal, &setup, &finish, &tx, r, f->all_ports);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake link_baseline(void)
{
	struct fake f = ports_baseline(0);
	unsigned int i;

	f.link_mode = 1;
	for (i = 0; i < 4; i++)
		f.common[0x2c + i] = 0x69 + i * 2;
	f.chip[0][5] = 0x16;
	return f;
}

static int run_link(struct fake *f, struct gc573_splitter_link_result *r)
{
	struct gc573_splitter_result identity;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_link_status(&io, &identity, r);

	assert(!f->writes && !f->bank && !f->rx_bank);
	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake hpd_baseline(unsigned int ab)
{
	struct fake f = link_baseline();

	f.hpd_mode = 1;
	f.tx_ports[1][3] = 0x13;
	f.rx[0][0x13] = 1;
	f.rx[0][0x19] = 0x20;
	f.rx[0][0x26] = 0xff;
	f.rx[0][0x55] = 0;
	f.rx[0][0xc5] = 3;
	f.rx[3][0xab] = ab;
	f.rx[3][0x3a] = 0x20;
	return f;
}

static int run_hpd(struct fake *f, struct gc573_splitter_hpd_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_input_hpd(&io, &identity, &link, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	return ret;
}

static struct fake edid_baseline(void)
{
	struct fake f = hpd_baseline(0xca);
	unsigned int i;

	f.edid_mode = 1;
	f.rx[0][0x4b] = 0xd8;
	for (i = 0; i < 256; i++)
		f.edid[i] = i;
	f.edid[0] = f.edid[7] = 0;
	for (i = 1; i < 7; i++)
		f.edid[i] = 255;
	return f;
}

static int run_edid(struct fake *f, struct gc573_splitter_edid_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	int ret = gc573_splitter_edid_read(&io, &identity, &link, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	assert(f->writes <= 1 && !f->bank && !f->rx_bank);
	if (f->writes)
		assert(f->trace[0][0] == 0x38 && f->trace[0][1] == 0x4b &&
		       f->trace[0][2] == 0xd9);
	return ret;
}

static unsigned char splitter_resident[256];

static struct fake edid_enable_baseline(void)
{
	struct fake f = edid_baseline();
	unsigned int i;

	f.edid_enable_mode = 1;
	f.rx[0][0x26] = 0;
	f.rx[0][0x55] = 255;
	f.rx[0][0x34] = 1;
	for (i = 0; i < 256; i++)
		f.edid[i] = splitter_resident[i];
	return f;
}

static int run_edid_enable(struct fake *f, struct gc573_splitter_hpd_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	struct gc573_splitter_edid_result edid;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	unsigned int i;
	int ret = gc573_splitter_edid_enable(&io, &identity, &link, &edid, r);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	for (i = 0; i < f->writes; i++)
		assert((f->trace[i][0] == 0x38 || f->trace[i][0] == 0x3b) &&
		       f->trace[i][1] >= 0x0f);
	return ret;
}

static struct fake activate_baseline(void)
{
	struct fake f = hpd_baseline(0xca);

	f.activate_mode = 1;
	f.selected_port = 1;
	f.tx_ports[1][3] = 0x17;
	f.tx_ports[1][0x84] = 0xe4;
	f.tx_ports[1][0x86] = 0;
	f.tx_ports[1][0xc1] = 0x37;
	f.rx[0][0x13] = 0xbf;
	f.rx[0][0x19] = 0xb0;
	f.rx[0][0x26] = 0;
	f.rx[0][0x55] = 255;
	f.rx[0][0xc5] = 2;
	return f;
}

static int run_activate(struct fake *f, struct gc573_splitter_hpd_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	unsigned int i;
	int ret = gc573_splitter_port_activate(&io, &identity, &link, r, f->selected_port);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c && !f->rx_bank && !f->bank);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	for (i = 0; i < f->writes; i++)
		assert(f->trace[i][0] == (i ? 0x34 + f->selected_port : 0));
	return ret;
}

static struct fake video_baseline(void)
{
	struct fake f = activate_baseline();

	f.video_mode = 1;
	f.video_raw = 288;
	f.tx_ports[1][7] = 0;
	f.tx_ports[1][0x84] = 0x84;
	f.tx_ports[1][0x86] = 8;
	f.tx_ports[1][0xaf] = 0xc5;
	f.rx[0][0x98] = 0;
	f.timer[0] = 0x44;
	f.timer[1] = 0x31;
	f.timer[2] = 3;
	return f;
}

static int run_video(struct fake *f, struct gc573_splitter_video_result *r)
{
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	unsigned int i;
	int ret = gc573_splitter_video_clock(&io, &identity, &link, r, f->video_setup, f->selected_port);

	assert(f->regs[GC573_GPIO / 4] == 0x1fd7c && !f->bank);
	assert(ret || !f->rx_bank);
	assert(f->regs[GC573_BLOCK_DIVIDER / 4] == 0x321);
	for (i = 0; i < f->writes; i++)
		assert((f->trace[i][0] == 0x34 + f->selected_port &&
		       (f->video_setup || f->trace[i][1] == 7 || f->trace[i][1] == 0xaf)) ||
		       (f->video_setup == 2 && f->trace[i][1] == 0x0f &&
		        (f->trace[i][0] == 0x38 || f->trace[i][0] == 0x3a)));
	return ret;
}

static int run_passthrough(struct fake *f, struct gc573_passthrough_result *r)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg,
		.wait = wait_read, .wait_write = wait_write,
		.sleep_ms = sleep_ms, .time_ms = time_ms,
	};

	return gc573_splitter_passthrough(&io, r);
}

int main(void)
{
	static const unsigned char recovery_trace[][3] = {
		{ 0, 0x0a, 1 }, { 0, 0x0a, 0 }, { 0, 0xff, 0xc3 },
		{ 0, 0xff, 0xa5 }, { 0, 0x5f, 4 }, { 0, 0x58, 0x12 },
		{ 0, 0x58, 2 }, { 0, 0x5f, 0 }, { 0, 0xff, 0xff },
		{ 0, 0x0f, 1 }, { 1, 0x73, 0xa5 }, { 1, 0x0f, 0 },
		{ 0, 0x10, 0x6e }, { 0, 0xf0, 0x71 }, { 0, 0x0e, 0xb0 },
		{ 0, 0x08, 15 }, { 0, 0xf0, 0x71 }, { 0, 0xf1, 0x97 },
	};
	struct gc573_splitter_prepare_result r;
	struct gc573_splitter_clock_result clock;
	struct gc573_splitter_timing_result timing;
	struct gc573_splitter_timing_values values;
	struct gc573_splitter_map_result map;
	struct gc573_splitter_cal_result cal;
	struct gc573_splitter_setup_result setup;
	struct gc573_splitter_finish_result finish;
	struct gc573_splitter_tx_result tx;
	struct gc573_splitter_ports_result ports;
	struct gc573_splitter_link_result link;
	struct gc573_splitter_hpd_result hpd;
	struct fake f;
	unsigned int i, j, recovery, total, raw, khz;

	for (recovery = 0; recovery < 2; recovery++) {
		f = baseline(recovery);
		assert(!run(&f, &r) && r.complete && r.ready && r.bank_verified);
		assert(r.bank == 0 && r.phase == 5 && r.engine_status == 0x19);
		assert(r.recovery_used == recovery && r.transactions == f.transactions);
		assert(f.transactions == (recovery ? 51U : 27U));
		assert(f.writes == (recovery ? 18U : 8U) && r.writes_started == f.writes);
		assert(f.chip[0][0xf0] == 0x71 && f.chip[0][0xf1] == 0x97);
		assert(f.chip[0][0x0e] == 0xb0 && f.chip[0][0x08] == 0);
		if (recovery)
			for (i = 0; i < 18; i++)
				for (j = 0; j < 3; j++)
					assert(f.trace[i][j] == recovery_trace[i][j]);
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = baseline(recovery);
			f.fail_at = i;
			assert(run(&f, &r) == -ETIMEDOUT && !r.complete);
			assert(f.transactions == i);
		}
	}
	f = baseline(1);
	f.ready_after = 3;
	assert(!run(&f, &r) && r.poll_samples == 3 && f.transactions == 53);
	f = baseline(1);
	f.ready_after = 0;
	assert(run(&f, &r) == -ETIMEDOUT && !r.ready && !r.complete);
	assert(r.poll_samples == 50 && f.writes == 9 && f.transactions == 73);
	f = baseline(1);
	f.slow_at = 5;
	assert(run(&f, &r) == -ETIMEDOUT && f.transactions == 5 && !r.complete);
	/* A mismatched verified byte stops before any later register write. */
	f = baseline(0);
	f.wrong_at = 26;
	assert(run(&f, &r) == -EIO && !r.complete && f.transactions == 26);
	f = baseline(1);
	f.wrong_at = 27;
	assert(run(&f, &r) == -EIO && !r.complete && !r.bank_verified);
	for (i = 0; i < 7; i++) {
		f = baseline(0);
		switch (i) {
		case 0:
			f.regs[0] = 0;
			break;
		case 1:
			f.chip[0][2] = 0x64;
			break;
		case 2:
			f.bank = 0x80;
			break;
		case 3:
			f.regs[GC573_GPIO / 4] &= ~0x20U;
			break;
		case 4:
			f.regs[GC573_IRQ_ENABLE / 4] = 1;
			break;
		case 5:
			f.regs[GC573_IRQ_STATUS / 4] = 1;
			break;
		case 6:
			f.regs[GC573_BLOCK_STATUS / 4] = 8;
			break;
		}
		assert(run(&f, &r) < 0 && !f.writes && !r.preflight_complete);
	}
	f = clock_baseline();
	assert(!run_clock(&f, &clock) && clock.ops.complete && clock.cleanup_complete);
	assert(clock.data_valid == 15 && clock.valid && clock.selector_base == 0x4b0);
	assert(clock.khz == 22000 && clock.raw == 2200000);
	assert(f.transactions == 64 && f.writes == 23 && f.data_reads == 8);
	assert(clock.ops.bank_verified && !clock.ops.bank && !f.chip[0][0x5f]);
	assert(f.trace[0][1] == 0xff && f.trace[0][2] == 0xc3);
	assert(f.trace[1][1] == 0xff && f.trace[1][2] == 0xa5);
	assert(f.trace[22][1] == 0xff && f.trace[22][2] == 0xff);
	f = clock_baseline();
	f.words[0] = 0;
	f.words[1] = 0xffff;
	f.words[2] = 22000;
	f.words[3] = 0;
	assert(!run_clock(&f, &clock) && clock.selector_base == 0xb0 && clock.khz == 22000);
	for (i = 1; i <= 64; i++) {
		f = clock_baseline();
		f.fail_at = i;
		assert(run_clock(&f, &clock) == -ETIMEDOUT && !clock.ops.complete);
		assert(f.transactions == i && !clock.cleanup_complete);
	}
	for (i = 0; i < 5; i++) {
		static const unsigned char regs[] = { 0x60, 0x10, 0xf0, 0xf1, 0x0e };

		f = clock_baseline();
		f.chip[0][regs[i]] ^= 1;
		assert(run_clock(&f, &clock) == -EOPNOTSUPP && !f.writes);
		assert(!clock.ops.preflight_complete);
	}
	f = clock_baseline();
	f.slow_at = 10;
	assert(run_clock(&f, &clock) == -ETIMEDOUT && f.transactions == 10);
	f = clock_baseline();
	f.wrong_at = 14;
	assert(run_clock(&f, &clock) == -EIO && !clock.ops.bank_verified);
	f = clock_baseline();
	f.words[2] = 0;
	f.words[3] = 0;
	assert(run_clock(&f, &clock) == -ERANGE && clock.cleanup_complete);
	assert(!clock.valid && !clock.ops.complete && clock.data_valid == 15);
	assert(!gc573_splitter_clock_decode(10000, 0, &raw, &khz) && khz == 10000);
	assert(!gc573_splitter_clock_decode(34000, 0, &raw, &khz) && khz == 34000);
	assert(gc573_splitter_clock_decode(9999, 0, &raw, &khz) == -ERANGE);
	assert(gc573_splitter_clock_decode(34001, 0, &raw, &khz) == -ERANGE);
	assert(gc573_splitter_clock_decode(0xffff, 0xffff, &raw, &khz) == -ERANGE);
	assert(gc573_splitter_clock_decode(0x10000, 0, &raw, &khz) == -EINVAL);
	assert(gc573_splitter_clock_decode(0, 0x10000, &raw, &khz) == -EINVAL);
	f = timing_baseline();
	assert(!run_timing(&f, &timing) && timing.complete && timing.bank_verified);
	assert(f.transactions == 81 && f.writes == 28 && timing.transactions == 81);
	assert(timing.writes_started == 5 && timing.steps_verified == 5);
	assert(timing.before_valid == 31 && timing.after_valid == 31);
	assert(timing.mapping_verified && timing.values.ticks == 209220);
	assert(f.timer[0] == 0x44 && f.timer[1] == 0x31 && f.timer[2] == 0xb3);
	assert(f.chip[0][0x1e] == 0xd4 && f.chip[0][0x1f] == 0xec);
	for (i = 0; i < 3; i++) {
		assert(f.trace[23 + i][0] == 0x4b);
		assert(f.trace[23 + i][1] == 0x11 + i);
	}
	assert(f.trace[26][1] == 0x1e && f.trace[27][1] == 0x1f);
	for (i = 1; i <= 81; i++) {
		f = timing_baseline();
		f.fail_at = i;
		assert(run_timing(&f, &timing) == -ETIMEDOUT && !timing.complete);
		assert(f.transactions == i);
		if (i <= 64)
			assert(!timing.writes_started);
	}
	for (i = 0; i < 5; i++) {
		f = timing_baseline();
		f.wrong_at = 68 + i * 3;
		assert(run_timing(&f, &timing) == -EIO && !timing.complete);
		assert(timing.steps_verified == i && timing.writes_started == i + 1);
	}
	f = timing_baseline();
	f.wrong_at = 65;
	assert(run_timing(&f, &timing) == -EOPNOTSUPP && !timing.writes_started);
	f = timing_baseline();
	f.wrong_at = 81;
	assert(run_timing(&f, &timing) == -EIO && !timing.bank_verified);
	f = timing_baseline();
	f.slow_at = 66;
	assert(run_timing(&f, &timing) == -ETIMEDOUT && !timing.writes_started);
	f = timing_baseline();
	f.words[2] = 34000;
	f.words[3] = 0;
	assert(run_timing(&f, &timing) == -ERANGE && !timing.writes_started);
	assert(!timing.clock_error && f.writes == 23 && f.transactions == 64);
	f = map_baseline();
	assert(!run_map(&f, &map) && map.complete && map.bank_verified);
	assert(f.transactions == 103 && f.writes == 33 && map.transactions == 103);
	assert(map.writes_started == 5 && map.steps_verified == 5 && map.rx_valid == 15);
	assert(map.tx_mapping_verified && map.rx_mapping_verified && map.rx_bank_verified);
	assert(map.before_valid == 31 && map.after_valid == 31 && !map.timing_error);
	assert(f.common[0x50] == 0xa1 && map.rx_id[2] == 0x12 && map.rx_id[3] == 0x34);
	assert(map.rx_reset[0] == 0xab && map.rx_reset[1] == 0xcd && map.rx_reset[2] == 0xef);
	assert(map.rx_c5 == 0x55);
	for (i = 0; i < 5; i++) {
		assert(f.trace[28 + i][0] == 0x4b);
		assert(f.trace[28 + i][1] == (i ? 0x2b + i : 0x50));
		assert(f.trace[28 + i][2] == (i ? 0x67 + i * 2 : 0xa1));
	}
	for (i = 1; i <= 103; i++) {
		f = map_baseline();
		f.fail_at = i;
		assert(run_map(&f, &map) == -ETIMEDOUT && !map.complete);
		assert(f.transactions == i);
		if (i <= 81)
			assert(!map.writes_started && map.timing_error == -ETIMEDOUT);
	}
	for (i = 0; i < 5; i++) {
		f = map_baseline();
		f.wrong_at = 85 + i * 3;
		f.wrong_mask = i ? 1 : 4;
		assert(run_map(&f, &map) == -EIO && !map.complete && !map.rx_valid);
		assert(map.writes_started == i + 1 && map.steps_verified == i);
	}
	f = map_baseline();
	f.wrong_at = 82;
	assert(run_map(&f, &map) == -EOPNOTSUPP && !map.writes_started);
	f = map_baseline();
	f.wrong_at = 98;
	assert(run_map(&f, &map) == -EOPNOTSUPP && !map.rx_valid);
	f = map_baseline();
	f.rx_bank = 1;
	assert(run_map(&f, &map) == -EOPNOTSUPP && !map.rx_bank_verified);
	assert(map.rx_valid == 1 && f.transactions == 99);
	f = map_baseline();
	f.wrong_at = 103;
	assert(run_map(&f, &map) == -EIO && !map.bank_verified && !map.complete);
	f = map_baseline();
	f.slow_at = 83;
	assert(run_map(&f, &map) == -ETIMEDOUT && !map.writes_started);
	/* Reject unlisted registers/lengths without even accessing callbacks. */
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_map_read(&io, &result, 0x51) == -EINVAL);
		assert(gc573_splitter_map_write(&io, &result, 0x20, 2) == -EINVAL);
		assert(gc573_splitter_rx_read(&io, &result, 0x22, 4) == -EINVAL);
		assert(gc573_splitter_rx_read(&io, &result, 0, 1) == -EINVAL);
	}
	f = cal_baseline();
	assert(!run_cal(&f, &cal) && cal.complete && cal.cleanup_complete);
	assert(cal.bank_verified && !cal.bank && !f.rx_bank && !f.cal_running);
	assert(cal.preflight_complete && cal.reset_complete && cal.phase == 7);
	assert(cal.initial_flags == 0x40 && cal.flags == 0x50 && cal.flags_valid == 3);
	assert(cal.poll_samples == 1 && cal.completion_seen && !cal.poll_error);
	assert(cal.values_valid == 7 && cal.values[0] == 0xff &&
	       cal.values[1] == 0x8f && cal.values[2] == 0x8f);
	assert(cal.writes_started == 40 && cal.steps_completed == 40 && f.writes == 73);
	assert(cal.transactions == 201 && f.transactions == 201 && f.ms == 10);
	assert(f.rx[0][0x08] == 0x40 && !f.rx[0][0x24] && !(f.rx[0][0x29] & 1));
	assert(f.rx[3][0xa0] == 5 && f.rx[3][0xa1] == 6 && f.rx[3][0xa2] == 7);
	/* Independently transcribed reset prefix and delay placement. */
	{
		static const unsigned char reg[] = { 0x22, 0x23, 0x22, 0x24, 0x23, 0x22, 0x24 };
		static const unsigned char value[] = { 8, 1, 0x17, 0xf8, 0xa0, 0, 0 };

		for (i = 0; i < 7; i++) {
			assert(f.trace[33 + i][0] == 0x38);
			assert(f.trace[33 + i][1] == reg[i]);
			assert(f.trace[33 + i][2] == value[i]);
		}
	}
	for (i = 1; i <= 201; i++) {
		f = cal_baseline();
		f.fail_at = i;
		assert(run_cal(&f, &cal) == -ETIMEDOUT && !cal.complete);
		assert(f.transactions == i && !cal.cleanup_complete);
		if (i <= 103)
			assert(!cal.writes_started && cal.prerequisite_error == -ETIMEDOUT);
	}
	f = cal_baseline();
	f.cal_ready_after = 3;
	assert(!run_cal(&f, &cal) && cal.poll_samples == 3 && f.ms == 12);
	f = cal_baseline();
	f.cal_ready_after = 0;
	assert(run_cal(&f, &cal) == -ETIMEDOUT && cal.cleanup_complete && !cal.complete);
	assert(cal.poll_samples == 32 && !cal.completion_seen && cal.poll_error == -ETIMEDOUT);
	assert(f.transactions == 232 && f.writes == 73 && f.ms == 42);
	/* Every transport failure in the no-completion teardown stops as well. */
	for (i = 201; i <= 232; i++) {
		f = cal_baseline();
		f.cal_ready_after = 0;
		f.fail_at = i;
		assert(run_cal(&f, &cal) == -ETIMEDOUT && !cal.cleanup_complete);
		assert(f.transactions == i);
	}
	f = cal_baseline();
	f.rx[0][2] = 0x63;
	assert(run_cal(&f, &cal) == -ENODEV && !cal.writes_started);
	f = cal_baseline();
	f.rx[0][0x08] |= 0x10;
	assert(run_cal(&f, &cal) == -EBUSY && cal.reset_complete && cal.writes_started == 7);
	f = cal_baseline();
	f.wrong_at = 118;
	assert(run_cal(&f, &cal) == -EIO && !cal.reset_complete && !cal.bank_verified);
	f = cal_baseline();
	f.wrong_at = 122;
	assert(run_cal(&f, &cal) == -EIO && !cal.bank_verified);
	f = cal_baseline();
	f.slow_at = 104;
	assert(run_cal(&f, &cal) == -ETIMEDOUT && !cal.writes_started);
	for (i = 0; i < 5; i++) {
		static const unsigned int masks[] = { 1, 1, 4, 0x10, 0x20 };

		f = cal_baseline();
		f.wrong_at = 197 + i;
		f.wrong_mask = masks[i];
		assert(run_cal(&f, &cal) == -EIO && !cal.cleanup_complete && !cal.complete);
	}
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_rx_control_read(&io, &result, 0xf0) == -EINVAL);
		assert(gc573_splitter_rx_control_write(&io, &result, 0x59, 0) == -EINVAL);
		assert(gc573_splitter_rx_control_write(&io, &result, 0xc5, 0) == -EINVAL);
	}
	f = setup_baseline();
	assert(!run_setup(&f, &setup) && setup.complete && setup.bank_verified);
	assert(setup.phase == 3 && !setup.bank && !f.rx_bank);
	assert(setup.transactions == 299 && f.transactions == 299 && f.writes == 106);
	assert(setup.writes_started == 33 && setup.steps_completed == 33);
	assert(setup.steps_verified == 31 && setup.last_step == 32);
	assert(f.rx[0][0x44] == 0xd9 && f.rx[0][0x46] == 0x95);
	assert(f.rx[0][0xce] == 0x5f && f.rx[0][0x3c] == 0xde);
	assert(f.rx[3][0x26] == 0xdf && f.rx[3][0xe3] == 0xc3);
	assert(f.rx[3][0x27] == 0x9f && f.rx[3][0x28] == 0x9f && f.rx[3][0x29] == 0x9f);
	assert(f.rx[3][0xf0] == 0xa0 && f.rx[0][0xe3] == 4);
	assert(f.rx[0][0x28] == 0xd9 && f.rx[0][0x26] == 0xff);
	assert(f.trace[73][1] == 0x56 && f.trace[74][1] == 0x57);
	assert(f.trace[105][1] == 0x42 && !(f.trace[105][2] & 0x20));
	for (i = 1; i <= 299; i++) {
		f = setup_baseline();
		f.fail_at = i;
		assert(run_setup(&f, &setup) == -ETIMEDOUT && !setup.complete);
		assert(f.transactions == i);
		if (i <= 201)
			assert(!setup.writes_started && setup.prerequisite_error == -ETIMEDOUT);
	}
	for (i = 2; i < 33; i++) {
		f = setup_baseline();
		f.wrong_at = 208 + (i - 2) * 3;
		f.wrong_mask = 0xff;
		assert(run_setup(&f, &setup) == -EIO && !setup.complete);
		assert(setup.steps_verified == i - 2 && setup.steps_completed == i);
		assert(setup.writes_started == i + 1 && f.transactions == f.wrong_at);
	}
	f = setup_baseline();
	f.wrong_at = 299;
	assert(run_setup(&f, &setup) == -EIO && !setup.bank_verified);
	f = setup_baseline();
	f.slow_at = 202;
	assert(run_setup(&f, &setup) == -ETIMEDOUT && !setup.writes_started);
	f = setup_baseline();
	f.cal_ready_after = 0;
	assert(run_setup(&f, &setup) == -ETIMEDOUT && !setup.writes_started);
	assert(setup.prerequisite_error == -ETIMEDOUT && f.writes == 73);
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_rx_setup_read(&io, &result, 0xc5) == -EINVAL);
		assert(gc573_splitter_rx_setup_write(&io, &result, 0x22, 0) == -EINVAL);
	}
	for (j = 0; j < 2; j++) {
		f = finish_baseline(j);
		assert(!run_finish(&f, &finish) && finish.complete);
		assert(finish.phase == 5 && finish.bank_verified && !finish.bank);
		assert(finish.control_bank_verified && !f.bank && !f.rx_bank);
		assert(finish.transactions == 371 + j * 9 && f.transactions == finish.transactions);
		assert(finish.writes_started == 23 + j * 3 && f.writes == 129 + j * 3);
		assert(finish.steps_completed == finish.writes_started);
		assert(finish.steps_verified == 21 + j * 3 && f.pulse_delays == 1 && f.ms == 11);
		assert(finish.ab_valid && finish.ab_ca_branch == j);
		assert(finish.ab_before == (j ? 0xcaU : 0x4aU));
		assert(f.rx[3][0xab] == (j ? 0 : 0x4a) && f.rx[3][0xac] == (j ? 0 : 0x40));
		assert(f.rx[0][0xc5] == 3 && f.chip[0][0x0a] == 0x80);
		assert(f.rx[0][0x55] == 0 && f.rx[0][0x26] == 0xff);
		assert(f.rx[3][0x20] == 0x1b && f.rx[3][0x21] == 3);
		assert(finish.status_valid == 3 && finish.status[0] == 1 && finish.status[1] == 0x20);
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = finish_baseline(j);
			f.fail_at = i;
			assert(run_finish(&f, &finish) == -ETIMEDOUT && !finish.complete);
			assert(f.transactions == i);
			if (i <= 299)
				assert(!finish.writes_started && finish.prerequisite_error == -ETIMEDOUT);
		}
	}
	{
		static const unsigned int readbacks[] = {
			302, 305, 308, 311, 314, 317, 320, 323, 328, 333, 336,
			340, 343, 346, 349, 352, 355, 358, 361, 364, 367,
		};

		for (i = 0; i < sizeof(readbacks) / sizeof(readbacks[0]); i++) {
			f = finish_baseline(0);
			f.wrong_at = readbacks[i];
			f.wrong_mask = 0xff;
			assert(run_finish(&f, &finish) == -EIO && !finish.complete);
			assert(finish.steps_verified == i && f.transactions == f.wrong_at);
		}
	}
	for (i = 340; i <= 346; i += 3) {
		f = finish_baseline(1);
		f.wrong_at = i;
		assert(run_finish(&f, &finish) == -EIO && !finish.complete);
	}
	for (i = 368; i <= 369; i++) {
		f = finish_baseline(0);
		f.wrong_at = i;
		assert(run_finish(&f, &finish) == -EIO && !finish.control_bank_verified);
	}
	f = finish_baseline(0);
	f.slow_at = 300;
	assert(run_finish(&f, &finish) == -ETIMEDOUT && !finish.writes_started);
	f = finish_baseline(0);
	f.cal_ready_after = 0;
	assert(run_finish(&f, &finish) == -ETIMEDOUT && !finish.writes_started);
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_rx_finish_read(&io, &result, 0x22) == -EINVAL);
		assert(gc573_splitter_rx_finish_write(&io, &result, 0x13, 0) == -EINVAL);
	}
	f = tx_baseline();
	assert(!run_tx(&f, &tx) && tx.complete && tx.reset_complete);
	assert(tx.common_mapping_verified && tx.mapping_valid == 15);
	assert(tx.bank_verified && tx.control_bank_verified && tx.status_valid == 3);
	assert(tx.transactions == 408 && f.transactions == 408 && f.writes == 132);
	assert(tx.writes_started == 3 && tx.steps_verified == 2 && f.port_reads == 20);
	assert(f.rx[0][0xc5] == 3 && !f.common[0x20]);
	assert(f.trace[129][0] == 0x38 && f.trace[129][1] == 0xc5 && f.trace[129][2] == 3);
	assert(f.trace[130][0] == 0x4b && f.trace[130][1] == 0x20 && f.trace[130][2] == 2);
	assert(f.trace[131][0] == 0x4b && f.trace[131][1] == 0x20 && !f.trace[131][2]);
	for (i = 0; i < 4; i++) {
		static const unsigned char regs[] = { 3, 1, 0x84, 0x86, 0x88 };

		assert(tx.port_valid[i] == 31 && tx.mapping[i] == 0x69 + i * 2);
		for (j = 0; j < 5; j++)
			assert(tx.ports[i][j] == ((i * 0x10) ^ regs[j]));
	}
	for (i = 1; i <= 408; i++) {
		f = tx_baseline();
		f.fail_at = i;
		assert(run_tx(&f, &tx) == -ETIMEDOUT && !tx.complete && f.transactions == i);
		if (i <= 371)
			assert(!tx.writes_started && tx.prerequisite_error == -ETIMEDOUT);
		if (i >= 385 && i <= 404) {
			assert(f.port_reads == i - 385);
			assert(tx.port_valid[(i - 385) / 5] == (1U << ((i - 385) % 5)) - 1);
		}
	}
	for (i = 381; i <= 384; i++) {
		f = tx_baseline();
		f.wrong_at = i;
		assert(run_tx(&f, &tx) == -EOPNOTSUPP && !f.port_reads && !tx.complete);
	}
	f = tx_baseline();
	f.lose_tx_mapping = 1;
	assert(run_tx(&f, &tx) == -EOPNOTSUPP && tx.reset_complete && !f.port_reads);
	f = tx_baseline();
	f.wrong_at = 372;
	assert(run_tx(&f, &tx) == -EOPNOTSUPP && !tx.writes_started);
	for (i = 375; i <= 380; i += 5) {
		f = tx_baseline();
		f.wrong_at = i;
		assert(run_tx(&f, &tx) == -EIO && !tx.reset_complete && !f.port_reads);
	}
	for (i = 405; i <= 406; i++) {
		f = tx_baseline();
		f.wrong_at = i;
		assert(run_tx(&f, &tx) == -EIO && !tx.complete);
	}
	f = tx_baseline();
	f.slow_at = 373;
	assert(run_tx(&f, &tx) == -ETIMEDOUT && !tx.writes_started);
	f = tx_baseline();
	f.cal_ready_after = 0;
	assert(run_tx(&f, &tx) == -ETIMEDOUT && !tx.writes_started);
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_tx_port_read(&io, &result, 4, 3) == -EINVAL);
		assert(gc573_splitter_tx_port_read(&io, &result, 0, 0xff) == -EINVAL);
		assert(gc573_splitter_tx_reset_write(&io, &result, 4) == -EINVAL);
	}
	for (j = 0; j < 2; j++) {
		f = ports_baseline(j);
		assert(!run_ports(&f, &ports) && ports.complete && ports.mapping_verified);
		assert(ports.ports_complete == 9 && ports.no_sink_mask == (j ? 0U : 9U));
		assert(ports.transactions == (j ? 577U : 607U) && f.transactions == ports.transactions);
		assert(ports.writes_started == (j ? 54U : 66U) && f.writes == 132 + ports.writes_started);
		assert(ports.steps_verified == (j ? 38U : 44U) && ports.steps_completed == ports.writes_started);
		assert(ports.bank_verified && ports.control_bank_verified && ports.status_valid == 3);
		for (i = 0; i < 2; i++) {
			unsigned int port = i ? 3 : 0;

			assert(ports.sink_valid[i] == 3 && ports.snapshot_valid[i] == 31);
			assert(f.tx_ports[port][1] == 0 && f.tx_ports[port][0x84] == 0xe4);
			assert(f.tx_ports[port][0x86] == 0 && f.tx_ports[port][0x88] == 0x13);
			assert(f.tx_ports[port][0x18] == 0x23 && f.tx_ports[port][0x94] == 0xaa);
			assert(f.tx_ports[port][0x35] == 0xe5 && !f.tx_ports[port][0x19]);
		}
		assert(f.chip[0][0x0d] == 0x3c && !f.chip[0][0x0c]);
		for (i = 1; i < 3; i++) {
			assert(f.tx_ports[i][1] == 6 && f.tx_ports[i][0x84] == 0x84);
			assert(f.tx_ports[i][0x86] == 8 && f.tx_ports[i][0x88] == 0x11);
		}
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = ports_baseline(j);
			f.fail_at = i;
			assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.complete);
			assert(f.transactions == i);
			if (i <= 408)
				assert(!ports.writes_started && ports.prerequisite_error == -ETIMEDOUT);
		}
	}
	{
		static const unsigned int readbacks[] = {
			418, 421, 424, 427, 430, 433, 436, 439, 444, 449, 454,
			461, 468, 473, 478, 483, 486, 489, 492, 495, 498, 503,
		};

		for (j = 0; j < 2; j++) {
			for (i = 0; i < sizeof(readbacks) / sizeof(readbacks[0]); i++) {
				f = ports_baseline(0);
				f.wrong_at = readbacks[i] + 90 * j;
				f.wrong_mask = 0xff;
				assert(run_ports(&f, &ports) == -EIO && !ports.complete);
				assert(ports.steps_verified == i + j * 22 && f.transactions == f.wrong_at);
			}
		}
	}
	for (i = 409; i <= 413; i++) {
		f = ports_baseline(0);
		f.wrong_at = i;
		assert(run_ports(&f, &ports) == -EOPNOTSUPP && !ports.writes_started);
	}
	for (i = 604; i <= 605; i++) {
		f = ports_baseline(0);
		f.wrong_at = i;
		assert(run_ports(&f, &ports) == -EIO && !ports.complete);
	}
	f = ports_baseline(0);
	f.slow_at = 414;
	assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.writes_started);
	f = ports_baseline(0);
	f.cal_ready_after = 0;
	assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.writes_started);
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_tx_control_write(&io, &result, 4, 1, 0) == -EINVAL);
		assert(gc573_splitter_tx_control_write(&io, &result, 0, 0xc3, 0) == -EINVAL);
		assert(gc573_splitter_tx_control_write(&io, &result, 3, 3, 0) == -EINVAL);
	}
	/* Full cold-start order 0,3,1,2, including the extra reset for 1/2. */
	for (j = 0; j < 2; j++) {
		f = ports_baseline(j);
		f.all_ports = 1;
		f.common[0x15] = 0xa5;
		assert(!run_ports(&f, &ports) && ports.complete);
		assert(ports.ports_complete == 15 && ports.common_enabled);
		assert(ports.transactions == (j ? 960U : 1020U));
		assert(ports.writes_started == (j ? 187U : 211U));
		assert(ports.steps_verified == (j ? 141U : 153U));
		assert(f.common[0x15] == 0xad && !f.chip[0][0x0d]);
		assert(ports.no_sink_mask == (j ? 0U : 15U));
		for (i = 0; i < 4; i++) {
			assert(ports.snapshot_valid[i] == 31 && ports.sink_valid[i] == 3);
			assert(!f.tx_ports[i][1] && f.tx_ports[i][0x84] == 0xe4);
			assert(!f.tx_ports[i][0x86]);
			assert(f.tx_ports[i][0x88] == ((i == 1 || i == 2) ? 0x57 : 0x13));
		}
		for (i = 1; i < 3; i++) {
			assert((f.tx_ports[i][0xc1] & 5) == 1);
			assert((f.tx_ports[i][0xc0] & 0x11) == 0x11);
			assert((f.tx_ports[i][0x34] & 0xc0) == 0x80);
			assert((f.tx_ports[i][0x3a] & 0xfc) == 0x90);
			assert(f.tx_ports[i][0x93] == 0x40);
			assert((f.tx_ports[i][0x94] & 0x3e) == 0x26);
			assert((f.tx_ports[i][0xc3] & 15) == 1);
			assert(!f.tx_ports[i][0x8a] && f.tx_ports[i][0x8b] == 7);
		}
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = ports_baseline(j);
			f.all_ports = 1;
			f.fail_at = i;
			assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.complete);
			assert(f.transactions == i);
		}
	}
	/* Every added retained-field check rejects corrupted readback immediately. */
	{
		static const unsigned int reset_checks[] = {
			668, 673, 678, 683, 686, 689, 692, 695, 698, 703,
		};
		static const unsigned int shutdown_checks[] = {
			708, 711, 714, 717, 720, 723, 726, 729, 734, 739, 744,
			751, 758, 763, 768, 773, 776, 779, 782, 785, 788, 793,
		};

		for (j = 0; j < 2; j++) {
			for (i = 0; i < 54; i++) {
				f = ports_baseline(0);
				f.all_ports = 1;
				f.wrong_at = 200 * j + (i < 22 ? (i ? 598 + i * 3 : 596) :
					i < 32 ? reset_checks[i - 22] : shutdown_checks[i - 32]);
				f.wrong_mask = 255;
				assert(run_ports(&f, &ports) == -EIO && !ports.complete);
				assert(f.transactions == f.wrong_at && !ports.common_enabled);
			}
		}
	}
	f = ports_baseline(0);
	f.all_ports = 1;
	f.wrong_at = 996;
	f.wrong_mask = 8;
	assert(run_ports(&f, &ports) == -EIO && !ports.common_enabled);
	f = ports_baseline(0);
	f.all_ports = 1;
	f.slow_at = 594;
	assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.complete);
	assert(f.transactions == 594 && ports.ports_complete == 9);
	for (i = 1017; i <= 1018; i++) {
		f = ports_baseline(0);
		f.all_ports = 1;
		f.wrong_at = i;
		assert(run_ports(&f, &ports) == -EIO && !ports.complete);
	}
	puts("PASS: all four TX ports, 1020/960 transfer failures, reset order and shared bit preservation");
	for (j = 0; j < 2; j++) {
		f = ports_baseline(j);
		f.all_ports = 2;
		f.chip[0][0x6b] = 0xff;
		f.chip[0][0x6c] = 0xff;
		f.chip[0][0x6d] = 0xff;
		f.chip[1][0x10] = 0xff;
		assert(!run_ports(&f, &ports) && ports.complete && ports.tail_complete);
		assert(ports.transactions == (j ? 1058U : 1118U));
		assert(ports.writes_started == (j ? 222U : 246U));
		assert(ports.steps_verified == (j ? 169U : 181U));
		assert(!f.bank && ports.control_bank_verified);
		assert(f.chip[0][0x6b] == 0xc3 && f.chip[0][0x6c] == 0xe7);
		assert(f.chip[0][0x6d] == 0xcf && f.chip[1][0x10] == 0xf7);
		assert((f.chip[1][0x1d] & 0x80) && (f.chip[1][0x20] & 0x78) == 0x78);
		for (i = 0; i < 4; i++) {
			assert(!(f.tx_ports[i][0x41] & 1) && (f.tx_ports[i][0xc1] & 1));
			assert(f.tx_ports[i][0x88] == ((i == 0 || i == 3) ? 0x0b : 0x57));
			assert(f.tx_ports[i][0x84] == ((i == 0 || i == 3) ? 0x60 : 0xe4));
		}
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = ports_baseline(j);
			f.all_ports = 2;
			f.fail_at = i;
			assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.complete);
			assert(f.transactions == i);
		}
	}
	{
		static const unsigned int checks[] = {
			1001, 1004, 1007, 1010, 1013, 1016,
			1021, 1024, 1027, 1030, 1033, 1036, 1039, 1042, 1045, 1058,
			1061, 1064, 1067, 1070, 1073, 1076,
			1079, 1082, 1085, 1088, 1091, 1094,
		};

		for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
			f = ports_baseline(0);
			f.all_ports = 2;
			f.wrong_at = checks[i];
			f.wrong_mask = 255;
			assert(run_ports(&f, &ports) == -EIO && !ports.tail_complete);
			assert(f.transactions == f.wrong_at);
		}
	}
	f = ports_baseline(0);
	f.all_ports = 2;
	f.slow_at = 1058;
	assert(run_ports(&f, &ports) == -ETIMEDOUT && !ports.tail_complete);
	assert(f.transactions == 1058);
	puts("PASS: shared TX tail, 1118/1058 transfer failures, 28 readbacks, bank restoration, deadline");
	f = link_baseline();
	assert(!run_link(&f, &link) && link.complete && link.transactions == 36);
	assert(link.mapping_verified && link.rx_identity_verified && link.banks_verified == 3);
	assert(link.rx_valid == 0x3ffff && link.tx_valid == 15 && link.irq_valid == 3);
	assert(link.irq_before == 0x16 && link.irq_after == 0x16 && f.chip[0][5] == 0x16);
	for (i = 0; i < GC573_SPLITTER_LINK_REGS; i++)
		assert(link.rx[i] == f.rx[0][gc573_splitter_link_regs[i]]);
	for (i = 1; i <= 36; i++) {
		f = link_baseline();
		f.fail_at = i;
		assert(run_link(&f, &link) == -ETIMEDOUT && !link.complete);
		assert(f.transactions == i);
	}
	for (i = 3; i <= 9; i++) {
		f = link_baseline();
		f.wrong_at = i;
		assert(run_link(&f, &link) == -EOPNOTSUPP && !link.complete);
		assert(f.transactions == i && !link.rx_identity_verified);
	}
	f = link_baseline();
	f.rx[0][2] = 0x63;
	assert(run_link(&f, &link) == -ENODEV && !link.rx_identity_verified);
	for (i = 35; i <= 36; i++) {
		f = link_baseline();
		f.wrong_at = i;
		assert(run_link(&f, &link) == -EIO && !link.complete);
	}
	{
		struct gc573_block_io io = { 0 };
		struct gc573_block_result result;

		assert(gc573_splitter_rx_event_read(&io, &result, 0x16) == -EINVAL);
		assert(gc573_splitter_rx_event_read(&io, &result, 0x100) == -EINVAL);
	}
	puts("PASS: runtime snapshot, 36 failure positions, mapping/ID/bank gates, zero chip writes");
	for (j = 0; j < 2; j++) {
		f = hpd_baseline(j ? 0xca : 0x4a);
		assert(!run_hpd(&f, &hpd) && hpd.setup_complete && !hpd.lock_seen);
		assert(hpd.writes_started == (j ? 15U : 16U));
		assert(hpd.steps_verified == (j ? 14U : 15U));
		assert(hpd.transactions == (j ? 123U : 126U));
		assert(hpd.samples == 20 && f.ms == 2000 && hpd.bank_verified && !hpd.bank);
		assert(hpd.sink_mask == 2 && f.rx[3][0xab] == 0xca);
		assert(!f.rx[0][0x26] && f.rx[0][0x55] == 0xff && f.rx[3][0x3a] == 0x22);
		assert(f.chip[0][5] == 0x16 && !f.chip[0][0x0c]);
		for (i = 0; i < f.writes; i++) {
			/* The bounded probe never acknowledges RX or TX events. */
			assert(f.trace[i][0] != 0x35 && f.trace[i][0] != 0x36);
			assert(f.trace[i][0] != 0x38 || f.trace[i][1] >= 0x0f);
		}
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = hpd_baseline(j ? 0xca : 0x4a);
			f.fail_at = i;
			assert(run_hpd(&f, &hpd) == -ETIMEDOUT);
			assert(f.transactions == i);
			if (i <= (j ? 83U : 86U))
				assert(!hpd.setup_complete);
		}
	}
	for (i = 0; i < 5; i++) {
		f = hpd_baseline(0x4a);
		switch (i) {
		case 0: f.tx_ports[1][3] = 0x10; break;
		case 1: f.rx[0][0x13] = 0x41; break;
		case 2: f.rx[0][0x26] = 0; break;
		case 3: f.rx[0][0x55] = 0xff; break;
		case 4: f.rx[0][0xc5] = 0x13; break;
		}
		assert(run_hpd(&f, &hpd) == -EOPNOTSUPP && !f.writes);
	}
	f = hpd_baseline(0x4a);
	f.lock_after = 3;
	assert(!run_hpd(&f, &hpd) && hpd.lock_seen && hpd.samples == 3);
	assert(f.ms == 300 && hpd.transactions == 92);
	{
		static const unsigned int checks[] = {
			41, 44, 47, 50, 54, 57, 60, 63, 66, 69, 73, 77, 80, 83, 86,
		};

		for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
			f = hpd_baseline(0x4a);
			f.wrong_at = checks[i];
			f.wrong_mask = 255;
			assert(run_hpd(&f, &hpd) == -EIO && !hpd.setup_complete);
			assert(f.transactions == f.wrong_at);
		}
	}
	for (i = 0; i < 2; i++) {
		f = hpd_baseline(0x4a);
		f.wrong_at = i ? 70 : 51;
		assert(run_hpd(&f, &hpd) == -ENOLINK && !hpd.setup_complete);
	}
	f = hpd_baseline(0x4a);
	f.slow_at = 37;
	assert(run_hpd(&f, &hpd) == -ETIMEDOUT && !f.writes);
	puts("PASS: one-shot input HPD, 126/123 failure points, 15 readbacks, mode/sink guards, lock polling");
	{
		struct gc573_splitter_edid_result edid;
		unsigned char sums[2] = { 0 };

		f = edid_baseline();
		assert(!run_edid(&f, &edid) && edid.complete && edid.header_matches);
		assert(edid.mapping_verified && edid.bytes_read == 256);
		assert(edid.transactions == 110 && edid.writes_started == 1);
		for (i = 0; i < 256; i++) {
			assert(edid.data[i] == f.edid[i]);
			sums[i / 128] += f.edid[i];
		}
		assert(edid.checksum[0] == sums[0] && edid.checksum[1] == sums[1]);
		for (i = 1; i <= 110; i++) {
			f = edid_baseline();
			f.fail_at = i;
			assert(run_edid(&f, &edid) == -ETIMEDOUT && !edid.complete);
			assert(f.transactions == i);
			assert(edid.bytes_read == (i > 47 ? (i - 47) * 4 : 0));
		}
		f = edid_baseline();
		f.rx[0][0xc5] = 2;
		assert(run_edid(&f, &edid) == -EOPNOTSUPP && !f.writes);
		f = edid_baseline();
		f.wrong_at = 46;
		assert(run_edid(&f, &edid) == -EIO && !edid.mapping_verified && !edid.bytes_read);
		f = edid_baseline();
		f.slow_at = 44;
		assert(run_edid(&f, &edid) == -ETIMEDOUT && !f.writes);
		f = edid_baseline();
		f.edid[0] = 1;
		assert(!run_edid(&f, &edid) && edid.complete && !edid.header_matches);
		puts("PASS: splitter EDID, all 110 transfer failures, map/DDC/deadline guards, partial reads");
	}
	{
		FILE *file = fopen("tests/receiver-edid.hex", "r");
		unsigned int value;

		assert(file);
		for (i = 0; i < 256; i++) {
			assert(fscanf(file, "%x", &value) == 1 && value <= 255);
			splitter_resident[i] = value;
		}
		fclose(file);
		f = edid_enable_baseline();
		assert(!run_edid_enable(&f, &hpd) && hpd.setup_complete && hpd.edid_configured);
		assert(hpd.writes_started == 20 && hpd.steps_verified == 20);
		assert(hpd.samples == 20 && !hpd.lock_seen && f.ms == 2500);
		assert(f.rx[0][0xc5] == 2 && f.rx[0][0xc9] == 0xe7 && f.rx[0][0xca] == 0x79);
		assert(!f.rx[0][0xc6] && !f.rx[0][0xc7] && f.rx[0][0xc8] == 255);
		assert(!f.rx[0][0x26] && f.rx[0][0x55] == 255 && f.rx[3][0xab] == 0xca);
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = edid_enable_baseline();
			f.fail_at = i;
			assert(run_edid_enable(&f, &hpd) == -ETIMEDOUT);
			assert(f.transactions == i);
		}
		for (i = 0; i < 6; i++) {
			f = edid_enable_baseline();
			switch (i) {
			case 0: f.edid[0] = 1; break;
			case 1: f.tx_ports[1][3] = 0x10; break;
			case 2: f.rx[0][0x13] = 0x41; break;
			case 3: f.rx[0][0x26] = 255; break;
			case 4: f.rx[0][0x55] = 0; break;
			case 5: f.rx[0][0x34] = 0; break;
			}
			assert(run_edid_enable(&f, &hpd) < 0 && !hpd.writes_started);
		}
		f = edid_enable_baseline();
		f.rx[3][0xab] = 0x4a;
		assert(run_edid_enable(&f, &hpd) == -EOPNOTSUPP && !f.rx_bank);
		assert(!hpd.edid_configured && f.rx[0][0xc5] == 3);
		/* All retained-field verification failures stop immediately. */
		for (i = 113; i <= 156; i += 3) {
			/* AB read at 114 shifts subsequent operation triplets. */
			unsigned int check = i == 113 ? 113 : i + 1;

			f = edid_enable_baseline();
			f.wrong_at = check;
			f.wrong_mask = 255;
			assert(run_edid_enable(&f, &hpd) == -EIO && !hpd.setup_complete);
			assert(f.transactions == check);
		}
		for (i = 160; i <= 172; i += 3) {
			f = edid_enable_baseline();
			f.wrong_at = i;
			f.wrong_mask = 255;
			assert(run_edid_enable(&f, &hpd) == -EIO && !hpd.setup_complete);
		}
		f = edid_enable_baseline();
		f.wrong_at = 157;
		assert(run_edid_enable(&f, &hpd) == -ENOLINK && hpd.edid_configured);
		assert(!f.rx[0][0x55] && !hpd.setup_complete);
		f = edid_enable_baseline();
		f.lock_after = 3;
		assert(!run_edid_enable(&f, &hpd) && hpd.lock_seen && hpd.samples == 3);
		assert(f.ms == 800);
		printf("PASS: resident EDID enable, %u failures, readback/HPD/source/EDID guards\n", total);
	}
	f = activate_baseline();
	assert(!run_activate(&f, &hpd) && hpd.setup_complete && hpd.lock_seen);
	assert(hpd.transactions == 57 && hpd.writes_started == 6 && hpd.steps_verified == 5);
	assert(f.tx_ports[1][0xc1] == 0x87 && f.tx_ports[1][0x84] == 0x84);
	assert(f.tx_ports[1][0x86] == 8 && !(f.tx_ports[1][2] & 1));
	assert((f.tx_ports[1][0x19] & 7) == 7 && f.ms == 100);
	for (i = 1; i <= 57; i++) {
		f = activate_baseline();
		f.fail_at = i;
		assert(run_activate(&f, &hpd) == -ETIMEDOUT && f.transactions == i);
		if (i <= 55)
			assert(!hpd.setup_complete);
	}
	for (i = 43; i <= 55; i += 3) {
		f = activate_baseline();
		f.wrong_at = i;
		f.wrong_mask = 255;
		assert(run_activate(&f, &hpd) == -EIO && !hpd.setup_complete);
		assert(f.transactions == i);
	}
	for (i = 0; i < 6; i++) {
		f = activate_baseline();
		switch (i) {
		case 0: f.tx_ports[1][3] &= ~1; break;
		case 1: f.rx[0][0x13] &= ~0x10; break;
		case 2: f.rx[0][0x19] &= ~0x80; break;
		case 3: f.rx[0][0xc5] = 3; break;
		case 4: f.tx_ports[1][0x84] = 0x84; break;
		case 5: f.tx_ports[1][0x86] = 8; break;
		}
		assert(run_activate(&f, &hpd) == -EOPNOTSUPP && !f.writes);
	}
	f = activate_baseline();
	f.slow_at = 38;
	assert(run_activate(&f, &hpd) == -ETIMEDOUT && !f.writes);
	f = activate_baseline();
	f.c1_status = 0x40;
	assert(!run_activate(&f, &hpd) && hpd.setup_complete && hpd.c1_valid);
	assert(hpd.c1_readback == 0xc7);
	for (i = 0x10; i <= 0x80; i <<= 1) {
		if (i == 0x40)
			continue;
		f = activate_baseline();
		f.wrong_at = 43;
		f.wrong_mask = i;
		assert(run_activate(&f, &hpd) == -EIO && !hpd.setup_complete);
	}
	puts("PASS: port1 activation prefix, all 57 transfer failures, 5 readbacks, link/state guards");
	{
		struct gc573_splitter_video_result video;
		unsigned int pixel, rate;

		f = video_baseline();
		assert(!run_video(&f, &video) && video.complete && video.rate_valid);
		assert(video.reference_khz == 20922 && video.reference_ticks == 209220);
		assert(video.initial_count == 576 && video.exponent == 1 && video.samples == 10);
		assert(video.pixel_khz == (20922U << 12) / 576 && video.link_khz == video.pixel_khz);
		assert(video.writes_started == 23 && video.steps_verified == 12);
		assert(f.tx_ports[1][0xaf] == 5 && f.ms == 1);
		total = f.transactions;
		for (i = 1; i <= total; i++) {
			f = video_baseline();
			f.fail_at = i;
			assert(run_video(&f, &video) == -ETIMEDOUT && !video.complete);
			assert(f.transactions == i);
		}
		for (i = 0; i < 6; i++) {
			f = video_baseline();
			switch (i) {
			case 0: f.rx[0][0x19] &= ~0x80; break;
			case 1: f.tx_ports[1][3] &= ~1; break;
			case 2: f.timer[0]++; break;
			case 3: f.timer[2] = 4; break;
			case 4: f.tx_ports[1][0x86] = 0; break;
			case 5: f.slow_at = 41; break;
			}
			assert(run_video(&f, &video) < 0 && !f.writes);
		}
		for (i = 0; i < 12; i++) {
			f = video_baseline();
			f.wrong_at = i == 0 ? 46 : i == 11 ? total : 53 + (i - 1) * 7;
			f.wrong_mask = i == 11 ? 0xc0 : 0x80;
			assert(run_video(&f, &video) == -EIO && !video.complete);
		}
		for (i = 0; i < 8; i++) {
			/* Exercise every initial-count exponent branch. */
			f = video_baseline();
			f.video_raw = 4U << i;
			assert(run_video(&f, &video) == (i < 5 ? -ERANGE : 0));
			assert(video.exponent == 7 - i);
		}
		f = video_baseline();
		f.video_raw = 0;
		assert(run_video(&f, &video) == -ERANGE && !video.complete);
		assert(!gc573_splitter_video_rate(20922, 11520, 1, 1, &pixel, &rate));
		assert(rate == pixel * 5 / 4);
		assert(!gc573_splitter_video_rate(20922, 11520, 1, 2, &pixel, &rate));
		assert(rate == pixel * 3 / 2);
		assert(gc573_splitter_video_rate(20922, 0, 0, 0, &pixel, &rate) == -ERANGE);
		assert(gc573_splitter_video_rate(20922, 100, 0, 0, &pixel, &rate) == -ERANGE);
		assert(gc573_splitter_video_rate(26215, 11520, 1, 0, &pixel, &rate) == -ERANGE);
		printf("PASS: TX1 video clock, %u transfer failures, all scale branches and readbacks\n", total);
	}
	{
		struct gc573_splitter_video_result video;
		static const unsigned int checks[] = { 64, 148, 151, 154, 157, 160, 163, 170, 175, 178 };

		f = video_baseline();
		f.video_setup = 1;
		assert(!run_video(&f, &video) && video.complete && video.output_setup_complete);
		assert(video.analog_complete && video.irq_valid == 63);
		assert(video.transactions == 178 && video.writes_started == 43 && video.steps_verified == 22);
		assert(f.tx_ports[1][0x84] == 0x84 && !(f.tx_ports[1][0x88] & 4));
		assert((f.tx_ports[1][0x87] & 31) == 3 && (f.tx_ports[1][0x89] & 0xbf) == 0x80);
		assert(!(f.tx_ports[1][0x8a] & 15) && (f.tx_ports[1][0x8b] & 15) == 3);
		assert(!f.tx_ports[1][1] && (f.tx_ports[1][0x18] & 0x80) && f.ms == 101);
		for (i = 1; i <= 178; i++) {
			f = video_baseline();
			f.video_setup = 1;
			f.fail_at = i;
			assert(run_video(&f, &video) == -ETIMEDOUT && !video.complete);
			assert(f.transactions == i);
		}
		for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
			f = video_baseline();
			f.video_setup = 1;
			f.wrong_at = checks[i];
			f.wrong_mask = 255;
			assert(run_video(&f, &video) == -EIO && !video.complete);
			assert(f.transactions == f.wrong_at);
		}
		/* Exercise low/mid/high analog branches using independent rate values. */
		for (i = 0; i < 4; i++) {
			const unsigned int raw[] = { 450, 210, 130, 100 };
			const unsigned int reg87[] = { 3, 9, 13, 14 };
			const unsigned int reg8b[] = { 3, 9, 11, 13 };
			const unsigned int reg89[] = { 0x80, 0x21, 0x25, 0x25 };

			f = video_baseline();
			f.video_setup = 1;
			f.video_raw = raw[i];
			assert(!run_video(&f, &video));
			assert((f.tx_ports[1][0x87] & 31) == reg87[i]);
			assert((f.tx_ports[1][0x8b] & 15) == reg8b[i]);
			assert((f.tx_ports[1][0x89] & 0xbf) == reg89[i]);
		}
		puts("PASS: TX1 first-SCDT continuation, 178 failures, all analog branches, reset/readback gates");
	}
	{
		struct gc573_splitter_video_result video;
		static const unsigned int checks[] = { 180, 183, 188, 191, 194, 197,
			200, 203, 206, 209, 212, 215, 218, 221, 225, 228 };

		f = video_baseline();
		f.video_setup = 2;
		f.tx_ports[1][3] = 0x9f;
		assert(!run_video(&f, &video) && video.complete && video.output_enabled);
		assert(video.transactions == 228 && video.writes_started == 59 && video.steps_verified == 38);
		assert(video.bank_verified && !video.bank && video.format_valid);
		assert(f.ms == 201 && (f.tx_ports[1][0xc0] & 3) == 1);
		assert(!(f.tx_ports[1][0xc1] & 0xb5) && (f.tx_ports[1][0xc1] & 8));
		for (i = 1; i <= 228; i++) {
			f = video_baseline();
			f.video_setup = 2;
			f.tx_ports[1][3] = 0x9f;
			f.fail_at = i;
			assert(run_video(&f, &video) == -ETIMEDOUT && !video.complete && !video.output_enabled);
			assert(f.transactions == i);
		}
		for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
			f = video_baseline();
			f.video_setup = 2;
			f.tx_ports[1][3] = 0x9f;
			f.wrong_at = checks[i];
			f.wrong_mask = 255;
			assert(run_video(&f, &video) == -EIO && !video.complete);
			assert(f.transactions == f.wrong_at);
		}
		for (i = 0; i < 6; i++) {
			f = video_baseline();
			f.video_setup = 2;
			f.tx_ports[1][3] = 0x9f;
			switch (i) {
			case 0: f.rx[2][0x15] = 0x20; break;
			case 1: f.rx[0][0x98] = 0x10; break;
			case 2: f.rx[0][0xcf] = 0x20; break;
			case 3: f.rx[0][0x13] &= ~2; break;
			case 4: f.video_raw = 210; break;
			case 5: f.tx_ports[1][3] &= ~8; break;
			}
			assert(run_video(&f, &video) == (i == 5 ? -ENOLINK : -EOPNOTSUPP));
			assert(!video.output_enabled && !video.complete && !f.rx_bank);
		}
		puts("PASS: RGB8 output, all 228 transfer failures, 16 readbacks, format and link gates");
	}
	/* TX2 must never mutate the internal TX1, including partial failures. */
	{
		struct gc573_splitter_video_result video;
		unsigned char internal[256];
		unsigned int failure;

		for (failure = 0; failure <= 57; failure++) {
			f = activate_baseline();
			f.selected_port = 2;
			memcpy(f.tx_ports[2], f.tx_ports[1], sizeof(internal));
			memcpy(internal, f.tx_ports[1], sizeof(internal));
			f.fail_at = failure;
			assert(run_activate(&f, &hpd) == (failure ? -ETIMEDOUT : 0));
			assert(!memcmp(internal, f.tx_ports[1], sizeof(internal)));
			if (!failure) {
				assert(hpd.sink_mask == 4 && hpd.setup_complete);
				assert(f.tx_ports[2][0x86] == 8);
			}
		}
		for (failure = 0; failure <= 228; failure++) {
			f = video_baseline();
			f.selected_port = 2;
			f.video_setup = 2;
			memcpy(f.tx_ports[2], f.tx_ports[1], sizeof(internal));
			f.tx_ports[2][3] = 0x9f;
			memcpy(internal, f.tx_ports[1], sizeof(internal));
			f.fail_at = failure;
			assert(run_video(&f, &video) == (failure ? -ETIMEDOUT : 0));
			assert(!memcmp(internal, f.tx_ports[1], sizeof(internal)));
			assert(video.output_enabled == !failure);
		}
		f = activate_baseline();
		f.selected_port = 2;
		f.tx_ports[2][3] = 0x14;
		assert(run_activate(&f, &hpd) == -EOPNOTSUPP && !f.writes);
		f = video_baseline();
		f.selected_port = 2;
		f.tx_ports[2][3] = 0x14;
		assert(run_video(&f, &video) == -ENOLINK && !f.writes);
		puts("PASS: TX2 output, 285 transfer failures, absent sink, TX1 preserved");
	}
	{
		struct gc573_passthrough_result pass;
		unsigned char internal[256];
		unsigned int failure;

		for (failure = 0; failure <= 327; failure++) {
			f = video_baseline();
			f.selected_port = 2;
			f.video_setup = 2;
			memcpy(f.tx_ports[2], f.tx_ports[1], sizeof(internal));
			memcpy(internal, f.tx_ports[1], sizeof(internal));
			f.tx_ports[2][3] = 0x9f;
			f.tx_ports[2][0x84] = 0xe4;
			f.tx_ports[2][0x86] = 0;
			f.fail_at = failure;
			assert(run_passthrough(&f, &pass) == (failure ? -ETIMEDOUT : 0));
			assert(pass.enabled == !failure);
			assert(!memcmp(internal, f.tx_ports[1], sizeof(internal)));
			if (!failure) {
				assert(f.transactions == 327 && pass.sink_present && !pass.preserved);
				unsigned int writes = f.writes;
				assert(!run_passthrough(&f, &pass) && pass.enabled && pass.preserved);
				assert(f.writes == writes);
			}
		}
		f = video_baseline();
		f.selected_port = 2;
		f.tx_ports[2][3] = 0x14;
		assert(!run_passthrough(&f, &pass) && !pass.sink_present && !pass.enabled && !f.writes);
		f.tx_ports[2][3] = 0x17;
		f.tx_ports[2][0x84] = 0xff;
		assert(run_passthrough(&f, &pass) == -EOPNOTSUPP && !f.writes);
		puts("PASS: automatic TX2 startup, 327 failures, active output preserved, absent/unknown gates");
	}
	/* Compare all accepted rates against the Windows multiply/shift math. */
	for (i = 10000; i <= 34000; i++) {
		unsigned int whole = ((unsigned long long)i * 0x10624dd3) >> 38;
		unsigned int fraction = ((unsigned long long)((i - whole * 1000) << 8) *
					 0x10624dd3) >> 38;

		if (i > 26214) {
			assert(gc573_splitter_timing_compute(i, &values) == -ERANGE);
			continue;
		}
		assert(!gc573_splitter_timing_compute(i, &values));
		assert(values.bytes[3] == (whole & 63) && values.bytes[4] == fraction);
		assert(((unsigned int)values.bytes[0] | (values.bytes[1] << 8) |
			(values.bytes[2] << 16)) == i * 10);
	}
	assert(gc573_splitter_timing_compute(0, &values) == -ERANGE);
	assert(gc573_splitter_timing_compute(0xffffffff, &values) == -ERANGE);
	puts("PASS: TX ports 0/3, 607/577 failure positions, 44 readbacks, masks/reset/branch gates");
	puts("PASS: shared TX reset, four read-only ports, 408 failures, mapping loss/partial validity");
	puts("PASS: RX startup tail, 371/380 failure positions, both AB branches, reset pulses/delay");
	puts("PASS: RX setup, all 299 transfer failures/31 readback mismatches, calibration gate");
	puts("PASS: splitter RX reset/CAOF, 201 transfer failures, timeout cleanup, identity/stale flags");
	puts("PASS: TX address setup, RX snapshot, 103 failure points, mapping/bank guards");
	puts("PASS: splitter reset/base sequence, optional bounded engine recovery,");
	puts("      all transfer failure points, identity/GPIO/bank/IRQ gates, deadline,");
	puts("      bank/value verification, command self-clear, fixed endpoint/no GPIO writes");
	puts("      splitter clock selectors, encodings/range, all 64 failure positions,");
	puts("      prerequisite gates, deadline, cleanup, no default-clock substitution");
	puts("      fresh-clock timing, fixed TX-common timer endpoint, five readbacks,");
	puts("      all 81 failure points, mapping/bank/deadline guards, 18-bit overflow refusal");
	return 0;
}
