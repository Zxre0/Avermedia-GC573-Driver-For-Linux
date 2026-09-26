// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "gc573_modes.h"
int main(void)
{
	unsigned int widths[] = {0, 1, 1279, 1280, 1281, 1919, 1920, 1921, 2559, 2560, 2561, 3840, UINT_MAX};
	unsigned int heights[] = {0, 1, 719, 720, 721, 1079, 1080, 1081, 1439, 1440, 1441, 2160, UINT_MAX};
	unsigned int i, j;
	for (i = 0; i < sizeof(widths)/sizeof(widths[0]); i++)
		for (j = 0; j < sizeof(heights)/sizeof(heights[0]); j++) {
			unsigned int size = gc573_mode_bytes(widths[i], heights[j]);
			assert(size <= GC573_MAX_FRAME_BYTES);
			assert((size != 0) == gc573_input_supported(widths[i], heights[j]));
		}
	assert(gc573_mode_bytes(1280,720) == 2764800);
	assert(gc573_mode_bytes(1920,1080) == 6220800);
	assert(gc573_mode_bytes(2560,1440) == 11059200);
	assert(!gc573_mode_supported(2560,1440)); /* Legacy single-TTL setup unchanged. */
	assert(gc573_capture_max_fps(2560,1440,16000) == 120);
	assert(!gc573_capture_max_fps(2560,1440,8000));
	assert(!gc573_capture_max_fps(2560,1440,15999));
	assert(!gc573_capture_max_fps(2560,1440,0));
	assert(gc573_capture_max_fps(1920,1080,8000) == 60);
	assert(!gc573_capture_max_fps(3840,2160,32000));
	assert(gc573_capture_rate(120, 120) == 120);
	assert(gc573_capture_rate(144, 120) == 120);
	assert(gc573_capture_rate(90, 120) == 60);
	assert(gc573_capture_rate(119, 120) == 60);
	assert(gc573_capture_rate(120, 60) == 60);
	assert(gc573_capture_rate(30, 120) == 30);
	assert(gc573_capture_rate(0, 120) == 24);
	assert(!gc573_capture_rate(120, 0));
	/* A 120 fps request must not duplicate a 59.94 Hz source at 120 fps.
	 * Real 119.889 Hz input must also escape the reset-time 60 fps cap.
	 */
	assert(gc573_capture_timer(120, 834103) == 1238643);
	assert(gc573_capture_timer(120, 1668206) == 2477286);
	assert(gc573_capture_timer(120, 833333) == 1237500);
	assert(gc573_capture_timer(60, 834103) == 2475000);
	assert(gc573_capture_timer(30, 834103) == 2475000);
	assert(!gc573_capture_timer(120, 0));
	assert(!gc573_capture_timer(120, UINT_MAX));
	assert(!gc573_capture_timer(90, 834103));
	/* Live 1440p dual-DDR reports 640 interface periods per active line. */
	assert(gc573_input_pixels(640, 3) == 2560);
	assert(gc573_input_pixels(1920, 0) == 1920);
	assert(gc573_input_pixels(1280, 0) == 1280);
	assert(!gc573_input_pixels(UINT_MAX, 3));
	assert(!gc573_input_pixels(640, UINT_MAX));
	assert(!gc573_input_pixels(640, 4));
	puts("PASS: mode geometry and frame bounds, including overflow inputs");
	return 0;
}
