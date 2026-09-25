/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_CAPTURE_H
#define GC573_CAPTURE_H
#include <linux/pci.h>
#define GC573_FRAME_BYTES (1920U * 1080U * 3U)
struct gc573_capture;
struct gc573_block_io;
struct gc573_capture *gc573_capture_create(struct pci_dev *pdev, void __iomem *bar, bool stream, const struct gc573_block_io *io);
void gc573_capture_destroy(struct gc573_capture *c);
bool gc573_capture_registered(struct gc573_capture *c);
ssize_t gc573_capture_status(struct gc573_capture *c, char *buf, ssize_t used);
ssize_t gc573_capture_read(struct gc573_capture *c, char *buf, loff_t offset, size_t count);
#endif
