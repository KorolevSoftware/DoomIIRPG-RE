#ifndef NEW_RENDER_SDL_SDLTEXTURESTORE_H
#define NEW_RENDER_SDL_SDLTEXTURESTORE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/api/TextureStore.h"
#include "render/sdl/SdlCommon.h"

namespace newcore {

// The SDL_Render TextureStore. SDL2 rejects SDL_PIXELFORMAT_INDEX8 on every
// driver ("Palettized textures are not supported", spec §4.2.0 fact 1), so the
// palette-indexed data of createIndexed is expanded once, at creation, into a
// 32-bit texture (ADR 0021). The format is fixed to ARGB8888 (user decision):
// no driver advertises a 16-bit format with alpha, and the one driver-negotiated
// alternative, letting SDL_CreateTextureFromSurface pick, yields the alpha-less
// RGB888 on the opengl/software drivers, which would drop the transparent key.
// initialize() logs the driver and its advertised format list so a platform
// where ARGB8888 is not native shows up immediately.
class SdlTextureStore : public TextureStore {
public:
	static constexpr uint32_t kPixelFormat = SDL_PIXELFORMAT_ARGB8888;

	// What the devices in this library need per texture. Returned by value:
	// creating a texture may reallocate the slot vector while a device holds
	// its bound one (World3D::ensureSpriteTexture uploads mid-frame).
	struct Entry {
		SDL_Texture* tex = nullptr;
		int w = 0;
		int h = 0;
		TextureFlags flags = TextureFlags::None;
	};

	SdlTextureStore();
	~SdlTextureStore() override;

	SdlTextureStore(const SdlTextureStore&) = delete;
	SdlTextureStore& operator=(const SdlTextureStore&) = delete;

	// Logs the SDL driver plus its advertised texture formats and the format
	// this store actually uses.
	bool initialize(SDL_Renderer* renderer);

	TextureId createIndexed(const uint8_t* indices, int w, int h,
		const uint16_t* palette, int paletteCount, TextureFlags flags) override;
	TextureId createRgba(const uint8_t* rgba, int w, int h,
		TextureFlags flags) override;

	void destroy(TextureId id) override;
	bool query(TextureId id, int& w, int& h) const override;
	size_t textureBytes() const override;

	// Resolution for SdlDraw2D / SdlScene3D. False for an invalid or already
	// freed id.
	bool lookup(TextureId id, Entry& out) const;

private:
	TextureId allocSlot();
	void freeSlot(TextureId id);
	// Converts `src` to kPixelFormat, uploads it and takes a slot.
	TextureId adopt(SDL_Surface* src, TextureFlags flags);

	SDL_Renderer* renderer_ = nullptr;
	// Slot 0 stays empty: TextureId 0 must never resolve to a texture.
	std::vector<Entry> slots_;
	std::vector<uint32_t> freeSlots_;
	size_t bytes_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_SDL_SDLTEXTURESTORE_H
