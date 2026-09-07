#include "render/gl/GlTextureStore.h"

namespace newcore {

GlTextureStore::GlTextureStore() {
	// Slot 0 stays empty: TextureId 0 must never resolve to a texture.
	slots_.emplace_back();
}

size_t GlTextureStore::bytesOf(const GlTexture& tex) {
	const size_t pixels = (size_t)tex.width() * (size_t)tex.height();
	// Indexed textures cost one byte per texel plus the 256-entry RGBA8 LUT.
	return tex.format() == GlTexture::Format::Indexed ? pixels + 1024 : pixels * 4;
}

TextureId GlTextureStore::allocSlot() {
	if (!freeSlots_.empty()) {
		const uint32_t slot = freeSlots_.back();
		freeSlots_.pop_back();
		slots_[slot] = std::make_unique<GlTexture>();
		return TextureId{ slot };
	}
	slots_.push_back(std::make_unique<GlTexture>());
	return TextureId{ (uint32_t)(slots_.size() - 1) };
}

void GlTextureStore::freeSlot(TextureId id) {
	slots_[id.value].reset();
	freeSlots_.push_back(id.value);
}

TextureId GlTextureStore::createIndexed(const uint8_t* indices, int w, int h,
	const uint16_t* palette, int paletteCount, TextureFlags flags) {
	const TextureId id = allocSlot();
	GlTexture& tex = *slots_[id.value];
	if (!tex.uploadIndexed(indices, w, h, palette, paletteCount,
			hasFlag(flags, TextureFlags::TransparentKey),
			hasFlag(flags, TextureFlags::Tiled))) {
		freeSlot(id);
		return TextureId{};
	}
	bytes_ += bytesOf(tex);
	return id;
}

TextureId GlTextureStore::createRgba(const uint8_t* rgba, int w, int h, TextureFlags) {
	const TextureId id = allocSlot();
	GlTexture& tex = *slots_[id.value];
	if (!tex.uploadRgba(rgba, w, h)) {
		freeSlot(id);
		return TextureId{};
	}
	bytes_ += bytesOf(tex);
	return id;
}

void GlTextureStore::destroy(TextureId id) {
	const GlTexture* tex = lookup(id);
	if (tex == nullptr) return;
	bytes_ -= bytesOf(*tex);
	freeSlot(id);
}

bool GlTextureStore::query(TextureId id, int& w, int& h) const {
	const GlTexture* tex = lookup(id);
	if (tex == nullptr) return false;
	w = tex->width();
	h = tex->height();
	return true;
}

size_t GlTextureStore::textureBytes() const {
	return bytes_;
}

const GlTexture* GlTextureStore::lookup(TextureId id) const {
	if (!id.valid() || id.value >= slots_.size()) return nullptr;
	return slots_[id.value].get();
}

} // namespace newcore
