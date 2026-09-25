// SPDX-License-Identifier: GPL-2.0-only
/* Finish the checked HDMI startup after the V4L2/ALSA devices exist.
 * One phase per call. Only explicit signal waits and pre-write link races
 * can retry. A failed writing phase remains stopped for diagnosis.
 */
#include <linux/errno.h>
#include "gc573_hdmi.h"
#include "gc573_modes.h"

static int receiver_wait(const struct gc573_block_io *io, struct gc573_hdmi *h, int power)
{
	int ret = gc573_receiver_status(io, &h->signal);

	if (ret)
		return ret;
	return (power ? (h->signal.port0 & 1) :
		((h->signal.port0 & 0x11) == 0x11 && (h->signal.sync & 0x80))) ? 0 : -EAGAIN;
}

static int splitter_wait(const struct gc573_block_io *io, struct gc573_hdmi *h, int power)
{
	int ret = gc573_splitter_link_status(io, &h->identity, &h->link);

	if (ret)
		return ret;
	return (power ? (h->link.rx[8] & 1) :
		((h->link.tx[1] & 7) == 7 && (h->link.rx[8] & 0x10) &&
		 (h->link.rx[11] & 0x80))) ? 0 : -EAGAIN;
}

static int activate(const struct gc573_block_io *io, struct gc573_hdmi *h)
{
	struct gc573_block_result last;
	unsigned int control;
	int ret = splitter_wait(io, h, 0);

	if (ret)
		return ret;
	ret = gc573_splitter_video_tx_read(io, &last, 1, 0x84);
	if (ret)
		return ret;
	control = last.data[0];
	ret = gc573_splitter_video_tx_read(io, &last, 1, 0x86);
	if (ret)
		return ret;
	/* Same checked activated state used for TX2 passthrough. Do not replay
	 * cold activation on a transmitter which already has its analog power.
	 */
	if ((control & 0xe0) == 0x80 && (last.data[0] & 8)) {
		h->tx_preserved = 1;
		return 0;
	}
	ret = gc573_splitter_port_activate(io, &h->identity, &h->link, &h->hpd, 1);
	return ret == -ENOLINK && !h->hpd.writes_started ? -EAGAIN : ret;
}

static int phase(const struct gc573_block_io *io, struct gc573_hdmi *h)
{
	int ret;

	switch (h->phase) {
	case 6:
		return receiver_wait(io, h, 1);
	case 7:
		return gc573_receiver_input(io, &h->signal, &h->clock, &h->timing,
					    &h->edid, &h->ddc, &h->input);
	case 8:
		return splitter_wait(io, h, 1);
	case 9:
		ret = gc573_splitter_input_hpd(io, &h->identity, &h->link, &h->hpd);
		if (ret == -EOPNOTSUPP && !h->hpd.writes_started &&
		    !h->hpd.prerequisite_error && !(h->link.rx[8] & 1))
			return -EAGAIN;
		return ret;
	case 10:
		return gc573_splitter_edid_enable(io, &h->identity, &h->link,
						 &h->splitter_edid, &h->hpd);
	case 11:
	case 13:
		return splitter_wait(io, h, 0);
	case 12:
		return activate(io, h);
	case 14:
		ret = gc573_splitter_video_clock(io, &h->identity, &h->link, &h->video, 2, 1);
		return ret == -ENOLINK && !h->video.writes_started ? -EAGAIN : ret;
	case 15:
		return receiver_wait(io, h, 0);
	case 16:
		ret = gc573_receiver_video(io, &h->signal, &h->receiver_video, 1);
		return ret == -ENOLINK && !h->receiver_video.writes_started ? -EAGAIN : ret;
	case 17:
		return (io->read(io->ctx, 0x1004) & 1) &&
			gc573_mode_supported(io->read(io->ctx, 0x1008), io->read(io->ctx, 0x100c)) ? 0 : -EAGAIN;
	case 18:
		/* An absent/unsupported external sink must not block capture. */
		h->passthrough_error = gc573_splitter_passthrough(io, &h->passthrough);
		return 0;
	default:
		return -EINVAL;
	}
}

int gc573_hdmi_poll(const struct gc573_block_io *io, struct gc573_hdmi *h)
{
	int ret;

	if (h->error || h->complete)
		return h->error;
	if (h->phase < GC573_HDMI_FIRST || h->phase >= GC573_HDMI_DONE)
		return h->error = -EINVAL;
	h->polls++;
	ret = phase(io, h);
	h->waiting = ret == -EAGAIN;
	if (ret == -EAGAIN)
		return 0;
	if (ret)
		return h->error = ret;
	h->phase++;
	h->complete = h->phase == GC573_HDMI_DONE;
	return 0;
}
