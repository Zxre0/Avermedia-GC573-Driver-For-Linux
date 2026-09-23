/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_AUDIO_H
#define GC573_AUDIO_H
#include "gc573_block.h"
struct gc573_audio_signal {
	unsigned char status[16], clock[5], controls[16], output;
	unsigned int valid;
};
int gc573_audio_signal_enable(const struct gc573_block_io *io, struct gc573_audio_signal *r);
int gc573_audio_signal_read(const struct gc573_block_io *io, struct gc573_audio_signal *r);
#ifdef __KERNEL__
#include <linux/pci.h>
#include <linux/spinlock.h>
struct gc573_audio;
struct gc573_audio *gc573_audio_create(struct pci_dev *pdev, void __iomem *bar,
				     int irq, spinlock_t *lock, const struct gc573_block_io *io);
void gc573_audio_interrupt(struct gc573_audio *a);
bool gc573_audio_destroy(struct gc573_audio *a);
ssize_t gc573_audio_status(struct gc573_audio *a, char *buf, ssize_t used);
#endif
#endif
