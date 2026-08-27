#include "render/Graphics2D.h"

#include <algorithm>

#include "render/gl/SpriteBatch.h"
#include "text/Font.h"
#include "text/Text.h"

namespace newcore {

static constexpr int kHardSpace = 0xA0; // hard space (non-breaking)

void Graphics2D::setBlendMode(int mode) {
	if (batch_) batch_->setBlendMode(mode);
}

void Graphics2D::setClip(int x, int y, int w, int h) {
	clipX_ = x;
	clipY_ = y;
	clipW_ = w;
	clipH_ = h;
	hasClip_ = true;

	if (batch_) {
		// Convert canvas clip to scissor coords. The batch flushes first so the
		// previous draw calls land, then we enable scissor.
		// (Scissor applied by the caller frame; here we just record the region.)
	}
}

void Graphics2D::clearClip() {
	hasClip_ = false;
	clipW_ = 0;
	clipH_ = 0;
}

void Graphics2D::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (batch_) batch_->fillRect(x, y, w, h, r / 255.f, g / 255.f, b / 255.f, a / 255.f);
}

void Graphics2D::drawImage(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
	int dstX, int dstY, int dstW, int dstH, int rotateMode,
	uint8_t tintR, uint8_t tintG, uint8_t tintB, uint8_t alpha) {
	if (batch_) batch_->draw(tex, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH,
		rotateMode, tintR / 255.f, tintG / 255.f, tintB / 255.f, alpha / 255.f);
}

void Graphics2D::drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (!batch_ || w <= 0 || h <= 0) return;
	fillRect(x, y, w, 1, r, g, b, a);
	fillRect(x, y + h - 1, w, 1, r, g, b, a);
	fillRect(x, y, 1, h, r, g, b, a);
	fillRect(x + w - 1, y, 1, h, r, g, b, a);
}

void Graphics2D::drawLine(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (!batch_) return;
	int dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx - dy;
	for (;;) {
		fillRect(x0, y0, 1, 1, r, g, b, a);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx) { err += dx; y0 += sy; }
	}
}

void Graphics2D::drawImageScaled(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
	int dstX, int dstY, float scale, int rotateMode) {
	if (!batch_) return;
	int dstW = (int)(srcW * scale);
	int dstH = (int)(srcH * scale);
	batch_->draw(tex, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, rotateMode);
}

void Graphics2D::drawImage(const Texture& tex, int posX, int posY, int flags,
	int rotateMode, int renderMode) {
	if (!batch_ || !tex.valid()) return;
	int x = posX;
	int y = posY;
	if (flags & kAnchorHCenter) x = posX - tex.width() / 2;
	else if (flags & kAnchorRight) x = posX - tex.width();
	if (flags & kAnchorVCenter) y = posY - tex.height() / 2;
	else if (flags & kAnchorBottom) y = posY - tex.height();
	batch_->draw(tex, 0, 0, tex.width(), tex.height(), x, y, tex.width(), tex.height(), rotateMode);
}

void Graphics2D::drawRegion(const Texture& tex, int srcX, int srcY, int srcW, int srcH,
	int posX, int posY, int flags, int rotateMode, int renderMode) {
	if (!batch_ || !tex.valid()) return;
	int x = posX;
	int y = posY;
	if (flags & kAnchorHCenter) x = posX - srcW / 2;
	else if (flags & kAnchorRight) x = posX - srcW;
	if (flags & kAnchorVCenter) y = posY - srcH / 2;
	else if (flags & kAnchorBottom) y = posY - srcH;
	batch_->draw(tex, srcX, srcY, srcW, srcH, x, y, srcW, srcH, rotateMode);
}

void Graphics2D::drawString(const Font& font, const Text& text, int x, int y,
	int flags, int lineHeight, int strBeg, int strEnd) {
	if (!batch_ || !font.valid()) return;

	int rotateMode = (flags & kAnchorRotate) ? 3 : 0;

	if (strEnd == -1) strEnd = text.length();
	if (strEnd == 0) return;

	int stringWidth = text.getStringWidth(strBeg, strBeg + strEnd, false);
	int n6 = (rotateMode == 3) ? y : x;

	// Horizontal anchor.
	if (flags & kAnchorRight) {
		if (rotateMode == 3) {
			y += stringWidth;
			n6 = y;
		} else {
			x -= stringWidth;
			n6 = x;
		}
	} else if (flags & kAnchorHCenter) {
		if (rotateMode == 3) {
			y += stringWidth / 2 + (stringWidth & 1);
		} else {
			x -= stringWidth / 2 + (stringWidth & 1);
		}
	}
	// Vertical anchor.
	if (flags & kAnchorBottom) {
		if (rotateMode == 3) {
			x += lineHeight;
		} else {
			y -= lineHeight;
		}
	} else if (flags & kAnchorVCenter) {
		if (rotateMode == 3) {
			x -= lineHeight * text.getNumLines() / 2;
		} else {
			y -= lineHeight * text.getNumLines() / 2;
		}
	}

	int n7 = strBeg + std::min(strEnd, text.length() - strBeg);
	int curColor = 0;
	for (int i = strBeg; i < n7; ++i) {
		char ch = text.charAt(i);
		if (ch == '\n' || ch == '|') {
			if (rotateMode == 3) {
				x += lineHeight;
			} else {
				y += lineHeight;
			}
			if (flags & kAnchorHCenter) {
				int w = text.getStringWidth(i + 1, n7, false) / 2;
				if (rotateMode == 3) {
					y = n6 + w;
				} else {
					x = n6 - w;
				}
			} else {
				if (rotateMode == 3) {
					y = n6;
				} else {
					x = n6;
				}
			}
		} else if (ch == ' ' || (uint8_t)ch == kHardSpace) {
			if (rotateMode == 3) {
				y -= Font::kAdvance;
			} else {
				x += Font::kAdvance;
			}
		} else if (ch == '\\' && i + 1 < n7) {
			char ch2 = text.charAt(++i);
			int icon = ch2 - 'A';
			if (icon < 0 || icon >= 15) {
				font.drawChar(*batch_, ch2, x, y, rotateMode);
				if (rotateMode == 3) y -= Font::kAdvance;
				else x += Font::kAdvance;
			} else {
				if (buffIcons_) {
					uint8_t cr = 255, cg = 255, cb = 255, ca = 255;
					if (curColor != 0) {
						uint32_t col = Font::kCharColors[curColor];
						ca = (uint8_t)(col >> 24);
						cr = (uint8_t)(col >> 16);
						cg = (uint8_t)(col >> 8);
						cb = (uint8_t)col;
					}
					drawBuffIcon(icon, x, y, 0, cr, cg, cb, ca);
				}
				if (rotateMode == 3) y -= Font::kAdvance;
				else x += Font::kAdvance * 3;
			}
		} else if (ch == '^' && i + 1 < n7) {
			char d = text.charAt(++i);
			if (d >= '0' && d <= '9') {
				curColor = d - '0';
				continue;
			}
			font.drawChar(*batch_, '^', x, y, rotateMode);
			if (rotateMode == 3) y -= Font::kAdvance;
			else x += Font::kAdvance;
		} else {
			uint8_t cr = 255, cg = 255, cb = 255, ca = 255;
			if (curColor != 0) {
				uint32_t col = Font::kCharColors[curColor];
				ca = (uint8_t)(col >> 24);
				cr = (uint8_t)(col >> 16);
				cg = (uint8_t)(col >> 8);
				cb = (uint8_t)col;
			}
			font.drawChar(*batch_, ch, x, y, rotateMode, cr, cg, cb, ca);
			if (rotateMode == 3) y -= Font::kAdvance;
			else x += Font::kAdvance;
		}
	}
}

void Graphics2D::drawBuffIcon(int iconIndex, int x, int y, int flags, uint8_t tintR, uint8_t tintG, uint8_t tintB, uint8_t alpha) {
	if (!batch_ || !buffIcons_ || !buffIcons_->valid()) return;
	int px = x;
	int py = y;
	if (flags & kAnchorHCenter) px -= 15;
	else if (flags & kAnchorRight) px -= 30;
	if (flags & kAnchorVCenter) py -= 15;
	else if (flags & kAnchorBottom) py -= 30;
	batch_->draw(*buffIcons_, 0, iconIndex * 30, 30, 30, px, py, 30, 30, 0,
		tintR / 255.f, tintG / 255.f, tintB / 255.f, alpha / 255.f);
}

} // namespace newcore