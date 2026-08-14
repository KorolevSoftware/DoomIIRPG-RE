#include "io/BmpImageLoader.h"

#include <cstring>

namespace newcore {

namespace {

#pragma pack(push, 1)
struct BmpHeader {
	char id[2];
	int32_t fileSize;
	char reserved[4];
	int32_t dataOffset;
	int32_t headerSize;
	int32_t width;
	int32_t height;
	int16_t colorPlanes;
	int16_t bitsPerPixel;
	int32_t compression;
	int32_t imageSize;
	int32_t xPpm;
	int32_t yPpm;
	int32_t colorsUsed;
	int32_t importantColors;
};
#pragma pack(pop)

} // namespace

ImagePtr BmpImageLoader::load(const std::vector<uint8_t>& data, bool transparentMask) {
	if (data.size() < sizeof(BmpHeader)) return nullptr;

	BmpHeader header;
	std::memcpy(&header, data.data(), sizeof(BmpHeader));

	if (header.id[0] != 'B' || header.id[1] != 'M') return nullptr;
	if (header.bitsPerPixel != 4 && header.bitsPerPixel != 8) return nullptr;
	if (header.compression != 0) return nullptr;

	int32_t width = header.width;
	int32_t height = header.height;
	if (width <= 0 || height == 0) return nullptr;
	int flip = 1;
	if (height < 0) { height = -height; flip = 0; }

	int32_t colorsUsed = header.colorsUsed;
	if (colorsUsed == 0) colorsUsed = 1 << (header.bitsPerPixel & 0xFF);
	if (colorsUsed < 0 || colorsUsed > 256) return nullptr;

	size_t paletteOffset = sizeof(BmpHeader);
	if (header.headerSize < sizeof(BmpHeader) - 14) {
		// Older BITMAPINFOHEADER variants still have 4-byte palette entries.
		// Offset is headerSize-based only for non-standard cases; assume standard.
	}

	size_t paletteBytes = (size_t)colorsUsed * 4;
	if (data.size() < paletteOffset + paletteBytes) return nullptr;

	std::vector<uint32_t> rgb888;
	rgb888.resize(colorsUsed);
	for (int32_t i = 0; i < colorsUsed; ++i) {
		size_t off = paletteOffset + (size_t)i * 4;
		uint8_t b = data[off];
		uint8_t g = data[off + 1];
		uint8_t r = data[off + 2];
		// alpha byte ignored; SDL RGB888 layout is 0x00RRGGBB (B in low byte).
		uint32_t val = (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
		if (transparentMask) {
			// Numeric clamp so pure black never maps to the 0xF81F transparent
			// color after RGB565 conversion (matches legacy behavior).
			val = std::max<uint32_t>(val, 8);
		}
		rgb888[i] = val;
	}
	// The palette LUT is always 256 entries in the GL pipeline; pad short
	// palettes with the transparent color so empty slots never render.
	rgb888.resize(256, 0xF81F);

	ImagePtr image = std::make_shared<Image>();
	image->width_ = width;
	image->height_ = height;
	image->depth_ = header.bitsPerPixel;
	image->transparent_ = transparentMask;
	image->palette_.resize(256);
	for (int32_t i = 0; i < 256; ++i) {
		uint32_t c = rgb888[i];
		uint8_t r = (uint8_t)((c >> 16) & 0xFF);
		uint8_t g = (uint8_t)((c >> 8) & 0xFF);
		uint8_t b = (uint8_t)(c & 0xFF);
		image->palette_[i] = rgb888To565(r, g, b);
	}

	// Pixel data.
	size_t pixelStart = (size_t)header.dataOffset;
	if (pixelStart > data.size()) return nullptr;

	const int bitsPerRow = header.bitsPerPixel * width;
	const int srcStride = ((bitsPerRow + 31) / 32) * 4; // 4-byte aligned
	const int copyBytes = (bitsPerRow + 7) / 8;         // useful bytes per row

	size_t pixelBytes = (size_t)srcStride * (size_t)height;
	if (data.size() < pixelStart + pixelBytes) return nullptr;

	std::vector<uint8_t> raw(data.begin() + (int64_t)pixelStart,
		data.begin() + (int64_t)pixelStart + (int64_t)pixelBytes);

	std::vector<uint8_t> unpacked;
	uint8_t* pixelData = raw.data();
	int pixelStride = srcStride;

	if (header.bitsPerPixel == 4) {
		unpacked.resize((size_t)width * (size_t)height);
		for (int row = 0; row < height; ++row) {
			const uint8_t* src = raw.data() + (size_t)srcStride * (size_t)row;
			uint8_t* dst = unpacked.data() + (size_t)width * (size_t)row;
			for (int col = 0; col < width / 2; ++col) {
				dst[col * 2] = (uint8_t)(src[col] >> 4);
				dst[col * 2 + 1] = (uint8_t)(src[col] & 0x0F);
			}
		}
		pixelData = unpacked.data();
		pixelStride = width;
	}

	image->indices_.resize((size_t)width * (size_t)height);
	for (int row = 0; row < height; ++row) {
		int srcRow = flip ? (height - 1 - row) : row;
		std::memcpy(image->indices_.data() + (size_t)width * (size_t)row,
			pixelData + (size_t)pixelStride * (size_t)srcRow,
			(size_t)width);
	}

	return image;
}

} // namespace newcore