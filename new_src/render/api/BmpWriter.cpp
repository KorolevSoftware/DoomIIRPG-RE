#include "render/api/BmpWriter.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace newcore {

bool writeBmp24(const char* path, const uint8_t* rgba, int w, int h, bool bottomUp) {
	if (path == nullptr || rgba == nullptr || w <= 0 || h <= 0) return false;

	const int rowSize = ((w * 3) + 3) & ~3;
	const int imageSize = rowSize * h;
	const uint32_t dataOff = 54;

	FILE* f = fopen(path, "wb");
	if (!f) return false;

	uint8_t header[54] = { 0 };
	header[0] = 'B'; header[1] = 'M';
	const uint32_t fileSize = dataOff + (uint32_t)imageSize;
	std::memcpy(header + 2, &fileSize, 4);
	std::memcpy(header + 10, &dataOff, 4);
	const uint32_t hdrSize = 40; std::memcpy(header + 14, &hdrSize, 4);
	const int32_t w32 = w; std::memcpy(header + 18, &w32, 4);
	const int32_t h32 = h; std::memcpy(header + 22, &h32, 4);
	const uint16_t planes = 1; std::memcpy(header + 26, &planes, 2);
	const uint16_t bpp = 24; std::memcpy(header + 28, &bpp, 2);
	fwrite(header, 1, 54, f);

	// BMP rows are bottom-up.
	std::vector<uint8_t> row(rowSize, 0);
	for (int i = 0; i < h; ++i) {
		const int srcY = bottomUp ? i : (h - 1 - i);
		const uint8_t* src = rgba + (size_t)srcY * w * 4;
		for (int x = 0; x < w; ++x) {
			row[x * 3 + 0] = src[x * 4 + 2]; // B
			row[x * 3 + 1] = src[x * 4 + 1]; // G
			row[x * 3 + 2] = src[x * 4 + 0]; // R
		}
		fwrite(row.data(), 1, rowSize, f);
	}

	fclose(f);
	return true;
}

} // namespace newcore
