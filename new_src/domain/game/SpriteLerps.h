#ifndef NEW_DOMAIN_GAME_SPRITELERPS_H
#define NEW_DOMAIN_GAME_SPRITELERPS_H

#include <cstdint>
#include <vector>

namespace newcore {

class EntityDb;
class MapData;
class ScriptVM;
struct ScriptThread;

// Peer subsystem owning the script sprite lerp pool: the LERP* opcode
// interpolation of sprite X/Y/Z/scale, the walk-state anim writer and the
// internal ms clock they run on (spec 2026-08-26-decomposition §P2-GD).
// Moved verbatim out of Game. See docs/original-code/lerp-opcodes.md.
class SpriteLerps {
public:
	// Non-owning views on the world. db owns the entity array (it feeds
	// findEntityBySprite) and the 1024 tile heads (spec §P2-GF); vm resumes
	// the owner thread of a completed blocking lerp.
	struct Env {
		EntityDb* db = nullptr;
		MapData* map = nullptr;
		ScriptVM* vm = nullptr;
	};

	static constexpr int kMaxLerpSprites = 16;   // pool size (src/Game.cpp:3028)

	// One active script lerp; subset of legacy LerpSprite (save/load,
	// TRUNC/chicken/door/secret tails are not ported).
	struct SpriteLerp {
		// Runtime flag bits (src/Enums.h:293-295 subset).
		static constexpr int kFlagAsync = 0x1;
		static constexpr int kFlagAnimatingEffect = 0x2;
		static constexpr int kFlagParabola = 0x4;
		static constexpr int kFlagAutoFace = 0x800;   // LS_FLAG_AUTO_FACE (src/Enums.h:295)

		int hSprite = 0;          // sprite+1; 0 = free slot (src/Game.cpp:3030)
		ScriptThread* ownerThread = nullptr;
		int startTime = 0;        // ms on the SpriteLerps::update clock (clockMs)
		int travelTime = 0;       // ms
		int srcX = 0, srcY = 0, srcZ = 0;
		int dstX = 0, dstY = 0, dstZ = 0;
		int srcScale = 64, dstScale = 64;
		int height = 0;           // parabola arc peak (canvas z units)
		int flags = 0;            // SpriteLerp::kFlag* bits
		int dist = 0;             // Euclidean move length, canvas units

		// dist = isqrt((dx²+dy²)<<8) >> 8 (src/LerpSprite.cpp:48); feeds the
		// distance-driven walk phase ((1+(p*dist>>12))&3, one cycle per tile).
		void calcDist();
	};

	// Wiring + per-level reset (called from Game::loadEntities).
	void init(const Env& env);

	// Pool tick (src/Game.cpp:2985-3021), called by Game::update before the
	// door animations: advances the clock, ticks every active lerp and
	// resumes the owner threads of the completed blocking ones.
	void update(int dtMs);

	// Pool lookup mirroring allocLerpSprite (src/Game.cpp:3028-3066): reuse
	// the slot of a still-active lerp for the same sprite, else take a free
	// one. Exhaustion logs instead of the legacy Error(36) fatal.
	SpriteLerp* allocLerpSprite(ScriptThread* thread, int sprite, bool block);

	// Single tick (src/Game.cpp:2855-2956): writes S_X/S_Y/S_Z/S_SCALEFACTOR
	// with p=(elapsed<<16)/(travelTime<<8), snaps + frees on completion.
	// Returns 3 when completed, 4 when not started yet, else 0.
	int updateLerpSprite(SpriteLerp* ls);

	// snapLerpSprites(sprite) (src/Game.cpp:1149-1166): force-complete every
	// active lerp of one sprite.
	void snap(int sprite);

	// Stored-vs-baked S_Z bias: legacy postProcessSprites bakes terrain
	// height (-32 for z-sprites) into stored S_Z at load and the legacy
	// renderer consumes it directly (src/Render.cpp:2459-2467,1424); our
	// renderer keeps stored Z raw-relative and re-adds terrain per frame
	// (World3D.cpp:547-556). Lerps interpolate in legacy baked space:
	// baked = stored + spriteZBias, stored = baked - spriteZBias.
	int spriteZBias(int sprite, int x, int y) const;

	// Internal ms clock for lerp start/elapsed math; advanced by update(dtMs).
	int clockMs() const { return lerpClock_; }

	// 1024-entry fixed-point sine table for the parabola arc (sin << 14);
	// wired once after construction.
	void setSinTable(const std::vector<int32_t>* sinTable) { sinTable_ = sinTable; }

	// View angle used by the walk-state writer's front/back chooser. Fed by
	// GameContext each tick (maya pose during cinematics, else player view) —
	// reproduces legacy reading app->render->viewAngle, i.e. the previous
	// frame's view (src/Game.cpp:2910).
	void setLerpViewAngle(int a) { lerpViewAngle_ = a; }

	// Move vector -> 8-direction angle * 128 with ±32 thresholds
	// (src/Game.cpp:3596-3628, b=true).
	static int vecToDir(int dx, int dy);

private:
	void freeLerpSprite(SpriteLerp* ls);       // completion snap + slot free (src/Game.cpp:3078-3243, subset)

	Env env_;

	// Script sprite lerp pool (legacy Game::lerpSprites[16]).
	SpriteLerp spriteLerps_[kMaxLerpSprites];
	int lerpClock_ = 0;
	int lerpViewAngle_ = 0;                // last render view angle (setLerpViewAngle)
	const std::vector<int32_t>* sinTable_ = nullptr;
};

} // namespace newcore

#endif // NEW_DOMAIN_GAME_SPRITELERPS_H
