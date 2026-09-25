// SPDX-License-Identifier: GPL-2.0-only
#define GC573_SINK_TEST_NO_MAIN
#include "gc573_passthrough.h"
#include "sink_test.c"
static struct gc573_block_io ops(struct fake *f)
{
	return (struct gc573_block_io){.ctx = f,
				       .read = sink_read,
				       .write = write_reg,
				       .wait = wait_read,
				       .wait_write = sink_wait_write,
				       .sleep_ms = delay,
				       .time_ms = time_ms};
}
static struct fake pt_setup(void)
{
	struct fake f = setup(1);
	f.passthrough_mode = f.edid_mode = 1;
	gc573_capture_edid(f.edid);
	f.edid[127] = f.edid[255] = 0;
	f.tx_ports[2][0x84] = 0x84;
	f.tx_ports[2][0x86] = 8;
	f.rx[0][0x13] = 0x13;
	f.rx[0][0x19] = 0x80;
	f.rx[0][0x9b] = 0xa0;
	f.rx[0][0x9c] = 0x0a;
	f.rx[0][0x9d] = 0;
	f.rx[0][0x9e] = 0x0a;
	f.rx[0][0xa2] = 0xf5;
	f.rx[0][0xa3] = 5;
	f.rx[0][0xa4] = 0xa0;
	f.rx[0][0xa5] = 5;
	f.video_raw = 86;
	f.rx[0][0xc5] = 2;
	return f;
}
int main(void)
{
	struct gc573_passthrough_state p = {0};
	struct gc573_sink_result d = {0};
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	struct gc573_splitter_video_result video;
	struct gc573_passthrough_edid caps = {.max_tmds_khz = 600000, .scdc = 1};
	struct fake f = pt_setup();
	struct gc573_block_io io = ops(&f);
	unsigned int i, n, value = 1;
	unsigned char target[256];
	/* Include and exercise the shared EDID harness helper. */
	assert(!run_sink(&f, &d));
	f = pt_setup();
	assert(!gc573_sink_scdc(&io, &d, 2, 1, &value) && scdc[2] == 1);
	value = 3;
	assert(!gc573_sink_scdc(&io, &d, 0x20, 1, &value));
	value = 0;
	assert(!gc573_sink_scdc(&io, &d, 0x20, 0, &value) && value == 3);
	value = 0x80;
	assert(gc573_sink_scdc(&io, &d, 0x74, 1, &value) == -EINVAL);
	f = pt_setup();
	d = (struct gc573_sink_result){.port = 1};
	f.selected_port = 1;
	f.tx_ports[1][3] = 0x9f;
	value = 3;
	assert(!gc573_sink_scdc(&io, &d, 0x20, 1, &value));
	value = 0;
	assert(!gc573_sink_scdc(&io, &d, 0x20, 0, &value) && value == 3);
	for (i = 0; i < f.writes; i++) assert(f.trace[i][0] == 0x35);
	d = (struct gc573_sink_result){0};

	f = pt_setup();
	assert(!gc573_splitter_video_external(&io, &identity, &link, &video, &caps));
	assert(video.output_enabled && video.link_khz > 490000 && video.link_khz < 510000 &&
	       scdc[0x20] == 3);
	assert((f.tx_ports[2][0xc0] & 0x46) == 0x46 && (f.tx_ports[2][0x83] & 8));
	n = f.transactions;
	for (i = 1; i <= n; i++) {
		f = pt_setup();
		f.fail_at = i;
		assert(gc573_splitter_video_external(&io, &identity, &link, &video, &caps) ==
		       -ETIMEDOUT);
		assert(!video.output_enabled && f.transactions == i);
	}
	f = pt_setup();
	f.video_raw = 72;
	assert(!gc573_splitter_video_external(&io, &identity, &link, &video, &caps) &&
	       video.link_khz > 590000);
	f = pt_setup();
	f.video_raw = 288;
	assert(!gc573_splitter_video_external(&io, &identity, &link, &video, &caps) &&
	       scdc[0x20] == 0);
	memcpy(target, display_edid, 256);
	for (i = 0; i < 256; i += 128) {
		unsigned int j, sum = 0;
		for (j = 0; j < 127; j++)
			sum += target[i + j];
		target[i + 127] = -sum;
	}
	f = pt_setup();
	p = (struct gc573_passthrough_state){0};
	assert(!gc573_passthrough_program_edid(&io, &p, target));
	assert(p.edid_verified && p.edid_written == 254 && p.bank_verified && !p.bank);
	for (i = 0; i < 256; i++)
		assert(f.edid[i] == ((i == 127 || i == 255) ? 0 : target[i]));
	n = f.transactions;
	for (i = 1; i <= n; i++) {
		f = pt_setup();
		p = (struct gc573_passthrough_state){0};
		f.fail_at = i;
		assert(gc573_passthrough_program_edid(&io, &p, target) == -ETIMEDOUT &&
		       f.transactions == i);
	}
	f = pt_setup();
	p = (struct gc573_passthrough_state){.advertised = caps};
	assert(!gc573_passthrough_program_edid(&io, &p, target));
	p.scaled = 1;
	f.all_ports = 1;
	f.tx_ports[1][3] = 0x9f;
	f.tx_ports[1][0xc0] |= 0x46;
	f.tx_ports[1][0x83] |= 8;
	assert(!gc573_passthrough_restore(&io, &p));
	assert(!(f.tx_ports[1][0xc0] & 0x46) && !(f.tx_ports[1][0x83] & 8));
	for (i = 0; i < 256; i++)
		assert(f.edid[i] == ((i == 127 || i == 255) ? 0 : p.prior_edid[i]));
	assert(!scdc[0x20] && !(f.tx_ports[2][0x83] & 8));
	n = f.transactions;
	p.error = -ETIMEDOUT;
	f.regs[GC573_BLOCK_STATUS / 4] = 8;
	assert(gc573_passthrough_restore(&io, &p) == -ETIMEDOUT && f.transactions == n);
	f = pt_setup();
	p = (struct gc573_passthrough_state){.phase = 3, .advertised = caps};
	assert(!gc573_passthrough_poll(&io, &p) && p.waiting && !p.active);
	assert(!gc573_passthrough_poll(&io, &p) && p.active && p.width == 2560 && p.height == 1440);
	assert(!gc573_passthrough_poll(&io, &p) && p.measured.complete && p.millihz > 115000);
	n = f.writes;
	assert(!gc573_passthrough_poll(&io, &p) && p.active &&
	       f.writes == n + 2); /* snapshots only */
	p.scaled = 1;
	p.polls = 3;
	f.video_raw *= 2;
	assert(!gc573_passthrough_poll(&io, &p) && p.waiting && !p.active && !p.configured);
	/* Identical totals with a halved pixel clock must trigger reconfiguration. */
	f.rx[0][0x19] = 0;
	assert(!gc573_passthrough_poll(&io, &p) && p.waiting && !p.active);
	puts("PASS: HDMI 2.0 SCDC, 498/595 MHz TX2 output, low-rate restore, EDID "
	     "SRAM verification, mode worker and failure injection");
}
