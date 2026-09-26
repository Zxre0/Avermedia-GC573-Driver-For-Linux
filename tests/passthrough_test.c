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
	f.tx_ports[2][3] &= ~2U; /* Monitor RxSense disappears before TX programming. */
	assert(gc573_splitter_video_external(&io, &identity, &link, &video, &caps) == -ENOLINK);
	assert(gc573_splitter_video_link_wait(&video, -ENOLINK));
	assert(!video.writes_started && !video.output_enabled);
	assert(!gc573_splitter_video_link_wait(&video, -ETIMEDOUT));
	video.phase = 8;
	assert(!gc573_splitter_video_link_wait(&video, -ENOLINK));
	video.phase = 1;
	video.prerequisite_error = -EIO;
	assert(!gc573_splitter_video_link_wait(&video, -ENOLINK));
	f = pt_setup();
	p = (struct gc573_passthrough_state){.phase = 3, .scaled = 1, .advertised = caps};
	f.tx_ports[2][3] &= ~2U;
	assert(!gc573_passthrough_poll(&io, &p));
	assert(!gc573_passthrough_poll(&io, &p) && p.waiting && p.stable && !p.active && !p.error);
	f.tx_ports[2][3] |= 2;
	assert(!gc573_passthrough_poll(&io, &p) && p.active && !p.error);
	p.polls = 3;
	f.tx_ports[2][3] &= ~2U;
	assert(!gc573_passthrough_poll(&io, &p) && p.waiting && !p.active && !p.configured);
	f.tx_ports[2][3] |= 2;
	assert(!gc573_passthrough_poll(&io, &p) && p.active && !p.error);
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
		assert(!gc573_splitter_video_format_wait(&video, -ETIMEDOUT));
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
	/* A transient unsupported source must not reset the working HDMI output. */
	for (i = 0; i < 3; i++) {
		unsigned int j;
		f = pt_setup();
		f.video_raw = 288;
		if (i == 0) f.rx[0][0xcf] = 0x20;
		if (i == 1) f.rx[2][0x15] = 0x20;
		if (i == 2) f.rx[0][0x98] = 0x10;
		assert(gc573_splitter_video_external(&io, &identity, &link, &video, &caps) == -EOPNOTSUPP);
		assert(gc573_splitter_video_format_wait(&video, -EOPNOTSUPP));
		assert(!video.analog_complete && !video.output_enabled && !video.irq_valid);
		for (j = 0; j < f.writes; j++)
			if (f.trace[j][0] == 0x36)
				assert(f.trace[j][1] == 7 || f.trace[j][1] == 0xaf);
	}
	video.phase = 8;
	assert(!gc573_splitter_video_format_wait(&video, -EOPNOTSUPP));
	video.phase = 7;
	video.bank_verified = 0;
	assert(!gc573_splitter_video_format_wait(&video, -EOPNOTSUPP));
	video.bank_verified = 1;
	video.last.status = 8;
	assert(!gc573_splitter_video_format_wait(&video, -EOPNOTSUPP));
	/* Phase-seven format rejection and snapshot depth rejection both recover. */
	for (i = 0; i < 2; i++) {
		f = pt_setup();
		p = (struct gc573_passthrough_state){.phase = 3, .advertised = caps, .scaled = 1};
		if (i) f.rx[0][0x98] = 0x10;
		else f.rx[0][0xcf] = 0x20;
		assert(!gc573_passthrough_poll(&io, &p));
		assert(!gc573_passthrough_poll(&io, &p));
		assert(p.waiting && p.format_rejected && p.format_waits == 1 && !p.error && !p.active);
		f.rx[0][0xcf] = f.rx[0][0x98] = 0;
		assert(!gc573_passthrough_poll(&io, &p));
		assert(!gc573_passthrough_poll(&io, &p));
		assert(p.active && p.configured && !p.waiting && !p.error && !p.format_rejected);
	}
	/* Output setup finished, but the final TX link bit arrived late. */
	f = pt_setup();
	f.tx_ports[2][3] = 0x17;
	assert(gc573_splitter_video_external(&io, &identity, &link, &video, &caps) == -ENOLINK);
	assert(video.waiting_link && video.output_setup_complete && !video.output_enabled);
	assert(gc573_splitter_video_link_wait(&video, -ENOLINK));
	{
		struct gc573_splitter_video_result pending = video;
		struct fake before = f;
		unsigned int j, count;

		n = f.writes;
		assert(gc573_splitter_video_resume(&io, &identity, &link, &video, 2, &caps) == -ENOLINK);
		assert(video.waiting_link && !video.complete);
		for (j = n; j < f.writes; j++)
			assert(f.trace[j][1] != 1 && f.trace[j][1] != 0xc0);
		f = before;
		video = pending;
		f.tx_ports[2][3] = 0x9f;
		n = f.writes;
		assert(!gc573_splitter_video_resume(&io, &identity, &link, &video, 2, &caps));
		assert(video.complete && video.output_enabled && !video.waiting_link);
		for (j = n; j < f.writes; j++)
			assert(f.trace[j][1] != 1 && f.trace[j][1] != 0xc0);
		count = f.transactions - before.transactions;
		for (j = 1; j <= count; j++) {
			f = before;
			video = pending;
			f.tx_ports[2][3] = 0x9f;
			f.fail_at = f.transactions + j;
			assert(gc573_splitter_video_resume(&io, &identity, &link, &video, 2, &caps) == -ETIMEDOUT);
			assert(f.transactions == f.fail_at && !video.complete && !video.waiting_link);
		}
		/* A 120->60 clock change invalidates the pending high-rate setup. */
		f = before;
		video = pending;
		f.tx_ports[2][3] = 0x9f;
		f.video_raw *= 2;
		assert(gc573_splitter_video_resume(&io, &identity, &link, &video, 2, &caps) == -EAGAIN);
		assert(!video.waiting_link && !video.complete);
		assert(!gc573_splitter_video_external(&io, &identity, &link, &video, &caps));
		assert(!scdc[0x20] && !(f.tx_ports[2][0xc0] & 0x46));
		f = before;
		video = pending;
		video.last.status = 8;
		n = f.transactions;
		assert(gc573_splitter_video_resume(&io, &identity, &link, &video, 2, &caps) == -EINVAL);
		assert(f.transactions == n);
	}
	f = pt_setup();
	f.tx_ports[2][3] = 0x17;
	p = (struct gc573_passthrough_state){.phase = 3, .scaled = 1, .advertised = caps};
	assert(!gc573_passthrough_poll(&io, &p));
	assert(!gc573_passthrough_poll(&io, &p) && p.video.waiting_link && !p.error);
	f.tx_ports[2][3] = 0x9f;
	assert(!gc573_passthrough_poll(&io, &p) && p.active && !p.video.waiting_link);
	/* The same continuation must address TX1 without changing TX2. */
	f = pt_setup();
	f.selected_port = 1;
	memcpy(f.tx_ports[1], f.tx_ports[2], 256);
	f.tx_ports[1][3] = 0x17;
	assert(gc573_splitter_video_internal(&io, &identity, &link, &video) == -ENOLINK);
	assert(video.waiting_link);
	f.tx_ports[1][3] = 0x9d;
	n = f.writes;
	assert(gc573_splitter_video_resume(&io, &identity, &link, &video, 1, 0) == -ENOLINK);
	assert(video.waiting_link && f.writes == n);
	f.tx_ports[1][3] = 0x9f;
	assert(!gc573_splitter_video_resume(&io, &identity, &link, &video, 1, 0));
	assert(video.complete && !video.waiting_link);
	for (i = n; i < f.writes; i++) assert(f.trace[i][0] != 0x36);
	/* A persistently unlocked, completed setup gets exactly one restart.
	 * Exercise both ports, the six-sample delay, and every restart failure.
	 */
	for (unsigned int port = 1; port <= 2; port++) {
		struct gc573_splitter_video_result pending;
		struct fake before;
		unsigned int j, count;

		f = pt_setup();
		memcpy(f.tx_ports[1], f.tx_ports[2], 256);
		f.selected_port = port;
		f.tx_ports[port][3] = 0x97;
		assert((port == 1 ? gc573_splitter_video_internal(&io, &identity, &link, &video) :
			gc573_splitter_video_external(&io, &identity, &link, &video, &caps)) == -ENOLINK);
		f.lock_on_tx_reset = 1;
		for (j = 0; j < 5; j++) {
			assert(gc573_splitter_video_resume(&io, &identity, &link, &video, port, &caps) == -ENOLINK);
			assert(!video.link_restarts && video.link_wait_polls == j + 1);
		}
		before = f; pending = video;
		assert(!gc573_splitter_video_resume(&io, &identity, &link, &video, port, &caps));
		assert(video.link_restarts == 1 && video.complete && !video.waiting_link);
		for (j = before.writes; j < f.writes; j++)
			assert(f.trace[j][0] != 0x34 + (port == 1 ? 2 : 1));
		count = f.transactions - before.transactions;
		for (j = 1; j <= count; j++) {
			f = before; video = pending;
			f.fail_at = f.transactions + j;
			assert(gc573_splitter_video_resume(&io, &identity, &link, &video, port, &caps) == -ETIMEDOUT);
			assert(f.transactions == f.fail_at && !video.waiting_link && !video.complete);
		}
		f = before; video = pending; f.lock_on_tx_reset = 0;
		assert(gc573_splitter_video_resume(&io, &identity, &link, &video, port, &caps) == -ENOLINK);
		assert(video.link_restarts == 1 && video.waiting_link);
		n = f.writes;
		for (j = 0; j < 8; j++)
			assert(gc573_splitter_video_resume(&io, &identity, &link, &video, port, &caps) == -ENOLINK);
		assert(video.link_restarts == 1 && video.link_wait_polls == 6);
		for (j = n; j < f.writes; j++)
			assert(f.trace[j][1] != 1 && f.trace[j][1] != 0xc0);
	}
	puts("PASS: one bounded TX1/TX2 stalled-lock restart, settling delay and every transport failure");
	puts("PASS: late HDMI lock continuation, 120->60 clock revalidation, all resume transport failures");
	puts("PASS: transient format waits recover; unsupported input never resets TX; transport failures remain stopped");
	puts("PASS: HDMI 2.0 SCDC, 498/595 MHz TX2 output, low-rate restore, EDID "
	     "SRAM verification, mode worker and failure injection");
}
