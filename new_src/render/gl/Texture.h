#ifndef NEW_RENDER_GL_TEXTURE_H
#define NEW_RENDER_GL_TEXTURE_H

#include <cstdint>
#include <vector>

#include "render/gl/GlCommon.h"

namespace newcore {

// A GPU texture. Two modes:
//  - indexed: R8 index texture + RGBA8 palette LUT (palette-indexed rendering),
//  - rgba:    RGBA8 texture for color data (fade, test patterns, ...).
// Owns its GL objects (move-only).
class Texture {
public:
	enum class Format { Indexed, Rgba };

	Texture() = default;
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	Texture(Texture&& other) noexcept;
	Texture& operator=(Texture&& other) noexcept;

	// Uploads an indexed image: indices (w*h bytes) + palette (RGB565).
	// If transparent is true, palette entries equal to the game's transparent
	// color 0xF81F are made fully transparent in the LUT (matches legacy
	// pixel-level transform). If repeat is true, GL_REPEAT wrapping is used
	// (needed for tiled world textures whose UVs leave [0,1]).
	bool uploadIndexed(const std::vector<uint8_t>& indices, int w, int h,
		const std::vector<uint16_t>& palette, bool transparent = false,
		bool repeat = false);
	// Uploads RGBA8 data.
	bool uploadRgba(const std::vector<uint8_t>& rgba, int w, int h);

	void bind(GLenum unit = GL_TEXTURE0) const;
	void bindPalette(GLenum unit) const;

	Format format() const { return format_; }
	int width() const { return width_; }
	int height() const { return height_; }
	GLuint id() const { return tex_; }
	GLuint paletteId() const { return palette_; }
	bool valid() const { return tex_ != 0; }

	void destroy();

private:
	bool uploadIndices(const std::vector<uint8_t>& indices, int w, int h, bool repeat);
	bool uploadPalette(const std::vector<uint16_t>& rgb565, bool transparent);

	GLuint tex_ = 0;
	GLuint palette_ = 0;
	Format format_ = Format::Indexed;
	int width_ = 0;
	int height_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_GL_TEXTURE_H