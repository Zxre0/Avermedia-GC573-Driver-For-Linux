// SPDX-License-Identifier: GPL-2.0-only
/* Build a bounded RGB8/SDR HDMI 2.0 EDID from the connected display's modes.
 * No vendor EDID blob, HDMI 2.1/FRL, DSC, VRR or unimplemented HDR
 * advertisement.
 */
#include "gc573_passthrough.h"
#include <linux/errno.h>
static void copy(unsigned char *to, const unsigned char *from, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; i++)
		to[i] = from[i];
}
static void checksum(unsigned char *p)
{
	unsigned int i, sum = 0;
	for (i = 0; i < 127; i++)
		sum += p[i];
	p[127] = -sum;
}
static unsigned int vic_clock(unsigned int vic)
{
	switch (vic) {
	case 1:
		return 25175;
	case 2:
	case 3:
	case 17:
	case 18:
		return 27000;
	case 4:
	case 19:
	case 32:
	case 33:
	case 34:
		return 74250;
	case 16:
	case 31:
	case 60:
	case 61:
	case 62:
		return vic >= 60 ? 59400 : 148500;
	case 63:
	case 64:
	case 93:
	case 94:
	case 95:
		return 297000;
	case 96:
	case 97:
		return 594000;
	default:
		return 0;
	}
}
static int dtd_allowed(const unsigned char *d, unsigned int max)
{
	unsigned int khz = (d[0] + 256U * d[1]) * 10, w = d[2] + ((d[4] & 0xf0) << 4),
		     h = d[5] + ((d[7] & 0xf0) << 4), ht = w + d[3] + ((d[4] & 15) << 8),
		     vt = h + d[6] + ((d[7] & 15) << 8), limit;
	if (!khz || khz > max || w >= ht || h >= vt || (d[17] & 0x80))
		return 0;
	limit = w == 1280 && h == 720	 ? 120
		: w == 1920 && h == 1080 ? 240
		: w == 2560 && h == 1440 ? 144
		: w == 3840 && h == 2160 ? 60
					 : 0;
	return limit && (unsigned long long)khz * 1000 <= (unsigned long long)limit * ht * vt;
}
int gc573_passthrough_edid(const unsigned char *data, unsigned int length,
			   struct gc573_passthrough_edid *out)
{
	static const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
	unsigned char vics[32] = {0}, dtds[6][18] = {{0}};
	unsigned int b, i, j, end, n = 0, nd = 0, sum, legacy = 0, forum = 0, scdc = 0, audio = 0,
				   pos;
	unsigned char *c;
	*out = (struct gc573_passthrough_edid){0};
	if (length < 128 || length > 512 || length % 128 || length != (data[126] + 1U) * 128)
		return -EINVAL;
	for (i = 0; i < 8; i++)
		if (data[i] != header[i])
			return -EBADMSG;
	for (b = 0; b < length; b += 128) {
		sum = 0;
		for (i = 0; i < 128; i++)
			sum += data[b + i];
		if (sum & 255)
			return -EBADMSG;
		if (!b || data[b] != 2)
			continue;
		end = data[b + 2];
		if (end && (end < 4 || end > 127))
			return -EBADMSG;
		for (i = 4; i < end;) {
			unsigned int tag = data[b + i] >> 5, len = data[b + i] & 31;
			const unsigned char *p = data + b + i + 1;
			if (i + 1 + len > end)
				return -EBADMSG;
			if (tag == 3 && len >= 5 && p[0] == 3 && p[1] == 12 && !p[2]) {
				legacy = 165000;
				if (len >= 7 && p[6])
					legacy = p[6] * 5000U;
			}
			if (tag == 3 && len >= 7 && p[0] == 0xd8 && p[1] == 0x5d && p[2] == 0xc4) {
				forum = p[4] * 5000U;
				scdc = !!(p[5] & 0x80);
			}
			if (tag == 1)
				for (j = 0; j + 2 < len; j += 3)
					if ((p[j] >> 3) == 1 && (p[j] & 7) >= 1 && (p[j + 1] & 4) &&
					    (p[j + 2] & 1))
						audio = 1;
			if (tag == 2)
				for (j = 0; j < len; j++) {
					unsigned int vic = p[j] & 127, k;
					if (!vic_clock(vic))
						continue;
					for (k = 0; k < n && vics[k] != vic; k++)
						;
					if (k == n && n < 31)
						vics[n++] = vic;
				}
			i += 1 + len;
		}
	}
	if (!legacy)
		return -EOPNOTSUPP;
	out->max_tmds_khz = forum ? forum : legacy;
	if (out->max_tmds_khz > 600000)
		out->max_tmds_khz = 600000;
	if (!scdc && out->max_tmds_khz > 340000)
		out->max_tmds_khz = 340000;
	out->scdc = scdc;
	out->audio = audio;
	for (b = 0; b < length; b += 128) {
		if (b && data[b] != 2)
			continue;
		i = b ? data[b + 2] : 54;
		if (!i)
			continue;
		end = b ? 127 : 126;
		for (; i + 18 <= end; i += 18) {
			if (nd < 6 && dtd_allowed(data + b + i, out->max_tmds_khz))
				copy(dtds[nd++], data + b + i, 18);
		}
	}
	if (!nd)
		return -EOPNOTSUPP;
	copy(out->data, data, 54);
	out->data[20] = 0x80; /* RGB8 digital; preserve EDID 1.3 compatibility. */
	out->data[18] = 1;
	out->data[19] = 3;
	out->data[24] = 0x0a; /* RGB, preferred timing; no continuous-frequency promise. */
	out->data[35] = out->data[36] = out->data[37] = 0;
	for (i = 38; i < 54; i++)
		out->data[i] = 1;
	for (i = 0; i < nd && i < 3; i++)
		copy(out->data + 54 + i * 18, dtds[i], 18);
	for (; i < 3; i++)
		out->data[54 + i * 18 + 3] = 0x10;
	{
		const unsigned char name[18] = {0,   0,	  0,   0xfc, 0,	  'G', 'C', '5', '7',
						'3', ' ', 'H', 'D',  'M', 'I', 10,  32,	 32};
		copy(out->data + 108, name, 18);
	}
	out->timings = nd < 3 ? nd : 3;
	out->data[126] = 1;
	c = out->data + 128;
	c[0] = 2;
	c[1] = 3;
	c[3] = 0; /* The SAD promises only the verified 48 kHz stereo format. */
	pos = 4;
	for (i = 0, j = 0; i < n; i++)
		if (vic_clock(vics[i]) <= out->max_tmds_khz)
			vics[j++] = vics[i];
	n = j;
	if (n) {
		c[pos++] = 0x40 | n;
		copy(c + pos, vics, n);
		pos += n;
	}
	out->video_codes = n;
	if (audio) {
		const unsigned char a[] = {0x23, 0x09, 0x04, 0x01, 0x83, 1, 0, 0};
		copy(c + pos, a, sizeof(a));
		pos += sizeof(a);
	}
	{
		const unsigned char hdmi[] = {0x67, 3, 12, 0, 0x10, 0, 0, 0};
		copy(c + pos, hdmi, sizeof(hdmi));
		c[pos + 7] = (out->max_tmds_khz > 340000 ? 340000 : out->max_tmds_khz) / 5000;
		pos += sizeof(hdmi);
	}
	if (scdc) {
		const unsigned char hf[] = {0x67, 0xd8, 0x5d, 0xc4, 1, 0, 0x80, 0};
		copy(c + pos, hf, sizeof(hf));
		c[pos + 5] = out->max_tmds_khz / 5000;
		pos += sizeof(hf);
	}
	c[2] = pos;
	for (i = 3; i < nd && pos + 18 <= 127; i++, pos += 18) {
		copy(c + pos, dtds[i], 18);
		out->timings++;
	}
	checksum(out->data);
	checksum(c);
	return 0;
}

/* Original conservative recovery profile: standard CTA 1080p60 RGB, PCM stereo.
 */
void gc573_capture_edid(unsigned char out[256])
{
	static const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
	static const unsigned char dtd[] = {2,	  0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58,
					    0x2c, 0x45, 0,    0x08, 0x22, 0x21, 0,    0,    0x1e};
	static const unsigned char cta[] = {2, 3,    18, 0x40, 0x41, 0x90, 0x23, 9, 7,
					    1, 0x67, 3,	 12,   0,    0x10, 0,	 0, 30};
	static const unsigned char name[] = {0,	  0,   0,   0xfc, 0,   'G', 'C', '5', '7',
					     '3', ' ', 'C', 'a',  'p', 't', 'u', 'r', 10};
	unsigned int i;
	for (i = 0; i < 256; i++)
		out[i] = 0;
	copy(out, header, 8);
	out[8] = 0x1c;
	out[9] = 0x63;
	out[10] = 0x30;
	out[11] = 0x57;
	out[16] = 1;
	out[17] = 36;
	out[18] = 1;
	out[19] = 3;
	out[20] = 0x80;
	out[21] = 52;
	out[22] = 29;
	out[23] = 120;
	out[24] = 0x0a;
	for (i = 38; i < 54; i++)
		out[i] = 1;
	copy(out + 54, dtd, 18);
	copy(out + 72, name, 18);
	out[93] = out[111] = 0x10;
	out[126] = 1;
	copy(out + 128, cta, sizeof(cta));
	checksum(out);
	checksum(out + 128);
}
