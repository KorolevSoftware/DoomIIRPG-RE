#ifndef NEW_CORE_CINEMATICCAMERA_H
#define NEW_CORE_CINEMATICCAMERA_H

#include <cstdint>
#include <vector>

#include "core/GameStates.h"
#include "core/MayaCamera.h"

namespace newcore {

class MapData;
class Player;
struct ScriptThread;
class ScriptVM;

// Scripted cutscene playback: the maya clock, the key boundaries and the
// ADV_CAMERAKEY thread parking (docs/original-code/cutscenes-camera.md §1-§3).
// Two-field model restored from legacy (spec 2026-08-26-camera-key0 §1):
// cameraView_ binds the maya view (legacy activeCameraView,
// src/ScriptThread.cpp:186; cleared by the Snap tail, src/MayaCamera.cpp:379),
// while activeCameraKey_ keeps its pure legacy meaning. Phases: idle
// (false/-1) -> armed post-STARTCINEMATIC (true/-1; static key-0 pose renders,
// boundary engine off) -> playing (true/>=0) -> finished or skipped (false/-1).
class CinematicCamera {
public:
	struct Env {
		MapData* map = nullptr;
		Player* player = nullptr;
		ScriptVM* vm = nullptr;
		StateHost* host = nullptr;
		const int64_t* gameTime = nullptr;
	};

	void init(const Env& env);

	// EV_STARTCINEMATIC target: bind map camera camIdx, capture the player
	// pose for -2 sentinel channels, enter ST_CAMERA with the 1 s skip
	// lockout (src/ScriptThread.cpp:183-228, :400-411).
	void startCinematic(int camIdx);

	// EV_ADV_CAMERAKEY target: park the caller via the -1 protocol and queue
	// it on cameraResumeList_; Snap resumes it after `resumeCount` more key
	// completions (src/ScriptThread.cpp:690-702, src/MayaCamera.cpp:359-370).
	void advanceCameraKey(ScriptThread* t, int resumeCount);

	// Camera activity probe ("cinematic bound"): true whenever the maya view
	// is armed or playing, regardless of key index — the isCameraActive
	// analog (src/Game.cpp:3297-3299). "Started" = activeCameraKey_ >= 0.
	bool active() const { return cameraView_ && cameraCamIdx_ >= 0; }
	bool started() const { return activeCameraKey_ >= 0; }

	void tickClock();        // key-boundary engine (src/MayaCamera.cpp:46-148, :335-374)
	// ST_CAMERA per-frame order (src/Canvas.cpp:949-958). Returns false when
	// the skip path was taken: the caller must then NOT tick the HUD, matching
	// the pre-decomposition early return in GameContext::tickCamera().
	bool tickCameraState();

	void requestSkip() { skipCinematic_ = true; }
	void clearSkipRequest() { skipCinematic_ = false; }
	bool skipGateOpen() const { return *env_.gameTime >= cinUnpauseTime_; }

	// Display-rate resample + pose, nullptr when no cinematic owns the view.
	const MayaPose* renderPose();
	const MayaPose& pose() const { return maya_.pose(); }

private:
	void nextKey();                    // MayaCamera::NextKey (src/MayaCamera.cpp:36-44)
	bool resumeKeyWaits();             // ADV_CAMERAKEY countdown step (src/MayaCamera.cpp:359-370);
	                                   // true = a thread resumed (no auto-advance then)
	void finishCinematic();            // end-of-keys Snap (src/MayaCamera.cpp:376-397)
	void skipCinematicNow();           // Game::skipCinematic (src/Game.cpp:2507-2544)
	void flushParkedThreads(bool force); // cameraResumeList_ drain, pool-index order
	int keyDuration(int key) const;    // MS channel of the active camera's key

	Env env_;

	MayaCamera maya_;
	int cameraCamIdx_ = -1;
	bool cameraView_ = false;           // legacy activeCameraView (src/ScriptThread.cpp:186)
	int activeCameraKey_ = -1;          // -1 = bound-not-started (src/ScriptThread.cpp:188)
	int64_t cameraStartTime_ = 0;      // legacy activeCameraTime (src/Canvas.cpp:1092)
	int64_t cinUnpauseTime_ = 0;       // skip soft-key gate (src/ScriptThread.cpp:227-228)
	bool skipCinematic_ = false;
	std::vector<ScriptThread*> cameraResumeList_; // ADV_CAMERAKEY park list (§2)
	std::vector<int> cameraResumeCounts_;         // remaining key completions per entry
};

} // namespace newcore

#endif // NEW_CORE_CINEMATICCAMERA_H
