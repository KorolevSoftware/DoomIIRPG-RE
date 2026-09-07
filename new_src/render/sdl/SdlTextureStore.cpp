#include "render/sdl/SdlTextureStore.h"

#include <cstdio>

#include "render/api/PixelConvert.h"

namespace newcore {

SdlTextureStore::SdlTextureStore() {
	slots_.emplace_back();
}

SdlTextureStore::~SdlTextureStore() {
	if (!sdlVideoAlive()) return;
	for (Entry& e : slots_) {
		if (e.tex != nullptr) SDL_DestroyTexture(e.tex);
		e.tex = nullptr;
	}
}

bool SdlTextureStore::initialize(SDL_Renderer* renderer) {
	if (renderer == nullptr) {
		std::fprintf(stderr, "SdlTextureStore: no SDL_Renderer\n");
		return false;
	}
	renderer_ = renderer;

	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(renderer_, &info) != 0) {
		std::fprintf(stderr, "SdlTextureStore: SDL_GetRendererInfo failed: %s\n", SDL_GetError());
		return false;
	}

	// The whole point of this log: on another platform a driver that does not
	// advertise ARGB8888 must be visible at once, because SDL then keeps an
	// intermediate surface and converts on every upload (spec §4.2.0 fact 2).
	std::fprintf(stdout, "sdl render: driver '%s', flags 0x%08x, %u texture formats\n",
		info.name, info.flags, info.num_texture_formats);
	bool nativeFormat = false;
	for (Uint32 i = 0; i < info.num_texture_formats; ++i) {
		const Uint32 fmt = info.texture_formats[i];
		std::fprintf(stdout, "  %s\n", SDL_GetPixelFormatName(fmt));
		if (fmt == kPixelFormat) nativeFormat = true;
	}
	std::fprintf(stdout, "sdl render: textures expanded to %s (SDL2 has no indexed "
		"textures: \"Palettized textures are not supported\")\n",
		SDL_GetPixelFormatName(kPixelFormat));
	if (!nativeFormat) {
		std::fprintf(stderr, "sdl render: WARNING driver '%s' does not advertise %s; "
			"SDL will convert on every upload\n",
			info.name, SDL_GetPixelFormatName(kPixelFormat));
	}
	std::fflush(stdout);
	return true;
}

TextureId SdlTextureStore::allocSlot() {
	if (!freeSlots_.empty()) {
		const uint32_t slot = freeSlots_.back();
		freeSlots_.pop_back();
		slots_[slot] = Entry{};
		return TextureId{ slot };
	}
	slots_.emplace_back();
	return TextureId{ (uint32_t)(slots_.size() - 1) };
}

void SdlTextureStore::freeSlot(TextureId id) {
	slots_[id.value] = Entry{};
	freeSlots_.push_back(id.value);
}

TextureId SdlTextureStore::adopt(SDL_Surface* src, TextureFlags flags) {
	// One conversion, done by SDL: it also does the byte swizzle from the
	// RGBA-ordered palette entries of PixelConvert to ARGB8888's memory order.
	SDL_Surface* conv = SDL_ConvertSurfaceFormat(src, kPixelFormat, 0);
	if (conv == nullptr) {
		std::fprintf(stderr, "SdlTextureStore: SDL_ConvertSurfaceFormat failed: %s\n", SDL_GetError());
		return TextureId{};
	}

	SDL_Texture* tex = SDL_CreateTexture(renderer_, kPixelFormat,
		SDL_TEXTUREACCESS_STATIC, conv->w, conv->h);
	if (tex == nullptr) {
		std::fprintf(stderr, "SdlTextureStore: SDL_CreateTexture(%dx%d) failed: %s\n",
			conv->w, conv->h, SDL_GetError());
		SDL_FreeSurface(conv);
		return TextureId{};
	}
	if (SDL_UpdateTexture(tex, nullptr, conv->pixels, conv->pitch) != 0) {
		std::fprintf(stderr, "SdlTextureStore: SDL_UpdateTexture failed: %s\n", SDL_GetError());
		SDL_DestroyTexture(tex);
		SDL_FreeSurface(conv);
		return TextureId{};
	}
	// Nearest is the only filter the game ever wants; BLEND is the default of
	// render mode 0 (the per-batch mode is set right before each draw).
	SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

	const TextureId id = allocSlot();
	Entry& e = slots_[id.value];
	e.tex = tex;
	e.w = conv->w;
	e.h = conv->h;
	e.flags = flags;
	bytes_ += (size_t)e.w * (size_t)e.h * SDL_BYTESPERPIXEL(kPixelFormat);

	SDL_FreeSurface(conv);
	return id;
}

TextureId SdlTextureStore::createIndexed(const uint8_t* indices, int w, int h,
	const uint16_t* palette, int paletteCount, TextureFlags flags) {
	if (renderer_ == nullptr || indices == nullptr || w <= 0 || h <= 0) return TextureId{};

	uint8_t pal8888[1024];
	expandPalette565(palette, paletteCount, hasFlag(flags, TextureFlags::TransparentKey), pal8888);

	SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
		(void*)indices, w, h, 8, w, SDL_PIXELFORMAT_INDEX8);
	if (src == nullptr) {
		std::fprintf(stderr, "SdlTextureStore: INDEX8 surface %dx%d failed: %s\n",
			w, h, SDL_GetError());
		return TextureId{};
	}
	if (src->format->palette == nullptr) {
		std::fprintf(stderr, "SdlTextureStore: INDEX8 surface has no palette\n");
		SDL_FreeSurface(src);
		return TextureId{};
	}

	SDL_Color colors[256];
	for (int i = 0; i < 256; ++i) {
		colors[i].r = pal8888[i * 4 + 0];
		colors[i].g = pal8888[i * 4 + 1];
		colors[i].b = pal8888[i * 4 + 2];
		colors[i].a = pal8888[i * 4 + 3];
	}
	SDL_SetPaletteColors(src->format->palette, colors, 0, 256);
	// Without this the conversion blends against the (opaque) destination and
	// the keyed entries would lose their a = 0 (spec §4.2.2).
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);

	const TextureId id = adopt(src, flags);
	SDL_FreeSurface(src);
	return id;
}

TextureId SdlTextureStore::createRgba(const uint8_t* rgba, int w, int h, TextureFlags flags) {
	if (renderer_ == nullptr || rgba == nullptr || w <= 0 || h <= 0) return TextureId{};

	// The interface's byte order (R,G,B,A in memory) is SDL's ABGR8888.
	SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
		(void*)rgba, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
	if (src == nullptr) {
		std::fprintf(stderr, "SdlTextureStore: RGBA surface %dx%d failed: %s\n",
			w, h, SDL_GetError());
		return TextureId{};
	}
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);

	const TextureId id = adopt(src, flags);
	SDL_FreeSurface(src);
	return id;
}

void SdlTextureStore::destroy(TextureId id) {
	Entry e;
	if (!lookup(id, e)) return;
	bytes_ -= (size_t)e.w * (size_t)e.h * SDL_BYTESPERPIXEL(kPixelFormat);
	if (sdlVideoAlive()) SDL_DestroyTexture(e.tex);
	freeSlot(id);
}

bool SdlTextureStore::query(TextureId id, int& w, int& h) const {
	Entry e;
	if (!lookup(id, e)) return false;
	w = e.w;
	h = e.h;
	return true;
}

size_t SdlTextureStore::textureBytes() const {
	return bytes_;
}

bool SdlTextureStore::lookup(TextureId id, Entry& out) const {
	if (!id.valid() || id.value >= slots_.size()) return false;
	if (slots_[id.value].tex == nullptr) return false;
	out = slots_[id.value];
	return true;
}

} // namespace newcore
