#ifndef NEW_CORE_MAYACAMERA_H
#define NEW_CORE_MAYACAMERA_H

#include "domain/world/MapData.h"

namespace newcore {

// Resolved camera state. x/y/z are render units (map units << 4, cutscenes-camera.md
// §3 "converted <<4 before render"); pitch/yaw/roll are 0..1023 angle units.
struct MayaPose {
	int x = 0, y = 0, z = 0;
	int pitch = 0, yaw = 0, roll = 0;
};

// Scripted cutscene camera playback (docs/original-code/cutscenes-camera.md §3).
// Faithful port of the MayaCamera::Update interpolation (src/MayaCamera.cpp:46-148):
// keys sampled at sampleRate ms steps driven by i8 delta tables, shortest-arc
// angle interpolation, and -2 channels inheriting the player pose captured at
// setup (src/ScriptThread.cpp:192-228).
class MayaCamera {
public:
	// Binds camera entry camIdx and captures the player pose used to resolve
	// -2 sentinel channels. Returns false for an out-of-range camIdx.
	bool setup(const MapData& map, int camIdx, const MayaPose& playerPose);

	// Pose for the key segment starting at activeKeyIndex after elapsedMs,
	// elapsed clamped to that key's duration. Stateless per call; the caller
	// advances activeKeyIndex itself (original NextKey, src/MayaCamera.cpp:36-44).
	void update(int activeKeyIndex, int elapsedMs);

	// Resolved pose of this camera's final key.
	MayaPose snapLast() const;

	// Snap pose half (src/MayaCamera.cpp:338-357): pose := static values of
	// key relKey (-2 resolved against the captured player pose, xyz <<4),
	// WITHOUT touching the active key index or the clock.
	void snap(int relKey);

	const MayaPose& pose() const { return m_pose; }
	bool valid() const { return m_map != nullptr; }

private:
	// Channel order as stored in MapData::MayaCamera::keys (channel-major,
	// src/Game.cpp:606-609) and in m_agg / OFS_MAYAKEY_* (src/Game.cpp:577-583).
	enum { CH_X, CH_Y, CH_Z, CH_PITCH, CH_YAW, CH_ROLL, CH_MS };

	void resolveKey(int k);
	void resolvedKey(int k, int* agg, bool* inherit) const;
	int keyField(int absKey, int ch) const;
	bool hasTweens(int k) const;
	int estNumTweens(int absKey) const;
	int tweenSample(int k, int ch, int step) const;
	int getAngleDifference(int a, int b) const;
	void getKeyOfs(int nextAbsKey, int* delta) const;
	void staticPose();
	void interpolate(const int* delta, int num, int den);

	const MapData* m_map = nullptr;
	const MapData::MayaCamera* m_cam = nullptr;
	int m_camIdx = -1;
	int m_keyOffset = 0;   // absolute index of this camera's key 0
	int m_chanOfs[6] = {}; // own channel byte offsets inside tweens
	int m_basePrev[6] = {}; // cross-camera running tween base (src/Game.cpp:615-621)
	MayaPose m_player;     // player pose captured at setup (-2 sentinel source)
	MayaPose m_pose;
	int m_agg[6] = {};     // aggregate components, CH_ order (src/MayaCamera.cpp:243-251)
	bool m_inherit[6] = {}; // per-channel inherit flags for X,Y,Z,PITCH,YAW
};

} // namespace newcore

#endif // NEW_CORE_MAYACAMERA_H
