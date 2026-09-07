#ifndef NEW_RENDER_API_TEXTUREID_H
#define NEW_RENDER_API_TEXTUREID_H

#include <cstdint>

namespace newcore {

// Opaque texture handle. 0 == invalid. Values are backend-private.
struct TextureId {
	uint32_t value = 0;

	bool valid() const { return value != 0; }
	bool operator==(TextureId o) const { return value == o.value; }
	bool operator!=(TextureId o) const { return value != o.value; }
};

// Creation-time intents, NOT API state.
enum class TextureFlags : uint32_t {
	None = 0,
	// Palette entries equal to the game's transparent color 0xF81F become fully
	// transparent (docs/original-code/image-formats.md §0, §1.2).
	TransparentKey = 1u << 0,
	// The caller's UVs may leave [0,1]: world walls/floors/ceilings and the sky.
	// GL uses GL_REPEAT; SDL splits geometry per tile (ADR 0021).
	Tiled = 1u << 1,
};

constexpr TextureFlags operator|(TextureFlags a, TextureFlags b) {
	return static_cast<TextureFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr bool hasFlag(TextureFlags set, TextureFlags f) {
	return (static_cast<uint32_t>(set) & static_cast<uint32_t>(f)) != 0;
}

} // namespace newcore

#endif // NEW_RENDER_API_TEXTUREID_H
