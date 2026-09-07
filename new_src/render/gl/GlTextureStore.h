#ifndef NEW_RENDER_GL_GLTEXTURESTORE_H
#define NEW_RENDER_GL_GLTEXTURESTORE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/api/TextureStore.h"
#include "render/gl/GlTexture.h"

namespace newcore {

// The GL TextureStore: keeps every texture indexed (R8 index texture + RGBA8
// palette LUT), so the palette-indexed data stays as it is on disk (ADR 0020).
// Slot 0 is never handed out, which makes TextureId 0 invalid.
class GlTextureStore : public TextureStore {
public:
	GlTextureStore();

	TextureId createIndexed(const uint8_t* indices, int w, int h,
		const uint16_t* palette, int paletteCount, TextureFlags flags) override;
	TextureId createRgba(const uint8_t* rgba, int w, int h,
		TextureFlags flags) override;

	void destroy(TextureId id) override;
	bool query(TextureId id, int& w, int& h) const override;
	size_t textureBytes() const override;

	// GL-side resolution for the devices in this library: a plain pointer, no
	// virtual call per quad. nullptr for an invalid or already freed id.
	const GlTexture* lookup(TextureId id) const;

private:
	TextureId allocSlot();
	void freeSlot(TextureId id);
	static size_t bytesOf(const GlTexture& tex);

	// One heap node per texture so a lookup() result stays valid across later
	// creations: sprite textures are uploaded lazily in the middle of a frame
	// (World3D::ensureSpriteTexture) while a device still holds its bound one.
	std::vector<std::unique_ptr<GlTexture>> slots_;
	std::vector<uint32_t> freeSlots_;
	size_t bytes_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_GL_GLTEXTURESTORE_H
