#include "render/gl/Texture.h"

namespace newcore {

Texture::~Texture() {
	destroy();
}

Texture::Texture(Texture&& other) noexcept
	: tex_(other.tex_), palette_(other.palette_), format_(other.format_),
	  width_(other.width_), height_(other.height_) {
	other.tex_ = 0;
	other.palette_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
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

void Texture::destroy() {
	if (tex_) glDeleteTextures(1, &tex_);
	if (palette_) glDeleteTextures(1, &palette_);
	tex_ = 0;
	palette_ = 0;
	width_ = height_ = 0;
}

void Texture::bind(GLenum unit) const {
	glActiveTexture(unit);
	glBindTexture(GL_TEXTURE_2D, tex_);
}

void Texture::bindPalette(GLenum unit) const {
	glActiveTexture(unit);
	glBindTexture(GL_TEXTURE_2D, palette_);
}

bool Texture::uploadIndices(const std::vector<uint8_t>& indices, int w, int h, bool repeat) {
	if (w <= 0 || h <= 0 || indices.size() < (size_t)w * h) return false;

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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, indices.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

bool Texture::uploadPalette(const std::vector<uint16_t>& rgb565, bool transparent) {
	if (rgb565.size() < 256) return false;

	// Convert RGB565 palette -> RGBA8 for the LUT texture.
	std::vector<uint8_t> rgba(256 * 4);
	for (int i = 0; i < 256; ++i) {
		uint16_t c = rgb565[i];
		uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
		uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
		uint8_t b5 = (uint8_t)(c & 0x1F);
		rgba[i * 4 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
		rgba[i * 4 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
		rgba[i * 4 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
		if (transparent && c == 0xF81F) {
			rgba[i * 4 + 0] = 0;
			rgba[i * 4 + 1] = 0;
			rgba[i * 4 + 2] = 0;
			rgba[i * 4 + 3] = 0;
		} else {
			rgba[i * 4 + 3] = 0xFF;
		}
	}

	glGenTextures(1, &palette_);
	glBindTexture(GL_TEXTURE_2D, palette_);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

bool Texture::uploadIndexed(const std::vector<uint8_t>& indices, int w, int h,
	const std::vector<uint16_t>& palette, bool transparent, bool repeat) {
	if (!uploadIndices(indices, w, h, repeat)) return false;
	if (!uploadPalette(palette, transparent)) {
		destroy();
		return false;
	}
	return true;
}

bool Texture::uploadRgba(const std::vector<uint8_t>& rgba, int w, int h) {
	if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return false;

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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	{
		GLenum err = glGetError();
		if (err != GL_NO_ERROR) fprintf(stderr, "uploadRgba GL error: 0x%04x\n", err);
	}
	return true;
}

} // namespace newcore