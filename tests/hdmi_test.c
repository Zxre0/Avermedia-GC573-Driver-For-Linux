// SPDX-License-Identifier: GPL-2.0-only
/* Reuse the protocol-level fake, with real deferred startup operations. */
#define main splitter_suite_main
#include "splitter_prepare_test.c"
#undef main
#include "gc573_hdmi.h"

static int tick(struct fake *f, struct gc573_hdmi *h)
{
	const struct gc573_block_io io = {
		.ctx = f, .read = read_reg, .write = write_reg, .wait = wait_read,
		.wait_write = wait_write, .sleep_ms = sleep_ms, .time_ms = time_ms,
	};
	return gc573_hdmi_poll(&io, h);
}

int main(void)
{
	struct fake f = activate_baseline();
	struct gc573_hdmi h = { .phase = 8 };
	unsigned int i, transactions, writes;

	f.rx[0][0x13] = 0;
	for (i = 0; i < 200; i++)
		assert(!tick(&f, &h) && h.waiting && h.phase == 8 && !f.writes && !h.complete);
	f.rx[0][0x13] = 1;
	assert(!tick(&f, &h) && !h.waiting && h.phase == 9 && !f.writes);

	/* A later lock drop waits without touching chip settings. */
	f = activate_baseline();
	h = (struct gc573_hdmi) { .phase = 12 };
	f.rx[0][0x19] = 0x30;
	assert(!tick(&f, &h) && h.waiting && h.phase == 12 && !f.writes);
	f.rx[0][0x19] = 0xb0;
	assert(!tick(&f, &h) && !h.waiting && h.phase == 13 && h.hpd.setup_complete);
	assert(f.writes == 6);

	/* Recognized powered transmitter: preserve it, no cold activation. */
	f = video_baseline();
	h = (struct gc573_hdmi) { .phase = 12 };
	assert(!tick(&f, &h) && h.tx_preserved && h.phase == 13 && !f.writes);

	/* Every failed transfer stops permanently, including partial writes. */
	f = activate_baseline();
	h = (struct gc573_hdmi) { .phase = 12 };
	assert(!tick(&f, &h));
	transactions = f.transactions;
	for (i = 1; i <= transactions; i++) {
		f = activate_baseline();
		h = (struct gc573_hdmi) { .phase = 12 };
		f.fail_at = i;
		assert(tick(&f, &h) == -ETIMEDOUT && h.error && !h.complete);
		writes = f.writes;
		assert(tick(&f, &h) == -ETIMEDOUT && f.writes == writes && f.transactions == i);
	}
	f = activate_baseline();
	f.tx_ports[1][0x84] = 0x44;
	h = (struct gc573_hdmi) { .phase = 12 };
	assert(tick(&f, &h) == -EOPNOTSUPP && !f.writes && !h.complete);

	f = video_baseline();
	f.video_setup = 2;
	f.tx_ports[1][3] = 0x9f;
	h = (struct gc573_hdmi) { .phase = 14 };
	assert(!tick(&f, &h) && h.phase == 15 && h.video.output_enabled);
	transactions = f.transactions;
	for (i = 1; i <= transactions; i++) {
		f = video_baseline();
		f.video_setup = 2;
		f.tx_ports[1][3] = 0x9f;
		f.fail_at = i;
		h = (struct gc573_hdmi) { .phase = 14 };
		assert(tick(&f, &h) == -ETIMEDOUT && !h.complete);
		assert(tick(&f, &h) == -ETIMEDOUT && f.transactions == i);
	}
	/* An absent external display permits completion; completed work stays idle. */
	f = video_baseline();
	f.tx_ports[2][3] = 0x14;
	h = (struct gc573_hdmi) { .phase = 18 };
	assert(!tick(&f, &h) && h.complete && !h.passthrough_error);
	transactions = f.transactions;
	assert(!tick(&f, &h) && f.transactions == transactions);
	h = (struct gc573_hdmi) { .phase = 5 };
	assert(tick(&f, &h) == -EINVAL && f.transactions == transactions);
	puts("PASS: deferred HDMI, no-source waits, powered TX preservation, transfer-failure replay guards");
	return 0;
}
