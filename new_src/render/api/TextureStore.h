#ifndef NEW_RENDER_API_TEXTURESTORE_H
#define NEW_RENDER_API_TEXTURESTORE_H

#include <cstddef>
#include <cstdint>

#include "render/api/TextureId.h"

namespace newcore {

// The only way to own pixels (ADR 0020). Creation speaks indices + an RGB565
// palette because that is what every shipped image is; whether the
// implementation keeps it indexed (GL: R8 + LUT) or expands it (SDL: 8888) is
// its private business.
//
// Threading: every TextureStore, Draw2D and Scene3D call happens on the single
// render/game thread. No implementation may assume otherwise.
class TextureStore {
public:
	virtual ~TextureStore() = default;

	// indices: w*h bytes, top-left origin (the format BmpImageLoader and
	// MediaLoader already produce). palette: RGB565, paletteCount <= 256
	// entries; missing entries are padded with 0xF81F by the implementation
	// (matches io/BmpImageLoader.cpp:51-91). Returns an invalid id on failure.
	virtual TextureId createIndexed(const uint8_t* indices, int w, int h,
		const uint16_t* palette, int paletteCount, TextureFlags flags) = 0;

	// rgba: w*h*4 bytes, top-left origin, non-premultiplied.
	virtual TextureId createRgba(const uint8_t* rgba, int w, int h,
		TextureFlags flags) = 0;

	virtual void destroy(TextureId id) = 0;
	virtual bool query(TextureId id, int& w, int& h) const = 0;

	// Bytes currently held for texture pixel data (for the memory log; the GL
	// store counts index+LUT bytes, the SDL store counts expanded RGBA bytes).
	virtual size_t textureBytes() const = 0;
};

} // namespace newcore

#endif // NEW_RENDER_API_TEXTURESTORE_H
