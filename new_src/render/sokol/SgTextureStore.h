#ifndef NEW_RENDER_SOKOL_SGTEXTURESTORE_H
#define NEW_RENDER_SOKOL_SGTEXTURESTORE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/api/TextureStore.h"
#include "render/sokol/SgTexture.h"

namespace newcore {

// The sokol TextureStore: keeps every texture indexed (R8 index image + RGBA8
// palette LUT image), exactly like the GL store, so textureBytes() is
// comparable between the two backends (spec §5.3). Slot 0 is never handed out,
// which makes TextureId 0 invalid.
class SgTextureStore : public TextureStore {
public:
	SgTextureStore();

	// Creates the two shared samplers. Must run after sg_setup().
	bool initialize();
	// Destroys every texture and both samplers. Must run before sg_shutdown().
	void shutdown();

	TextureId createIndexed(const uint8_t* indices, int w, int h,
		const uint16_t* palette, int paletteCount, TextureFlags flags) override;
	TextureId createRgba(const uint8_t* rgba, int w, int h,
		TextureFlags flags) override;

	void destroy(TextureId id) override;
	bool query(TextureId id, int& w, int& h) const override;
	size_t textureBytes() const override;

	// sokol-side resolution for the devices in this library: a plain pointer,
	// no virtual call per quad. nullptr for an invalid or already freed id.
	const SgTexture* lookup(TextureId id) const;

private:
	TextureId allocSlot();
	void freeSlot(TextureId id);
	static size_t bytesOf(const SgTexture& tex);

	// One heap node per texture so a lookup() result stays valid across later
	// creations: sprite textures are uploaded lazily in the middle of a frame
	// (World3D::ensureSpriteTexture) while a device still holds its bound one.
	std::vector<std::unique_ptr<SgTexture>> slots_;
	std::vector<uint32_t> freeSlots_;
	SgSamplers samplers_;
	size_t bytes_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGTEXTURESTORE_H
