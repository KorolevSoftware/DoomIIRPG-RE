#include "render/gl/GlTexture.h"

#include <cstdio>

#include "render/api/PixelConvert.h"

namespace newcore {

GlTexture::~GlTexture() {
	destroy();
}

GlTexture::GlTexture(GlTexture&& other) noexcept
	: tex_(other.tex_), palette_(other.palette_), format_(other.format_),
	  width_(other.width_), height_(other.height_) {
	other.tex_ = 0;
	other.palette_ = 0;
}

GlTexture& GlTexture::operator=(GlTexture&& other) noexcept {
	if (this != &other) {
		destroy();
		tex_ = other.tex_;
		palette_ = other.palette_;
		format_ = other.format_;
		width_ = other.width_;
		height_ = other.height_;
		other.tex_ = 0;
		other.palette_ = 0;
	}
	return *this;
}

void GlTexture::destroy() {
	if (tex_) glDeleteTextures(1, &tex_);
	if (palette_) glDeleteTextures(1, &palette_);
	tex_ = 0;
	palette_ = 0;
	width_ = height_ = 0;
}

bool GlTexture::uploadIndices(const uint8_t* indices, int w, int h, bool repeat) {
	if (w <= 0 || h <= 0 || indices == nullptr) return false;

	destroy();
	format_ = Format::Indexed;
	width_ = w;
	height_ = h;

	GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glGenTextures(1, &tex_);
	glBindTexture(GL_TEXTURE_2D, tex_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, indices);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

bool GlTexture::uploadPalette(const uint16_t* rgb565, int count, bool transparent) {
	if (rgb565 == nullptr || count <= 0) return false;

	// Convert RGB565 palette -> RGBA8 for the LUT texture; entries beyond
	// `count` are padded with the transparent key.
	uint8_t rgba[256 * 4];
	expandPalette565(rgb565, count, transparent, rgba);

	glGenTextures(1, &palette_);
	glBindTexture(GL_TEXTURE_2D, palette_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

bool GlTexture::uploadIndexed(const uint8_t* indices, int w, int h,
	const uint16_t* palette, int paletteCount, bool transparent, bool repeat) {
	if (!uploadIndices(indices, w, h, repeat)) return false;
	if (!uploadPalette(palette, paletteCount, transparent)) {
		destroy();
		return false;
	}
	return true;
}

bool GlTexture::uploadRgba(const uint8_t* rgba, int w, int h) {
	if (w <= 0 || h <= 0 || rgba == nullptr) return false;

	destroy();
	format_ = Format::Rgba;
	width_ = w;
	height_ = h;

	glGenTextures(1, &tex_);
	glBindTexture(GL_TEXTURE_2D, tex_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glBindTexture(GL_TEXTURE_2D, 0);
	{
		GLenum err = glGetError();
		if (err != GL_NO_ERROR) fprintf(stderr, "uploadRgba GL error: 0x%04x\n", err);
	}
	return true;
}

} // namespace newcore
