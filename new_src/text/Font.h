#ifndef NEW_TEXT_FONT_H
#define NEW_TEXT_FONT_H

#include <cstdint>
#include <vector>

#include "render/gl/Texture.h"

namespace newcore {

class SpriteBatch;

// Renders the game's fixed-width font (Font.bmp). Each glyph is 12x16 and the
// sheet is laid out as a 16-column grid. Text layout (anchors, per-char color,
// buff icons) mirrors the legacy Graphics::drawString.
class Font {
public:
	static constexpr int kGlyphW = 12;
	static constexpr int kGlyphH = 16;
	static constexpr int kAdvance = 9; // cursor advance per glyph

	// 0xAARRGGBB; index matches the game's '^N' char-color codes.
	static constexpr uint32_t kCharColors[12] = {
		0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFF8BBC5D,
		0xFF0000FF, 0xFF3180C3, 0xFFFFAFCC, 0xFFFF7F00,
		0xFF7F7F7F, 0xFF000000, 0xFF3F3F3F, 0xFFBFBFBF };

	Font() = default;
	~Font() = default;

	Font(const Font&) = delete;
	Font& operator=(const Font&) = delete;

	// Uploads the indexed font bitmap.
	bool upload(const std::vector<uint8_t>& indices, int w, int h,
		const std::vector<uint16_t>& palette);

	bool valid() const { return tex_.valid(); }
	const Texture& texture() const { return tex_; }

	// Maps a game char to glyph sheet index(es). index2==0 means single glyph.
	// Mirrors Localization::getCharIndices.
	static void getCharIndices(char c, int* index1, int* index2);

	// Draws one glyph at (x,y) in canvas coords. rotateMode 0..8.
	void drawChar(SpriteBatch& batch, char c, int x, int y, int rotateMode,
		uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255) const;

private:
	Texture tex_;
};

} // namespace newcore

#endif // NEW_TEXT_FONT_H