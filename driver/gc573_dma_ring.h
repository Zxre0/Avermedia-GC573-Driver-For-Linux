/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_DMA_RING_H
#define GC573_DMA_RING_H
/* Hardware reports the most recently completed slot, encoded 1..4. A delayed
 * IRQ can cover several sequential completions; never infer one across a slot
 * which software has not queued, and never count a duplicate IRQ twice.
 */
struct gc573_dma_ring {
	unsigned int pending, next;
};
static inline unsigned int gc573_dma_ring_complete(struct gc573_dma_ring *r,
						 unsigned int status)
{
	unsigned int slot, done = 0, i;

	if (status < 1 || status > 4 || !(r->pending & (1U << (status - 1))))
		return 0;
	slot = r->next;
	for (i = 0; i < 4; i++) {
		if (!(r->pending & (1U << slot)))
			return 0;
		done |= 1U << slot;
		if (slot == status - 1) {
			r->pending &= ~done;
			r->next = (slot + 1) % 4;
			return done;
		}
		slot = (slot + 1) % 4;
	}
	return 0;
}
#endif
