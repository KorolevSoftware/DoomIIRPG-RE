#include <stdexcept>

#include "Span.h"
#include "TinyGL.h"

static constexpr int COLOR_LSB = 0x10101;
static constexpr int COLOR_2LSB = 0x30303;
static constexpr int COLOR_3LSB = 0x70707;
static constexpr int COLOR_MSB = 0x808080;
static constexpr int MASK_LSB = 0xFEFEFE;
static constexpr int MASK_2LSB = 0xFCFCFC;
static constexpr int MASK_3LSB = 0xF8F8F8;
static constexpr int ALPHA_LSBMASK = 0x10821;

static inline uint16_t blend25_565(uint16_t dst, uint16_t src) {
	return ((dst & 0xF7DE) >> 1) + ((src & 0xC718) >> 2);
}

static inline uint16_t blend50_565(uint16_t dst, uint16_t src) {
	return ((dst & 0xF7DE) >> 1) + ((src & 0xF7DE) >> 1);
}

static inline uint16_t add565(uint16_t dst, uint16_t src) {
	int r = (dst >> 11)       + (src >> 11);
	int g = ((dst >> 5) & 63) + ((src >> 5) & 63);
	int b = (dst & 31)        + (src & 31);
	return (uint16_t)(
		(r > 31 ? 31 : r) << 11 |
		(g > 63 ? 63 : g) << 5  |
		(b > 31 ? 31 : b)
	);
}

static inline uint16_t sub565(uint16_t dst, uint16_t src) {
	int r = (dst >> 11)       - (src >> 11);
	int g = ((dst >> 5) & 63) - ((src >> 5) & 63);
	int b = (dst & 31)        - (src & 31);
	return (uint16_t)(
		(r < 0 ? 0 : r) << 11 |
		(g < 0 ? 0 : g) << 5  |
		(b < 0 ? 0 : b)
	);
}


void spanNoDraw(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
}

void spanNoDrawStretch(uint16_t* pixels, int32_t n2, int32_t n3, int32_t n4, int32_t i, TinyGL* tinyGL) {
}

void spanTransparent(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	int pMask = tinyGL->paletteTransparentMask;
	int16_t b;
	while (i-- > 0) {
		b = textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)];
		if (b != pMask) { *pixels = spanPalette[b]; }
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanTransparentDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	int pMask = tinyGL->paletteTransparentMask;
	int16_t b;
	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		b = textureBase[(n3 >> tShift & tMask) | n2];
		if (b != pMask) { *pixels = spanPalette[b]; }
		n3 += n6;
		pixels += n4;
	}
}

void spanTransparentDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	int pMask = tinyGL->paletteTransparentMask;
	int16_t b;
	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		b = textureBase[(n2 >> sShift & sMask) | n3];
		if (b != pMask) { *pixels = spanPalette[b]; }
		n2 += n5;
		pixels += n4;
	}
}

void spanTransparentStretch(uint16_t* pixels, int32_t n2, int32_t n3, int32_t n4, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	while (i-- > 0) {
		*pixels = spanPalette[textureBase[n2 >> 12]];
		n2 += n3;
		pixels += n4;
	}
}

void spanBlend50Transparent(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	while (i-- > 0) {
		*pixels = blend50_565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanBlend50TransparentDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = blend50_565(*pixels, spanPalette[textureBase[(n3 >> tShift & tMask) | n2]]);
		n3 += n6;
		pixels += n4;
	}
}

void spanBlend50TransparentDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = blend50_565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | n3]]);
		n2 += n5;
		pixels += n4;
	}
}

void spanBlend50TransparentStretch(uint16_t* pixels, int32_t n2, int32_t n3, int32_t n4, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	while (i-- > 0) {
		*pixels = blend50_565(*pixels, spanPalette[textureBase[n2 >> 12]]);
		n2 += n3;
		pixels += n4;
	}
}

void spanAddTransparent(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanAddTransparentDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[(n3 >> tShift & tMask) | n2]]);
		n3 += n6;
		pixels += n4;
	}
}

void spanAddTransparentDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | n3]]);
		n2 += n5;
		pixels += n4;
	}
}

void spanAddTransparentStretch(uint16_t* pixels, int32_t n2, int32_t n3, int32_t n4, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;

	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[n2 >> 12]]);
		n2 += n3;
		pixels += n4;
	}
}

void spanSubTransparent(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanSubTransparentDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[(n3 >> tShift & tMask) | n2]]);
		n3 += n6;
		pixels += n4;
	}
}

void spanSubTransparentDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | n3]]);
		n2 += n5;
		pixels += n4;
	}
}

void spanSubTransparentStretch(uint16_t* pixels, int32_t n2, int32_t n3, int32_t n4, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;

	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[n2 >> 12]]);
		n2 += n3;
		pixels += n4;
	}
}

void spanPerfTexture(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	while (i-- > 0) {
		*pixels = blend50_565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanPerfTextureStretch(uint16_t* pixels, int32_t n2, int32_t n3, int32_t n4, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	while (i-- > 0) {
		*pixels = blend50_565(*pixels, spanPalette[textureBase[n2 >> 12]]);
		n2 += n3;
		pixels += n4;
	}
}

void spanTexture(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	while (i-- > 0) {
		*pixels = spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]];
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanTextureDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = spanPalette[textureBase[(n3 >> tShift & tMask) | n2]];
		n3 += n6;
		pixels += n4;
	}
}

void spanTextureDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = spanPalette[textureBase[(n2 >> sShift & sMask) | n3]];
		n2 += n5;
		pixels += n4;
	}
}

void spanBlend25Texture(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	while (i-- > 0) {
		*pixels = blend25_565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanBlend25TextureDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = blend25_565(*pixels, spanPalette[textureBase[(n3 >> tShift & tMask) | n2]]);
		n3 += n6;
		pixels += n4;
	}
}

void spanBlend25TextureDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;
	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = blend25_565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | n3]]);
		n2 += n5;
		pixels += n4;
	}
}

void spanAddTexture(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanAddTextureDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[(n3 >> tShift & tMask) | n2]]);
		n3 += n6;
		pixels += n4;
	}
}

void spanAddTextureDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = add565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | n3]]);
		n2 += n5;
		pixels += n4;
	}
}

void spanSubTexture(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | (n3 >> tShift & tMask)]]);
		n2 += n5;
		n3 += n6;
		pixels += n4;
	}
}

void spanSubTextureDT(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n2 = (n2 >> sShift & sMask);
	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[(n3 >> tShift & tMask) | n2]]);
		n3 += n6;
		pixels += n4;
	}
}

void spanSubTextureDS(uint16_t* pixels, int32_t n2, int32_t n3, uint32_t n4, int32_t n5, int32_t n6, int32_t i, TinyGL* tinyGL) {
	uint8_t* textureBase = tinyGL->textureBase;
	uint16_t* spanPalette = tinyGL->spanPalette;
	int sShift = tinyGL->sShift;
	int sMask = tinyGL->sMask;
	int tShift = tinyGL->tShift;
	int tMask = tinyGL->tMask;

	n3 = (n3 >> tShift & tMask);
	while (i-- > 0) {
		*pixels = sub565(*pixels, spanPalette[textureBase[(n2 >> sShift & sMask) | n3]]);
		n2 += n5;
		pixels += n4;
	}
}
