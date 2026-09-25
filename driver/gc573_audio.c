// SPDX-License-Identifier: GPL-2.0-only
/* Native stereo PCM capture. Exact-target Windows 1a570 / 196d4 / 18970:
 * two alternating physical buffers, 10 ms per period, completion IRQ bit 5.
 */
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "gc573_audio.h"

#define AUDIO_BYTES 1920U
#define AUDIO_ALLOC 4096U
struct gc573_audio {
	struct pci_dev *pdev;
	void __iomem *bar;
	spinlock_t *lock;
	struct snd_card *card;
	struct gc573_block_io io;
	struct gc573_audio_signal signal;
	int prepare_error;
	struct snd_pcm_substream *stream;
	void *area[2];
	dma_addr_t address[2];
	unsigned int position, periods, nonzero, bad_guard, last_slot, allocated;
	int irq;
	bool running;
};

static u32 aud_read(struct gc573_audio *a, unsigned int reg)
{
	return ioread32(a->bar + reg);
}

static void aud_write(struct gc573_audio *a, unsigned int reg, u32 value)
{
	iowrite32(value, a->bar + reg);
}

static const struct snd_pcm_hardware aud_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE, .rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000, .rate_max = 48000, .channels_min = 2, .channels_max = 2,
	.buffer_bytes_max = AUDIO_BYTES * 32, .period_bytes_min = AUDIO_BYTES,
	.period_bytes_max = AUDIO_BYTES, .periods_min = 4, .periods_max = 32,
};

static int aud_open(struct snd_pcm_substream *s)
{
	s->runtime->hw = aud_hardware;
	return snd_pcm_hw_constraint_integer(s->runtime, SNDRV_PCM_HW_PARAM_PERIODS);
}

static void aud_disable(struct gc573_audio *a)
{
	unsigned long flags;

	spin_lock_irqsave(a->lock, flags);
	a->running = false;
	aud_write(a, 8, aud_read(a, 8) & ~2U);
	aud_write(a, 0x1c, aud_read(a, 0x1c) & ~0x20U);
	aud_read(a, 8);
	spin_unlock_irqrestore(a->lock, flags);
}

static int aud_close(struct snd_pcm_substream *s)
{
	struct gc573_audio *a = snd_pcm_substream_chip(s);

	aud_disable(a);
	synchronize_irq(a->irq);
	a->stream = NULL;
	return 0;
}

static int aud_sync_stop(struct snd_pcm_substream *s)
{
	struct gc573_audio *a = snd_pcm_substream_chip(s);

	synchronize_irq(a->irq);
	return 0;
}

static int aud_prepare(struct snd_pcm_substream *s)
{
	struct gc573_audio *a = snd_pcm_substream_chip(s);
	unsigned int i;

	aud_disable(a);
	synchronize_irq(a->irq);
	if (a->io.control_lock) a->io.control_lock(a->io.ctx);
	a->prepare_error = a->io.ready && !a->io.ready(a->io.ctx) ? -ENOLINK :
		gc573_audio_signal_read(&a->io, &a->signal);
	if (!a->prepare_error)
		a->prepare_error = gc573_audio_signal_enable(&a->io, &a->signal);
	if (a->io.control_unlock) a->io.control_unlock(a->io.ctx);
	if (a->prepare_error)
		return a->prepare_error;
	a->stream = s;
	a->position = a->periods = a->nonzero = a->bad_guard = 0;
	for (i = 0; i < 2; i++) {
		memset(a->area[i], 0, AUDIO_BYTES);
		memset(a->area[i] + AUDIO_BYTES, 0xa5, AUDIO_ALLOC - AUDIO_BYTES);
	}
	aud_write(a, 0x200, 0); /* 16-bit, two channels. */
	aud_write(a, 0x204, AUDIO_BYTES / 4);
	aud_write(a, 0x21c, 480);
	for (i = 0; i < 2; i++) {
		aud_write(a, 0x208 + 8 * i, lower_32_bits(a->address[i]));
		aud_write(a, 0x20c + 8 * i, upper_32_bits(a->address[i]));
	}
	aud_write(a, 0x218, 0);
	/* HDMI channel ordering, exact target 16c50 non-0x13 input branch. */
	aud_write(a, 0x2c4, 2);
	aud_write(a, 0x2b4, 0);
	aud_write(a, 0x2b8, 1);
	aud_write(a, 0x2bc, 2);
	aud_write(a, 0x2c0, 3);
	dma_wmb();
	return 0;
}

static int aud_trigger(struct snd_pcm_substream *s, int cmd)
{
	struct gc573_audio *a = snd_pcm_substream_chip(s);
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		spin_lock_irqsave(a->lock, flags);
		pci_set_master(a->pdev);
		aud_write(a, 0x10, 0x20);
		a->running = true;
		aud_write(a, 0x1c, aud_read(a, 0x1c) | 0x20);
		aud_write(a, 8, aud_read(a, 8) | 2);
		spin_unlock_irqrestore(a->lock, flags);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		aud_disable(a);
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t aud_pointer(struct snd_pcm_substream *s)
{
	struct gc573_audio *a = snd_pcm_substream_chip(s);

	return bytes_to_frames(s->runtime, READ_ONCE(a->position));
}

static const struct snd_pcm_ops aud_ops = {
	.open = aud_open, .close = aud_close, .prepare = aud_prepare,
	.trigger = aud_trigger, .pointer = aud_pointer, .sync_stop = aud_sync_stop,
};

void gc573_audio_interrupt(struct gc573_audio *a)
{
	struct snd_pcm_substream *s;
	unsigned int slot, i, position;

	if (!a)
		return;
	slot = aud_read(a, 0x14) & 3;
	aud_write(a, 0x10, 0x20);
	s = READ_ONCE(a->stream);
	if (!READ_ONCE(a->running) || !s || slot < 1 || slot > 2)
		return;
	a->last_slot = slot;
	slot--;
	dma_rmb();
	for (i = AUDIO_BYTES; i < AUDIO_ALLOC; i++)
		if (((unsigned char *)a->area[slot])[i] != 0xa5) {
			a->bad_guard++;
			aud_disable(a);
			snd_pcm_stop_xrun(s);
			return;
		}
	position = a->position;
	memcpy(s->runtime->dma_area + position, a->area[slot], AUDIO_BYTES);
	for (i = 0; i < AUDIO_BYTES; i++)
		a->nonzero += !!((unsigned char *)a->area[slot])[i];
	position += AUDIO_BYTES;
	if (position >= snd_pcm_lib_buffer_bytes(s))
		position = 0;
	WRITE_ONCE(a->position, position);
	a->periods++;
	snd_pcm_period_elapsed(s);
}

struct gc573_audio *gc573_audio_create(struct pci_dev *pdev, void __iomem *bar,
				     int irq, spinlock_t *lock, const struct gc573_block_io *io)
{
	struct gc573_audio *a;
	struct snd_pcm *pcm;
	unsigned int i;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return ERR_PTR(-ENOMEM);
	a->pdev = pdev; a->bar = bar; a->irq = irq; a->lock = lock;
	a->io = *io;
	ret = snd_card_new(&pdev->dev, -1, "GC573", THIS_MODULE, 0, &a->card);
	if (ret)
		goto fail;
	for (i = 0; i < 2; i++) {
		a->area[i] = dma_alloc_coherent(&pdev->dev, AUDIO_ALLOC, &a->address[i], GFP_KERNEL);
		if (!a->area[i]) {
			ret = -ENOMEM;
			goto fail;
		}
		a->allocated++;
	}
	strscpy(a->card->driver, "gc573_native", sizeof(a->card->driver));
	strscpy(a->card->shortname, "GC573 HDMI", sizeof(a->card->shortname));
	snprintf(a->card->longname, sizeof(a->card->longname), "GC573 HDMI audio at %s", pci_name(pdev));
	ret = snd_pcm_new(a->card, "GC573 HDMI", 0, 0, 1, &pcm);
	if (ret)
		goto fail;
	pcm->private_data = a;
	strscpy(pcm->name, "GC573 HDMI", sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &aud_ops);
	ret = snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_VMALLOC, NULL,
					    AUDIO_BYTES * 32, AUDIO_BYTES * 32);
	if (ret)
		goto fail;
	ret = snd_card_register(a->card);
	if (ret)
		goto fail;
	return a;
fail:
	if (a->card)
		snd_card_free(a->card);
	for (i = 0; i < a->allocated; i++)
		dma_free_coherent(&pdev->dev, AUDIO_ALLOC, a->area[i], a->address[i]);
	kfree(a);
	return ERR_PTR(ret);
}

bool gc573_audio_destroy(struct gc573_audio *a)
{
	unsigned int i;
	bool drained;

	if (!a)
		return true;
	snd_card_disconnect(a->card);
	aud_disable(a);
	synchronize_irq(a->irq);
	snd_card_free(a->card);
	/* Capture teardown has disabled the video engine before calling us. */
	pci_clear_master(a->pdev);
	drained = pci_wait_for_pending_transaction(a->pdev);
	msleep(30);
	for (i = 0; i < a->allocated; i++)
		if (drained)
			dma_free_coherent(&a->pdev->dev, AUDIO_ALLOC, a->area[i], a->address[i]);
	if (!drained)
		dev_err(&a->pdev->dev, "Retaining audio DMA buffers: PCI drain failed\n");
	kfree(a);
	return drained;
}

ssize_t gc573_audio_status(struct gc573_audio *a, char *buf, ssize_t used)
{
	if (!a)
		return used + sysfs_emit_at(buf, used, "audio_registered=0\n");
	return used + sysfs_emit_at(buf, used,
		"audio_registered=1\naudio_running=%u\naudio_periods=%u\naudio_nonzero_bytes=%u\n"
		"audio_guard_errors=%u\naudio_last_slot=%u\naudio_prepare_error=%d\n"
		"audio_prepared_valid=%u\naudio_prepared_status=%16ph\n"
		"audio_prepared_controls=%16ph\naudio_prepared_output=0x%02x\n",
		READ_ONCE(a->running), READ_ONCE(a->periods), READ_ONCE(a->nonzero),
		READ_ONCE(a->bad_guard), READ_ONCE(a->last_slot), READ_ONCE(a->prepare_error),
		a->signal.valid, a->signal.status, a->signal.controls, a->signal.output);
}
