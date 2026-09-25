// SPDX-License-Identifier: GPL-2.0-only
/* Native BGR24 capture, exact GC573 Windows descriptor/slot layout.
 * Only DMA API addresses are submitted. No physical-address or register API.
 */
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-v4l2.h>
#include "gc573_capture.h"
#include "gc573_led.h"
#include "gc573_audio.h"
#include "gc573_modes.h"
#include "gc573_scaler.h"

#define CHUNK_BYTES 65536U
#define CHUNKS DIV_ROUND_UP(GC573_FRAME_BYTES, CHUNK_BYTES)
#define DESCRIPTORS DIV_ROUND_UP(GC573_FRAME_BYTES, 4096U)
struct gc573_descriptor { __le32 low, high, words, control; };
struct capture_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};
struct gc573_capture {
	struct pci_dev *pdev;
	void __iomem *bar;
	struct gc573_block_io io;
	void *chunks[CHUNKS];
	dma_addr_t addresses[CHUNKS];
	struct gc573_descriptor *desc;
	dma_addr_t desc_address;
	void *frame;
	unsigned int allocated, writes, started, complete, guard_ok, master_cleared;
	unsigned int irq, dma_status, dma_control, polls, changed_bytes, pending_drained;
	int error, led_error;
	struct gc573_led_result led;
	struct gc573_audio_signal audio_signal;
	int audio_signal_error, audio_error;
	struct gc573_audio *audio;
	spinlock_t engine_lock;
	struct mutex led_mutex;
	unsigned int led_mode, led_color, led_brightness;
	struct v4l2_ctrl_handler controls;
	struct delayed_work led_work;
	unsigned int led_checks, led_observed_divider, led_observed_enabled;
	struct completion done;
	unsigned int irq_requested, vectors, interrupts;
	struct v4l2_device v4l2;
	struct video_device video;
	struct vb2_queue queue;
	struct mutex mutex;
	spinlock_t queue_lock;
	struct list_head queued;
	struct task_struct *thread;
	bool stopping, registered, streaming;
	unsigned int sequence, slot, link_recoveries;
	unsigned int width, height, frame_bytes, descriptors;
	unsigned int input_width, input_height, fps, video_control;
	struct gc573_scaler_result scaler;
	int scaler_error;
	u64 next_frame_ns;
};

static u32 cap_read(struct gc573_capture *c, unsigned int reg)
{
	return ioread32(c->bar + reg);
}

static void cap_write(struct gc573_capture *c, unsigned int reg, u32 value)
{
	iowrite32(value, c->bar + reg);
	c->writes++;
}

static unsigned int cap_input_width(struct gc573_capture *c)
{
	return gc573_input_pixels(cap_read(c, 0x1008), cap_read(c, 0x1088));
}

static unsigned int cap_led_read(void *ctx, unsigned int reg)
{
	return cap_read(ctx, reg);
}

static void cap_led_write(void *ctx, unsigned int reg, unsigned int value)
{
	cap_write(ctx, reg, value);
}

static void cap_led_sleep(void *ctx, unsigned int milliseconds)
{
	usleep_range(milliseconds * 1000, milliseconds * 1000 + 1000);
}

static void cap_rgb_locked(struct gc573_capture *c)
{
	const struct gc573_block_io io = {
		.ctx = c, .read = cap_led_read, .write = cap_led_write,
		.sleep_ms = cap_led_sleep,
	};

	c->led_error = gc573_led_set(&io, &c->led, c->led_mode,
				     c->led_color, c->led_brightness);
}

static void cap_rgb(struct gc573_capture *c)
{
	mutex_lock(&c->led_mutex);
	cap_rgb_locked(c);
	mutex_unlock(&c->led_mutex);
}

/* On this FPGA, LED control reads keep the programmed animation alive.
 * Idle hardware testing retained RGB for 125 s with reads every 100 ms,
 * then reverted to the red default after a two-second gap. Do not restart
 * the animation or reprogram command RAM on each keepalive.
 */
static void cap_led_keepalive(struct work_struct *work)
{
	struct gc573_capture *c = container_of(to_delayed_work(work),
					     struct gc573_capture, led_work);

	c->led_observed_divider = cap_read(c, 0x800);
	c->led_observed_enabled = cap_read(c, 0x804);
	c->led_checks++;
	schedule_delayed_work(&c->led_work, msecs_to_jiffies(250));
}

/* Exact-target 19b00: request video reset, wait for completion bit 3. */
static int cap_reset(struct gc573_capture *c)
{
	unsigned int i;
	u32 packing, mode;
	int ret = -ETIMEDOUT;

	/* Reset clears the input packing too. Serialize its restoration with
	 * HDMI mode changes, so a stream reopen retains the receiver interface.
	 */
	if (c->io.control_lock) c->io.control_lock(c->io.ctx);
	packing = cap_read(c, 0x1088);
	mode = cap_read(c, 0x1040);

	cap_write(c, 0x0c, 1);
	for (i = 0; i < 12; i++) {
		usleep_range(1000, 2000);
		if (cap_read(c, 0x0c) & 8) {
			msleep(30);
			cap_write(c, 0x1040, mode);
			cap_write(c, 0x1088, packing);
			ret = cap_read(c, 0x1040) == mode &&
				cap_read(c, 0x1088) == packing ? 0 : -EIO;
			/* This FPGA reset also restores the default flashing-red LED program. */
			cap_rgb(c);
			break;
		}
	}
	if (c->io.control_unlock) c->io.control_unlock(c->io.ctx);
	return ret;
}

static int cap_stop(struct gc573_capture *c)
{
	unsigned long flags;
	int ret;

	/* A queue opened before the first HDMI signal has never armed DMA. */
	if (!c->started)
		return 0;
	WRITE_ONCE(c->streaming, false);
	/* Stop video and its IRQ before removing bus master and resetting DMA. */
	cap_write(c, 0x1000, cap_read(c, 0x1000) & ~1U);
	spin_lock_irqsave(&c->engine_lock, flags);
	cap_write(c, 0x1c, cap_read(c, 0x1c) & ~2U);
	spin_unlock_irqrestore(&c->engine_lock, flags);
	if (c->irq_requested)
		synchronize_irq(pci_irq_vector(c->pdev, 0));
	cap_write(c, 0x304, 0);
	cap_read(c, 0x304);
	if (!c->audio) {
		pci_clear_master(c->pdev);
		c->master_cleared = 1;
		c->pending_drained = pci_wait_for_pending_transaction(c->pdev);
	}
	msleep(100);
	ret = cap_reset(c);
	if (ret)
		c->error = ret;
	/* Keep allocations until remove, even on timeout. */
	return ret;
}

static irqreturn_t cap_interrupt(int irq, void *opaque)
{
	struct gc573_capture *c = opaque;
	u32 status = cap_read(c, 0x10);

	if (!(status & 0x22))
		return IRQ_NONE;
	if (status & 0x20) {
		struct gc573_audio *audio = READ_ONCE(c->audio);

		if (audio)
			gc573_audio_interrupt(audio);
		else
			iowrite32(0x20, c->bar + 0x10);
	}
	if (!(status & 2))
		return IRQ_HANDLED;
	c->irq = status;
	c->dma_status = cap_read(c, 0x300);
	c->dma_control = cap_read(c, 0x304);
	iowrite32(2, c->bar + 0x10);
	c->interrupts++;
	if ((c->dma_status & 7) == c->slot + 1) {
		c->complete = 1;
		complete(&c->done);
	}
	return IRQ_HANDLED;
}

static bool cap_input_matches(struct gc573_capture *c)
{
	u32 period = cap_read(c, 0x1010);

	return (!c->io.ready || c->io.ready(c->io.ctx)) &&
		(cap_read(c, 0x1004) & 1) && cap_input_width(c) == c->input_width &&
		cap_read(c, 0x100c) == c->input_height && period >= 100000000U / 121 &&
		period <= 100000000U / 23;
}

/* Called with the V4L2 queue idle, before any DMA is armed. */
static void cap_layout(struct gc573_capture *c)
{
	unsigned int i, off, size, n = 0;

	c->frame_bytes = gc573_mode_bytes(c->width, c->height);
	for (i = 0; i < c->allocated; i++)
		memset(c->chunks[i], 0xa5, CHUNK_BYTES);
	for (off = 0; off < c->frame_bytes; off += size) {
		dma_addr_t address = c->addresses[off / CHUNK_BYTES] + off % CHUNK_BYTES;

		size = min(4096U, c->frame_bytes - off);
		c->desc[n++] = (struct gc573_descriptor) {
			cpu_to_le32(lower_32_bits(address)), cpu_to_le32(upper_32_bits(address)),
			cpu_to_le32(size / 4), cpu_to_le32(0x80008000),
		};
	}
	c->descriptors = n;
	cap_write(c, 0x1020, 0);
	cap_write(c, 0x1024, c->input_width);
	cap_write(c, 0x1028, 0);
	cap_write(c, 0x102c, c->input_height);
	dma_wmb();
}

static int cap_prepare(struct gc573_capture *c)
{
	unsigned int i;

	if (cap_read(c, 0) != 0x20201015 || cap_read(c, 0x64) != 0x57300102)
		return -ENODEV;
	if (cap_read(c, 0x1c) || cap_read(c, 0x304) || (cap_read(c, 8) & 0x22) ||
	    (cap_read(c, 0x1000) & 1))
		return -EBUSY;
	c->width = cap_input_width(c);
	c->height = cap_read(c, 0x100c);
	/* Register an idle device even while the prepared HDMI input is absent.
	 * DMA remains gated by cap_input_matches() on every transfer.
	 */
	if (!gc573_mode_supported(c->width, c->height)) {
		c->width = 1920;
		c->height = 1080;
	}
	c->input_width = c->width;
	c->input_height = c->height;
	c->fps = 60;
	/* The previous stream's reset can briefly deassert DDR readiness. */
	for (i = 0; i < 50 && !(cap_read(c, 0x107c) & 1); i++)
		msleep(10);
	if (!(cap_read(c, 0x107c) & 1))
		return -ETIMEDOUT;
	if (cap_reset(c))
		return -ETIMEDOUT;
	if (dma_set_mask_and_coherent(&c->pdev->dev, DMA_BIT_MASK(64)))
		return -EIO;
	c->frame = vzalloc(GC573_FRAME_BYTES);
	if (!c->frame)
		return -ENOMEM;
	c->desc = dma_alloc_coherent(&c->pdev->dev, DESCRIPTORS * sizeof(*c->desc),
				     &c->desc_address, GFP_KERNEL);
	if (!c->desc)
		return -ENOMEM;
	for (i = 0; i < CHUNKS; i++) {
		c->chunks[i] = dma_alloc_coherent(&c->pdev->dev, CHUNK_BYTES,
						&c->addresses[i], GFP_KERNEL);
		if (!c->chunks[i])
			return -ENOMEM;
		c->allocated++;
		memset(c->chunks[i], 0xa5, CHUNK_BYTES);
	}
	cap_layout(c);
	cap_write(c, 0x1040, cap_read(c, 0x1040) & ~0x8002fU);
	cap_write(c, 0x1088, 0);
	cap_write(c, 0x1000, 0x200);
	cap_write(c, 0x10, cap_read(c, 0x10) & 3);
	dma_wmb();
	cap_write(c, 0x308, lower_32_bits(c->desc_address));
	cap_write(c, 0x30c, upper_32_bits(c->desc_address));
	cap_write(c, 0x310, c->descriptors);
	init_completion(&c->done);
	if (pci_alloc_irq_vectors(c->pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_MSIX) < 0)
		return -ENODEV;
	c->vectors = 1;
	if (request_irq(pci_irq_vector(c->pdev, 0), cap_interrupt, 0, "gc573_native", c))
		return -EBUSY;
	c->irq_requested = 1;
	return 0;
}

static int cap_enable(struct gc573_capture *c)
{
	unsigned int slot;
	unsigned long flags;
	const struct gc573_block_io scaler_io = {
		.ctx = c, .read = cap_led_read, .write = cap_led_write,
		.sleep_ms = cap_led_sleep,
	};

	c->scaler_error = gc573_scaler_configure(&scaler_io, &c->scaler,
		c->input_width, c->input_height, c->width, c->height);
	if (c->scaler_error)
		return c->scaler_error;
	c->video_control = 0x200 | (c->scaler.enabled ? 0x80 : 0);
	c->next_frame_ns = 0;
	cap_write(c, 0x1000, c->video_control);
	/* Initialize all video slots to owned DMA memory; arm only one at a time. */
	for (slot = 0; slot < 4; slot++) {
		cap_write(c, 0x308 + slot * 12, lower_32_bits(c->desc_address));
		cap_write(c, 0x30c + slot * 12, upper_32_bits(c->desc_address));
		cap_write(c, 0x310 + slot * 12, c->descriptors);
	}
	dma_wmb();
	pci_set_master(c->pdev);
	c->master_cleared = 0;
	c->pending_drained = 0;
	c->started = 1;
	WRITE_ONCE(c->streaming, true);
	cap_write(c, 0x10, cap_read(c, 0x10) & 3);
	spin_lock_irqsave(&c->engine_lock, flags);
	cap_write(c, 0x1c, cap_read(c, 0x1c) | 2);
	spin_unlock_irqrestore(&c->engine_lock, flags);
	/* Video is enabled by 0x304/0x1000. Global 0x08 bit 1 is audio DMA. */
	return 0;
}

static int cap_transfer(struct gc573_capture *c)
{
	u32 source_period = cap_read(c, 0x1010);
	u64 interval = div_u64(1000000000ULL, c->fps), now;

	/* Select frames before arming DMA. The FPGA scaler still handles pixels;
	 * unwanted source frames never traverse PCIe or consume a userspace buffer.
	 * Arm half a source period before the desired next completion, allowing
	 * the FPGA to choose the next complete frame without a torn snapshot.
	 */
	if (source_period && source_period < 100000000U / c->fps && c->next_frame_ns) {
		u64 lead = (u64)source_period * 5;

		now = ktime_get_ns();
		if (c->next_frame_ns > now + lead) {
			u32 wait_us = div_u64(c->next_frame_ns - now - lead, 1000);

			if (wait_us > 0 && wait_us <= 50000)
				usleep_range(wait_us, wait_us + 100);
		}
	}
	if (!cap_input_matches(c))
		return -ENOLINK;
	if (READ_ONCE(c->stopping))
		return -ECANCELED;
	c->complete = 0;
	reinit_completion(&c->done);
	c->slot = (cap_read(c, 0x300) & 7) % 4;
	cap_write(c, 0x308 + c->slot * 12, lower_32_bits(c->desc_address));
	cap_write(c, 0x30c + c->slot * 12, upper_32_bits(c->desc_address));
	cap_write(c, 0x310 + c->slot * 12, c->descriptors);
	dma_wmb();
	cap_write(c, 0x304, 1 | BIT(c->slot + 1));
	cap_write(c, 0x1000, c->video_control | 1);
	c->polls = wait_for_completion_timeout(&c->done, msecs_to_jiffies(1500)) ? 1 : 0;
	if (!c->complete) {
		c->irq = cap_read(c, 0x10);
		c->dma_status = cap_read(c, 0x300);
		c->dma_control = cap_read(c, 0x304);
		if (!cap_input_matches(c))
			return -ENOLINK;
		return -ETIMEDOUT;
	}
	now = ktime_get_ns();
	if (!c->next_frame_ns || now > c->next_frame_ns + interval)
		c->next_frame_ns = now;
	c->next_frame_ns += interval;
	dma_rmb();
	return 0;
}

static void cap_copy(struct gc573_capture *c, void *dest)
{
	unsigned int off, size;

	for (off = 0; off < c->frame_bytes; off += size) {
		size = min(CHUNK_BYTES, c->frame_bytes - off);
		memcpy(dest + off, c->chunks[off / CHUNK_BYTES], size);
	}
}

static int cap_guard(struct gc573_capture *c)
{
	unsigned int i;

	c->guard_ok = 1;
	for (i = c->frame_bytes % CHUNK_BYTES; i && i < CHUNK_BYTES; i++)
		if (((unsigned char *)c->chunks[c->frame_bytes / CHUNK_BYTES])[i] != 0xa5)
			c->guard_ok = 0;
	return c->guard_ok ? 0 : -EIO;
}

static int cap_once(struct gc573_capture *c)
{
	unsigned int i;
	int ret;

	ret = cap_enable(c);
	if (ret)
		return ret;
	ret = cap_transfer(c);
	cap_stop(c);
	cap_copy(c, c->frame);
	for (i = 0; i < c->frame_bytes; i++)
		c->changed_bytes += ((unsigned char *)c->frame)[i] != 0xa5;
	if (cap_guard(c) || !c->pending_drained)
		return -EIO;
	return ret;
}

static void cap_return_buffers(struct gc573_capture *c, enum vb2_buffer_state state)
{
	struct capture_buffer *b;
	unsigned long flags;

	for (;;) {
		spin_lock_irqsave(&c->queue_lock, flags);
		if (list_empty(&c->queued)) {
			spin_unlock_irqrestore(&c->queue_lock, flags);
			break;
		}
		b = list_first_entry(&c->queued, struct capture_buffer, list);
		list_del(&b->list);
		spin_unlock_irqrestore(&c->queue_lock, flags);
		vb2_buffer_done(&b->vb.vb2_buf, state);
	}
}

static int cap_worker(void *opaque)
{
	struct gc573_capture *c = opaque;
	struct capture_buffer *b;
	unsigned long flags;
	int ret;

	while (!kthread_should_stop() && !READ_ONCE(c->stopping)) {
		spin_lock_irqsave(&c->queue_lock, flags);
		b = list_first_entry_or_null(&c->queued, struct capture_buffer, list);
		if (b)
			list_del(&b->list);
		spin_unlock_irqrestore(&c->queue_lock, flags);
		if (!b) {
			usleep_range(2000, 3000);
			continue;
		}
		ret = cap_transfer(c);
		/* Keep the userspace queue alive across loss of the supported input.
		 * No DMA is armed while geometry is absent or unsupported. Resuming
		 * requires the receiver/FPGA to report the same locked input geometry.
		 */
		if (ret == -ENOLINK && !READ_ONCE(c->stopping) && !cap_stop(c)) {
			const struct v4l2_event event = {
				.type = V4L2_EVENT_SOURCE_CHANGE,
				.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
			};

			v4l2_event_queue(&c->video, &event);
			c->error = -ENOLINK;
			while (!kthread_should_stop() && !READ_ONCE(c->stopping)) {
				if ((!c->io.ready || c->io.ready(c->io.ctx)) &&
				    (cap_read(c, 0x1004) & 1) &&
				    gc573_input_supported(cap_input_width(c), cap_read(c, 0x100c))) {
					c->input_width = cap_input_width(c);
					c->input_height = cap_read(c, 0x100c);
					cap_layout(c);
				}
				if (cap_input_matches(c)) {
					ret = cap_enable(c);
					if (!ret)
						ret = cap_transfer(c);
					if (!ret) {
						c->error = 0;
						c->link_recoveries++;
					}
					break;
				}
				msleep(100);
			}
		}
		if (ret || READ_ONCE(c->stopping) || cap_guard(c)) {
			if (!READ_ONCE(c->stopping))
				c->error = ret ? ret : -EIO;
			vb2_buffer_done(&b->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			if (!READ_ONCE(c->stopping))
				vb2_queue_error(&c->queue);
			break;
		}
		cap_copy(c, vb2_plane_vaddr(&b->vb.vb2_buf, 0));
		vb2_set_plane_payload(&b->vb.vb2_buf, 0, c->frame_bytes);
		b->vb.vb2_buf.timestamp = ktime_get_ns();
		b->vb.sequence = c->sequence++;
		b->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&b->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
	cap_stop(c);
	return 0;
}

static int cap_queue_setup(struct vb2_queue *q, unsigned int *buffers, unsigned int *planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct gc573_capture *c = vb2_get_drv_priv(q);

	if (*planes)
		return *planes == 1 && sizes[0] >= c->frame_bytes ? 0 : -EINVAL;
	*planes = 1;
	sizes[0] = c->frame_bytes;
	return 0;
}

static int cap_buf_prepare(struct vb2_buffer *vb)
{
	struct gc573_capture *c = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < c->frame_bytes)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, c->frame_bytes);
	return 0;
}

static void cap_buf_queue(struct vb2_buffer *vb)
{
	struct gc573_capture *c = vb2_get_drv_priv(vb->vb2_queue);
	struct capture_buffer *b = container_of(to_vb2_v4l2_buffer(vb), struct capture_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&c->queue_lock, flags);
	list_add_tail(&b->list, &c->queued);
	spin_unlock_irqrestore(&c->queue_lock, flags);
}

static int cap_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct gc573_capture *c = vb2_get_drv_priv(q);

	c->stopping = false;
	c->sequence = 0;
	c->error = 0;
	if (gc573_input_supported(cap_input_width(c), cap_read(c, 0x100c))) {
		c->input_width = cap_input_width(c);
		c->input_height = cap_read(c, 0x100c);
	}
	cap_layout(c);
	if (cap_input_matches(c)) {
		int ret = cap_enable(c);

		if (ret) {
			c->error = ret;
			cap_return_buffers(c, VB2_BUF_STATE_QUEUED);
			return ret;
		}
	}
	c->thread = kthread_run(cap_worker, c, "gc573-capture");
	if (IS_ERR(c->thread)) {
		int ret = PTR_ERR(c->thread);

		c->thread = NULL;
		cap_stop(c);
		cap_return_buffers(c, VB2_BUF_STATE_QUEUED);
		return ret;
	}
	return 0;
}

static void cap_stop_streaming(struct vb2_queue *q)
{
	struct gc573_capture *c = vb2_get_drv_priv(q);

	WRITE_ONCE(c->stopping, true);
	complete_all(&c->done);
	if (c->thread) {
		kthread_stop(c->thread);
		c->thread = NULL;
	}
	cap_return_buffers(c, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops cap_queue_ops = {
#ifdef GC573_VB2_NEEDS_WAIT_OPS
	.wait_prepare = vb2_ops_wait_prepare, .wait_finish = vb2_ops_wait_finish,
#endif
	.queue_setup = cap_queue_setup, .buf_prepare = cap_buf_prepare,
	.buf_queue = cap_buf_queue, .start_streaming = cap_start_streaming,
	.stop_streaming = cap_stop_streaming,
};

static int cap_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct gc573_capture *c = video_drvdata(file);

	strscpy(cap->driver, "gc573_native", sizeof(cap->driver));
	strscpy(cap->card, "AVerMedia GC573 Native", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s", pci_name(c->pdev));
	return 0;
}

static int cap_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;
	f->pixelformat = V4L2_PIX_FMT_BGR24;
	return 0;
}

static void cap_fill_format(struct v4l2_format *f, unsigned int width, unsigned int height)
{
	f->fmt.pix = (struct v4l2_pix_format) {
		.width = width, .height = height, .pixelformat = V4L2_PIX_FMT_BGR24,
		.field = V4L2_FIELD_NONE, .bytesperline = width * 3,
		.sizeimage = gc573_mode_bytes(width, height), .colorspace = V4L2_COLORSPACE_SRGB,
		.quantization = V4L2_QUANTIZATION_FULL_RANGE, .xfer_func = V4L2_XFER_FUNC_SRGB,
	};
}

static int cap_format(struct file *file, void *priv, struct v4l2_format *f)
{
	struct gc573_capture *c = video_drvdata(file);

	cap_fill_format(f, c->width, c->height);
	return 0;
}

static int cap_try_format(struct file *file, void *priv, struct v4l2_format *f)
{
	unsigned int width = f->fmt.pix.width <= 1280 && f->fmt.pix.height <= 720 ? 1280 : 1920;

	cap_fill_format(f, width, width == 1280 ? 720 : 1080);
	return 0;
}

static int cap_set_format(struct file *file, void *priv, struct v4l2_format *f)
{
	struct gc573_capture *c = video_drvdata(file);

	if (vb2_is_busy(&c->queue))
		return -EBUSY;
	cap_try_format(file, priv, f);
	c->width = f->fmt.pix.width;
	c->height = f->fmt.pix.height;
	c->frame_bytes = gc573_mode_bytes(c->width, c->height);
	return 0;
}

static int cap_enum_size(struct file *file, void *priv, struct v4l2_frmsizeenum *f)
{
	if (f->index > 1 || f->pixel_format != V4L2_PIX_FMT_BGR24)
		return -EINVAL;
	f->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	f->discrete.width = f->index ? 1280 : 1920;
	f->discrete.height = f->index ? 720 : 1080;
	return 0;
}

static int cap_enum_interval(struct file *file, void *priv, struct v4l2_frmivalenum *f)
{
	static const struct v4l2_fract intervals[] = {
		{ 1, 60 }, { 1001, 60000 }, { 1, 50 }, { 1, 30 }, { 1001, 30000 },
		{ 1, 25 }, { 1, 24 }, { 1001, 24000 },
	};

	if (f->index >= ARRAY_SIZE(intervals) || (f->width == 1280 && f->index > 2) ||
	    f->pixel_format != V4L2_PIX_FMT_BGR24 ||
	    !gc573_mode_supported(f->width, f->height))
		return -EINVAL;
	f->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	f->discrete = intervals[f->index];
	return 0;
}

static int cap_parm(struct file *file, void *priv, struct v4l2_streamparm *p)
{
	struct gc573_capture *c = video_drvdata(file);
	u32 period = cap_read(c, 0x1010);

	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	memset(&p->parm, 0, sizeof(p->parm));
	/* Capture cadence is independent of HDMI OUT. */
	p->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	p->parm.capture.timeperframe = cap_input_matches(c) && period >= 100000000U / c->fps ?
		(struct v4l2_fract) { period, 100000000 } : (struct v4l2_fract) { 1, c->fps };
	p->parm.capture.readbuffers = 2;
	return 0;
}

static int cap_set_parm(struct file *file, void *priv, struct v4l2_streamparm *p)
{
	struct gc573_capture *c = video_drvdata(file);
	struct v4l2_fract f = p->parm.capture.timeperframe;
	unsigned int fps;

	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (vb2_is_busy(&c->queue))
		return -EBUSY;
	/* Quantize to a supported whole-fps cap; 59.94 must select 60, not 59. */
	fps = f.numerator ? DIV_ROUND_CLOSEST_ULL((u64)f.denominator, f.numerator) : 60;
	c->fps = clamp(fps, 24U, 60U);
	return cap_parm(file, priv, p);
}

static int cap_enum_input(struct file *file, void *priv, struct v4l2_input *input)
{
	if (input->index)
		return -EINVAL;
	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "HDMI", sizeof(input->name));
	return 0;
}

static int cap_get_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cap_set_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int cap_subscribe(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_SOURCE_CHANGE)
		return v4l2_event_subscribe(fh, sub, 4, NULL);
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops cap_ioctl_ops = {
	.vidioc_querycap = cap_querycap, .vidioc_enum_fmt_vid_cap = cap_enum_fmt,
	.vidioc_g_fmt_vid_cap = cap_format, .vidioc_try_fmt_vid_cap = cap_try_format,
	.vidioc_s_fmt_vid_cap = cap_set_format, .vidioc_enum_framesizes = cap_enum_size,
	.vidioc_enum_frameintervals = cap_enum_interval,
	.vidioc_g_parm = cap_parm, .vidioc_s_parm = cap_set_parm,
	.vidioc_enum_input = cap_enum_input, .vidioc_g_input = cap_get_input,
	.vidioc_s_input = cap_set_input,
	.vidioc_reqbufs = vb2_ioctl_reqbufs, .vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf, .vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf, .vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf, .vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_subscribe_event = cap_subscribe,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations cap_fops = {
	.owner = THIS_MODULE, .open = v4l2_fh_open, .release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2, .read = vb2_fop_read,
	.poll = vb2_fop_poll, .mmap = vb2_fop_mmap,
};

static void cap_video_release(struct video_device *vdev)
{
	struct gc573_capture *c = container_of(vdev, struct gc573_capture, video);

	v4l2_ctrl_handler_free(&c->controls);
	v4l2_device_unregister(&c->v4l2);
	vfree(c->frame);
	kfree(c);
}

/* Private controls for this out-of-tree driver; stable IDs for the app. */
#define GC573_CID_LED_MODE (V4L2_CID_USER_BASE + 0x2000)
#define GC573_CID_LED_COLOR (GC573_CID_LED_MODE + 1)
#define GC573_CID_LED_BRIGHTNESS (GC573_CID_LED_MODE + 2)

static int cap_control(struct v4l2_ctrl *ctrl)
{
	struct gc573_capture *c = container_of(ctrl->handler, struct gc573_capture, controls);
	unsigned int old_mode, old_color, old_brightness;
	int ret;

	mutex_lock(&c->led_mutex);
	old_mode = c->led_mode;
	old_color = c->led_color;
	old_brightness = c->led_brightness;
	switch (ctrl->id) {
	case GC573_CID_LED_MODE: c->led_mode = ctrl->val; break;
	case GC573_CID_LED_COLOR: c->led_color = ctrl->val; break;
	case GC573_CID_LED_BRIGHTNESS: c->led_brightness = ctrl->val; break;
	default:
		mutex_unlock(&c->led_mutex);
		return -EINVAL;
	}
	cap_rgb_locked(c);
	ret = c->led_error;
	if (ret) {
		c->led_mode = old_mode;
		c->led_color = old_color;
		c->led_brightness = old_brightness;
	}
	mutex_unlock(&c->led_mutex);
	return ret;
}

static const struct v4l2_ctrl_ops cap_control_ops = { .s_ctrl = cap_control };

static int cap_register(struct gc573_capture *c)
{
	static const char * const modes[] = { "Rainbow", "Solid", "Off", NULL };
	const struct v4l2_ctrl_config configs[] = {
		{ .ops = &cap_control_ops, .id = GC573_CID_LED_MODE, .name = "RGB Mode",
		  .type = V4L2_CTRL_TYPE_MENU, .max = 2, .qmenu = modes },
		{ .ops = &cap_control_ops, .id = GC573_CID_LED_COLOR, .name = "RGB Color",
		  .type = V4L2_CTRL_TYPE_INTEGER, .max = 0xffffff, .step = 1, .def = 0xffffff },
		{ .ops = &cap_control_ops, .id = GC573_CID_LED_BRIGHTNESS, .name = "RGB Brightness",
		  .type = V4L2_CTRL_TYPE_INTEGER, .max = 100, .step = 1, .def = 100 },
	};
	unsigned int i;
	int ret;

	mutex_init(&c->mutex);
	spin_lock_init(&c->queue_lock);
	INIT_LIST_HEAD(&c->queued);
	ret = v4l2_device_register(&c->pdev->dev, &c->v4l2);
	if (ret)
		return ret;
	v4l2_ctrl_handler_init(&c->controls, ARRAY_SIZE(configs));
	for (i = 0; i < ARRAY_SIZE(configs); i++)
		v4l2_ctrl_new_custom(&c->controls, &configs[i], NULL);
	ret = c->controls.error;
	if (ret)
		goto unregister;
	c->v4l2.ctrl_handler = &c->controls;
	c->queue = (struct vb2_queue) {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF,
		.drv_priv = c, .buf_struct_size = sizeof(struct capture_buffer),
		.ops = &cap_queue_ops, .mem_ops = &vb2_vmalloc_memops,
		.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC,
		.lock = &c->mutex, .dev = &c->pdev->dev, .min_queued_buffers = 2,
	};
	ret = vb2_queue_init(&c->queue);
	if (ret)
		goto unregister;
	strscpy(c->video.name, "AVerMedia GC573 Native", sizeof(c->video.name));
	c->video.v4l2_dev = &c->v4l2;
	c->video.fops = &cap_fops;
	c->video.ioctl_ops = &cap_ioctl_ops;
	c->video.release = cap_video_release;
	c->video.lock = &c->mutex;
	c->video.queue = &c->queue;
	c->video.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	video_set_drvdata(&c->video, c);
	ret = video_register_device(&c->video, VFL_TYPE_VIDEO, -1);
	if (ret) {
		vb2_queue_release(&c->queue);
		goto unregister;
	}
	c->registered = true;
	return 0;
unregister:
	v4l2_ctrl_handler_free(&c->controls);
	v4l2_device_unregister(&c->v4l2);
	return ret;
}

struct gc573_capture *gc573_capture_create(struct pci_dev *pdev, void __iomem *bar, bool stream, const struct gc573_block_io *io)
{
	struct gc573_capture *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;
	c->pdev = pdev;
	c->bar = bar;
	c->io = *io;
	spin_lock_init(&c->engine_lock);
	mutex_init(&c->led_mutex);
	c->led_color = 0xffffff;
	c->led_brightness = 100;
	INIT_DELAYED_WORK(&c->led_work, cap_led_keepalive);
	c->error = cap_prepare(c);
	if (c->error)
		return c;
	c->audio_signal_error = io->ready && !io->ready(io->ctx) ? -ENOLINK :
		gc573_audio_signal_read(io, &c->audio_signal);
	if (!c->audio_signal_error)
		c->audio_signal_error = gc573_audio_signal_enable(io, &c->audio_signal);
	if (!c->error)
		c->error = stream ? cap_register(c) : cap_once(c);
	if (!c->error && stream) {
		c->audio = gc573_audio_create(pdev, bar, pci_irq_vector(pdev, 0), &c->engine_lock, io);
		if (IS_ERR(c->audio)) {
			c->audio_error = PTR_ERR(c->audio);
			c->audio = NULL;
		}
	}
	if (!c->error && !c->led_error)
		schedule_delayed_work(&c->led_work, msecs_to_jiffies(250));
	return c;
}

bool gc573_capture_registered(struct gc573_capture *c)
{
	return c && c->registered && !c->error;
}

void gc573_capture_destroy(struct gc573_capture *c)
{
	unsigned int i;
	bool leak;

	if (!c)
		return;
	cancel_delayed_work_sync(&c->led_work);
	if (c->registered) {
		/* Hold the embedded device through DMA teardown and block new ioctls. */
		get_device(&c->video.dev);
		vb2_video_unregister_device(&c->video);
	}
	if (c->started && !c->master_cleared)
		cap_stop(c);
	if (c->audio) {
		struct gc573_audio *audio = c->audio;

		/* Detach the IRQ callback and wait for any old pointer users. */
		WRITE_ONCE(c->audio, NULL);
		synchronize_irq(pci_irq_vector(c->pdev, 0));
		c->pending_drained = gc573_audio_destroy(audio);
		c->master_cleared = 1;
	}
	if (c->irq_requested)
		free_irq(pci_irq_vector(c->pdev, 0), c);
	if (c->vectors)
		pci_free_irq_vectors(c->pdev);
	leak = c->started && !c->pending_drained;
	if (leak)
		dev_err(&c->pdev->dev, "Retaining DMA buffers: PCI transactions did not drain\n");
	else {
		for (i = 0; i < c->allocated; i++)
			dma_free_coherent(&c->pdev->dev, CHUNK_BYTES, c->chunks[i], c->addresses[i]);
		if (c->desc)
			dma_free_coherent(&c->pdev->dev, DESCRIPTORS * sizeof(*c->desc), c->desc, c->desc_address);
	}
	if (c->registered) {
		v4l2_device_disconnect(&c->v4l2);
		put_device(&c->video.dev);
	} else {
		vfree(c->frame);
		kfree(c);
	}
}

ssize_t gc573_capture_status(struct gc573_capture *c, char *buf, ssize_t used)
{
	if (!c)
		return used + sysfs_emit_at(buf, used, "capture_error=%d\n", -ENOMEM);
	{
		u32 valid = cap_read(c, 0x1004) & 1, period = cap_read(c, 0x1010);
		u32 width = cap_input_width(c) & 0xffff, height = cap_read(c, 0x100c) & 0xffff;

		used += sysfs_emit_at(buf, used,
			"input_present=%u\ninput_width=%u\ninput_height=%u\ninput_fps_milli=%llu\n"
			"rgb_mode=%u\nrgb_color=%u\nrgb_brightness=%u\n",
			valid, valid ? width : 0, valid ? height : 0,
			valid && period ? div_u64(100000000000ULL, period) : 0,
			c->led_mode, c->led_color, c->led_brightness);
	}
	used += sysfs_emit_at(buf, used,
		"audio_signal_error=%d\naudio_signal_valid=%u\naudio_status=%16ph\n"
		"audio_clock=%5ph\naudio_controls=%16ph\naudio_output=0x%02x\n",
		c->audio_signal_error, c->audio_signal.valid, c->audio_signal.status,
		c->audio_signal.clock, c->audio_signal.controls, c->audio_signal.output);
	used += sysfs_emit_at(buf, used,
		"led_error=%d\nled_rgb_complete=%u\n"
		"led_controls_verified=%u\nled_commands_written=%u\n"
		"led_divider=0x%08x\nled_enabled=0x%08x\n"
		"led_live_divider=0x%08x\nled_live_enabled=0x%08x\n",
		c->led_error, c->led.complete, c->led.controls_verified,
		c->led.commands_written, c->led.divider, c->led.after,
		cap_read(c, 0x800), cap_read(c, 0x804));
	used += sysfs_emit_at(buf, used,
		"led_keepalive_checks=%u\nled_observed_divider=0x%08x\n"
		"led_observed_enabled=0x%08x\n",
		c->led_checks, c->led_observed_divider, c->led_observed_enabled);
	used = gc573_audio_status(c->audio, buf, used);
	used += sysfs_emit_at(buf, used, "audio_error=%d\n", c->audio_error);
	used += sysfs_emit_at(buf, used,
		"capture_width=%u\ncapture_height=%u\ncapture_fps_limit=%u\n"
		"capture_scaler_error=%d\ncapture_scaler_enabled=%u\ncapture_scaler_verified=%u\n"
		"capture_scaler_last_reg=0x%x\ncapture_scaler_expected=0x%x\ncapture_scaler_observed=0x%x\n",
		c->width, c->height, c->fps, c->scaler_error, c->scaler.enabled,
		c->scaler.verified, c->scaler.last_reg, c->scaler.expected, c->scaler.observed);
	used += sysfs_emit_at(buf, used, "capture_video_registered=%u\ncapture_streaming=%u\ncapture_link_recoveries=%u\ncapture_frames=%u\ncapture_interrupts=%u\n",
		c->registered, READ_ONCE(c->streaming), c->link_recoveries, c->sequence, c->interrupts);
	return used + sysfs_emit_at(buf, used,
		"capture_error=%d\ncapture_started=%u\ncapture_complete=%u\n"
		"capture_irq=0x%08x\ncapture_dma_status=0x%08x\ncapture_dma_control=0x%08x\n"
		"capture_wait_completed=%u\ncapture_changed_bytes=%u\ncapture_guard_ok=%u\n"
		"capture_master_cleared=%u\ncapture_pending_drained=%u\ncapture_writes=%u\n",
		c->error, c->started, c->complete, c->irq, c->dma_status, c->dma_control,
		c->polls, c->changed_bytes, c->guard_ok, c->master_cleared, c->pending_drained, c->writes);
}

ssize_t gc573_capture_read(struct gc573_capture *c, char *buf, loff_t offset, size_t count)
{
	if (!c || c->registered || !c->started || !c->frame || offset < 0)
		return -ENODATA;
	if (offset >= c->frame_bytes)
		return 0;
	count = min_t(size_t, count, c->frame_bytes - offset);
	memcpy(buf, c->frame + offset, count);
	return count;
}
