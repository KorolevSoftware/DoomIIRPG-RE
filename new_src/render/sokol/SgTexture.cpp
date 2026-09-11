#include "render/sokol/SgTexture.h"

#include <cstdio>

#include "render/api/PixelConvert.h"

namespace newcore {

namespace {

// One immutable 2D image + the view it is sampled through (spec §0.6).
bool makeSampledImage(int w, int h, sg_pixel_format fmt, const void* data,
	size_t size, const char* label, sg_image& outImg, sg_view& outView) {
	sg_image_desc imgDesc = {};
	imgDesc.width = w;
	imgDesc.height = h;
	imgDesc.pixel_format = fmt;
	imgDesc.data.mip_levels[0].ptr = data;
	imgDesc.data.mip_levels[0].size = size;
	imgDesc.label = label;
	const sg_image img = sg_make_image(&imgDesc);
	if (sg_query_image_state(img) != SG_RESOURCESTATE_VALID) {
		std::fprintf(stderr, "sokol: sg_make_image failed for %s (%dx%d)\n", label, w, h);
		sg_destroy_image(img);
		return false;
	}

	sg_view_desc viewDesc = {};
	viewDesc.texture.image = img;
	viewDesc.label = label;
	const sg_view view = sg_make_view(&viewDesc);
	if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
		std::fprintf(stderr, "sokol: sg_make_view failed for %s\n", label);
		sg_destroy_view(view);
		sg_destroy_image(img);
		return false;
	}

	outImg = img;
	outView = view;
	return true;
}

} // namespace

SgTexture::~SgTexture() {
	destroy();
}

SgTexture::SgTexture(SgTexture&& other) noexcept
	: indexImg_(other.indexImg_), palImg_(other.palImg_),
	  indexView_(other.indexView_), palView_(other.palView_), smp_(other.smp_),
	  palSmp_(other.palSmp_), format_(other.format_), width_(other.width_),
	  height_(other.height_) {
	other.indexImg_ = {};
	other.palImg_ = {};
	other.indexView_ = {};
	other.palView_ = {};
}

SgTexture& SgTexture::operator=(SgTexture&& other) noexcept {
	if (this != &other) {
		destroy();
		indexImg_ = other.indexImg_;
		palImg_ = other.palImg_;
		indexView_ = other.indexView_;
		palView_ = other.palView_;
		smp_ = other.smp_;
		palSmp_ = other.palSmp_;
		format_ = other.format_;
		width_ = other.width_;
		height_ = other.height_;
		other.indexImg_ = {};
		other.palImg_ = {};
		other.indexView_ = {};
		other.palView_ = {};
	}
	return *this;
}

void SgTexture::destroy() {
	// Views first: a view keeps its image alive. The samplers are shared and
	// belong to SgTextureStore, so they are never destroyed here.
	if (indexView_.id != SG_INVALID_ID) sg_destroy_view(indexView_);
	if (palView_.id != SG_INVALID_ID) sg_destroy_view(palView_);
	if (indexImg_.id != SG_INVALID_ID) sg_destroy_image(indexImg_);
	if (palImg_.id != SG_INVALID_ID) sg_destroy_image(palImg_);
	indexView_ = {};
	palView_ = {};
	indexImg_ = {};
	palImg_ = {};
	width_ = height_ = 0;
}

bool SgTexture::uploadIndexed(const uint8_t* indices, int w, int h,
	const uint16_t* palette, int paletteCount, bool transparent, bool tiled,
	const SgSamplers& samplers) {
	if (w <= 0 || h <= 0 || indices == nullptr) return false;
	if (palette == nullptr || paletteCount <= 0) return false;

	destroy();
	format_ = Format::Indexed;
	width_ = w;
	height_ = h;

	if (!makeSampledImage(w, h, SG_PIXELFORMAT_R8, indices, (size_t)w * (size_t)h,
			"indices", indexImg_, indexView_)) {
		destroy();
		return false;
	}

	// Same conversion as GlTexture::uploadPalette, so the 0xF81F key and the
	// 5/6/5 bit replication cannot drift between the backends.
	uint8_t lut[256 * 4];
	expandPalette565(palette, paletteCount, transparent, lut);
	if (!makeSampledImage(256, 1, SG_PIXELFORMAT_RGBA8, lut, sizeof(lut),
			"palette", palImg_, palView_)) {
		destroy();
		return false;
	}

	smp_ = tiled ? samplers.repeat : samplers.clamp;
	// The LUT clamps even when the indices repeat, exactly like the two GL
	// texture objects (GlTexture.cpp:50-56 vs :73-76).
	palSmp_ = samplers.clamp;
	return true;
}

bool SgTexture::uploadRgba(const uint8_t* rgba, int w, int h, const SgSamplers& samplers) {
	if (w <= 0 || h <= 0 || rgba == nullptr) return false;

	destroy();
	format_ = Format::Rgba;
	width_ = w;
	height_ = h;

	if (!makeSampledImage(w, h, SG_PIXELFORMAT_RGBA8, rgba,
			(size_t)w * (size_t)h * 4u, "rgba", indexImg_, indexView_)) {
		destroy();
		return false;
	}

	smp_ = samplers.clamp;
	return true;
}

} // namespace newcore
