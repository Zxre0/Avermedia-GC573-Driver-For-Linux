/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_HDMI_H
#define GC573_HDMI_H
#include "gc573_block.h"

/* Indices 6..17 match the checked userspace startup prefix. */
#define GC573_HDMI_FIRST 6U
#define GC573_HDMI_DONE 19U
struct gc573_hdmi {
	unsigned int phase, waiting, complete, polls, tx_preserved;
	int error, passthrough_error;
	struct gc573_signal_result signal;
	struct gc573_clock_result clock;
	struct gc573_timing_result timing;
	struct gc573_edid_result edid;
	struct gc573_ddc_result ddc;
	struct gc573_input_result input;
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	struct gc573_splitter_hpd_result hpd;
	struct gc573_splitter_edid_result splitter_edid;
	struct gc573_splitter_video_result video;
	struct gc573_receiver_video_result receiver_video;
	struct gc573_passthrough_result passthrough;
};

int gc573_hdmi_poll(const struct gc573_block_io *io, struct gc573_hdmi *h);
#endif
