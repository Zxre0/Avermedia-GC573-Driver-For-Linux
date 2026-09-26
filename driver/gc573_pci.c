// SPDX-License-Identifier: GPL-2.0-only
/* GC573 native bring-up: PCI ownership and explicit register observations.
 * Optional polled I2C receiver identification; no video capture or DMA.
 */
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include "gc573_modes.h"
#include "gc573_i2c.h"
#include "gc573_block.h"
#include "gc573_capture.h"
#include "gc573_hdmi.h"
#include "gc573_passthrough.h"

#define GC573_BAR_BYTES 0x80000
#define GC573_MAX_READS 16
#define GC573_I2C_BASE 0x120

static bool output_receiver_video;
module_param(output_receiver_video, bool, 0400);
MODULE_PARM_DESC(output_receiver_video, "Configure and enable 720p/1080p RGB8 receiver TTL output");
static bool probe_receiver_video;
module_param(probe_receiver_video, bool, 0400);
MODULE_PARM_DESC(probe_receiver_video, "Read locked receiver timing, AVI and output controls");

static bool capture_video;
module_param(capture_video, bool, 0400);
MODULE_PARM_DESC(capture_video, "Expose bounded RGB24 V4L2 capture, up to 1440p120 on Gen2 x4");
static bool scaled_capture;
module_param(scaled_capture, bool, 0400);
MODULE_PARM_DESC(scaled_capture, "Experimental 1440p120 HDMI with native or downscaled capture");
static bool passthrough_only;
module_param(passthrough_only, bool, 0400);
MODULE_PARM_DESC(passthrough_only, "Experimental display-matched SDR HDMI OUT; host capture stays stopped");
static unsigned int hdmi_start_phase;
module_param(hdmi_start_phase, uint, 0400);
MODULE_PARM_DESC(hdmi_start_phase, "Continue checked HDMI startup in the background from phase 6..18");
static bool capture_once;
module_param(capture_once, bool, 0400);
MODULE_PARM_DESC(capture_once, "Capture one bounded 1080p RGB24 DMA frame");
static bool initialize_receiver;
module_param(initialize_receiver, bool, 0400);
MODULE_PARM_DESC(initialize_receiver, "Apply GC573 B1 static receiver table (persistent changes)");

static unsigned int splitter_tx_port = 1;
module_param(splitter_tx_port, uint, 0400);
MODULE_PARM_DESC(splitter_tx_port, "Transmitter for activation/video diagnostics: 1 internal, 2 external candidate");
static bool measure_splitter_video;
module_param(measure_splitter_video, bool, 0400);
static bool output_splitter_video;
module_param(output_splitter_video, bool, 0400);
MODULE_PARM_DESC(output_splitter_video, "Enable restricted RGB 8-bit unencrypted transmitter output");
static bool configure_splitter_video;
module_param(configure_splitter_video, bool, 0400);
MODULE_PARM_DESC(configure_splitter_video, "Run selected transmitter first-SCDT clock, analog and reset continuation");
MODULE_PARM_DESC(measure_splitter_video, "Measure selected transmitter clock before rate-dependent setup");
static bool activate_splitter_port1;
module_param(activate_splitter_port1, bool, 0400);
MODULE_PARM_DESC(activate_splitter_port1, "Apply the bounded selected-transmitter activation prefix");
static bool enable_splitter_edid;
module_param(enable_splitter_edid, bool, 0400);
MODULE_PARM_DESC(enable_splitter_edid, "Configure resident splitter EDID checksums and pulse HPD once");
static bool probe_splitter_edid;
module_param(probe_splitter_edid, bool, 0400);
MODULE_PARM_DESC(probe_splitter_edid, "Map and read splitter EDID SRAM; leave HPD and DDC unchanged");
static bool request_splitter_hpd;
module_param(request_splitter_hpd, bool, 0400);
MODULE_PARM_DESC(request_splitter_hpd, "Prepare cold splitter input and request HPD once with bounded polling");
static bool restore_capture_profile;
module_param(restore_capture_profile, bool, 0400);
MODULE_PARM_DESC(restore_capture_profile, "Restore a conservative 1080p60 source EDID");
static bool probe_sink;
module_param(probe_sink, bool, 0444);
MODULE_PARM_DESC(probe_sink, "Read external HDMI display EDID through TX2 DDC");
static bool probe_splitter_link;
module_param(probe_splitter_link, bool, 0400);
MODULE_PARM_DESC(probe_splitter_link, "Read splitter runtime status without resetting or acknowledging events");
static bool finish_splitter_tx;
module_param(finish_splitter_tx, bool, 0400);
MODULE_PARM_DESC(finish_splitter_tx, "Finish splitter transmitter and shared startup settings");
static bool initialize_splitter_tx;
module_param(initialize_splitter_tx, bool, 0400);
MODULE_PARM_DESC(initialize_splitter_tx, "Initialize all four splitter transmitter ports");
static bool initialize_splitter_ports;
module_param(initialize_splitter_ports, bool, 0400);
MODULE_PARM_DESC(initialize_splitter_ports, "Initialize splitter transmitter ports zero and three");
static bool prepare_splitter_tx;
module_param(prepare_splitter_tx, bool, 0400);
MODULE_PARM_DESC(prepare_splitter_tx, "Reset shared TX controls and read four mapped TX ports");
static bool finish_splitter_rx;
module_param(finish_splitter_rx, bool, 0400);
MODULE_PARM_DESC(finish_splitter_rx, "Finish splitter RX startup controls and snapshot raw status");
static bool setup_splitter_rx;
module_param(setup_splitter_rx, bool, 0400);
MODULE_PARM_DESC(setup_splitter_rx, "Program splitter RX controls after verified calibration");
static bool calibrate_splitter_rx;
module_param(calibrate_splitter_rx, bool, 0400);
MODULE_PARM_DESC(calibrate_splitter_rx, "Reset splitter RX and run one bounded CAOF calibration");
static bool configure_splitter_map;
module_param(configure_splitter_map, bool, 0400);
MODULE_PARM_DESC(configure_splitter_map, "Configure splitter TX addresses and snapshot RX registers");
static bool program_splitter_timing;
module_param(program_splitter_timing, bool, 0400);
MODULE_PARM_DESC(program_splitter_timing, "Program splitter timing from fresh reference-clock data");

static bool probe_splitter_clock;
module_param(probe_splitter_clock, bool, 0400);
MODULE_PARM_DESC(probe_splitter_clock, "Read prepared splitter reference-clock data without timing writes");

static bool prepare_splitter;
module_param(prepare_splitter, bool, 0400);
MODULE_PARM_DESC(prepare_splitter, "Prepare observed splitter control block and internal addresses");

static bool start_splitter;
module_param(start_splitter, bool, 0400);
MODULE_PARM_DESC(start_splitter, "Start splitter GPIO once or continue ID reads from released state");

static bool probe_splitter;
module_param(probe_splitter, bool, 0400);
MODULE_PARM_DESC(probe_splitter, "Read fixed splitter bank/identity without GPIO changes");

static bool start_input;
module_param(start_input, bool, 0400);
MODULE_PARM_DESC(start_input, "Prepare B1 port zero, assert HPD once and poll input lock");

static bool configure_edid;
module_param(configure_edid, bool, 0400);
MODULE_PARM_DESC(configure_edid, "Configure resident EDID checksums and DDC with HPD low");

static bool probe_edid;
module_param(probe_edid, bool, 0400);
MODULE_PARM_DESC(probe_edid, "Map and read EDID SRAM after B1 clock/timing setup; no SRAM writes");

static bool program_timing;
module_param(program_timing, bool, 0400);
MODULE_PARM_DESC(program_timing, "Apply B1 timing settings from a fresh reference-clock read");

static bool probe_clock;
module_param(probe_clock, bool, 0400);
MODULE_PARM_DESC(probe_clock, "Read B1 internal reference-clock data after CAOF");

static bool calibrate_receiver;
module_param(calibrate_receiver, bool, 0400);
MODULE_PARM_DESC(calibrate_receiver, "One B1 CAOF calibration attempt after static initialization");

static bool probe_write;
module_param(probe_write, bool, 0400);
MODULE_PARM_DESC(probe_write, "Verify one bank-zero receiver write after identity/revision checks");

static bool probe_signal;
module_param(probe_signal, bool, 0400);
MODULE_PARM_DESC(probe_signal, "Read receiver bank/identity/input status without GPIO reset");

static bool probe_receiver;
module_param(probe_receiver, bool, 0400);
MODULE_PARM_DESC(probe_receiver, "Receiver GPIO startup then one read (default off)");

static bool prepare_gpio;
module_param(prepare_gpio, bool, 0400);
MODULE_PARM_DESC(prepare_gpio, "Set vendor initialization GPIO bit; observe only (default off)");

static bool probe_block;
module_param(probe_block, bool, 0400);
MODULE_PARM_DESC(probe_block, "One polled block-controller receiver read (default off)");

static bool inspect_access;
module_param(inspect_access, bool, 0400);
MODULE_PARM_DESC(inspect_access, "Compare byte/dword register reads; no MMIO writes");

/* Identity, two legacy control/status pairs, and block I2C status.
 * Exclude data/FIFO registers, which can consume data when read.
 */
static const unsigned int access_offsets[] = {
	0x000, 0x108, 0x110, 0x128, 0x130, 0x1a4,
};

static bool probe_i2c;
module_param(probe_i2c, bool, 0400);
MODULE_PARM_DESC(probe_i2c, "Run one bounded receiver ID transaction (MMIO writes; default off)");

static char *target_bdf;
module_param(target_bdf, charp, 0400);
MODULE_PARM_DESC(target_bdf, "Required PCI address, e.g. 0000:05:00.0");

static unsigned int read_offsets[GC573_MAX_READS];
static unsigned int read_count;
module_param_array(read_offsets, uint, &read_count, 0400);
MODULE_PARM_DESC(read_offsets,
	"Optional BAR0 byte offsets to read once; aligned 32-bit reads only; empty by default");

struct gc573_device {
	struct gc573_capture *capture;
	struct delayed_work hdmi_work;
	struct gc573_hdmi hdmi;
	struct gc573_block_io hdmi_io;
	bool hdmi_ready;
	struct mutex control_mutex;
	unsigned int combined_phase, combined_changes, combined_format_waits;
	unsigned int combined_period;
	int combined_error;
	int passthrough_error;
	struct gc573_passthrough_result passthrough;
	int receiver_video_error;
	struct gc573_receiver_video_result receiver_video;
	void __iomem *bar;
	u16 initial_command;
	u32 samples[GC573_MAX_READS];
	unsigned int mmio_writes;
	int i2c_error;
	struct gc573_i2c_result i2c;
	u8 access_byte[ARRAY_SIZE(access_offsets)];
	u32 access_word[ARRAY_SIZE(access_offsets)];
	int block_error;
	struct gc573_block_result block;
	int gpio_error;
	struct gc573_gpio_result gpio;
	struct gc573_receiver_result receiver;
	int signal_error;
	struct gc573_signal_result signal;
	int write_test_error;
	struct gc573_write_test_result write_test;
	int init_error;
	struct gc573_init_result init;
	int cal_error;
	struct gc573_cal_result cal;
	int clock_error;
	struct gc573_clock_result clock;
	int timing_error;
	struct gc573_timing_result timing;
	int edid_error;
	struct gc573_edid_result edid;
	int ddc_error;
	struct gc573_ddc_result ddc;
	int input_error;
	struct gc573_input_result input;
	int splitter_error;
	struct gc573_splitter_result splitter;
	struct gc573_splitter_start_result splitter_start;
	int splitter_prepare_error;
	struct gc573_splitter_prepare_result splitter_prepare;
	int splitter_clock_error;
	struct gc573_splitter_clock_result splitter_clock;
	int splitter_edid_error;
	struct gc573_splitter_edid_result splitter_edid;
	int splitter_video_error;
	struct gc573_splitter_video_result splitter_video;
	int splitter_hpd_error;
	struct gc573_splitter_hpd_result splitter_hpd;
	int sink_error;
	struct gc573_sink_result sink;
	struct gc573_passthrough_state external;
	int splitter_link_error;
	struct gc573_splitter_link_result splitter_link;
	int splitter_ports_error;
	struct gc573_splitter_ports_result splitter_ports;
	int splitter_tx_error;
	struct gc573_splitter_tx_result splitter_tx;
	int splitter_finish_error;
	struct gc573_splitter_finish_result splitter_finish;
	int splitter_setup_error;
	struct gc573_splitter_setup_result splitter_setup;
	int splitter_cal_error;
	struct gc573_splitter_cal_result splitter_cal;
	int splitter_map_error;
	struct gc573_splitter_map_result splitter_map;
	int splitter_timing_error;
	struct gc573_splitter_timing_result splitter_timing;
};

static unsigned long gc573_time_ms(void *ctx)
{
	return ktime_to_ms(ktime_get());
}

static void gc573_sleep_ms(void *ctx, unsigned int milliseconds)
{
	msleep(milliseconds);
}

static unsigned int gc573_block_read(void *ctx, unsigned int offset)
{
	struct gc573_device *card = ctx;

	return ioread32(card->bar + offset);
}

static void gc573_block_write(void *ctx, unsigned int offset, unsigned int value)
{
	struct gc573_device *card = ctx;

	iowrite32(value, card->bar + offset);
	card->mmio_writes++;
}

static unsigned int gc573_block_start_read(void *ctx)
{
	struct gc573_device *card = ctx;
	unsigned long flags;
	unsigned int status;

	/* Only two MMIO accesses are protected, never the sleeping completion poll.
	 * Otherwise an interrupt/preemption between START and the first sample can
	 * hide the entire busy interval and leave a fresh result looking stale.
	 */
	local_irq_save(flags);
	iowrite32(0x08, card->bar + GC573_BLOCK_COMMAND);
	status = ioread32(card->bar + GC573_BLOCK_STATUS);
	local_irq_restore(flags);
	card->mmio_writes++;
	return status;
}

static int gc573_block_wait(void *ctx, unsigned int *status,
			    unsigned int *saw_clear)
{
	struct gc573_device *card = ctx;
	unsigned int sample;
	int ret, observed;

	ret = read_poll_timeout(ioread32, sample,
			       (observed = gc573_block_observe(sample, saw_clear)) != 0,
			       50, 2000000, false, card->bar + GC573_BLOCK_STATUS);
	*status = sample;
	return ret ? ret : (observed < 0 ? observed : 0);
}

static int gc573_block_wait_write(void *ctx, unsigned int *status,
				  unsigned int *saw_clear)
{
	struct gc573_device *card = ctx;
	unsigned int sample;
	int ret, observed;

	ret = read_poll_timeout(ioread32, sample,
			       (observed = gc573_block_observe_write(sample, saw_clear)) != 0,
			       50, 2000000, false, card->bar + GC573_BLOCK_STATUS);
	*status = sample;
	return ret ? ret : (observed < 0 ? observed : 0);
}

static int gc573_hdmi_ready(void *ctx)
{
	struct gc573_device *card = ctx;

	return smp_load_acquire(&card->hdmi_ready);
}

static void gc573_control_lock(void *ctx)
{ mutex_lock(&((struct gc573_device *)ctx)->control_mutex); }
static void gc573_control_unlock(void *ctx)
{ mutex_unlock(&((struct gc573_device *)ctx)->control_mutex); }

/* Called while holding the same control-bus mutex as ALSA prepare. */
static int gc573_combined_poll(struct gc573_device *card)
{
	struct gc573_passthrough_state *p = &card->external;
	struct gc573_hdmi *h = &card->hdmi;
	const struct gc573_block_io *io = &card->hdmi_io;
	int ret;

	if (card->combined_error) return card->combined_error;
	ret = gc573_passthrough_poll(io, p);
	if (ret) goto fail;
	/* TX2's monitor can stop asserting RxSense while the PS5 and internal
	 * receiver remain active. Capture follows the stable input, not TX2.
	 */
	if (!p->stable || p->format_rejected || card->combined_changes != p->changes) {
		smp_store_release(&card->hdmi_ready, false);
		card->combined_phase = 0;
		h->video.waiting_link = 0;
		card->combined_changes = p->changes;
	}
	if (!p->stable || p->format_rejected) return 0;
	switch (card->combined_phase) {
	case 0:
		ret = h->video.waiting_link ?
			gc573_splitter_video_resume(io, &h->identity, &h->link, &h->video, 1, NULL) :
			gc573_splitter_video_internal(io, &h->identity, &h->link, &h->video);
		if (ret == -EAGAIN) return 0;
		if (gc573_splitter_video_link_wait(&h->video, ret)) return 0;
		if (gc573_splitter_video_format_wait(&h->video, ret)) {
			card->combined_format_waits++;
			return 0;
		}
		if (ret) goto fail;
		card->combined_phase = 1;
		break;
	case 1:
		ret = gc573_receiver_video(io, &h->signal, &h->receiver_video, 2);
		if (gc573_receiver_video_retryable(&h->receiver_video, ret)) return 0;
		if (ret == -EOPNOTSUPP && h->receiver_video.phase == 5 &&
		    h->receiver_video.bank_verified && !h->receiver_video.bank &&
		    h->receiver_video.last.status == GC573_BLOCK_READ_DONE) {
			card->combined_format_waits++;
			return 0;
		}
		if (ret) goto fail;
		card->combined_phase = 2;
		break;
	case 2: {
		unsigned int dual = h->receiver_video.width > 1920 ||
			h->receiver_video.pixel_max_khz > 150000;
		unsigned int packing = dual ? 3 : 0;
		unsigned int mode = io->read(io->ctx, 0x1040);
		unsigned int target = (mode & ~0x20U) | (dual ? 0x20U : 0);
		unsigned int period = io->read(io->ctx, 0x1010);

		/* Match the traced RGB single SDR / dual DDR receiver interface. Do not
		 * change packing while a previous capture transfer is stopping.
		 */
		if ((io->read(io->ctx, 0x1000) & 1) || io->read(io->ctx, 0x304))
			return 0;
		if (mode != target || io->read(io->ctx, 0x1088) != packing) {
			io->write(io->ctx, 0x1040, target);
			io->write(io->ctx, 0x1088, packing);
			if (io->read(io->ctx, 0x1040) != target ||
			    io->read(io->ctx, 0x1088) != packing) {
				ret = -EIO;
				goto fail;
			}
			return 0;
		}
		if (!(io->read(io->ctx, 0x1004) & 1) ||
		    gc573_input_pixels(io->read(io->ctx, 0x1008), packing) != h->receiver_video.width ||
		    io->read(io->ctx, 0x100c) != h->receiver_video.height ||
		    period < 100000000U / 121 || period > 100000000U / 23)
			return 0;
		card->combined_phase = 3;
		card->combined_period = period;
		smp_store_release(&card->hdmi_ready, true);
		break;
	}
	case 3: {
		unsigned int period = io->read(io->ctx, 0x1010);
		unsigned int packing = io->read(io->ctx, 0x1088);

		/* Also detect rate-only changes while the external monitor is idle.
		 * Reconfigure TX1/SCDC before capture resumes on the new source mode.
		 */
		if (!(io->read(io->ctx, 0x1004) & 1) ||
		    gc573_input_pixels(io->read(io->ctx, 0x1008), packing) != h->receiver_video.width ||
		    io->read(io->ctx, 0x100c) != h->receiver_video.height ||
		    period < card->combined_period * 9 / 10 ||
		    period > card->combined_period * 11 / 10) {
			smp_store_release(&card->hdmi_ready, false);
			card->combined_phase = 0;
			h->video.waiting_link = 0;
		}
		break;
	}
	}
	return 0;
fail:
	smp_store_release(&card->hdmi_ready, false);
	return card->combined_error = ret;
}

static void gc573_hdmi_work(struct work_struct *work)
{
	struct gc573_device *card = container_of(to_delayed_work(work),
						struct gc573_device, hdmi_work);
	int ret;
	if (scaled_capture) {
		gc573_control_lock(card);
		ret = gc573_combined_poll(card);
		gc573_control_unlock(card);
		if (ret) {
			pr_err("gc573_native: scaled capture phase %u stopped: %d\n", card->combined_phase, ret);
			return;
		}
		schedule_delayed_work(&card->hdmi_work, msecs_to_jiffies(500));
		return;
	}
	if (passthrough_only) {
		ret = gc573_passthrough_poll(&card->hdmi_io, &card->external);
		if (ret) {
			pr_err("gc573_native: external HDMI phase %u stopped: %d\n", card->external.phase, ret);
			return;
		}
		schedule_delayed_work(&card->hdmi_work, msecs_to_jiffies(500));
		return;
	}
	ret = gc573_hdmi_poll(&card->hdmi_io, &card->hdmi);

	if (ret) {
		pr_err("gc573_native: HDMI startup stopped at phase %u: %d; capture device remains registered\n",
		       card->hdmi.phase, ret);
		return;
	}
	if (card->hdmi.complete) {
		card->passthrough = card->hdmi.passthrough;
		card->passthrough_error = card->hdmi.passthrough_error;
		/* No further worker I2C accesses after publishing readiness. ALSA
		 * prepare may now take exclusive ownership of the shared bus.
		 */
		smp_store_release(&card->hdmi_ready, true);
		return;
	}
	schedule_delayed_work(&card->hdmi_work,
		msecs_to_jiffies(card->hdmi.waiting ? 500 : 10));
}

static unsigned char gc573_read_i2c(void *ctx, unsigned int reg)
{
	struct gc573_device *card = ctx;

	return ioread8(card->bar + GC573_I2C_BASE + reg * 4);
}

static void gc573_write_i2c(void *ctx, unsigned int reg, unsigned char value)
{
	struct gc573_device *card = ctx;

	iowrite8(value, card->bar + GC573_I2C_BASE + reg * 4);
	card->mmio_writes++;
}

static int gc573_wait_i2c(void *ctx, unsigned char mask, unsigned char value,
			 unsigned char *status)
{
	struct gc573_device *card = ctx;
	unsigned char sample;
	int ret;

	ret = read_poll_timeout(ioread8, sample, (sample & mask) == value,
			       10, 20000, false,
			       card->bar + GC573_I2C_BASE + GC573_I2C_COMMAND * 4);
	*status = sample;
	return ret;
}

static ssize_t bringup_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct gc573_device *card = dev_get_drvdata(dev);
	const char *stage = "pci-diagnostics-only";
	ssize_t used;
	unsigned int i;

	if (capture_video)
		stage = "v4l2-capture";
	else if (capture_once || capture_video)
		stage = "fpga-single-frame";
	else if (output_receiver_video)
		stage = "receiver-video-output";
	else if (probe_receiver_video)
		stage = "receiver-video-format";
	else if (output_splitter_video)
		stage = "splitter-transmitter-video-output";
	else if (configure_splitter_video)
		stage = "splitter-transmitter-video-setup";
	else if (measure_splitter_video)
		stage = "splitter-transmitter-video-clock";
	else if (activate_splitter_port1)
		stage = "splitter-transmitter-activation-prefix";
	else if (enable_splitter_edid)
		stage = "splitter-edid-enable";
	else if (probe_splitter_edid)
		stage = "splitter-edid-memory-read";
	else if (request_splitter_hpd)
		stage = "splitter-input-hotplug";
	else if (probe_splitter_link)
		stage = "splitter-link-status";
	else if (initialize_splitter_ports || initialize_splitter_tx || finish_splitter_tx)
		stage = finish_splitter_tx ? "splitter-transmitter-startup-tail" :
			initialize_splitter_tx ? "splitter-transmitter-initialization" :
			"splitter-transmitter-ports03";
	else if (prepare_splitter_tx)
		stage = "splitter-transmitter-preparation";
	else if (finish_splitter_rx)
		stage = "splitter-receiver-startup-tail";
	else if (setup_splitter_rx)
		stage = "splitter-receiver-setup";
	else if (calibrate_splitter_rx)
		stage = "splitter-receiver-calibration";
	else if (configure_splitter_map)
		stage = "splitter-address-configuration";
	else if (program_splitter_timing)
		stage = "splitter-clock-timing";
	else if (probe_splitter_clock)
		stage = "splitter-reference-clock";
	else if (prepare_splitter)
		stage = "splitter-control-preparation";
	else if (start_splitter)
		stage = "splitter-startup-identification";
	else if (probe_splitter)
		stage = "splitter-identification";
	else if (start_input)
		stage = "receiver-input-startup";
	else if (configure_edid)
		stage = "receiver-edid-configuration";
	else if (probe_edid)
		stage = "receiver-edid-memory-read";
	else if (program_timing)
		stage = "receiver-clock-timing";
	else if (probe_clock)
		stage = "receiver-reference-clock";
	else if (calibrate_receiver)
		stage = "receiver-caof-calibration";
	else if (initialize_receiver)
		stage = "receiver-static-initialization";
	else if (probe_write)
		stage = "receiver-write-verification";
	else if (probe_signal)
		stage = "receiver-signal-status";
	else if (probe_receiver)
		stage = "receiver-startup-identification";
	else if (prepare_gpio)
		stage = "gpio-preparation";
	else if (probe_block)
		stage = "block-i2c-identification";
	else if (inspect_access)
		stage = "register-access-check";
	else if (probe_i2c)
		stage = "i2c-identification";
	used = sysfs_emit(buf,
		"stage=%s\nvideo_capture=%s\n"
		"initial_pci_command=0x%04x\nbar0_bytes=0x%x\n"
		"mmio_writes=%u\nsampled_registers=%u\n",
		stage, capture_video ? "v4l2-rgb24" : capture_once ? "experimental-single-frame" : "unimplemented",
		card->initial_command, GC573_BAR_BYTES, card->mmio_writes, read_count);
	for (i = 0; i < read_count; i++)
		used += sysfs_emit_at(buf, used, "bar0[0x%08x]=0x%08x\n",
				     read_offsets[i], card->samples[i]);
	if (capture_video)
		used += sysfs_emit_at(buf, used,
			"hdmi_deferred=%u\nhdmi_ready=%u\nhdmi_phase=%u\nhdmi_waiting=%u\nhdmi_error=%d\nhdmi_polls=%u\nhdmi_tx_preserved=%u\n",
			!!hdmi_start_phase, gc573_hdmi_ready(card), READ_ONCE(card->hdmi.phase),
			READ_ONCE(card->hdmi.waiting), READ_ONCE(card->hdmi.error),
			READ_ONCE(card->hdmi.polls), READ_ONCE(card->hdmi.tx_preserved));
	if (capture_video && hdmi_start_phase) {
		const struct gc573_receiver_video_result *r = &card->hdmi.receiver_video;

		used += sysfs_emit_at(buf, used,
			"hdmi_receiver_phase=%u\nhdmi_receiver_width=%u\nhdmi_receiver_height=%u\n"
			"hdmi_receiver_reference_half_khz=%u\nhdmi_receiver_pixel_min_khz=%u\nhdmi_receiver_pixel_max_khz=%u\n"
			"hdmi_receiver_clock_samples=%u\nhdmi_receiver_measurement_restored=%u\nhdmi_receiver_writes=%u\n",
			READ_ONCE(r->phase), READ_ONCE(r->width), READ_ONCE(r->height),
			READ_ONCE(r->reference_half_khz), READ_ONCE(r->pixel_min_khz), READ_ONCE(r->pixel_max_khz),
			READ_ONCE(r->clock_samples), READ_ONCE(r->measurement_restored), READ_ONCE(r->writes_started));
	}
	if (capture_video)
		used += sysfs_emit_at(buf, used,
			"passthrough_startup_error=%d\npassthrough_startup_phase=%u\n"
			"passthrough_startup_sink=%u\npassthrough_startup_tx_status=0x%02x\n"
			"passthrough_startup_preserved=%u\npassthrough_startup_enabled=%u\n",
			card->passthrough_error, card->passthrough.phase, card->passthrough.sink_present,
			card->passthrough.tx_status, card->passthrough.preserved, card->passthrough.enabled);
	if (capture_once || capture_video)
		used = gc573_capture_status(card->capture, buf, used);
	if (probe_receiver_video || output_receiver_video) {
		const struct gc573_receiver_video_result *r = &card->receiver_video;
		unsigned int j;

		used += sysfs_emit_at(buf, used,
			"receiver_video_error=%d\nreceiver_video_phase=%u\n"
			"receiver_video_transactions=%u\nreceiver_video_writes_started=%u\n"
			"receiver_video_last_reg=0x%02x\nreceiver_video_bank=%u\n"
			"receiver_video_bank_verified=%u\nreceiver_video_timing_samples=%u\n"
			"receiver_video_width=%u\nreceiver_video_height=%u\n"
			"receiver_video_htotal=%u\nreceiver_video_vtotal=%u\n"
			"receiver_video_interlaced=%u\nreceiver_video_complete=%u\n",
			card->receiver_video_error, r->phase, r->transactions, r->writes_started,
			r->last_reg, r->bank, r->bank_verified, r->timing_samples,
			r->width, r->height, r->htotal, r->vtotal, r->interlaced, r->complete);
		used += sysfs_emit_at(buf, used,
			"receiver_video_pixel_min_khz=%u\nreceiver_video_pixel_max_khz=%u\n"
			"receiver_video_steps_verified=%u\nreceiver_video_output_enabled=%u\n"
			"receiver_video_expected=0x%02x\nreceiver_video_observed=0x%02x\n",
			r->pixel_min_khz, r->pixel_max_khz, r->steps_verified, r->output_enabled,
			r->expected, r->observed);
		for (i = 0; i < r->clock_samples; i++)
			used += sysfs_emit_at(buf, used, "receiver_video_clock_count[%u]=%u\n", i, r->counts[i]);
		for (j = 0; j < r->timing_samples; j++)
			for (i = 0; i < 19; i++)
				used += sysfs_emit_at(buf, used, "receiver_video_timing%u_%02x=0x%02x\n",
					j, 0x98 + i, r->timing[j][i]);
		used += sysfs_emit_at(buf, used, "receiver_video_extra=%*ph\nreceiver_video_avi=%*ph\nreceiver_video_output=%*ph\n",
			4, r->extra, 5, r->avi, 7, r->output);
	}
	if (measure_splitter_video || configure_splitter_video || output_splitter_video || activate_splitter_port1)
		used += sysfs_emit_at(buf, used, "splitter_tx_port=%u\n", splitter_tx_port);
	if (measure_splitter_video || configure_splitter_video || output_splitter_video) {
		const struct gc573_splitter_video_result *r = &card->splitter_video;

		used += sysfs_emit_at(buf, used,
			"splitter_video_error=%d\nsplitter_video_phase=%u\n"
			"splitter_video_prerequisite_error=%d\nsplitter_video_transactions=%u\n"
			"splitter_video_writes_started=%u\nsplitter_video_steps_verified=%u\n"
			"splitter_video_last_address=0x%02x\nsplitter_video_last_reg=0x%02x\n"
			"splitter_video_last_status=0x%08x\nsplitter_video_expected=0x%02x\n"
			"splitter_video_observed=0x%02x\nsplitter_video_reference_ticks=%u\n"
			"splitter_video_reference_khz=%u\nsplitter_video_initial_count=%u\n"
			"splitter_video_exponent=%u\nsplitter_video_samples=%u\n"
			"splitter_video_depth=%u\nsplitter_video_pixel_khz=%u\n"
			"splitter_video_link_khz=%u\nsplitter_video_rate_valid=%u\n"
			"splitter_video_complete=%u\n",
			card->splitter_video_error, r->phase, r->prerequisite_error, r->transactions,
			r->writes_started, r->steps_verified, r->last_address, r->last_reg, r->last.status,
			r->expected, r->observed, r->reference_ticks, r->reference_khz, r->initial_count,
			r->exponent, r->samples, r->depth, r->pixel_khz, r->link_khz, r->rate_valid,
			r->complete);
		used += sysfs_emit_at(buf, used,
			"splitter_video_irq_valid=0x%x\nsplitter_video_analog_complete=%u\n"
			"splitter_video_output_setup_complete=%u\n",
			r->irq_valid, r->analog_complete, r->output_setup_complete);
		if (output_splitter_video)
			used += sysfs_emit_at(buf, used,
				"splitter_video_bank=%u\nsplitter_video_bank_verified=%u\n"
				"splitter_video_format_valid=%u\nsplitter_video_avi_color=0x%02x\n"
				"splitter_video_rx_cf=0x%02x\nsplitter_video_rx13=0x%02x\n"
				"splitter_video_tx_status=0x%02x\nsplitter_video_output_enabled=%u\n",
				r->bank, r->bank_verified, r->format_valid, r->avi_color,
				r->rx_cf, r->rx13, r->tx_status, r->output_enabled);
		for (i = 0; i < 6; i++)
			if (r->irq_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_video_irq%02x_before=0x%02x\n",
					0x10 + i, r->irq_before[i]);
		for (i = 0; i < r->samples; i++)
			used += sysfs_emit_at(buf, used, "splitter_video_count[%u]=%u\n", i,
				r->counts[i]);
	}
	if (probe_splitter_edid) {
		const struct gc573_splitter_edid_result *r = &card->splitter_edid;

		used += sysfs_emit_at(buf, used,
			"splitter_edid_error=%d\nsplitter_edid_phase=%u\n"
			"splitter_edid_prerequisite_error=%d\nsplitter_edid_transactions=%u\n"
			"splitter_edid_writes_started=%u\nsplitter_edid_control_valid=0x%x\n"
			"splitter_edid_mapping_verified=%u\nsplitter_edid_mapping_after=0x%02x\n"
			"splitter_edid_bytes_read=%u\nsplitter_edid_last_reg=0x%02x\n"
			"splitter_edid_last_status=0x%08x\nsplitter_edid_complete=%u\n"
			"splitter_edid_header_matches=%u\nsplitter_edid_checksum0=0x%02x\n"
			"splitter_edid_checksum1=0x%02x\n",
			card->splitter_edid_error, r->phase, r->prerequisite_error, r->transactions,
			r->writes_started, r->control_valid, r->mapping_verified, r->mapping_after,
			r->bytes_read, r->last_reg, r->last.status, r->complete, r->header_matches,
			r->checksum[0], r->checksum[1]);
		for (i = 0; i < GC573_SPLITTER_EDID_REGS; i++)
			if (r->control_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_edid_reg%02x=0x%02x\n",
					gc573_splitter_edid_regs[i], r->control[i]);
		for (i = 0; i < r->bytes_read; i += 16)
			used += sysfs_emit_at(buf, used, "splitter_edid[%02x]=%*ph\n", i,
				min(16U, r->bytes_read - i), &r->data[i]);
	}
	if (request_splitter_hpd || enable_splitter_edid || activate_splitter_port1) {
		const struct gc573_splitter_hpd_result *r = &card->splitter_hpd;

		used += sysfs_emit_at(buf, used,
			"splitter_hpd_error=%d\nsplitter_hpd_phase=%u\n"
			"splitter_hpd_prerequisite_error=%d\nsplitter_hpd_transactions=%u\n"
			"splitter_hpd_writes_started=%u\nsplitter_hpd_steps_completed=%u\n"
			"splitter_hpd_steps_verified=%u\nsplitter_hpd_last_address=0x%02x\n"
			"splitter_hpd_last_reg=0x%02x\nsplitter_hpd_last_status=0x%08x\n"
			"splitter_hpd_expected=0x%02x\nsplitter_hpd_observed=0x%02x\n"
			"splitter_hpd_bank=%u\nsplitter_hpd_bank_verified=%u\n"
			"splitter_hpd_sink_mask=0x%x\nsplitter_hpd_ab_valid=%u\n"
			"splitter_hpd_setup_complete=%u\nsplitter_hpd_samples=%u\n"
			"splitter_hpd_lock_seen=%u\n",
			card->splitter_hpd_error, r->phase, r->prerequisite_error, r->transactions,
			r->writes_started, r->steps_completed, r->steps_verified,
			r->last_address, r->last_reg, r->last.status, r->expected, r->observed,
			r->bank, r->bank_verified, r->sink_mask, r->ab_valid, r->setup_complete,
			r->samples, r->lock_seen);
		if (activate_splitter_port1 && r->c1_valid)
			used += sysfs_emit_at(buf, used, "splitter_tx%u_c1_readback=0x%02x\n",
				splitter_tx_port, r->c1_readback);
		if (r->ab_valid)
			used += sysfs_emit_at(buf, used, "splitter_hpd_ab_before=0x%02x\n",
				r->ab_before);
		if (enable_splitter_edid)
			used += sysfs_emit_at(buf, used,
				"splitter_ddc_configured=%u\nsplitter_ddc_checksum0=0x%02x\n"
				"splitter_ddc_checksum1=0x%02x\n",
				r->edid_configured, r->edid_checksum[0], r->edid_checksum[1]);
		for (i = 0; i < r->samples; i++)
			used += sysfs_emit_at(buf, used, "splitter_hpd_poll[%u]=%02x %02x\n",
				i, r->poll[i][0], r->poll[i][1]);
	}
	if (probe_splitter_link) {
		const struct gc573_splitter_link_result *r = &card->splitter_link;

		used += sysfs_emit_at(buf, used,
			"splitter_link_error=%d\nsplitter_link_phase=%u\n"
			"splitter_link_prerequisite_error=%d\nsplitter_link_transactions=%u\n"
			"splitter_link_last_address=0x%02x\nsplitter_link_last_reg=0x%02x\n"
			"splitter_link_last_status=0x%08x\nsplitter_link_mapping_verified=%u\n"
			"splitter_link_rx_identity_verified=%u\nsplitter_link_rx_valid=0x%x\n"
			"splitter_link_tx_valid=0x%x\nsplitter_link_irq_valid=0x%x\n"
			"splitter_link_banks_verified=0x%x\nsplitter_link_complete=%u\n",
			card->splitter_link_error, r->phase, r->prerequisite_error, r->transactions,
			r->last_address, r->last_reg, r->last.status, r->mapping_verified,
			r->rx_identity_verified, r->rx_valid, r->tx_valid, r->irq_valid,
			r->banks_verified, r->complete);
		if (r->irq_valid & 1)
			used += sysfs_emit_at(buf, used, "splitter_link_sw05_before=0x%02x\n",
				r->irq_before);
		if (r->irq_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_link_sw05_after=0x%02x\n",
				r->irq_after);
		for (i = 0; i < GC573_SPLITTER_LINK_REGS; i++)
			if (r->rx_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_link_rx%02x=0x%02x\n",
					gc573_splitter_link_regs[i], r->rx[i]);
		for (i = 0; i < 4; i++)
			if (r->tx_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_link_tx%u_reg03=0x%02x\n",
					i, r->tx[i]);
	}
	if (start_splitter) {
		const struct gc573_splitter_start_result *r = &card->splitter_start;

		used += sysfs_emit_at(buf, used,
			"splitter_start_preflight_complete=%u\n"
			"splitter_start_resumed=%u\nsplitter_start_gpio_settle_changed=0x%08x\n"
			"splitter_start_gpio_before=0x%08x\nsplitter_start_gpio_after=0x%08x\n"
			"splitter_start_status_before=0x%08x\nsplitter_start_status_after_valid=%u\n"
			"splitter_start_writes_started=%u\nsplitter_start_steps_completed=%u\n"
			"splitter_start_complete=%u\n",
			r->preflight_complete, r->resumed, r->gpio_settle_changed,
			r->gpio_before, r->gpio_after, r->status_before,
			r->status_after_valid, r->writes_started,
			r->steps_completed, r->complete);
		if (r->status_after_valid)
			used += sysfs_emit_at(buf, used, "splitter_start_status_after=0x%08x\n",
					     r->status_after);
		for (i = 0; i < r->writes_started; i++)
			used += sysfs_emit_at(buf, used, "splitter_start_gpio[%u]=0x%08x\n",
					     i, r->gpio_steps[i]);
	}
	if (prepare_splitter) {
		const struct gc573_splitter_prepare_result *r = &card->splitter_prepare;

		used += sysfs_emit_at(buf, used,
			"splitter_prepare_error=%d\nsplitter_prepare_phase=%u\n"
			"splitter_prepare_prerequisite_error=%d\nsplitter_prepare_preflight_complete=%u\n"
			"splitter_prepare_transactions=%u\nsplitter_prepare_writes_started=%u\n"
			"splitter_prepare_bank=%u\nsplitter_prepare_bank_verified=%u\n"
			"splitter_prepare_last_reg=0x%02x\nsplitter_prepare_last_status=0x%08x\n"
			"splitter_prepare_expected=0x%02x\nsplitter_prepare_observed=0x%02x\n"
			"splitter_engine_valid=%u\nsplitter_engine_before=0x%02x\n"
			"splitter_engine_status=0x%02x\nsplitter_engine_recovery_used=%u\n"
			"splitter_engine_poll_samples=%u\nsplitter_engine_ready=%u\n"
			"splitter_prepare_complete=%u\n",
			card->splitter_prepare_error, r->phase, r->prerequisite_error,
			r->preflight_complete, r->transactions, r->writes_started,
			r->bank, r->bank_verified, r->last_reg, r->last.status,
			r->expected, r->observed, r->engine_valid, r->engine_before,
			r->engine_status, r->recovery_used, r->poll_samples, r->ready, r->complete);
	}
	if (initialize_splitter_ports || initialize_splitter_tx || finish_splitter_tx) {
		const struct gc573_splitter_ports_result *r = &card->splitter_ports;
		static const unsigned char regs[] = { 3, 1, 0x84, 0x86, 0x88 };
		static const unsigned char order[] = { 0, 3, 1, 2 };
		unsigned int j;

		used += sysfs_emit_at(buf, used,
			"splitter_ports_error=%d\nsplitter_ports_phase=%u\n"
			"splitter_ports_prerequisite_error=%d\nsplitter_ports_transactions=%u\n"
			"splitter_ports_writes_started=%u\nsplitter_ports_steps_completed=%u\n"
			"splitter_ports_steps_verified=%u\nsplitter_ports_current_port=%u\n"
			"splitter_ports_last_address=0x%02x\nsplitter_ports_last_reg=0x%02x\n"
			"splitter_ports_last_status=0x%08x\nsplitter_ports_last_started=%u\n"
			"splitter_ports_expected=0x%02x\nsplitter_ports_observed=0x%02x\n"
			"splitter_ports_mapping_verified=%u\nsplitter_ports_completed_mask=0x%x\n"
			"splitter_ports_no_sink_mask=0x%x\nsplitter_ports_bank_verified=%u\n"
			"splitter_ports_control_bank_verified=%u\nsplitter_ports_status_valid=0x%x\n"
			"splitter_ports_complete=%u\nsplitter_tx_complete=%u\nsplitter_tx_phase=%u\n",
			card->splitter_ports_error, r->phase, r->prerequisite_error, r->transactions,
			r->writes_started, r->steps_completed, r->steps_verified, r->port,
			r->last_address, r->last_reg, r->last.status, r->last.started,
			r->expected, r->observed, r->mapping_verified, r->ports_complete,
			r->no_sink_mask, r->bank_verified, r->control_bank_verified,
			r->status_valid, r->complete, card->splitter_tx.complete, card->splitter_tx.phase);
		used += sysfs_emit_at(buf, used, "splitter_ports_common_enabled=%u\n"
			"splitter_ports_tail_complete=%u\n", r->common_enabled, r->tail_complete);
		for (i = 0; i < ((initialize_splitter_tx || finish_splitter_tx) ? 4 : 2); i++) {
			used += sysfs_emit_at(buf, used,
				"splitter_ports_port%u_sink_valid=0x%x\n"
				"splitter_ports_port%u_snapshot_valid=0x%x\n",
				order[i], r->sink_valid[i], order[i], r->snapshot_valid[i]);
			for (j = 0; j < 2; j++)
				if (r->sink_valid[i] & (1U << j))
					used += sysfs_emit_at(buf, used,
						"splitter_ports_during%u_port%u_reg03=0x%02x\n",
						order[i], j + 1, r->sink[i][j]);
			for (j = 0; j < 5; j++)
				if (r->snapshot_valid[i] & (1U << j))
					used += sysfs_emit_at(buf, used,
						"splitter_ports_port%u_reg%02x=0x%02x\n",
						order[i], regs[j], r->snapshot[i][j]);
		}
		if (r->status_valid & 1)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg13=0x%02x\n", r->status[0]);
		if (r->status_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg19=0x%02x\n", r->status[1]);
	}
	if (prepare_splitter_tx) {
		const struct gc573_splitter_tx_result *r = &card->splitter_tx;
		static const unsigned char regs[] = { 0x03, 0x01, 0x84, 0x86, 0x88 };
		unsigned int j;

		used += sysfs_emit_at(buf, used,
			"splitter_tx_error=%d\nsplitter_tx_phase=%u\n"
			"splitter_tx_prerequisite_error=%d\nsplitter_tx_transactions=%u\n"
			"splitter_tx_writes_started=%u\nsplitter_tx_steps_verified=%u\n"
			"splitter_tx_common_mapping_verified=%u\nsplitter_tx_mapping_valid=0x%x\n"
			"splitter_tx_reset_complete=%u\nsplitter_tx_bank_verified=%u\n"
			"splitter_tx_control_bank_verified=%u\nsplitter_tx_last_address=0x%02x\n"
			"splitter_tx_last_reg=0x%02x\nsplitter_tx_last_status=0x%08x\n"
			"splitter_tx_last_started=%u\nsplitter_tx_completion_armed=%u\n"
			"splitter_tx_expected=0x%02x\nsplitter_tx_observed=0x%02x\n"
			"splitter_tx_status_valid=0x%x\nsplitter_tx_complete=%u\n"
			"splitter_finish_complete=%u\nsplitter_finish_phase=%u\n",
			card->splitter_tx_error, r->phase, r->prerequisite_error,
			r->transactions, r->writes_started, r->steps_verified,
			r->common_mapping_verified, r->mapping_valid, r->reset_complete,
			r->bank_verified, r->control_bank_verified, r->last_address, r->last_reg,
			r->last.status, r->last.started, r->last.completion_armed,
			r->expected, r->observed, r->status_valid, r->complete,
			card->splitter_finish.complete, card->splitter_finish.phase);
		for (i = 0; i < 4; i++) {
			if (r->mapping_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_tx_mapping[%u]=0x%02x\n",
						     i, r->mapping[i]);
			used += sysfs_emit_at(buf, used, "splitter_tx_port%u_valid=0x%x\n",
					     i, r->port_valid[i]);
			for (j = 0; j < 5; j++)
				if (r->port_valid[i] & (1U << j))
					used += sysfs_emit_at(buf, used,
						"splitter_tx_port%u_reg%02x=0x%02x\n",
						i, regs[j], r->ports[i][j]);
		}
		if (r->status_valid & 1)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg13=0x%02x\n", r->status[0]);
		if (r->status_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg19=0x%02x\n", r->status[1]);
	}
	if (finish_splitter_rx) {
		const struct gc573_splitter_finish_result *r = &card->splitter_finish;

		used += sysfs_emit_at(buf, used,
			"splitter_finish_error=%d\nsplitter_finish_phase=%u\n"
			"splitter_finish_prerequisite_error=%d\nsplitter_finish_transactions=%u\n"
			"splitter_finish_writes_started=%u\nsplitter_finish_steps_completed=%u\n"
			"splitter_finish_steps_verified=%u\nsplitter_finish_bank=%u\n"
			"splitter_finish_bank_verified=%u\nsplitter_finish_control_bank_verified=%u\n"
			"splitter_finish_last_address=0x%02x\nsplitter_finish_last_reg=0x%02x\n"
			"splitter_finish_last_status=0x%08x\nsplitter_finish_last_started=%u\n"
			"splitter_finish_completion_armed=%u\nsplitter_finish_expected=0x%02x\n"
			"splitter_finish_observed=0x%02x\nsplitter_finish_ab_valid=%u\n"
			"splitter_finish_ab_before=0x%02x\nsplitter_finish_ab_ca_branch=%u\n"
			"splitter_finish_status_valid=0x%x\nsplitter_finish_complete=%u\n"
			"splitter_setup_complete=%u\nsplitter_setup_phase=%u\n"
			"splitter_cal_complete=%u\nsplitter_clock_khz=%u\n",
			card->splitter_finish_error, r->phase, r->prerequisite_error,
			r->transactions, r->writes_started, r->steps_completed, r->steps_verified,
			r->bank, r->bank_verified, r->control_bank_verified, r->last_address,
			r->last_reg, r->last.status, r->last.started, r->last.completion_armed,
			r->expected, r->observed, r->ab_valid, r->ab_before, r->ab_ca_branch,
			r->status_valid, r->complete, card->splitter_setup.complete,
			card->splitter_setup.phase, card->splitter_cal.complete,
			card->splitter_clock.khz);
		if (r->status_valid & 1)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg13=0x%02x\n", r->status[0]);
		if (r->status_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg19=0x%02x\n", r->status[1]);
	}
	if (setup_splitter_rx) {
		const struct gc573_splitter_setup_result *r = &card->splitter_setup;

		used += sysfs_emit_at(buf, used,
			"splitter_setup_error=%d\nsplitter_setup_phase=%u\n"
			"splitter_setup_prerequisite_error=%d\nsplitter_setup_transactions=%u\n"
			"splitter_setup_writes_started=%u\nsplitter_setup_steps_completed=%u\n"
			"splitter_setup_steps_verified=%u\nsplitter_setup_last_step=%u\n"
			"splitter_setup_bank=%u\nsplitter_setup_bank_verified=%u\n"
			"splitter_setup_last_reg=0x%02x\nsplitter_setup_last_status=0x%08x\n"
			"splitter_setup_last_started=%u\nsplitter_setup_completion_armed=%u\n"
			"splitter_setup_expected=0x%02x\nsplitter_setup_observed=0x%02x\n"
			"splitter_setup_complete=%u\nsplitter_cal_complete=%u\n"
			"splitter_cal_phase=%u\nsplitter_cal_flags=0x%02x\n"
			"splitter_cal_cleanup_complete=%u\nsplitter_cal_poll_samples=%u\n"
			"splitter_clock_khz=%u\n",
			card->splitter_setup_error, r->phase, r->prerequisite_error,
			r->transactions, r->writes_started, r->steps_completed, r->steps_verified,
			r->last_step, r->bank, r->bank_verified, r->last_reg, r->last.status,
			r->last.started, r->last.completion_armed, r->expected, r->observed,
			r->complete, card->splitter_cal.complete, card->splitter_cal.phase,
			card->splitter_cal.flags, card->splitter_cal.cleanup_complete,
			card->splitter_cal.poll_samples, card->splitter_clock.khz);
		if (card->splitter_map.rx_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_rx_id=%*ph\n",
					     4, card->splitter_map.rx_id);
	}
	if (calibrate_splitter_rx) {
		const struct gc573_splitter_cal_result *r = &card->splitter_cal;

		used += sysfs_emit_at(buf, used,
			"splitter_cal_error=%d\nsplitter_cal_phase=%u\n"
			"splitter_cal_prerequisite_error=%d\nsplitter_cal_transactions=%u\n"
			"splitter_cal_writes_started=%u\nsplitter_cal_steps_completed=%u\n"
			"splitter_cal_preflight_complete=%u\nsplitter_cal_reset_complete=%u\n"
			"splitter_cal_bank=%u\nsplitter_cal_bank_verified=%u\n"
			"splitter_cal_last_reg=0x%02x\nsplitter_cal_last_value=0x%02x\n"
			"splitter_cal_last_status=0x%08x\nsplitter_cal_last_started=%u\n"
			"splitter_cal_flags_valid=0x%x\nsplitter_cal_initial_flags=0x%02x\n"
			"splitter_cal_flags=0x%02x\nsplitter_cal_poll_samples=%u\n"
			"splitter_cal_completion_seen=%u\nsplitter_cal_poll_error=%d\n"
			"splitter_cal_values_valid=0x%x\nsplitter_cal_cleanup_complete=%u\n"
			"splitter_cal_complete=%u\nsplitter_map_complete=%u\n"
			"splitter_map_phase=%u\nsplitter_clock_khz=%u\n",
			card->splitter_cal_error, r->phase, r->prerequisite_error, r->transactions,
			r->writes_started, r->steps_completed, r->preflight_complete,
			r->reset_complete, r->bank, r->bank_verified, r->last_reg, r->last_value,
			r->last.status, r->last.started, r->flags_valid, r->initial_flags,
			r->flags, r->poll_samples, r->completion_seen, r->poll_error,
			r->values_valid, r->cleanup_complete, r->complete,
			card->splitter_map.complete, card->splitter_map.phase,
			card->splitter_clock.khz);
		if (card->splitter_map.rx_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_rx_id=%*ph\n",
					     4, card->splitter_map.rx_id);
		for (i = 0; i < 3; i++)
			if (r->values_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_cal_result[%u]=0x%02x\n",
						     i, r->values[i]);
	}
	if (configure_splitter_map) {
		const struct gc573_splitter_map_result *r = &card->splitter_map;

		used += sysfs_emit_at(buf, used,
			"splitter_map_error=%d\nsplitter_map_phase=%u\n"
			"splitter_map_timing_error=%d\nsplitter_map_transactions=%u\n"
			"splitter_map_writes_started=%u\nsplitter_map_steps_verified=%u\n"
			"splitter_map_tx_mapping_verified=%u\nsplitter_map_rx_mapping_verified=%u\n"
			"splitter_map_bank_verified=%u\nsplitter_map_last_address=0x%02x\n"
			"splitter_map_last_reg=0x%02x\nsplitter_map_last_status=0x%08x\n"
			"splitter_map_last_started=%u\nsplitter_map_completion_armed=%u\n"
			"splitter_map_expected=0x%02x\nsplitter_map_observed=0x%02x\n"
			"splitter_map_before_valid=0x%x\nsplitter_map_after_valid=0x%x\n"
			"splitter_map_complete=%u\nsplitter_rx_valid=0x%x\n"
			"splitter_rx_bank_verified=%u\nsplitter_timing_complete=%u\n"
			"splitter_clock_khz=%u\n",
			card->splitter_map_error, r->phase, r->timing_error, r->transactions,
			r->writes_started, r->steps_verified, r->tx_mapping_verified,
			r->rx_mapping_verified, r->bank_verified, r->last_address, r->last_reg,
			r->last.status, r->last.started, r->last.completion_armed,
			r->expected, r->observed, r->before_valid, r->after_valid, r->complete,
			r->rx_valid, r->rx_bank_verified, card->splitter_timing.complete,
			card->splitter_clock.khz);
		for (i = 0; i < 5; i++) {
			if (r->before_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_map_before[%u]=0x%02x\n",
						     i, r->before[i]);
			if (r->after_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_map_after[%u]=0x%02x\n",
						     i, r->after[i]);
		}
		if (r->rx_valid & 1)
			used += sysfs_emit_at(buf, used, "splitter_rx_bank=0x%02x\n", r->rx_bank);
		if (r->rx_valid & 2)
			used += sysfs_emit_at(buf, used, "splitter_rx_id=%*ph\n", 4, r->rx_id);
		if (r->rx_valid & 4)
			used += sysfs_emit_at(buf, used, "splitter_rx_reg22_24=%*ph\n",
					     3, r->rx_reset);
		if (r->rx_valid & 8)
			used += sysfs_emit_at(buf, used, "splitter_rx_regc5=0x%02x\n", r->rx_c5);
	}
	if (program_splitter_timing) {
		const struct gc573_splitter_timing_result *r = &card->splitter_timing;

		used += sysfs_emit_at(buf, used,
			"splitter_timing_error=%d\nsplitter_timing_phase=%u\n"
			"splitter_timing_clock_error=%d\nsplitter_timing_transactions=%u\n"
			"splitter_timing_writes_started=%u\nsplitter_timing_steps_verified=%u\n"
			"splitter_timing_mapping_verified=%u\nsplitter_timing_bank_verified=%u\n"
			"splitter_timing_last_address=0x%02x\nsplitter_timing_last_reg=0x%02x\n"
			"splitter_timing_last_status=0x%08x\nsplitter_timing_expected=0x%02x\n"
			"splitter_timing_observed=0x%02x\nsplitter_timing_ticks=%u\n"
			"splitter_timing_targets=%*ph\nsplitter_timing_before_valid=0x%x\n"
			"splitter_timing_after_valid=0x%x\nsplitter_timing_complete=%u\n",
			card->splitter_timing_error, r->phase, r->clock_error, r->transactions,
			r->writes_started, r->steps_verified, r->mapping_verified, r->bank_verified,
			r->last_address, r->last_reg, r->last.status, r->expected, r->observed,
			r->values.ticks, 5, r->values.bytes, r->before_valid, r->after_valid,
			r->complete);
		for (i = 0; i < 5; i++) {
			if (r->before_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_timing_before[%u]=0x%02x\n",
						     i, r->before[i]);
			if (r->after_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_timing_after[%u]=0x%02x\n",
						     i, r->after[i]);
		}
	}
	if (probe_splitter_clock || program_splitter_timing) {
		const struct gc573_splitter_clock_result *r = &card->splitter_clock;
		const struct gc573_splitter_prepare_result *op = &r->ops;

		used += sysfs_emit_at(buf, used,
			"splitter_clock_error=%d\nsplitter_clock_phase=%u\n"
			"splitter_clock_prerequisite_error=%d\nsplitter_clock_preflight_complete=%u\n"
			"splitter_clock_transactions=%u\nsplitter_clock_writes_started=%u\n"
			"splitter_clock_bank=%u\nsplitter_clock_bank_verified=%u\n"
			"splitter_clock_last_reg=0x%02x\nsplitter_clock_last_status=0x%08x\n"
			"splitter_clock_data_valid=0x%x\nsplitter_clock_selector_base=0x%x\n"
			"splitter_clock_raw=%u\nsplitter_clock_khz=%u\nsplitter_clock_valid=%u\n"
			"splitter_clock_measurement_error=%d\nsplitter_clock_cleanup_complete=%u\n"
			"splitter_clock_complete=%u\n",
			card->splitter_clock_error, op->phase, op->prerequisite_error,
			op->preflight_complete, op->transactions, op->writes_started,
			op->bank, op->bank_verified, op->last_reg, op->last.status,
			r->data_valid, r->selector_base,
			r->raw, r->khz, r->valid, r->measurement_error,
			r->cleanup_complete, op->complete);
		for (i = 0; i < 4; i++)
			if (r->data_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "splitter_clock_word%u=0x%04x\n",
						     i, r->words[i]);
	}
	if (probe_splitter || start_splitter || prepare_splitter || probe_splitter_clock ||
	    program_splitter_timing || configure_splitter_map || calibrate_splitter_rx || setup_splitter_rx || finish_splitter_rx || prepare_splitter_tx || initialize_splitter_ports || initialize_splitter_tx || finish_splitter_tx || probe_splitter_link || request_splitter_hpd || probe_splitter_edid || enable_splitter_edid || activate_splitter_port1 || measure_splitter_video || configure_splitter_video || output_splitter_video) {
		const struct gc573_splitter_result *r = &card->splitter;

		used += sysfs_emit_at(buf, used,
			"splitter_error=%d\nsplitter_i2c_address=0x2c\n"
			"splitter_gpio=0x%08x\nsplitter_transactions=%u\n"
			"splitter_last_subaddr=0x%02x\nsplitter_bank_valid=%u\n"
			"splitter_id_valid=%u\nsplitter_id_matches=%u\n"
			"splitter_initial_status=0x%08x\nsplitter_prepared_status=0x%08x\n"
			"splitter_started=%u\nsplitter_completion_armed=%u\n"
			"splitter_status=0x%08x\nsplitter_cleanup_status=0x%08x\n"
			"splitter_fifo_bytes=%u\n",
			card->splitter_error, r->gpio, r->transactions, r->last_subaddr,
			r->bank_valid, r->id_valid, r->id_matches, r->last.initial_status,
			r->last.prepared_status, r->last.started, r->last.completion_armed,
			r->last.status, r->last.cleanup_status, r->last.bytes_read);
		if (r->bank_valid)
			used += sysfs_emit_at(buf, used, "splitter_bank=0x%02x\n", r->bank);
		if (r->id_valid)
			used += sysfs_emit_at(buf, used, "splitter_id=%*ph\n", 4, r->id);
	}
	if (prepare_gpio) {
		used += sysfs_emit_at(buf, used,
			"gpio_prepare_error=%d\ngpio_fpga_id=0x%08x\ngpio_board_id=0x%08x\n"
			"gpio_before=0x%08x\ngpio_after=0x%08x\ngpio_changed=%u\n"
			"gpio_status_before=0x%08x\ngpio_status_100ms=0x%08x\n"
			"gpio_status_2000ms=0x%08x\ngpio_status_samples=%u\n"
			"gpio_irq_enable=0x%08x\ngpio_irq_before=0x%08x\ngpio_irq_after=0x%08x\n",
			card->gpio_error, card->gpio.fpga_id, card->gpio.board_id,
			card->gpio.before, card->gpio.after, card->gpio.changed,
			card->gpio.status_before, card->gpio.status_100ms,
			card->gpio.status_2000ms, card->gpio.samples,
			card->gpio.irq_enable, card->gpio.irq_before, card->gpio.irq_after);
	}
	if (inspect_access) {
		for (i = 0; i < ARRAY_SIZE(access_offsets); i++)
			used += sysfs_emit_at(buf, used,
				"access[0x%04x]: byte=0x%02x dword=0x%08x\n",
				access_offsets[i], card->access_byte[i], card->access_word[i]);
	}
	if (probe_i2c) {
		used += sysfs_emit_at(buf, used,
			"i2c_address=0x48\ni2c_error=%d\ni2c_stop_error=%d\n"
			"i2c_initial_control=0x%02x\ni2c_initial_status=0x%02x\n"
			"i2c_last_status=0x%02x\ni2c_bytes_read=%u\n",
			card->i2c_error, card->i2c.stop_error,
			card->i2c.initial_control, card->i2c.initial_status,
			card->i2c.status, card->i2c.bytes_read);
		for (i = 0; i < card->i2c.bytes_read; i++)
			used += sysfs_emit_at(buf, used, "receiver_reg[%u]=0x%02x\n",
					     i, card->i2c.id[i]);
	}
	if (probe_write) {
		const struct gc573_write_test_result *test = &card->write_test;

		used += sysfs_emit_at(buf, used,
			"write_test_error=%d\nwrite_started=%u\nwrite_prepared_status=0x%08x\n"
			"write_status=0x%08x\nwrite_completion_armed=%u\n"
			"verify_started=%u\nverify_status=0x%08x\n"
			"verify_bytes=%u\nbank_verified=%u\n",
			card->write_test_error, test->write.started, test->write.prepared_status,
			test->write.status, test->write.completion_armed,
			test->verify.started, test->verify.status,
			test->verify.bytes_read, test->bank_verified);
	}
	if (initialize_receiver) {
		const struct gc573_init_result *init = &card->init;

		used += sysfs_emit_at(buf, used,
			"init_error=%d\ninit_preflight_complete=%u\ninit_steps_completed=%u\n"
			"init_writes_started=%u\ninit_last_step=%u\ninit_last_bank=%u\n"
			"init_last_reg=0x%02x\ninit_last_value=0x%02x\n"
			"init_last_status=0x%08x\ninit_bank_verified=%u\n"
			"init_table_complete=%u\ninit_post_attempted=%u\n"
			"signal_phase=%s\n",
			card->init_error, init->preflight_complete, init->steps_completed,
			init->writes_started, init->last_step, init->last_bank,
			init->last_reg, init->last_value, init->last.status,
			init->bank_verified, init->table_complete, init->post_attempted,
			init->post_attempted ? "after-table" : "before-table");
	}
	if (calibrate_receiver) {
		const struct gc573_cal_result *cal = &card->cal;

		used += sysfs_emit_at(buf, used,
			"cal_error=%d\ncal_phase=%u\ncal_preflight_complete=%u\n"
			"cal_steps=%u\ncal_writes_started=%u\ncal_bank=%u\ncal_bank_verified=%u\n"
			"cal_last_reg=0x%02x\ncal_last_value=0x%02x\ncal_last_status=0x%08x\n"
			"cal_poll_samples=%u\ncal_completion_seen=%u\ncal_poll_error=%d\n"
			"cal_cleanup_complete=%u\ncal_flags_valid=0x%x\ncal_results_valid=0x%x\n"
			"signal_phase=%s\n",
			card->cal_error, cal->phase, cal->preflight_complete,
			cal->steps, cal->writes_started, cal->bank, cal->bank_verified,
			cal->last_reg, cal->last_value, cal->last.status,
			cal->poll_samples, cal->completion_seen, cal->poll_error,
			cal->cleanup_complete, cal->flags_valid, cal->results_valid,
			cal->post_attempted ? "after-calibration" : "before-calibration");
		for (i = 0; i < 2; i++) {
			if (cal->flags_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "cal_initial_flags%u=0x%02x\n",
						     i, cal->initial_flags[i]);
			if (cal->flags_valid & (1U << (i + 2)))
				used += sysfs_emit_at(buf, used, "cal_flags%u=0x%02x\n",
						     i, cal->flags[i]);
			if (cal->results_valid & (1U << i))
				used += sysfs_emit_at(buf, used,
					"cal_port%u_reg59=0x%02x\ncal_port%u_reg5a=0x%02x\n"
					"cal_port%u_reg59_repeat=0x%02x\n",
					i, cal->values[i][0], i, cal->values[i][1],
					i, cal->values[i][2]);
		}
	}
	if (start_input) {
		const struct gc573_input_result *r = &card->input;

		used += sysfs_emit_at(buf, used,
			"input_error=%d\ninput_phase=%u\ninput_prerequisite_error=%d\n"
			"input_bank=%u\ninput_bank_verified=%u\ninput_writes_started=%u\n"
			"input_steps_completed=%u\ninput_last_reg=0x%02x\n"
			"input_last_status=0x%08x\ninput_expected=0x%02x\ninput_observed=0x%02x\n"
			"input_setup_complete=%u\ninput_hpd_written=%u\ninput_hpd_verified=%u\n"
			"input_gpio_before=0x%08x\ninput_gpio_after=0x%08x\n"
			"input_samples=%u\ninput_lock_seen=%u\nddc_complete=%u\nsignal_phase=%s\n",
			card->input_error, r->phase, r->prerequisite_error, r->bank,
			r->bank_verified, r->writes_started, r->steps_completed, r->last_reg,
			r->last.status, r->expected, r->observed, r->setup_complete,
			r->hpd_written, r->hpd_verified, r->gpio_before, r->gpio_after,
			r->samples, r->lock_seen, card->ddc.complete,
			r->post_attempted ? "after-input-startup" :
			(card->ddc.post_attempted ? "after-edid-configuration" :
			 (card->edid.post_attempted ? "after-edid-read" :
			  (card->timing.post_attempted ? "after-timing" :
			   (card->clock.post_attempted ? "after-clock-read" : "before-clock-read")))));
		for (i = 0; i < r->samples; i++)
			used += sysfs_emit_at(buf, used, "input_poll[%u]=%02x %02x\n",
					     i, r->poll[i][0], r->poll[i][1]);
	}
	if (configure_edid) {
		const struct gc573_ddc_result *d = &card->ddc;
		unsigned int j;

		used += sysfs_emit_at(buf, used,
			"ddc_error=%d\nddc_phase=%u\nddc_prerequisite_error=%d\n"
			"ddc_bank=%u\nddc_bank_verified=%u\nddc_writes_started=%u\n"
			"ddc_steps_completed=%u\nddc_last_reg=0x%02x\nddc_last_status=0x%08x\n"
			"ddc_expected=0x%02x\nddc_observed=0x%02x\nddc_gpio=0x%08x\n"
			"ddc_before_valid=0x%x\nddc_after_valid=0x%x\nddc_complete=%u\n"
			"ddc_physical_offset=0x%02x\nddc_base_checksum=0x%02x\n"
			"ddc_port0_checksum=0x%02x\nddc_port1_checksum=0x%02x\n"
			"signal_phase=%s\n",
			card->ddc_error, d->phase, d->prerequisite_error,
			d->bank, d->bank_verified, d->writes_started, d->steps_completed,
			d->last_reg, d->last.status, d->expected, d->observed, d->gpio,
			d->before_valid, d->after_valid, d->complete, d->plan.physical_offset,
			d->plan.base_checksum, d->plan.extension_checksum[0],
			d->plan.extension_checksum[1],
			d->post_attempted ? "after-edid-configuration" :
			(card->edid.post_attempted ? "after-edid-read" :
			 (card->timing.post_attempted ? "after-timing" :
			  (card->clock.post_attempted ? "after-clock-read" : "before-clock-read"))));
		for (i = 0; i < 2; i++)
			for (j = 0; j < 6; j++) {
				if (d->before_valid & (1U << i))
					used += sysfs_emit_at(buf, used,
						"ddc_before_port%u_reg%02x=0x%02x\n",
						i, 0xc5 + j, d->before[i][j]);
				if (d->after_valid & (1U << i))
					used += sysfs_emit_at(buf, used,
						"ddc_after_port%u_reg%02x=0x%02x\n",
						i, 0xc5 + j, d->after[i][j]);
			}
	}
	if (probe_edid) {
		const struct gc573_edid_result *e = &card->edid;
		unsigned int j;

		used += sysfs_emit_at(buf, used,
			"edid_error=%d\nedid_phase=%u\nedid_prerequisite_error=%d\n"
			"timing_complete=%u\nclock_khz=%u\n"
			"edid_mapping_written=%u\nedid_mapping_verified=%u\n"
			"edid_mapping_before=0x%02x\nedid_mapping_after=0x%02x\n"
			"edid_i2c_address=0x54\nedid_bytes_read=%u\nedid_complete=%u\n"
			"edid_last_offset=0x%02x\nedid_last_status=0x%08x\n"
			"edid_last_started=%u\nedid_completion_armed=%u\nsignal_phase=%s\n",
			card->edid_error, e->phase, e->prerequisite_error,
			card->timing.complete, card->clock.khz,
			e->mapping_written, e->mapping_verified, e->mapping_before,
			e->mapping_after, e->bytes_read, e->complete, e->last_offset,
			e->last.status, e->last.started, e->last.completion_armed,
			e->post_attempted ? "after-edid-read" :
			(card->timing.post_attempted ? "after-timing" :
			 (card->clock.post_attempted ? "after-clock-read" : "before-clock-read")));
		if (e->complete)
			used += sysfs_emit_at(buf, used,
				"edid_header_matches=%u\nedid_checksum0=0x%02x\n"
				"edid_checksum1=0x%02x\nedid_extensions=%u\n",
				e->header_matches, e->checksum[0], e->checksum[1], e->data[126]);
		for (i = 0; i < e->bytes_read; i += 16) {
			used += sysfs_emit_at(buf, used, "edid[0x%02x]=", i);
			for (j = i; j < i + 16 && j < e->bytes_read; j++)
				used += sysfs_emit_at(buf, used, "%s%02x", j == i ? "" : " ",
						     e->data[j]);
			used += sysfs_emit_at(buf, used, "\n");
		}
	}
	if (program_timing) {
		const struct gc573_timing_result *t = &card->timing;

		used += sysfs_emit_at(buf, used,
			"timing_error=%d\ntiming_phase=%u\ntiming_bank=%u\n"
			"timing_bank_verified=%u\ntiming_writes_started=%u\n"
			"timing_steps_verified=%u\ntiming_last_reg=0x%02x\n"
			"timing_last_status=0x%08x\ntiming_expected=0x%02x\n"
			"timing_observed=0x%02x\ntiming_complete=%u\n"
			"timing_half_khz=%u\ntiming_adjusted_khz=%u\n"
			"timing_target_reg91=0x%02x\ntiming_target_reg92=0x%02x\n"
			"timing_target_regfd=0x%02x\ntiming_target_reg45=0x%02x\n"
			"timing_target_reg44=0x%02x\ntiming_target_reg46=0x%02x\n"
			"timing_target_reg47=0x%02x\n",
			card->timing_error, t->phase, t->bank, t->bank_verified,
			t->writes_started, t->steps_verified, t->last_reg, t->last.status,
			t->expected, t->observed, t->complete,
			t->values.half_khz, t->values.adjusted_khz,
			t->values.reg91, t->values.reg92, t->values.regfd, t->values.reg45,
			t->values.reg44, t->values.reg46, t->values.reg47);
	}
	if (probe_clock || program_timing) {
		const struct gc573_clock_result *clock = &card->clock;

		used += sysfs_emit_at(buf, used,
			"clock_error=%d\nclock_phase=%u\nclock_preflight_complete=%u\n"
			"clock_bank=%u\nclock_bank_verified=%u\nclock_writes_started=%u\n"
			"clock_last_reg=0x%02x\nclock_last_status=0x%08x\n"
			"clock_recovery_used=%u\nclock_ready_samples=%u\nclock_ready=%u\n"
			"clock_engine_status=0x%02x\nclock_output_mode=0x%02x\n"
			"clock_selector_base=%u\nclock_data_valid=0x%x\nclock_raw=%u\n"
			"clock_khz=%u\nclock_valid=%u\nclock_measurement_error=%d\n"
			"clock_cleanup_complete=%u\nsignal_phase=%s\n",
			card->clock_error, clock->phase, clock->preflight_complete,
			clock->bank, clock->bank_verified, clock->writes_started,
			clock->last_reg, clock->last.status, clock->recovery_used,
			clock->ready_samples, clock->ready, clock->engine_status,
			clock->output_mode, clock->selector_base, clock->data_valid,
			clock->raw_value, clock->khz, clock->clock_valid,
			clock->measurement_error, clock->cleanup_complete,
			program_timing && card->timing.post_attempted ? "after-timing" :
			(clock->post_attempted ? "after-clock-read" : "before-clock-read"));
		for (i = 0; i < 4; i++)
			if (clock->data_valid & (1U << i))
				used += sysfs_emit_at(buf, used, "clock_data%u=%02x %02x\n",
						     i, clock->data[i][0], clock->data[i][1]);
	}
	if (probe_signal || probe_write || initialize_receiver || calibrate_receiver ||
	    probe_clock || program_timing || probe_edid || configure_edid || start_input) {
		used += sysfs_emit_at(buf, used,
			"signal_error=%d\nsignal_valid_mask=0x%x\nsignal_transactions=%u\n"
			"signal_last_subaddr=0x%02x\nsignal_last_status=0x%08x\n"
			"signal_last_started=%u\nsignal_completion_armed=%u\n",
			card->signal_error, card->signal.valid, card->signal.transactions,
			card->signal.last_subaddr, card->signal.last.status,
			card->signal.last.started, card->signal.last.completion_armed);
		if (card->signal.valid & 1)
			used += sysfs_emit_at(buf, used, "receiver_bank=0x%02x\n",
					     card->signal.bank);
		if (card->signal.valid & 2)
			used += sysfs_emit_at(buf, used, "receiver_id=%*ph\n",
					     4, card->signal.id);
		if (card->signal.valid & 4)
			used += sysfs_emit_at(buf, used,
				"receiver_reg13=0x%02x\nport0_5v=%u\nport0_clock_valid=%u\n",
				card->signal.port0, !!(card->signal.port0 & 1),
				!!(card->signal.port0 & 8));
		if (card->signal.valid & 8)
			used += sysfs_emit_at(buf, used,
				"receiver_reg16=0x%02x\nport1_5v=%u\nport1_clock_valid=%u\n",
				card->signal.port1, !!(card->signal.port1 & 1),
				!!(card->signal.port1 & 8));
		if (card->signal.valid & 16)
			used += sysfs_emit_at(buf, used, "receiver_reg19=0x%02x\nscdt=%u\n",
					     card->signal.sync, !!(card->signal.sync & 0x80));
		for (i = 0; i < GC573_CONTROL_READS; i++) {
			const struct gc573_control_sample *sample = &card->signal.controls[i];
			unsigned int j;

			if (!(card->signal.valid & (1U << (i + 5))))
				continue;
			for (j = 0; j < sample->length; j++)
				used += sysfs_emit_at(buf, used, "receiver_reg%02x=0x%02x\n",
						     sample->subaddr + j, sample->data[j]);
			if (sample->subaddr == 0x35)
				used += sysfs_emit_at(buf, used, "receiver_selected_port=%u\n",
						     sample->data[0] & 1);
		}
	}
	if (probe_receiver) {
		used += sysfs_emit_at(buf, used,
			"receiver_gpio_before=0x%08x\nreceiver_gpio_after=0x%08x\n"
			"receiver_status_before=0x%08x\nreceiver_status_after=0x%08x\n"
			"receiver_gpio_steps=%u\nreceiver_setup_complete=%u\n",
			card->receiver.gpio_before, card->receiver.gpio_after,
			card->receiver.status_before, card->receiver.status_after,
			card->receiver.steps, card->receiver.setup_complete);
	}
	if (probe_block || probe_receiver) {
		used += sysfs_emit_at(buf, used,
			"block_test_divider=0x%08x\nblock_timeout_ms=2000\n"
			"block_prepared_status=0x%08x\nblock_started=%u\n"
			"block_completion_armed=%u\n",
			GC573_BLOCK_TEST_DIVIDER, card->block.prepared_status,
			card->block.started, card->block.completion_armed);
		used += sysfs_emit_at(buf, used,
			"block_address=0x48\nblock_error=%d\n"
			"block_initial_status=0x%08x\nblock_initial_divider=0x%08x\n"
			"block_irq_enable=0x%08x\nblock_status=0x%08x\n"
			"block_irq_status=0x%08x\nblock_cleanup_status=0x%08x\n"
			"block_fifo_bytes=%u\n",
			card->block_error, card->block.initial_status,
			card->block.initial_divider, card->block.irq_enable,
			card->block.status, card->block.irq_status,
			card->block.cleanup_status, card->block.bytes_read);
		for (i = 0; i < card->block.bytes_read; i++)
			used += sysfs_emit_at(buf, used, "block_rx[%u]=0x%02x\n",
					     i, card->block.data[i]);
	}
	if (passthrough_only || scaled_capture || restore_capture_profile) {
		const struct gc573_passthrough_state *p = &card->external;
		used += sysfs_emit_at(buf, used,
			"passthrough_only=%u\nexternal_phase=%u\nexternal_error=%d\nexternal_active=%u\n"
			"external_waiting=%u\nexternal_edid_verified=%u\nexternal_changes=%u\n"
			"external_width=%u\nexternal_height=%u\nexternal_fps_milli=%u\n"
			"external_pixel_khz=%u\nexternal_link_khz=%u\nexternal_video_phase=%u\n"
			"external_max_tmds_khz=%u\nexternal_scdc=%u\nexternal_scdc_status_valid=%u\nexternal_scdc_status=0x%02x\nexternal_sink_lock=0x%02x\n",
			passthrough_only, READ_ONCE(p->phase), READ_ONCE(p->error), READ_ONCE(p->active),
			READ_ONCE(p->waiting), READ_ONCE(p->edid_verified), READ_ONCE(p->changes),
			READ_ONCE(p->width), READ_ONCE(p->height), READ_ONCE(p->millihz),
			READ_ONCE(p->measured.complete) ? READ_ONCE(p->measured.pixel_khz) : READ_ONCE(p->video.pixel_khz),
			READ_ONCE(p->measured.complete) ? READ_ONCE(p->measured.link_khz) : READ_ONCE(p->video.link_khz), READ_ONCE(p->video.phase),
			READ_ONCE(p->advertised.max_tmds_khz), READ_ONCE(p->advertised.scdc),
			READ_ONCE(p->scdc_status_valid), READ_ONCE(p->scdc_status), READ_ONCE(p->sink_lock));
		used += sysfs_emit_at(buf, used,
			"external_rx13=0x%02x\nexternal_rx19=0x%02x\nexternal_tx_status=0x%02x\n"
			"external_last_status=0x%08x\nexternal_video_last_reg=0x%02x\n"
			"external_video_expected=0x%02x\nexternal_video_observed=0x%02x\n",
			READ_ONCE(p->link.rx[8]), READ_ONCE(p->link.rx[11]), READ_ONCE(p->link.tx[2]),
			READ_ONCE(p->last.status), READ_ONCE(p->video.last_reg),
			READ_ONCE(p->video.expected), READ_ONCE(p->video.observed));
		used += sysfs_emit_at(buf, used,
			"external_output_waiting=%u\nexternal_output_tx_status=0x%02x\n"
			"external_link_wait_polls=%u\nexternal_link_restarts=%u\n",
			p->video.waiting_link, p->video.tx_status,
			p->video.link_wait_polls, p->video.link_restarts);
		used += sysfs_emit_at(buf, used,
			"external_link_address=0x%x\nexternal_link_reg=0x%x\n"
			"external_link_prepared=0x%x\nexternal_link_start=0x%x\n"
			"external_link_status=0x%x\nexternal_link_armed=%u\n",
			p->link.last_address, p->link.last_reg, p->link.last.prepared_status,
			p->link.last.start_status, p->link.last.status, p->link.last.completion_armed);
		used += sysfs_emit_at(buf, used,
			"external_format_waits=%u\nexternal_format_rejected=%u\n"
			"external_format_avi=0x%x\nexternal_format_depth=0x%x\nexternal_format_cf=0x%x\n"
			"external_format_rx13=0x%x\nexternal_candidate_pixel_khz=%u\n",
			p->format_waits, p->format_rejected, p->video.avi_color, p->video.depth,
			p->video.rx_cf, p->video.rx13, p->video.pixel_khz);
	}
	if (scaled_capture) {
		const struct gc573_receiver_video_result *r = &card->hdmi.receiver_video;
		used += sysfs_emit_at(buf, used,
			"scaled_capture=1\ncombined_phase=%u\ncombined_error=%d\ncombined_ready=%u\n"
			"combined_tx_phase=%u\ncombined_receiver_phase=%u\ncombined_receiver_last=0x%x\n"
			"combined_receiver_width=%u\ncombined_receiver_height=%u\n"
			"combined_receiver_clock_min=%u\ncombined_receiver_clock_max=%u\n"
			"combined_format_waits=%u\ncombined_fpga_status=0x%x\n"
			"combined_fpga_width=%u\ncombined_fpga_height=%u\ncombined_fpga_packing=0x%x\n",
			card->combined_phase, card->combined_error, gc573_hdmi_ready(card),
			card->hdmi.video.phase, r->phase, r->last_reg, r->width, r->height,
			r->pixel_min_khz, r->pixel_max_khz, card->combined_format_waits,
			ioread32(card->bar + 0x1004), ioread32(card->bar + 0x1008),
			ioread32(card->bar + 0x100c), ioread32(card->bar + 0x1088));
	}

	if (scaled_capture)
		used += sysfs_emit_at(buf, used,
			"combined_output_waiting=%u\ncombined_output_tx_status=0x%02x\n"
			"combined_link_wait_polls=%u\ncombined_link_restarts=%u\n",
			card->hdmi.video.waiting_link, card->hdmi.video.tx_status,
			card->hdmi.video.link_wait_polls, card->hdmi.video.link_restarts);

	if (probe_sink) {
		const struct gc573_sink_result *r = &card->sink;
		used += sysfs_emit_at(buf, used,
			"sink_error=%d\nsink_phase=%u\nsink_transactions=%u\nsink_writes=%u\n"
			"sink_present=%u\nsink_bytes=%u\nsink_blocks=%u\nsink_complete=%u\n"
			"sink_status=0x%02x\nsink_last_reg=0x%02x\nsink_restored=%u\nsink_cleanup_error=%d\n",
			card->sink_error, r->phase, r->transactions, r->writes, r->sink_present,
			r->bytes, r->blocks, r->complete, r->status, r->last_reg, r->restored, r->cleanup_error);
		for (i = 0; i < r->bytes; i += 16)
			used += sysfs_emit_at(buf, used, "sink_edid[%03x]=%16ph\n", i, r->edid + i);
	}
	return used;
}
static DEVICE_ATTR_RO(bringup_status);

static struct attribute *gc573_attrs[] = {
	&dev_attr_bringup_status.attr,
	NULL,
};
static ssize_t captured_frame_read(struct file *file, struct kobject *kobj,
				  GC573_SYSFS_READ_CONST struct bin_attribute *attr, char *buf,
				  loff_t off, size_t count)
{
	struct gc573_device *card = dev_get_drvdata(kobj_to_dev(kobj));

	return gc573_capture_read(card->capture, buf, off, count);
}
static GC573_SYSFS_BIN_CONST BIN_ATTR_RO(captured_frame, GC573_FRAME_BYTES);
static GC573_SYSFS_BIN_CONST struct bin_attribute *GC573_SYSFS_BIN_CONST gc573_bin_attrs[] = {
	&bin_attr_captured_frame, NULL,
};
static const struct attribute_group gc573_group = {
	.attrs = gc573_attrs, .bin_attrs = gc573_bin_attrs,
};
__ATTRIBUTE_GROUPS(gc573);

static int gc573_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct gc573_device *card;
	u16 command;
	unsigned int i;
	int ret;

	if ((passthrough_only || scaled_capture) && (!capture_video || hdmi_start_phase ||
	    (passthrough_only && scaled_capture)))
		return -EINVAL;
	if (hdmi_start_phase && (!capture_video || hdmi_start_phase < GC573_HDMI_FIRST ||
				hdmi_start_phase >= GC573_HDMI_DONE))
		return -EINVAL;
	if ((unsigned int)inspect_access + probe_i2c + probe_block +
	    prepare_gpio + probe_receiver + probe_signal + probe_write +
	    initialize_receiver + calibrate_receiver + probe_clock + program_timing + probe_edid + configure_edid + start_input + probe_splitter + start_splitter + prepare_splitter + probe_splitter_clock + program_splitter_timing + configure_splitter_map + calibrate_splitter_rx + setup_splitter_rx + finish_splitter_rx + prepare_splitter_tx + initialize_splitter_ports + initialize_splitter_tx + finish_splitter_tx + probe_splitter_link + request_splitter_hpd + probe_splitter_edid + enable_splitter_edid + activate_splitter_port1 + measure_splitter_video + configure_splitter_video + output_splitter_video + probe_receiver_video + output_receiver_video + restore_capture_profile + probe_sink + capture_once + capture_video > 1)
		return -EINVAL;
	/* Recheck identity even if someone adds an ID through sysfs/new_id. */
	if (pdev->vendor != 0x1461 || pdev->device != 0x0054 ||
	    pdev->subsystem_vendor != 0x1461 ||
	    pdev->subsystem_device != 0x5730)
		return -ENODEV;
	if (!target_bdf || strcmp(target_bdf, pci_name(pdev)))
		return -ENODEV;
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    pci_resource_len(pdev, 0) != GC573_BAR_BYTES)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "Unexpected BAR0 layout; refusing probe\n");
	for (i = 0; i < read_count; i++) {
		if ((read_offsets[i] & 3) ||
		    read_offsets[i] > GC573_BAR_BYTES - sizeof(u32))
			return dev_err_probe(&pdev->dev, -EINVAL,
					     "Invalid 32-bit read offset %#x\n",
					     read_offsets[i]);
	}
	ret = pci_read_config_word(pdev, PCI_COMMAND, &command);
	if (ret)
		return -EIO;
	/* Do not take over a possibly running DMA engine from another driver. */
	if (command & PCI_COMMAND_MASTER)
		return dev_err_probe(&pdev->dev, -EBUSY,
				     "Bus mastering already enabled; refusing takeover\n");
	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	card->initial_command = command;
	mutex_init(&card->control_mutex);
	card->external.scaled = scaled_capture;

	ret = pci_enable_device_mem(pdev);
	if (ret)
		return ret;
	ret = pci_request_region(pdev, 0, "gc573_native");
	if (ret)
		goto disable;
	card->bar = pci_iomap(pdev, 0, GC573_BAR_BYTES);
	if (!card->bar) {
		ret = -ENOMEM;
		goto release;
	}
	/* No implicit BAR sweep: unknown registers may have read side effects. */
	for (i = 0; i < read_count; i++)
		card->samples[i] = ioread32(card->bar + read_offsets[i]);
	if (inspect_access) {
		for (i = 0; i < ARRAY_SIZE(access_offsets); i++) {
			card->access_byte[i] = ioread8(card->bar + access_offsets[i]);
			card->access_word[i] = ioread32(card->bar + access_offsets[i]);
		}
	}
	if (probe_i2c) {
		const struct gc573_i2c_io io = {
			.ctx = card,
			.read = gc573_read_i2c,
			.write = gc573_write_i2c,
			.wait = gc573_wait_i2c,
		};

		/* Restrict first write experiments to the observed FPGA image. */
		if (ioread32(card->bar) != 0x20201015)
			card->i2c_error = -ENODEV;
		else
			card->i2c_error = gc573_i2c_identify(&io, &card->i2c);
	}
	if (restore_capture_profile || probe_sink || probe_block || prepare_gpio || probe_receiver || probe_signal || probe_write ||
	    initialize_receiver || calibrate_receiver || probe_clock || program_timing || probe_edid || configure_edid || start_input || probe_splitter || start_splitter || prepare_splitter || probe_splitter_clock || program_splitter_timing || configure_splitter_map || calibrate_splitter_rx || setup_splitter_rx || finish_splitter_rx || prepare_splitter_tx || initialize_splitter_ports || initialize_splitter_tx || finish_splitter_tx || probe_splitter_link || request_splitter_hpd || probe_splitter_edid || enable_splitter_edid || activate_splitter_port1 || measure_splitter_video || configure_splitter_video || output_splitter_video || probe_receiver_video || output_receiver_video) {
		const struct gc573_block_io io = {
			.ctx = card,
			.read = gc573_block_read,
			.write = gc573_block_write,
			.start_read = gc573_block_start_read,
			.wait = gc573_block_wait,
			.sleep_ms = gc573_sleep_ms,
			.wait_write = gc573_block_wait_write,
			.time_ms = gc573_time_ms,
		};

		if (restore_capture_profile) {
			struct gc573_passthrough_state *p = &card->external;
			p->error = gc573_splitter_link_status(&io, &p->identity, &p->link);
			if (!p->error) {
				gc573_capture_edid(p->advertised.data);
				p->error = gc573_passthrough_program_edid(&io, p, p->advertised.data);
			}
		} else if (probe_sink) {
			card->sink_error = gc573_sink_read(&io, &card->sink);
		} else if (probe_receiver_video || output_receiver_video) {
			card->receiver_video_error = gc573_receiver_video(&io, &card->signal, &card->receiver_video, output_receiver_video);
		} else if (measure_splitter_video || configure_splitter_video || output_splitter_video) {
			card->splitter_video_error = gc573_splitter_video_clock(&io, &card->splitter,
				&card->splitter_link, &card->splitter_video, output_splitter_video ? 2 : configure_splitter_video, splitter_tx_port);
			card->splitter_link_error = card->splitter_video.prerequisite_error;
			card->splitter_error = card->splitter_link.prerequisite_error;
		} else if (activate_splitter_port1) {
			card->splitter_hpd_error = gc573_splitter_port_activate(&io, &card->splitter,
				&card->splitter_link, &card->splitter_hpd, splitter_tx_port);
			card->splitter_link_error = card->splitter_hpd.prerequisite_error;
			card->splitter_error = card->splitter_link.prerequisite_error;
		} else if (enable_splitter_edid) {
			card->splitter_hpd_error = gc573_splitter_edid_enable(&io, &card->splitter,
				&card->splitter_link, &card->splitter_edid, &card->splitter_hpd);
			card->splitter_edid_error = card->splitter_hpd.prerequisite_error;
			card->splitter_link_error = card->splitter_edid.prerequisite_error;
			card->splitter_error = card->splitter_link.prerequisite_error;
		} else if (probe_splitter_edid) {
			card->splitter_edid_error = gc573_splitter_edid_read(&io, &card->splitter,
				&card->splitter_link, &card->splitter_edid);
			card->splitter_link_error = card->splitter_edid.prerequisite_error;
			card->splitter_error = card->splitter_link.prerequisite_error;
		} else if (request_splitter_hpd) {
			card->splitter_hpd_error = gc573_splitter_input_hpd(&io, &card->splitter,
				&card->splitter_link, &card->splitter_hpd);
			card->splitter_link_error = card->splitter_hpd.prerequisite_error;
			card->splitter_error = card->splitter_link.prerequisite_error;
		} else if (probe_splitter_link) {
			card->splitter_link_error = gc573_splitter_link_status(&io, &card->splitter,
				&card->splitter_link);
			card->splitter_error = card->splitter_link.prerequisite_error;
		} else if (initialize_splitter_ports || initialize_splitter_tx || finish_splitter_tx) {
			card->splitter_ports_error = gc573_splitter_tx_ports(&io, &card->splitter,
				&card->splitter_clock, &card->splitter_timing, &card->splitter_map,
				&card->splitter_cal, &card->splitter_setup, &card->splitter_finish,
				&card->splitter_tx, &card->splitter_ports, finish_splitter_tx ? 2 : initialize_splitter_tx);
			card->splitter_tx_error = card->splitter_ports.prerequisite_error;
			card->splitter_finish_error = card->splitter_tx.prerequisite_error;
			card->splitter_setup_error = card->splitter_finish.prerequisite_error;
			card->splitter_cal_error = card->splitter_setup.prerequisite_error;
			card->splitter_map_error = card->splitter_cal.prerequisite_error;
			card->splitter_timing_error = card->splitter_map.timing_error;
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (prepare_splitter_tx) {
			card->splitter_tx_error = gc573_splitter_tx_prepare(&io, &card->splitter,
				&card->splitter_clock, &card->splitter_timing, &card->splitter_map,
				&card->splitter_cal, &card->splitter_setup, &card->splitter_finish,
				&card->splitter_tx);
			card->splitter_finish_error = card->splitter_tx.prerequisite_error;
			card->splitter_setup_error = card->splitter_finish.prerequisite_error;
			card->splitter_cal_error = card->splitter_setup.prerequisite_error;
			card->splitter_map_error = card->splitter_cal.prerequisite_error;
			card->splitter_timing_error = card->splitter_map.timing_error;
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (finish_splitter_rx) {
			card->splitter_finish_error = gc573_splitter_finish(&io, &card->splitter,
				&card->splitter_clock, &card->splitter_timing, &card->splitter_map,
				&card->splitter_cal, &card->splitter_setup, &card->splitter_finish);
			card->splitter_setup_error = card->splitter_finish.prerequisite_error;
			card->splitter_cal_error = card->splitter_setup.prerequisite_error;
			card->splitter_map_error = card->splitter_cal.prerequisite_error;
			card->splitter_timing_error = card->splitter_map.timing_error;
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (setup_splitter_rx) {
			card->splitter_setup_error = gc573_splitter_setup(&io, &card->splitter,
				&card->splitter_clock, &card->splitter_timing, &card->splitter_map,
				&card->splitter_cal, &card->splitter_setup);
			card->splitter_cal_error = card->splitter_setup.prerequisite_error;
			card->splitter_map_error = card->splitter_cal.prerequisite_error;
			card->splitter_timing_error = card->splitter_map.timing_error;
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (calibrate_splitter_rx) {
			card->splitter_cal_error = gc573_splitter_calibrate(&io, &card->splitter,
				&card->splitter_clock, &card->splitter_timing, &card->splitter_map,
				&card->splitter_cal);
			card->splitter_map_error = card->splitter_cal.prerequisite_error;
			card->splitter_timing_error = card->splitter_map.timing_error;
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (configure_splitter_map) {
			card->splitter_map_error = gc573_splitter_map(&io, &card->splitter,
				&card->splitter_clock, &card->splitter_timing, &card->splitter_map);
			card->splitter_timing_error = card->splitter_map.timing_error;
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (program_splitter_timing) {
			card->splitter_timing_error = gc573_splitter_timing(&io, &card->splitter,
						&card->splitter_clock, &card->splitter_timing);
			card->splitter_clock_error = card->splitter_timing.clock_error;
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (probe_splitter_clock) {
			card->splitter_clock_error = gc573_splitter_clock(&io, &card->splitter,
									&card->splitter_clock);
			card->splitter_error = card->splitter_clock.ops.prerequisite_error;
		} else if (prepare_splitter) {
			card->splitter_prepare_error = gc573_splitter_prepare(&io, &card->splitter,
									    &card->splitter_prepare);
			card->splitter_error = card->splitter_prepare.prerequisite_error;
		} else if (start_splitter) {
			card->splitter_error = gc573_splitter_startup(&io, &card->splitter_start,
								    &card->splitter);
		} else if (probe_splitter) {
			card->splitter_error = gc573_splitter_identify(&io, &card->splitter);
		} else if (start_input) {
			card->input_error = gc573_receiver_input(&io, &card->signal, &card->clock,
				&card->timing, &card->edid, &card->ddc, &card->input);
			card->signal_error = card->input.post_attempted ? card->input.post_error :
				(card->ddc.post_attempted ? card->ddc.post_error :
				 (card->edid.post_attempted ? card->edid.post_error :
				  (card->timing.post_attempted ? card->timing.post_error :
				   (card->clock.post_attempted ? card->clock.post_error :
				    (card->signal.valid == 0xfff ? 0 : card->input_error)))));
		} else if (configure_edid) {
			card->ddc_error = gc573_receiver_ddc(&io, &card->signal, &card->clock,
							   &card->timing, &card->edid, &card->ddc);
			card->edid_error = card->ddc.prerequisite_error;
			card->signal_error = card->ddc.post_attempted ? card->ddc.post_error :
				(card->edid.post_attempted ? card->edid.post_error :
				 (card->timing.post_attempted ? card->timing.post_error :
				  (card->clock.post_attempted ? card->clock.post_error :
				   (card->signal.valid == 0xfff ? 0 : card->ddc_error))));
		} else if (probe_edid) {
			card->edid_error = gc573_receiver_edid_read(&io, &card->signal, &card->clock,
								    &card->timing, &card->edid);
			card->timing_error = card->edid.prerequisite_error;
			card->clock_error = card->timing.clock_error;
			card->signal_error = card->edid.post_attempted ? card->edid.post_error :
				(card->timing.post_attempted ? card->timing.post_error :
				 (card->clock.post_attempted ? card->clock.post_error :
				  (card->signal.valid == 0xfff ? 0 : card->clock_error)));
		} else if (program_timing) {
			card->timing_error = gc573_receiver_timing(&io, &card->signal,
								 &card->clock, &card->timing);
			card->clock_error = card->timing.clock_error;
			card->signal_error = card->timing.post_attempted ? card->timing.post_error :
				(card->clock.post_attempted ? card->clock.post_error :
				 (card->signal.valid == 0xfff ? 0 : card->clock_error));
		} else if (probe_clock) {
			card->clock_error = gc573_receiver_clock(&io, &card->signal, &card->clock);
			card->signal_error = card->clock.post_attempted ? card->clock.post_error :
				(card->signal.valid == 0xfff ? 0 : card->clock_error);
		} else if (calibrate_receiver) {
			card->cal_error = gc573_receiver_calibrate(&io, &card->signal, &card->cal);
			card->signal_error = card->cal.post_attempted ? card->cal.post_error :
				(card->signal.valid == 0xfff ? 0 : card->cal_error);
		} else if (initialize_receiver) {
			card->init_error = gc573_receiver_init(&io, &card->signal, &card->init);
			card->signal_error = card->init.post_attempted ? card->init.post_error :
				(card->signal.valid == 0xfff ? 0 : card->init_error);
		} else if (probe_write) {
			card->write_test_error = gc573_receiver_write_test(&io, &card->signal,
								 &card->write_test);
			/* In this mode the signal fields describe the pre-write snapshot. */
			card->signal_error = card->signal.valid == 0xfff ? 0 :
				card->write_test_error;
		} else if (probe_signal)
			card->signal_error = gc573_receiver_status(&io, &card->signal);
		else if (probe_receiver)
			card->block_error = gc573_receiver_identify(&io, &card->receiver,
							   &card->block);
		else if (prepare_gpio)
			card->gpio_error = gc573_block_prepare_gpio(&io, &card->gpio);
		else if (ioread32(card->bar) != 0x20201015)
			card->block_error = -ENODEV;
		else
			card->block_error = gc573_block_identify(&io, &card->block);
	}
	if (capture_once || capture_video) {
		const struct gc573_block_io io = {
			.ctx = card, .read = gc573_block_read, .write = gc573_block_write,
			.start_read = gc573_block_start_read,
			.wait = gc573_block_wait, .sleep_ms = gc573_sleep_ms,
			.wait_write = gc573_block_wait_write, .time_ms = gc573_time_ms,
			.ready = gc573_hdmi_ready,
			.control_lock = gc573_control_lock, .control_unlock = gc573_control_unlock,
			.owned_irq_mask = 0x22,
		};

		card->hdmi_ready = !hdmi_start_phase && !passthrough_only && !scaled_capture;
		INIT_DELAYED_WORK(&card->hdmi_work, gc573_hdmi_work);
		if (capture_video && !hdmi_start_phase && !passthrough_only && !scaled_capture)
			card->passthrough_error = gc573_splitter_passthrough(&io, &card->passthrough);
		card->capture = gc573_capture_create(pdev, card->bar, capture_video, &io);
		if ((hdmi_start_phase || passthrough_only || scaled_capture) && gc573_capture_registered(card->capture)) {
			card->hdmi.phase = hdmi_start_phase;
			card->hdmi_io = io;
			schedule_delayed_work(&card->hdmi_work, msecs_to_jiffies(100));
		}
	}
	pci_set_drvdata(pdev, card);
	dev_info(&pdev->dev,
		 "%s ready; %u explicit reads\n",
		 capture_video ? "V4L2 capture" : "PCI diagnostics", read_count);
	return 0;

release:
	pci_release_region(pdev, 0);
disable:
	pci_disable_device(pdev);
	return ret;
}

static void gc573_remove(struct pci_dev *pdev)
{
	struct gc573_device *card = pci_get_drvdata(pdev);

	if (capture_once || capture_video)
		cancel_delayed_work_sync(&card->hdmi_work);
	if ((passthrough_only || scaled_capture) && card->external.prior_valid) {
		int ret = gc573_passthrough_restore(&card->hdmi_io, &card->external);
		if (ret) dev_warn(&pdev->dev, "HDMI profile restoration stopped: %d\n", ret);
	}
	gc573_capture_destroy(card->capture);
	pci_iounmap(pdev, card->bar);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

static const struct pci_device_id gc573_ids[] = {
	{ PCI_DEVICE_SUB(0x1461, 0x0054, 0x1461, 0x5730) },
	{ }
};
MODULE_DEVICE_TABLE(pci, gc573_ids);

static struct pci_driver gc573_driver = {
	.name = "gc573_native",
	.id_table = gc573_ids,
	.probe = gc573_probe,
	.remove = gc573_remove,
	.dev_groups = gc573_groups,
};
module_pci_driver(gc573_driver);

MODULE_DESCRIPTION("Original GC573 native HDMI capture and diagnostics");
MODULE_AUTHOR("GC573 native development");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.46.2");
