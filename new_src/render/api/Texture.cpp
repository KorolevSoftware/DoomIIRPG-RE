#include "render/api/Texture.h"

#include "render/api/TextureStore.h"

namespace newcore {

Texture::~Texture() {
	destroy();
}

Texture::Texture(Texture&& other) noexcept
	: store_(other.store_), id_(other.id_),
	  width_(other.width_), height_(other.height_) {
	other.store_ = nullptr;
	other.id_ = TextureId{};
	other.width_ = other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
	if (this != &other) {
		destroy();
		store_ = other.store_;
		id_ = other.id_;
		width_ = other.width_;
		height_ = other.height_;
		other.store_ = nullptr;
		other.id_ = TextureId{};
		other.width_ = other.height_ = 0;
	}
	return *this;
}

void Texture::destroy() {
	if (store_ && id_.valid()) store_->destroy(id_);
	store_ = nullptr;
	id_ = TextureId{};
	width_ = height_ = 0;
}

bool Texture::uploadIndexed(TextureStore& store, const std::vector<uint8_t>& indices,
	int w, int h, const std::vector<uint16_t>& palette, bool transparent, bool tiled) {
	if (w <= 0 || h <= 0 || indices.size() < (size_t)w * h) return false;

	TextureFlags flags = TextureFlags::None;
	if (transparent) flags = flags | TextureFlags::TransparentKey;
	if (tiled) flags = flags | TextureFlags::Tiled;

	const TextureId id = store.createIndexed(indices.data(), w, h,
		palette.data(), (int)palette.size(), flags);
	if (!id.valid()) return false;

	destroy();
	store_ = &store;
	id_ = id;
	width_ = w;
	height_ = h;
	return true;
}

bool Texture::uploadRgba(TextureStore& store, const std::vector<uint8_t>& rgba,
	int w, int h) {
	if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return false;

	const TextureId id = store.createRgba(rgba.data(), w, h, TextureFlags::None);
	if (!id.valid()) return false;

	destroy();
	store_ = &store;
	id_ = id;
	width_ = w;
	height_ = h;
	return true;
}

} // namespace newcore
