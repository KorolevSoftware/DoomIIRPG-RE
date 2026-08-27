#include "core/CinematicCamera.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapData.h"

namespace newcore {

// ---- cinematic camera (docs/original-code/cutscenes-camera.md §1-§3) ----

void CinematicCamera::init(const Env& env) {
	env_ = env;
}

void CinematicCamera::startCinematic(int camIdx) {
	Player& p = *env_.player;
	MayaPose pose;                     // map units; <<4 applied post-inherit
	// Dest-field capture like legacy setupCamera (src/ScriptThread.cpp:192-
	// 196): equal to the view fields in every current flow (triggers fire
	// from finishMovement after arrival) but faithful to a mid-walk start.
	pose.x = p.destX;
	pose.y = p.destY;
	pose.z = p.destZ;
	pose.yaw = p.destAngle & 0x3FF;    // viewPitch has no cinematic counterpart yet
	if (!maya_.setup(*env_.map, camIdx, pose)) {
		std::fprintf(stderr, "[camera] bad camIdx %d\n", camIdx);
		return;
	}
	cameraCamIdx_ = camIdx;
	cameraView_ = true;                // legacy activeCameraView (src/ScriptThread.cpp:186)
	activeCameraKey_ = -1;             // bound-not-started; first ADV_CAMERAKEY NextKeys onto key 0
	cameraStartTime_ = *env_.gameTime; // legacy activeCameraTime (src/Canvas.cpp:1092)
	cinUnpauseTime_ = *env_.gameTime + 1000; // skip lockout (src/ScriptThread.cpp:227-228)
	skipCinematic_ = false;
	// State guard like legacy (:406-408): a chained STARTCINEMATIC must not
	// re-clear subtitles/pending input via the enter hook.
	if (env_.host->state() != StateId::Camera) env_.host->requestState(StateId::Camera);
}

void CinematicCamera::nextKey() {
	// MayaCamera::NextKey (src/MayaCamera.cpp:36-44): restart the clock at
	// now and move to the next key. The ONLY place the key index advances
	// besides Snap's counting tail (:365-368). NOTE: no end guard here —
	// legacy happily parks PAST the last key; completion is the boundary
	// clock's job (tickClock). An early finish inside the parking
	// opcode would flush-resume the very thread still being parked
	// mid-dispatch (its IP still on the argument byte), desyncing the VM.
	cameraStartTime_ = *env_.gameTime;
	++activeCameraKey_;
}

void CinematicCamera::advanceCameraKey(ScriptThread* t, int resumeCount) {
	// EV_ADV_CAMERAKEY park half (src/ScriptThread.cpp:690-702): unpauseTime=-1
	// parks the thread; each completed key ticks the countdown and the flush
	// resumes it (resumeKeyWaits/flushParkedThreads).
	t->unpauseTime = -1;
	cameraResumeList_.push_back(t);
	cameraResumeCounts_.push_back(resumeCount);
	// The opcode tail calls NextKey() immediately: the remainder of the
	// current key is truncated and the next one starts now
	// (src/ScriptThread.cpp:695, src/MayaCamera.cpp:36-44).
	nextKey();
}

int CinematicCamera::keyDuration(int key) const {
	// MS channel, channel-major keys[numKeys*CH_MS + k] (src/Game.cpp:606-609),
	// masked &0xFFFF like MayaCamera::Update (src/MayaCamera.cpp:59).
	const MapData::MayaCamera& cam = env_.map->mayaCameras[cameraCamIdx_];
	return cam.keys[cam.numKeys * 6 + key] & 0xFFFF;
}

void CinematicCamera::tickCameraState() {
	// ST_CAMERA per-frame order (src/Canvas.cpp:949-958): camera Update ->
	// updateLerpSprites -> updateView. Lerps tick in the globals section
	// (before the clock — legacy runs them after; within-tick difference
	// only). Door lerps keep animating and parked threads keep ticking —
	// input is what's parked.
	if (skipCinematic_) {
		skipCinematicNow();
		return;
	}
	// TEMP [dbg] auto-skip (headless verification only; remove with the
	// D2R_AUTOTEST driver): past the skip lockout, end the cinematic like a
	// user key press. Inactive unless the env var is set.
	static int autoSkip = -1;
	if (autoSkip < 0) autoSkip = std::getenv("D2R_AUTOTEST") != nullptr ? 1 : 0;
	if (autoSkip != 0 && *env_.gameTime >= cinUnpauseTime_ + 1000) skipCinematic_ = true;
	tickClock();
}

void CinematicCamera::tickClock() {
	// Armed window (cameraView_ && key -1) runs no boundary engine and never
	// auto-completes — legacy Update is skipped while activeCameraKey == -1
	// (src/Canvas.cpp:949-952).
	if (!cameraView_ || activeCameraKey_ < 0 || cameraCamIdx_ < 0 ||
	    cameraCamIdx_ >= (int)env_.map->mayaCameras.size()) return;
	const MapData::MayaCamera& cam = env_.map->mayaCameras[cameraCamIdx_];

	// Key boundaries (src/MayaCamera.cpp:72-77): once a key's duration
	// elapsed, Update does NOT advance anything on its own — with an
	// outstanding ADV_CAMERAKEY park it calls Snap, else it returns and the
	// pose holds at the boundary. Snap (:335-374) snaps the pose to the NEXT
	// key's static value WITHOUT charging its duration or advancing the key,
	// ticks the countdown, and either resumes the expired thread (whose own
	// next ADV_CAMERAKEY then starts the following key fresh from now via
	// NextKey) or, still counting, auto-advances one key (:365-368). The
	// clock is only ever restarted by a NextKey, never by boundary
	// accumulation — that distinction is what keeps chained handshakes from
	// eating keys.
	if (activeCameraKey_ >= (int)cam.numKeys) {
		// Parked PAST the last key (final ADV_CAMERAKEY; legacy NextKey has
		// no end guard, src/MayaCamera.cpp:36-44): hold the end pose until
		// the last key's duration elapses from the restart, then complete —
		// Snap tail (:376-397) resumes the parked thread via flush.
		if (*env_.gameTime - cameraStartTime_ >= keyDuration((int)cam.numKeys - 1))
			finishCinematic();
		return;
	}
	if (!cameraResumeList_.empty() &&
	    *env_.gameTime - cameraStartTime_ >= keyDuration(activeCameraKey_)) {
		if (activeCameraKey_ + 1 >= cam.numKeys) {
			finishCinematic();         // park on the last boundary -> complete (:358)
			return;
		}
		maya_.snap(activeCameraKey_ + 1);      // Snap pose half (:338-357)
		if (!resumeKeyWaits() && !cameraResumeList_.empty()) {
			nextKey();                         // counting tail NextKey (:365-368)
		}
	}
	int keyMs = keyDuration(activeCameraKey_);
	int elapsed = (int)(*env_.gameTime - cameraStartTime_);
	if (elapsed < keyMs) maya_.update(activeCameraKey_, elapsed);
}

bool CinematicCamera::resumeKeyWaits() {
	// One completed key ticks every parked ADV_CAMERAKEY count; expired ones
	// resume in legacy callThreads[] pool order (src/MayaCamera.cpp:359-370).
	// Returns true if any thread was resumed — the caller then must NOT also
	// take Snap's auto-advance branch (legacy single keyThread slot returns
	// right after run(), :359-364).
	std::vector<size_t> done;
	for (size_t i = 0; i < cameraResumeCounts_.size(); ++i) {
		if (--cameraResumeCounts_[i] <= 0) done.push_back(i);
	}
	if (done.empty()) return false;
	std::sort(done.begin(), done.end(), [this](size_t a, size_t b) {
		return env_.vm->indexOf(cameraResumeList_[a]) < env_.vm->indexOf(cameraResumeList_[b]);
	});
	// Remove expired entries FIRST, collecting the threads: resumeThread()
	// runs scripts synchronously and a resumed script may immediately hit the
	// next ADV_CAMERAKEY, re-parking and reallocating these very vectors —
	// erasing afterwards used stale indices against the reallocated buffers
	// (user-visible SIGSEGV in vector::erase).
	std::vector<ScriptThread*> toResume;
	for (size_t i : done) toResume.push_back(cameraResumeList_[i]);
	for (size_t i = done.size(); i-- > 0;) {
		cameraResumeList_.erase(cameraResumeList_.begin() + done[i]);
		cameraResumeCounts_.erase(cameraResumeCounts_.begin() + done[i]);
	}
	for (ScriptThread* t : toResume) env_.vm->resumeThread(t);
	return true;
}

void CinematicCamera::finishCinematic() {
	// End-of-keys Snap (src/MayaCamera.cpp:376-397). The state restore only
	// applies while still inside ST_CAMERA — legacy returns early when the
	// canvas moved on (:380-382), e.g. a cinematic started mid-load whose
	// ST_PLAYING tail overwrite must not be re-restored.
	const MapData::MayaCamera& cam = env_.map->mayaCameras[cameraCamIdx_];
	maya_.snap(cam.numKeys - 1);   // hold the final-key pose
	activeCameraKey_ = -1;
	cameraView_ = false;           // Snap tail clears the flag (src/MayaCamera.cpp:379)
	// Snap tail: ST_CAMERA -> ST_PLAYING, never a pre-camera restore
	// (src/MayaCamera.cpp:380-384).
	if (env_.host->state() == StateId::Camera) env_.host->requestState(StateId::Playing);
	// Snap-tail view reset (src/MayaCamera.cpp:396-397). Guarded: an
	// unsettled angle means a scripted rotation is in flight and arrival
	// handling owns the refresh.
	env_.player->viewPitch = 0;                 // viewPitch = destPitch = 0 (:396)
	if (env_.player->viewAngle == env_.player->destAngle)
		env_.player->startRotation();           // startRotation(true) step-vector refresh (:397)
	flushParkedThreads(false);     // single run() per thread, like Snap's resume (:390-394)
}

void CinematicCamera::skipCinematicNow() {
	// Game::skipCinematic analog (src/Game.cpp:2507-2544): snap the remaining
	// keys, fast-forward the parked threads with the huge-timestamp analog,
	// immediate state restore. Subtitles/particles/fade have no rewrite
	// counterpart yet.
	const MapData::MayaCamera& cam = env_.map->mayaCameras[cameraCamIdx_];
	maya_.snap(cam.numKeys - 1);   // Snap the remaining keys' end pose
	activeCameraKey_ = -1;
	cameraView_ = false;           // skip force-ends the view (src/Game.cpp:2507-2544)
	if (env_.host->state() == StateId::Camera)
		env_.host->requestState(StateId::Playing);   // Snap tail (:380-384)
	flushParkedThreads(true);
}

void CinematicCamera::flushParkedThreads(bool force) {
	// Drain cameraResumeList_ in legacy callThreads[] pool order. force=false:
	// one run() per parked thread (Snap resume). force=true: skip fast-forward
	// — legacy attemptResume(gameTime + 0x40000000) falls through every WAIT
	// (src/Game.cpp:2507-2544); the rewrite reuses the public run()-based
	// resume path and expires whatever re-parks the thread, capped as a
	// runaway guard.
	std::vector<ScriptThread*> list;
	list.swap(cameraResumeList_);
	cameraResumeCounts_.clear();
	std::sort(list.begin(), list.end(), [this](ScriptThread* a, ScriptThread* b) {
		return env_.vm->indexOf(a) < env_.vm->indexOf(b);
	});
	for (ScriptThread* t : list) {
		if (!force) {
			env_.vm->resumeThread(t);
			continue;
		}
		int r = 2;
		for (int guard = 0; guard < 64 && r == 2; ++guard) {
			r = env_.vm->resumeThread(t);
			if (r == 2) t->unpauseTime = 0;    // huge-timestamp analog: expire any re-park
		}
		if (r == 2) std::fprintf(stderr, "[camera] fast-forward cap hit, thread left parked\n");
	}
}

const MayaPose* CinematicCamera::renderPose() {
	// Single source of truth for "a cinematic owns the world this frame":
	// legacy routes EVERYTHING through MayaCamera::Render then
	// (src/MovementController.cpp:518-523) — projection fov, view-weapon
	// suppression and the cockpit overlay. Dialogs run inside cinematics
	// (Dialog -> Camera on close, GameContext.cpp:44-46), so keying any of the
	// three off `state == StateId::Camera` alone would drift.
	if (!active() || cameraCamIdx_ >= (int)env_.map->mayaCameras.size()) return nullptr;
	// Cinematic takeover (legacy MayaCamera::Render, src/MayaCamera.cpp:
	// 302-310): the maya pose IS the view.
	// Legacy re-evaluates the pose EVERY rendered frame from absolute
	// elapsed (src/Canvas.cpp:951, src/MovementController.cpp:519);
	// sampling only in the 15 ms tick beats against the display refresh
	// and reads as periodic slow-motion waves.
	// Ticks own the key state (tickClock: boundaries, Snap
	// holds, resume handshake); render samples the interpolation of the
	// current key at display rate. Past a key's duration the pose HOLDS
	// (no update) exactly like the tick path.
	int64_t camElapsed = *env_.gameTime - cameraStartTime_;
	// Armed window must NOT resample: key -1 has no duration channel and
	// would overwrite the setup pose legacy renders statically
	// (src/Canvas.cpp:950 skips Update at -1).
	if (activeCameraKey_ >= 0 &&
	    activeCameraKey_ < (int)env_.map->mayaCameras[cameraCamIdx_].numKeys &&
	    camElapsed < keyDuration(activeCameraKey_))
		maya_.update(activeCameraKey_, (int)camElapsed);
	return &maya_.pose();
}

} // namespace newcore
