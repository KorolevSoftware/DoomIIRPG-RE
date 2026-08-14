#ifndef NEW_IO_BMPIMAGELOADER_H
#define NEW_IO_BMPIMAGELOADER_H

#include <cstdint>
#include <vector>

#include "graphics/Image.h"

namespace newcore {

// Decodes palette-based BMP files (4 or 8 bpp) into palette-indexed images,
// preserving the exact transform used by the legacy loader:
//  - 4bpp is expanded to 8bpp (high nibble first),
//  - rows are flipped (BMP is bottom-up),
//  - RGB888 palette is converted to RGB565,
//  - with transparent mask, palette entries are clamped so pure black maps to
//    the transparent color 0xF81F.
class BmpImageLoader {
public:
	// Returns null on parse failure.
	ImagePtr load(const std::vector<uint8_t>& data, bool transparentMask);

private:
	uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) const {
		return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	}
};

} // namespace newcore

#endif // NEW_IO_BMPIMAGELOADER_H