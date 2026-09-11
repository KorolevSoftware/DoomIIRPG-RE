#include "render/sokol/SgTextureStore.h"

#include <cstdio>

namespace newcore {

SgTextureStore::SgTextureStore() {
	// Slot 0 stays empty: TextureId 0 must never resolve to a texture.
	slots_.emplace_back();
}

bool SgTextureStore::initialize() {
	// Nearest everywhere, one clamp and one repeat variant (spec §5.2); this is
	// GlTexture::uploadIndices' GL_NEAREST + conditional GL_REPEAT.
	sg_sampler_desc desc = {};
	desc.min_filter = SG_FILTER_NEAREST;
	desc.mag_filter = SG_FILTER_NEAREST;
	desc.mipmap_filter = SG_FILTER_NEAREST; // no mipmaps exist; NONE has no enum here
	desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
	desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
	desc.label = "smp-clamp";
	samplers_.clamp = sg_make_sampler(&desc);

	desc.wrap_u = SG_WRAP_REPEAT;
	desc.wrap_v = SG_WRAP_REPEAT;
	desc.label = "smp-repeat";
	samplers_.repeat = sg_make_sampler(&desc);

	if (sg_query_sampler_state(samplers_.clamp) != SG_RESOURCESTATE_VALID ||
		sg_query_sampler_state(samplers_.repeat) != SG_RESOURCESTATE_VALID) {
		std::fprintf(stderr, "sokol: sampler creation failed\n");
		return false;
	}
	return true;
}

void SgTextureStore::shutdown() {
	slots_.clear();
	freeSlots_.clear();
	bytes_ = 0;
	if (samplers_.clamp.id != SG_INVALID_ID) sg_destroy_sampler(samplers_.clamp);
	if (samplers_.repeat.id != SG_INVALID_ID) sg_destroy_sampler(samplers_.repeat);
	samplers_ = SgSamplers{};
	// Slot 0 again, in case anything still creates textures after this.
	slots_.emplace_back();
}

size_t SgTextureStore::bytesOf(const SgTexture& tex) {
	const size_t pixels = (size_t)tex.width() * (size_t)tex.height();
	// Indexed textures cost one byte per texel plus the 256-entry RGBA8 LUT.
	// Must stay identical to GlTextureStore::bytesOf (GlTextureStore.cpp:10-14):
	// the startup log compares the two numbers across backends.
	return tex.format() == SgTexture::Format::Indexed ? pixels + 1024 : pixels * 4;
}

TextureId SgTextureStore::allocSlot() {
	if (!freeSlots_.empty()) {
		const uint32_t slot = freeSlots_.back();
		freeSlots_.pop_back();
		slots_[slot] = std::make_unique<SgTexture>();
		return TextureId{ slot };
	}
	slots_.push_back(std::make_unique<SgTexture>());
	return TextureId{ (uint32_t)(slots_.size() - 1) };
}

void SgTextureStore::freeSlot(TextureId id) {
	slots_[id.value].reset();
	freeSlots_.push_back(id.value);
}

TextureId SgTextureStore::createIndexed(const uint8_t* indices, int w, int h,
	const uint16_t* palette, int paletteCount, TextureFlags flags) {
	const TextureId id = allocSlot();
	SgTexture& tex = *slots_[id.value];
	if (!tex.uploadIndexed(indices, w, h, palette, paletteCount,
			hasFlag(flags, TextureFlags::TransparentKey),
			hasFlag(flags, TextureFlags::Tiled), samplers_)) {
		freeSlot(id);
		return TextureId{};
	}
	bytes_ += bytesOf(tex);
	return id;
}

TextureId SgTextureStore::createRgba(const uint8_t* rgba, int w, int h, TextureFlags) {
	const TextureId id = allocSlot();
	SgTexture& tex = *slots_[id.value];
	if (!tex.uploadRgba(rgba, w, h, samplers_)) {
		freeSlot(id);
		return TextureId{};
	}
	bytes_ += bytesOf(tex);
	return id;
}

void SgTextureStore::destroy(TextureId id) {
	const SgTexture* tex = lookup(id);
	if (tex == nullptr) return;
	bytes_ -= bytesOf(*tex);
	freeSlot(id);
}

bool SgTextureStore::query(TextureId id, int& w, int& h) const {
	const SgTexture* tex = lookup(id);
	if (tex == nullptr) return false;
	w = tex->width();
	h = tex->height();
	return true;
}

size_t SgTextureStore::textureBytes() const {
	return bytes_;
}

const SgTexture* SgTextureStore::lookup(TextureId id) const {
	if (!id.valid() || id.value >= slots_.size()) return nullptr;
	return slots_[id.value].get();
}

} // namespace newcore
