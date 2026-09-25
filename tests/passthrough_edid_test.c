// SPDX-License-Identifier: GPL-2.0-only
#include "gc573_passthrough.h"
#include <assert.h>
#include <linux/errno.h>
#include <stdio.h>
#include <string.h>
static void fix(unsigned char *p)
{
	unsigned int i, sum = 0;
	for (i = 0; i < 127; i++)
		sum += p[i];
	p[127] = -sum;
}
int main(int argc, char **argv)
{
	struct gc573_passthrough_edid out;
	unsigned char e[512] = {0}, bad[512];
	unsigned int i;
	if (argc == 3) {
		FILE *f = fopen(argv[1], "rb");
		size_t n;
		assert(f);
		n = fread(e, 1, sizeof(e), f);
		fclose(f);
		assert(!gc573_passthrough_edid(e, n, &out));
		f = fopen(argv[2], "wb");
		assert(f);
		assert(fwrite(out.data, 1, 256, f) == 256);
		fclose(f);
		printf("max_tmds=%u scdc=%u timings=%u video_codes=%u audio=%u\n", out.max_tmds_khz,
		       out.scdc, out.timings, out.video_codes, out.audio);
		return 0;
	}
	memset(e + 1, 255, 6);
	e[126] = 1;
	{
		const unsigned char d[] = {0x56, 0x5e, 0, 0xa0, 0xa0, 0xa0, 0x29, 0x50, 0x30,
					   0x20, 0x35, 0, 0,	0,    0,    0,	  0,	0x1a};
		memcpy(e + 54, d, 18);
	}
	{
		const unsigned char c[] = {2,	 3, 27,	  0,	0x42, 97,   118,  0x67, 3,
					   12,	 0, 0x10, 0,	0,    68,   0x67, 0xd8, 0x5d,
					   0xc4, 1, 120,  0x80, 0xff, 0x23, 9,	  4,	1};
		memcpy(e + 128, c, sizeof(c));
	}
	fix(e);
	fix(e + 128);
	assert(!gc573_passthrough_edid(e, 256, &out));
	assert(out.max_tmds_khz == 600000 && out.scdc && out.audio && out.video_codes == 1);
	assert(out.data[132] == 0x41 && out.data[133] == 97);
	assert(out.data[131] == 0); /* no YUV/HDR/FRL/VRR promises */
	for (i = 0; i < 2; i++) {
		unsigned int j, sum = 0;
		for (j = 0; j < 128; j++)
			sum += out.data[i * 128 + j];
		assert(!(sum & 255));
	}
	assert(!gc573_scaled_edid(e, 256, &out));
	assert(out.video_codes == 0 && out.timings == 1);
	assert((out.data[54 + 2] | ((out.data[54 + 4] & 0xf0) << 4)) == 2560);

	memcpy(bad, e, 512);
	bad[1] ^= 1;
	assert(gc573_passthrough_edid(bad, 256, &out) == -EBADMSG);
	memcpy(bad, e, 512);
	bad[151] = 0xff;
	fix(bad + 128);
	assert(gc573_passthrough_edid(bad, 256, &out) == -EBADMSG);
	memcpy(bad, e, 512);
	bad[149] = 0;
	fix(bad + 128);
	assert(!gc573_passthrough_edid(bad, 256, &out) && out.max_tmds_khz == 340000 &&
	       !out.video_codes);
	assert(gc573_passthrough_edid(e, 128, &out) == -EINVAL);
	for (i = 0; i < 256; i++) {
		memcpy(bad, e, 512);
		bad[i] ^= 0xff;
		(void)gc573_passthrough_edid(bad, 256, &out);
	}
	puts("PASS: bounded display EDID intersection, 4K60, SCDC rate limits, "
	     "malformed data and unsupported-mode filtering");
}
