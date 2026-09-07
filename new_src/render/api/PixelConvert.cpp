#include "render/api/PixelConvert.h"

#include <cstddef>

namespace newcore {

void rgb565ToRgba8(uint16_t c, bool keyTransparent, uint8_t out[4]) {
	const uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
	const uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
	const uint8_t b5 = (uint8_t)(c & 0x1F);
	out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
	out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
	out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
	if (keyTransparent && c == kTransparentKey565) {
		out[0] = 0;
		out[1] = 0;
		out[2] = 0;
		out[3] = 0;
	} else {
		out[3] = 0xFF;
	}
}

void expandPalette565(const uint16_t* pal, int count, bool keyTransparent, uint8_t out[1024]) {
	if (count < 0) count = 0;
	if (count > 256) count = 256;
	for (int i = 0; i < 256; ++i) {
		const uint16_t c = (pal != nullptr && i < count) ? pal[i] : kTransparentKey565;
		rgb565ToRgba8(c, keyTransparent, out + i * 4);
	}
}

void expandIndexed(const uint8_t* idx, int w, int h, const uint8_t pal8888[1024],
	uint8_t* outRgba) {
	const int count = w * h;
	for (int i = 0; i < count; ++i) {
		const uint8_t* src = pal8888 + (size_t)idx[i] * 4;
		uint8_t* dst = outRgba + (size_t)i * 4;
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
	}
}

} // namespace newcore
