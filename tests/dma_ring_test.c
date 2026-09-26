// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include "gc573_dma_ring.h"

int main(void)
{
	struct gc573_dma_ring r = {15, 0};
	unsigned int start, count, i, mask, last;

	/* Every starting slot, with one through four coalesced completions. */
	for (start = 0; start < 4; start++) {
		for (count = 1; count <= 4; count++) {
			r = (struct gc573_dma_ring){15, start};
			mask = 0;
			for (i = 0; i < count; i++)
				mask |= 1U << ((start + i) % 4);
			last = (start + count - 1) % 4 + 1;
			assert(gc573_dma_ring_complete(&r, last) == mask);
			assert(r.pending == (15U & ~mask));
			assert(r.next == (start + count) % 4);
			assert(!gc573_dma_ring_complete(&r, last));
		}
	}
	/* A slow consumer can stop all four slots, copy one, then requeue it. */
	r = (struct gc573_dma_ring){15, 0};
	assert(gc573_dma_ring_complete(&r, 4) == 15);
	assert(!r.pending && !r.next);
	r.pending |= 1;
	assert(gc573_dma_ring_complete(&r, 1) == 1);
	assert(r.next == 1 && !r.pending);
	/* No phantom completion across unqueued memory or invalid slot status. */
	r = (struct gc573_dma_ring){5, 0};
	assert(!gc573_dma_ring_complete(&r, 3));
	assert(r.pending == 5 && r.next == 0);
	assert(!gc573_dma_ring_complete(&r, 0));
	assert(!gc573_dma_ring_complete(&r, 5));
	assert(!gc573_dma_ring_complete(&r, ~0U));
	puts("PASS: DMA ring wrap, coalesced/duplicate IRQs, starvation and unqueued-slot guards");
	return 0;
}
