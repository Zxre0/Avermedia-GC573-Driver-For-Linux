/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_PASSTHROUGH_H
#define GC573_PASSTHROUGH_H
#include "gc573_sink.h"
struct gc573_passthrough_edid {
	unsigned char data[256];
	unsigned int max_tmds_khz, scdc, timings, video_codes, audio;
};
int gc573_passthrough_edid(const unsigned char *data, unsigned int length,
			   struct gc573_passthrough_edid *out);
struct gc573_passthrough_state {
	unsigned int phase, polls, waiting, active, changes, width, height, millihz;
	unsigned int scaled, format_waits, format_rejected;
	unsigned int edid_written, edid_verified, bank_verified, bank, writes;
	unsigned int snapshot_valid, stable, configured, prior_valid, scdc_status,
	    scdc_status_valid, sink_lock;
	unsigned char snapshot[18], previous[18], prior_edid[256];
	int error;
	struct gc573_sink_result sink;
	struct gc573_passthrough_edid advertised;
	struct gc573_splitter_result identity;
	struct gc573_splitter_link_result link;
	struct gc573_splitter_hpd_result hpd;
	struct gc573_splitter_video_result video, measured;
	struct gc573_block_result last;
};
int gc573_passthrough_poll(const struct gc573_block_io *io, struct gc573_passthrough_state *p);
int gc573_passthrough_program_edid(const struct gc573_block_io *io,
				   struct gc573_passthrough_state *p,
				   const unsigned char edid[256]);
int gc573_splitter_video_external(const struct gc573_block_io *io,
				  struct gc573_splitter_result *identity,
				  struct gc573_splitter_link_result *link,
				  struct gc573_splitter_video_result *r,
				  const struct gc573_passthrough_edid *sink);
int gc573_passthrough_restore(const struct gc573_block_io *io, struct gc573_passthrough_state *p);
void gc573_capture_edid(unsigned char out[256]);
int gc573_scaled_edid(const unsigned char *data, unsigned int length,
    struct gc573_passthrough_edid *out);
int gc573_splitter_video_internal(const struct gc573_block_io *io,
    struct gc573_splitter_result *identity, struct gc573_splitter_link_result *link,
    struct gc573_splitter_video_result *r);
int gc573_splitter_video_format_wait(const struct gc573_splitter_video_result *r, int error);
int gc573_splitter_video_link_wait(const struct gc573_splitter_video_result *r, int error);
int gc573_splitter_video_resume(const struct gc573_block_io *io,
	struct gc573_splitter_result *identity, struct gc573_splitter_link_result *link,
	struct gc573_splitter_video_result *r, unsigned int port,
	const struct gc573_passthrough_edid *sink);
#endif
