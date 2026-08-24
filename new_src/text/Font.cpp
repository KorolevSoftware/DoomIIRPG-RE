#include "text/Font.h"

#include "render/gl/SpriteBatch.h"

namespace newcore {

bool Font::upload(const std::vector<uint8_t>& indices, int w, int h,
	const std::vector<uint16_t>& palette) {
	return tex_.uploadIndexed(indices, w, h, palette, true);
}

void Font::getCharIndices(char c, int* index1, int* index2) {
	uint8_t chr = (uint8_t)c;

	int i1 = (int)chr - '!';
	int i2 = 0;

	switch (chr) {
	case 0x85: i1 = 94; break;
	case 0x8b: i1 = 101; break;
	case 0x8c: i1 = 142; break;
	case 0x8d: i1 = 100; break;
	case 0x99: i1 = 107; break;
	case 0x9c: i1 = 143; break;
	case 0xa1: i1 = 120; break;
	case 0xa2: i1 = 127; break;
	case 0xa9: i1 = 106; break;
	case 0xbc: case 0xbd: case 0xbe:
		i1 = chr - 0xbc + 108; break;
	case 0xbf: i1 = 119; break;
	case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5:
		i2 = 121 + (chr - 0xc0); i1 = 128; break;
	case 0xc8: case 0xc9: case 0xca: case 0xcb:
		i2 = 121 + (chr - 0xc8);
		if (chr == 0xcb) ++i2;
		i1 = 129; break;
	case 0xcc: case 0xcd: case 0xce: case 0xcf:
		i2 = 121 + (chr - 0xcc);
		if (chr == 0xcf) ++i2;
		i1 = 130; break;
	case 0xd1: i1 = 134; i2 = 122; break;
	case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6:
		i2 = 121 + (chr - 0xd2); i1 = 131; break;
	case 0xd8: i1 = 46; i2 = 14; break;
	case 0xd9: case 0xda: case 0xdb: case 0xdc:
		i2 = 121 + (chr - 0xd9);
		if (chr == 0xdc) ++i2;
		i1 = 132; break;
	case 0xdd: i1 = 133; i2 = 122; break;
	case 0xdf: i1 = 117; break;
	case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
		i2 = 121 + (chr - 0xe0); i1 = 135; break;
	case 0xe7: i1 = 116; break;
	case 0xe8: case 0xe9: case 0xea: case 0xeb:
		i2 = 121 + (chr - 0xe8);
		if (chr == 0xeb) ++i2;
		i1 = 136; break;
	case 0xec: case 0xed: case 0xee: case 0xef:
		i2 = 121 + (chr - 0xec);
		if (chr == 0xef) ++i2;
		i1 = 137; break;
	case 0xf0: i1 = 118; break;
	case 0xf1: i1 = 140; i2 = 124; break;
	case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6:
		i2 = 121 + (chr - 0xf2); i1 = 138; break;
	case 0xf9: case 0xfa: case 0xfb: case 0xfc:
		i2 = 121 + (chr - 0xf9);
		if (chr == 0xfc) ++i2;
		i1 = 139; break;
	case 0xfd: i1 = 141; i2 = 122; break;
	case 0xff: i1 = 141; i2 = 125; break;
	default: break;
	}

	*index1 = i1;
	*index2 = i2;
}

void Font::drawChar(SpriteBatch& batch, char c, int x, int y, int rotateMode,
	uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
	if (!tex_.valid()) return;

	int index1, index2;
	getCharIndices(c, &index1, &index2);
	// Out-of-range guard from src/Graphics.cpp:641-652. index1 must compare
	// unsigned so negative results (space 0x20 -> -1) also take the '?' cell
	// instead of sampling outside the sheet.
	if ((uint32_t)index1 > 143u || index2 < 0 || index2 > 143) {
		index1 = 30; // '?' fallback
		index2 = 0;
	}

	// Glyph at column (index & 15), row (index >> 4) of the 16-col grid.
	batch.draw(tex_, (index1 & 15) * kGlyphW, index1 & 240, kGlyphW, kGlyphH,
		x, y, kGlyphW, kGlyphH, rotateMode, r / 255.f, g / 255.f, b / 255.f, a / 255.f);
	if (index2 != 0) {
		batch.draw(tex_, (index2 & 15) * kGlyphW, index2 & 240, kGlyphW, kGlyphH,
			x, y, kGlyphW, kGlyphH, rotateMode, r / 255.f, g / 255.f, b / 255.f, a / 255.f);
	}
}

} // namespace newcore