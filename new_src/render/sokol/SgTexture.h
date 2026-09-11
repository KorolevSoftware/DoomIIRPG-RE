#ifndef NEW_RENDER_SOKOL_SGTEXTURE_H
#define NEW_RENDER_SOKOL_SGTEXTURE_H

#include <cstdint>

#include "render/sokol/SgCommon.h"

namespace newcore {

// The two samplers every texture picks from (spec §5.2). Owned by
// SgTextureStore, never destroyed per texture.
struct SgSamplers {
	sg_sampler clamp = {};
	sg_sampler repeat = {};
};

// A sokol_gfx texture, the transliteration of GlTexture. Two modes:
//  - indexed: R8 index image + RGBA8 palette LUT image (the memory scheme of
//    the GL backend, ADR 0020),
//  - rgba:    one RGBA8 image for color data (fade, test patterns, ...).
// Each image is bound through its own sg_view, so an indexed texture costs
// 2 images + 2 views (spec §0.6). Move-only, internal to SgTextureStore:
// callers outside render/sokol/ hold a newcore::Texture handle instead.
class SgTexture {
public:
	enum class Format { Indexed, Rgba };

	SgTexture() = default;
	~SgTexture();

	SgTexture(const SgTexture&) = delete;
	SgTexture& operator=(const SgTexture&) = delete;

	SgTexture(SgTexture&& other) noexcept;
	SgTexture& operator=(SgTexture&& other) noexcept;

	// Uploads an indexed image: indices (w*h bytes) + palette (paletteCount
	// RGB565 entries, missing ones padded with the transparent key).
	// If transparent is true, palette entries equal to the game's transparent
	// color 0xF81F become fully transparent in the LUT. If tiled is true the
	// repeat sampler is used (world textures whose UVs leave [0,1]).
	bool uploadIndexed(const uint8_t* indices, int w, int h,
		const uint16_t* palette, int paletteCount, bool transparent,
		bool tiled, const SgSamplers& samplers);
	// Uploads RGBA8 data (w*h*4 bytes).
	bool uploadRgba(const uint8_t* rgba, int w, int h, const SgSamplers& samplers);

	Format format() const { return format_; }
	int width() const { return width_; }
	int height() const { return height_; }
	// What the bindings of a draw need: the index/color view, the palette view
	// (invalid for Rgba) and one sampler per view. The palette one always
	// clamps, like the GL palette texture object (GlTexture.cpp:73-76): a
	// REPEAT wrap would fetch entry 0 for index 255.
	sg_view view() const { return indexView_; }
	sg_view paletteView() const { return palView_; }
	sg_sampler sampler() const { return smp_; }
	sg_sampler paletteSampler() const { return palSmp_; }
	bool valid() const { return indexImg_.id != SG_INVALID_ID; }

	void destroy();

private:
	sg_image indexImg_ = {};
	sg_image palImg_ = {};
	sg_view indexView_ = {};
	sg_view palView_ = {};
	sg_sampler smp_ = {};
	sg_sampler palSmp_ = {};
	Format format_ = Format::Indexed;
	int width_ = 0;
	int height_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_SOKOL_SGTEXTURE_H
