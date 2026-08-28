#include "ui/ViewWeapon.h"

#include <algorithm>
#include <cmath>

#include "io/Media.h"
#include "render/Camera3D.h"
#include "render/Graphics2D.h"
#include "render/World3D.h"

namespace newcore {

// First-person view weapon (spec combat-stage1 §6.2; legacy Combat::drawWeapon
// GL path src/Combat.cpp:621-844). The legacy anchors (196 + wpX + shakeX,
// 131 - (wpY + shakeY)) and the v12 box are VIEWPORT-relative inputs
// (src/Render.cpp:358-373), not final canvas pixels: draw2DSprite is no 1:1
// blit. On the GL path it builds a world-space billboard 400 units in front of
// the eye (offset 5*((view[k]&~31)+8*(view[k]>>5))>>8, src/Render.cpp:343-421)
// and lets the world projection magnify it (gles::DrawWorldSpaceSpriteLine,
// src/GLES.cpp:483-547); the software fallback at src/Render.cpp:419 is dead
// here, its scaleFactor *= 1.35f being an approximation of the same factor.
//
// Screen offsets from the billboard math (src/GLES.cpp:483-547):
//   dpx = (x - vpW/2) * m[0] / 12800
//   dpy = (y + v12 - vpH/2) * m[5] * vpH / (vpW * 12800)
// so the quad is scaled about the viewport centre (239,124) = canvas (240,131)
// by Kx = m[0]/12800, Ky = m[5]*vpH/2 / ((vpW/2) * 12800), read from the LIVE
// projection (Camera3D::projectionInt) instead of being hard-coded: the
// gameplay projection is buildProjectionMatrix(290,150) -> m[0]=17098,
// m[5]=-33053 (Kx=1.33578, Ky=1.33973, the 0.3% anisotropy is real integer
// aspect truncation 150.46 -> 150), but a cinematic renders at fov 315/290 and
// would otherwise mismatch. m[5] is stored negated by the GLES BeginFrame
// adjustment (new_src/render/Camera3D.cpp:88), hence the magnitude.
// Viewport centre in viewport space and the canvas point it maps to.
constexpr int kWeaponVpCx = 239, kWeaponVpCy = 124;
constexpr int kWeaponCanvasCx = 240, kWeaponCanvasCy = 131;

// Projects one legacy view-weapon quad (viewport-space top-left x, top edge
// y, box size v12) onto the canvas and blits it clipped to the world band
// (1,7,478,248) — the viewport that clips the billboard on the GL path.
// Source texels are always the top-left 176x176 of the 256x256 weapon media
// (src/GLES.cpp:539-542).
// Graphics2D::setClip (a GL scissor) is deliberately NOT used here: the band
// clip must trim the source sub-rect as well as the destination quad, which a
// scissor cannot do on magnified art. Both are trimmed by hand below.
static void drawWeaponQuad(Graphics2D& g, const Texture& tex, int x, int y, int v12,
	uint8_t tint, float magX, float magY) {
	constexpr int kSrc = 176;
	const float fxl = kWeaponCanvasCx + (x - kWeaponVpCx) * magX;
	const float fxr = kWeaponCanvasCx + (x + v12 - kWeaponVpCx) * magX;
	const float fyb = kWeaponCanvasCy + (y + v12 - kWeaponVpCy) * magY;
	const float fyt = fyb - v12 * magY;
	const int xl = (int)std::floor(fxl), xr = (int)std::floor(fxr);
	const int yt = (int)std::floor(fyt), yb = (int)std::floor(fyb);
	const int dw = xr - xl, dh = yb - yt;
	if (dw <= 0 || dh <= 0) return;
	// Band clip: the quad and its source rect are trimmed together (see above).
	const int cx0 = std::max(xl, 1), cx1 = std::min(xr, 1 + 478);
	const int cy0 = std::max(yt, 7), cy1 = std::min(yb, 7 + 248);
	if (cx1 <= cx0 || cy1 <= cy0) return;
	// Source edges in float + rounding: integer division here squeezed the
	// cropped art by ~0.5% vertically.
	const float su = (float)kSrc / (float)dw, sv = (float)kSrc / (float)dh;
	const int sx0 = (int)std::lround((cx0 - xl) * su);
	const int sx1 = (int)std::lround((cx1 - xl) * su);
	const int sy0 = (int)std::lround((cy0 - yt) * sv);
	const int sy1 = (int)std::lround((cy1 - yt) * sv);
	if (sx1 <= sx0 || sy1 <= sy0) return;
	g.drawImage(tex, sx0, sy0, sx1 - sx0, sy1 - sy0,
		cx0, cy0, cx1 - cx0, cy1 - cy0, 0, tint, tint, tint, 255);
}

void ViewWeapon::init(const Env& env) {
	env_ = env;
}

void ViewWeapon::draw(Graphics2D& g, const Camera3D& cam, const ViewWeaponModel& m) {
	// The gameplay-state gate (src/Combat.cpp:706-708 state check) and the
	// "a cinematic owns the view" suppression live at the single call site
	// now (spec §4). Zoom skip (src/Canvas.cpp:1348 isZoomedIn) is implicit
	// — no zoom system yet.
	// The Player / Game::combat / Tables reads, the wpinfo lookup, the pose
	// lerp and the two flash gates moved to
	// GameContext::buildViewWeaponModel (spec 2026-08-27-ui-layer §6): the
	// model's `visible` carries both former early returns (no weapon owned,
	// missing wpinfo row).
	if (!m.visible) return;
	const int w = m.weapon;

	// Legacy anchors (196,131) stay VIEWPORT-relative (draw2DSprite,
	// rendering.md §6.2); the viewport origin (1,7) enters through the
	// centre mapping in drawWeaponQuad, together with the projection
	// magnification.
	int scrX = 196;                        // 480/2 - 44                (:627)
	int scrY = 131;                        // 320/2 - 29                (:628)
	// weaponDown lower/raise lerp absent -> skip scrY += LOWEREDWEAPON_Y(38)
	// (:672-674); shiftWeapon/LOWERWEAPON_TIME=200 stays unported.
	// Per-weapon scrY bias (:679-693).
	scrY += (w == 1) ? 3 : (w == 2) ? 10 : (w >= 3 && w <= 6) ? 12 : 0;

	// Canvas shake; m.shakeY is already the legacy -|sy| (src/Combat.cpp:709).
	const int x = scrX + m.poseX + m.shakeX;                   // (:786)
	const int y = scrY - (m.poseY + m.shakeY);                 // (:787)

	// Muzzle flash FIRST so the gun art draws on top (:826-834): tile 1
	// frame 3 at (+flashX+40, +flashY+40), 88x88 (scaleFactor 0x8000).
	// renderMode 5 = RENDER_ADD50: additive blend with colour (.5,.5,.5,1)
	// (src/GLES.cpp:660-664, src/Combat.cpp:833).
	// Magnification from the projection actually in use this frame.
	const int* proj = cam.projectionInt();
	const float magX = (float)proj[0] / 12800.f;
	const float magY = (float)std::abs(proj[5]) * (float)kWeaponVpCy /
		((float)kWeaponVpCx * 12800.f);

	if (m.muzzleFlash) {   // both legacy gates applied by the producer (:741,:826)
		const Texture* ftex = env_.world->spriteTexture(*env_.media, m.flashTile, 3);
		if (ftex != nullptr) {
			g.setBlendMode(1);
			drawWeaponQuad(g, *ftex, x + m.flashX + 40, y + m.flashY + 40, 88, 128,
				magX, magY);
			g.setBlendMode(0);
		}
	}

	// Weapon art frame 0; the chainsaw-return/weapons 8+13 frame-1 rule
	// (:822-825) is dead on the map00 rifle route. Sentry-bot stack
	// (:797-813), weapon 14 (:814-820) and weapon 9 underlay (:835-837)
	// deferred (hero-choice doc §B.3 exclusions).
	const Texture* tex = env_.world->spriteTexture(*env_.media, m.weaponTile, 0);
	if (tex != nullptr) {
		drawWeaponQuad(g, *tex, x, y, 176, 255, magX, magY);
	}
}

} // namespace newcore
