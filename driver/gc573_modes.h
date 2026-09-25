/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_MODES_H
#define GC573_MODES_H
/* Bounded RGB8 single-TTL modes within the researched <=150 MHz branch. */
#define GC573_MAX_FRAME_BYTES (1920U * 1080U * 3U)
/* The input timing counter counts interface clock periods. DDR and dual
 * pixel packing each carry two samples per period into the video pipeline.
 */
static inline unsigned int gc573_input_pixels(unsigned int periods, unsigned int packing)
{
	if (packing > 3 || periods > 4096)
		return 0;
	return periods << ((packing & 1) + ((packing >> 1) & 1));
}
static inline int gc573_mode_supported(unsigned int width, unsigned int height)
{
	return (width == 1920 && height == 1080) || (width == 1280 && height == 720);
}
static inline int gc573_input_supported(unsigned int width, unsigned int height)
{
	return gc573_mode_supported(width, height) || (width == 2560 && height == 1440);
}
static inline unsigned int gc573_mode_bytes(unsigned int width, unsigned int height)
{
	return gc573_mode_supported(width, height) ? width * height * 3 : 0;
}
#endif
