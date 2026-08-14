#ifndef NEW_RENDER_GRAPHICS2D_H
#define NEW_RENDER_GRAPHICS2D_H

#include <cstdint>

#include "render/gl/Texture.h"

namespace newcore {

class SpriteBatch;
class Font;
class Text;

// High-level 2D drawing API in canvas (logical) coordinates.
// Mirrors the operations used by the game UI: image blits with sub-rects and
// rotation modes, solid fills, alpha/tint, and a scissor clip region.
class Graphics2D {
public:
	// Anchor flags used by drawString (match legacy Graphics anchors).
	static constexpr int kAnchorNone = 0;
	static constexpr int kAnchorHCenter = 1;
	static constexpr int kAnchorVCenter = 2;
	static constexpr int kAnchorLeft = 4;
	static constexpr int kAnchorRight = 8;
	static constexpr int kAnchorTop = 16;
	static constexpr int kAnchorBottom = 32;
	static constexpr int kAnchorRotate = 64; // draw rotated 270 degrees

	Graphics2D() = default;

	void setBatch(SpriteBatch* batch) { batch_ = batch; }

	// Clip region in canvas coordinates (inclusive bounds like the legacy API).
	void setClip(int x, int y, int w, int h);
	void clearClip();

	// Solid fill. Color components are 0..255.
	void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

	// 1px outline rectangle (same color semantics as fillRect).
	void drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

	// Bresenham line (1px, inclusive endpoints).
	void drawLine(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

	// Draws a sub-rect of a texture. rotateMode: 0=none, 1=90, 2=180, 3=270,
	// 4=mirror, 5..7=mirror+rotate, 8=flip vertically. tint 0..255.
	void drawImage(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
		int dstX, int dstY, int dstW, int dstH, int rotateMode = 0,
		uint8_t tintR = 255, uint8_t tintG = 255, uint8_t tintB = 255, uint8_t alpha = 255);

	// Legacy-style anchored blits. posX/posY is the anchor point; the flags
	// (kAnchor* values) select which corner/edge coincides with it, matching
	// legacy Graphics::drawImage/drawRegion (renderMode ignored for now).
	void drawImage(const Texture& tex, int posX, int posY, int flags,
		int rotateMode = 0, int renderMode = 0);
	void drawRegion(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
		int posX, int posY, int flags, int rotateMode = 0, int renderMode = 0);

	// Scaled draw with uniform scale around the anchor.
	void drawImageScaled(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
		int dstX, int dstY, float scale, int rotateMode = 0);

	// Draws a string using the fixed-width font. flags are kAnchor* values.
	// '^N' sets the per-char color (0..9 -> kCharColors), '\'+letter draws a
	// buff icon, '|'/'\\n' advance to the next line. lineHeight is the per-line
	// vertical step (default 16).
	void drawString(const Font& font, const Text& text, int x, int y,
		int flags = 0, int lineHeight = 16, int strBeg = 0, int strEnd = -1);

	// Draws a buff icon from the Icons_Buffs sheet (30x30 per icon).
	void drawBuffIcon(int iconIndex, int x, int y, int flags = 0,
		uint8_t tintR = 255, uint8_t tintG = 255, uint8_t tintB = 255, uint8_t alpha = 255);

	// Sets the Icons_Buffs texture used by drawString's '\'+letter icons.
	void setBuffIconTexture(const Texture& buffs) { buffIcons_ = &buffs; }

private:
	SpriteBatch* batch_ = nullptr;
	const Texture* buffIcons_ = nullptr;
	int clipX_ = 0, clipY_ = 0, clipW_ = 0, clipH_ = 0;
	bool hasClip_ = false;
};

} // namespace newcore

#endif // NEW_RENDER_GRAPHICS2D_H