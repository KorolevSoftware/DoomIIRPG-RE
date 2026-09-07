#ifndef NEW_RENDER_API_PIXELCONVERT_H
#define NEW_RENDER_API_PIXELCONVERT_H

#include <cstdint>

namespace newcore {

// The game's transparent color (docs/original-code/image-formats.md §0, §1.2).
constexpr uint16_t kTransparentKey565 = 0xF81F;

// RGB565 -> RGBA8 with bit replication (the exact expression of the GL palette
// LUT, gl/Texture.cpp:75-88). When keyTransparent is set, the key color yields
// all four bytes zero; every other color gets a = 0xFF.
void rgb565ToRgba8(uint16_t c, bool keyTransparent, uint8_t out[4]);

// Expands a palette of `count` RGB565 entries into 256 RGBA8 entries
// (out = 1024 bytes). Missing entries are padded with the key color, which
// means fully transparent when keyTransparent is set (matches
// io/BmpImageLoader.cpp:51-91).
void expandPalette565(const uint16_t* pal, int count, bool keyTransparent, uint8_t out[1024]);

// Expands w*h indices through an already expanded RGBA8 palette into
// w*h*4 bytes, top-left origin.
void expandIndexed(const uint8_t* idx, int w, int h, const uint8_t pal8888[1024],
	uint8_t* outRgba);

} // namespace newcore

#endif // NEW_RENDER_API_PIXELCONVERT_H
