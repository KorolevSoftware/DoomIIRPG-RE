#ifndef NEW_RENDER_API_TEXTURE_H
#define NEW_RENDER_API_TEXTURE_H

#include <cstdint>
#include <vector>

#include "render/api/TextureId.h"

namespace newcore {

class TextureStore;

// Move-only owner of one TextureId: the handle every caller keeps. Same member
// names as the GL-only handle it replaces, plus a TextureStore& on the upload
// calls.
class Texture {
public:
	Texture() = default;
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	Texture(Texture&& other) noexcept;
	Texture& operator=(Texture&& other) noexcept;

	// indices (w*h bytes) + palette (RGB565). transparent keys out the game's
	// transparent color; tiled allows UVs outside [0,1].
	bool uploadIndexed(TextureStore& store, const std::vector<uint8_t>& indices,
		int w, int h, const std::vector<uint16_t>& palette,
		bool transparent = false, bool tiled = false);
	bool uploadRgba(TextureStore& store, const std::vector<uint8_t>& rgba,
		int w, int h);

	void destroy();

	bool valid() const { return id_.valid(); }
	// Cached in the handle: Graphics2D's anchored blits need them every frame
	// (Graphics2D.cpp:79-101) and must not pay for a virtual query.
	int width() const { return width_; }
	int height() const { return height_; }
	TextureId id() const { return id_; }

private:
	TextureStore* store_ = nullptr;
	TextureId id_;
	int width_ = 0;
	int height_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_API_TEXTURE_H
