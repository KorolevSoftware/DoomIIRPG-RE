#ifndef NEW_GRAPHICS_IMAGE_H
#define NEW_GRAPHICS_IMAGE_H

#include <cstdint>
#include <memory>
#include <vector>

namespace newcore {

// Palette-indexed image loaded from a BMP file (4 or 8 bits per pixel).
// Matches the format expected by the render backend: indices + RGB565 palette.
class Image {
public:
	Image() = default;

	int width() const { return width_; }
	int height() const { return height_; }
	int depth() const { return depth_; }
	bool transparent() const { return transparent_; }

	// Palette indices, width*height, top-left origin.
	const std::vector<uint8_t>& indices() const { return indices_; }
	// RGB565 palette entries.
	const std::vector<uint16_t>& palette() const { return palette_; }

	// The image owns nothing else; texure handle is managed by the renderer.
	int64_t textureHandle() const { return textureHandle_; }
	void setTextureHandle(int64_t h) { textureHandle_ = h; }

private:
	friend class BmpImageLoader;
	int width_ = 0;
	int height_ = 0;
	int depth_ = 0;
	bool transparent_ = false;
	std::vector<uint8_t> indices_;
	std::vector<uint16_t> palette_;
	int64_t textureHandle_ = -1;
};

using ImagePtr = std::shared_ptr<Image>;

} // namespace newcore

#endif // NEW_GRAPHICS_IMAGE_H