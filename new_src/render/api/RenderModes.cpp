#include "render/api/RenderModes.h"

#include <cstdio>

namespace newcore {

// One row per legacy Render::RENDER_* value: the whole gles::SetupTexture
// renderMode switch (src/GLES.cpp:615-706) plus its fog decision
// (src/GLES.cpp:709-715), see docs/original-code/rendering.md §8.2 and ADR 0019.
//
// The additive rows deliberately keep SrcAlpha as the SOURCE factor: the
// glBlendFunc(GL_ONE, GL_ONE) variant is commented out at src/GLES.cpp:650. So
// dst += Atex*(k*Ctex) and the transparent key still cuts the sprite out instead
// of filling its bounding box with an opaque blob.
// Row 7 keeps the GL meaning dst = dst*(1 - Csrc); the software rasterizer's
// true clamped subtract (src/Span.cpp:230-244) is a divergence of the original
// itself and we are porting the GL path (ADR 0019).
// Rows 8/11/13 are unreachable for a 3D sprite (src/GLES.cpp:697-705) — see
// renderModeNeedsWarning below.
const RenderModeRow kRenderModes[kRenderModeCount] = {
	/* 0  NORMAL   */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 1.00f }, true  }, // src/GLES.cpp:625-635
	/* 1  BLEND25  */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 0.25f }, true  }, // :636-641
	/* 2  BLEND50  */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 0.50f }, true  }, // :642-647
	/* 3  ADD      */ { BlendFactor::SrcAlpha, BlendFactor::One,              { 1.00f, 1.00f, 1.00f, 1.00f }, false }, // :648-653
	/* 4  ADD75    */ { BlendFactor::SrcAlpha, BlendFactor::One,              { 0.75f, 0.75f, 0.75f, 1.00f }, false }, // :654-659
	/* 5  ADD50    */ { BlendFactor::SrcAlpha, BlendFactor::One,              { 0.50f, 0.50f, 0.50f, 1.00f }, false }, // :660-665
	/* 6  ADD25    */ { BlendFactor::SrcAlpha, BlendFactor::One,              { 0.25f, 0.25f, 0.25f, 1.00f }, false }, // :666-671
	/* 7  SUB      */ { BlendFactor::Zero,     BlendFactor::OneMinusSrcColor, { 1.00f, 1.00f, 1.00f, 1.00f }, false }, // :672-678
	/* 8  UNK      */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 1.00f }, true  }, // no case: assert(0) at :702-705
	/* 9  PERF     */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 0.50f }, true  }, // :679-685
	/* 10 NONE     */ { BlendFactor::Zero,     BlendFactor::One,              { 1.00f, 1.00f, 1.00f, 1.00f }, false }, // :686-690
	/* 11 (unused) */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 1.00f }, true  }, // no case: assert(0) at :702-705
	/* 12 BLEND75  */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 0.75f }, true  }, // :691-696
	/* 13 SPECIALA */ { BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, { 1.00f, 1.00f, 1.00f, 1.00f }, true  }, // :697-701, alpha pinned to 1
};

namespace {

// Modes 8/11 have no case in the legacy switch (default: assert(0),
// src/GLES.cpp:702-705) and 13 needs canvas->blendSpecialAlpha (:697-701), for
// which the rewrite has no analog. None of them is reachable from a 3D sprite,
// but a shipping frame must not crash: we draw the table row above and log each
// distinct offending value once (spec 2026-09-01-blend-modes G1.4).
void warnRenderModeOnce(int mode) {
	static bool logged[kRenderModeCount + 1] = {}; // last slot: everything out of range
	const int slot = (mode >= 0 && mode < kRenderModeCount) ? mode : kRenderModeCount;
	if (logged[slot]) return;
	logged[slot] = true;
	if (slot == kRenderModeCount) {
		fprintf(stderr, "World3D: unknown render mode %d out of range, using RENDER_NORMAL\n", mode);
	} else {
		fprintf(stderr, "World3D: unknown render mode %d has no legacy GL path, "
			"drawing it as alpha blend\n", mode);
	}
	fflush(stderr);
}

} // namespace

bool renderModeNeedsWarning(int mode) {
	return mode == 8 || mode == 11 || mode == 13;
}

int clampRenderMode(int mode) {
	if (mode < 0 || mode >= kRenderModeCount || renderModeNeedsWarning(mode)) {
		warnRenderModeOnce(mode);
		if (mode < 0 || mode >= kRenderModeCount) mode = 0; // RENDER_NORMAL
	}
	return mode;
}

} // namespace newcore
