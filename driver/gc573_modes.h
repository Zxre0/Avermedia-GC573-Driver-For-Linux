/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GC573_MODES_H
#define GC573_MODES_H
/* Bounded RGB8 single-TTL modes within the researched <=150 MHz branch. */
#define GC573_MAX_FRAME_BYTES (2560U * 1440U * 3U)
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
	return gc573_input_supported(width, height) ? width * height * 3 : 0;
}
/* Gen2 x4 supplies 16000 Mb/s after line coding, before packet overhead.
 * 1440p120 BGR24 needs 10617 Mb/s of video payload alone. Check the narrowest
 * upstream link, not just the capture card's advertised capability.
 */
static inline unsigned int gc573_capture_max_fps(unsigned int width, unsigned int height,
					       unsigned int bandwidth_mbps)
{
	if (gc573_mode_supported(width, height))
		return 60;
	return width == 2560 && height == 1440 && bandwidth_mbps >= 16000 ? 120 : 0;
}
static inline unsigned int gc573_capture_rate(unsigned int requested, unsigned int maximum)
{
	if (!maximum)
		return 0;
	if (requested < 24)
		requested = 24;
	if (requested > maximum)
		requested = maximum;
	/* Continuous ring capture is full rate. Intermediate caps use the paced
	 * path, which is bounded at 60; never silently run 120 for a 90 fps cap.
	 */
	return requested > 60 && requested < 120 ? 60 : requested;
}
#endif
