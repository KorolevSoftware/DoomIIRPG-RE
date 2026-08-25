#include "core/MayaCamera.h"

namespace newcore {
namespace {
constexpr int kInheritSentinel = -2; // cutscenes-camera.md §3 (src/MayaCamera.cpp:85-102)
}

bool MayaCamera::setup(const MapData& map, int camIdx, const MayaPose& playerPose) {
	if (camIdx < 0 || camIdx >= static_cast<int>(map.mayaCameras.size())) {
		return false;
	}
	m_map = &map;
	m_camIdx = camIdx;
	m_cam = &map.mayaCameras[camIdx];

	m_keyOffset = 0;
	for (int i = 0; i < camIdx; ++i) {
		m_keyOffset += map.mayaCameras[i].numKeys;
	}
	// Tween byte addressing: each camera's blob is [ch0 bytes][ch1 bytes]..,
	// and stream-relative indices are rebased by the byte counts of all
	// previous cameras per channel (running sum in src/Game.cpp:615-621, :635).
	for (int j = 0; j < 6; ++j) {
		m_chanOfs[j] = 0;
		m_basePrev[j] = 0;
	}
	for (int i = 0; i < camIdx; ++i) {
		for (int j = 0; j < 6; ++j) {
			m_basePrev[j] += map.mayaCameras[i].tweenCounts[j];
		}
	}
	int ofs = 0;
	for (int j = 0; j < 6; ++j) {
		m_chanOfs[j] = ofs;
		ofs += m_cam->tweenCounts[j];
	}

	m_player = playerPose;
	resolveKey(0);
	// Initial pose from key 0, -2 channels resolved then xyz <<4
	// (src/ScriptThread.cpp:198-227).
	m_pose.x = m_agg[CH_X] << 4;
	m_pose.y = m_agg[CH_Y] << 4;
	m_pose.z = m_agg[CH_Z] << 4;
	m_pose.pitch = m_agg[CH_PITCH];
	m_pose.yaw = m_agg[CH_YAW];
	m_pose.roll = m_agg[CH_ROLL];
	return true;
}

void MayaCamera::update(int activeKeyIndex, int elapsedMs) {
	if (!valid() || m_cam->numKeys <= 0) {
		return;
	}
	int k = activeKeyIndex;
	if (k < 0) k = 0;
	if (k > m_cam->numKeys - 1) k = m_cam->numKeys - 1;
	int absKey = m_keyOffset + k;
	int dur = keyField(absKey, CH_MS) & 0xFFFF; // ms channel masked (cutscenes-camera.md §3)
	int e = elapsedMs;
	if (e < 0) e = 0;
	if (e > dur) e = dur;

	resolveKey(k); // resetTweenBase for this key (src/MayaCamera.cpp:243-288)

	int sampleRate = m_cam->sampleRate;
	if (dur <= 0 || sampleRate <= 0 || !hasTweens(k)) {
		staticPose(); // no tween data: raw key values (src/MayaCamera.cpp:78-103)
	} else {
		int n4 = estNumTweens(absKey) - 1; // final window index (src/MayaCamera.cpp:173-179)
		int delta[6];
		int den;
		int num;
		if (n4 < 0 || e < sampleRate) {
			// First partial sample window interpolates from the key value with
			// sample-0 deltas (src/MayaCamera.cpp:107-116).
			for (int j = 0; j < 6; ++j) delta[j] = tweenSample(k, j, 0);
			den = sampleRate;
			num = e;
		} else {
			// Absolute-time form of the incremental window machine
			// (src/MayaCamera.cpp:107-131): w sample windows lie fully before
			// e, so agg absorbs samples s_0..s_{w-1} and the partial window
			// interpolates toward s_w. The final window (w == n4+1, entered
			// once curTween reaches n4 in legacy) targets the next key
			// directly over den = dur - (n4+1)*sampleRate. Accumulating s_w
			// AND aiming at s_{w+1} here instead made every window crossing
			// leap by a whole sample step (visible camera lurch every
			// sampleRate ms).
			int w = e / sampleRate;
			if (w > n4 + 1) w = n4 + 1;
			for (int s = 0; s < w; ++s) {
				for (int j = 0; j < 6; ++j) {
					m_agg[j] += tweenSample(k, j, s);
				}
			}
			num = e - w * sampleRate;
			if (w == n4 + 1) {
				getKeyOfs(absKey + 1, delta);
				den = dur - w * sampleRate;
			} else {
				for (int j = 0; j < 6; ++j) delta[j] = tweenSample(k, j, w);
				den = sampleRate;
			}
		}
		interpolate(delta, num, den); // t = ((elapsed-curTime)<<16)/rate (src/MayaCamera.cpp:131)
	}

	// Inherited channels glide linearly to the next key across the whole key
	// duration unless that key inherits too (src/MayaCamera.cpp:133-148).
	if (dur > 0) {
		int f = (e << 16) / dur;
		int nxt = keyField(absKey + 1, CH_X);
		if (m_inherit[CH_X] && nxt != kInheritSentinel) {
			m_pose.x = ((m_agg[CH_X] << 20) + (f * ((nxt - m_agg[CH_X]) << 4)) + 32768) >> 16;
		}
		nxt = keyField(absKey + 1, CH_Y);
		if (m_inherit[CH_Y] && nxt != kInheritSentinel) {
			m_pose.y = ((m_agg[CH_Y] << 20) + (f * ((nxt - m_agg[CH_Y]) << 4)) + 32768) >> 16;
		}
		nxt = keyField(absKey + 1, CH_Z);
		if (m_inherit[CH_Z] && nxt != kInheritSentinel) {
			m_pose.z = ((m_agg[CH_Z] << 20) + (f * ((nxt - m_agg[CH_Z]) << 4)) + 32768) >> 16;
		}
		nxt = keyField(absKey + 1, CH_YAW);
		if (m_inherit[CH_YAW] && nxt != kInheritSentinel) {
			m_pose.yaw = ((m_agg[CH_YAW] << 16) + (f * getAngleDifference(m_agg[CH_YAW], nxt)) + 32768) >> 16;
		}
		nxt = keyField(absKey + 1, CH_PITCH);
		if (m_inherit[CH_PITCH] && nxt != kInheritSentinel) {
			m_pose.pitch = ((m_agg[CH_PITCH] << 16) + (f * getAngleDifference(m_agg[CH_PITCH], nxt)) + 32768) >> 16;
		}
	}
}

MayaPose MayaCamera::snapLast() const {
	MayaPose out;
	if (!valid() || m_cam->numKeys <= 0) {
		return out;
	}
	// Final key values, -2 resolved against the captured player pose, xyz <<4
	// (Snap semantics, src/MayaCamera.cpp:338-357).
	int agg[6];
	bool inherit[6];
	resolvedKey(m_cam->numKeys - 1, agg, inherit);
	out.x = agg[CH_X] << 4;
	out.y = agg[CH_Y] << 4;
	out.z = agg[CH_Z] << 4;
	out.pitch = agg[CH_PITCH];
	out.yaw = agg[CH_YAW];
	out.roll = agg[CH_ROLL];
	return out;
}

void MayaCamera::snap(int relKey) {
	if (!valid() || m_cam->numKeys <= 0) {
		return;
	}
	int k = relKey;
	if (k < 0) k = 0;
	if (k > m_cam->numKeys - 1) k = m_cam->numKeys - 1;
	int agg[6];
	bool inherit[6];
	resolvedKey(k, agg, inherit);
	m_pose.x = agg[CH_X] << 4;
	m_pose.y = agg[CH_Y] << 4;
	m_pose.z = agg[CH_Z] << 4;
	m_pose.pitch = agg[CH_PITCH];
	m_pose.yaw = agg[CH_YAW];
	m_pose.roll = agg[CH_ROLL];
}

// Key value at absolute index absKey; keys of consecutive cameras form one
// virtual array in the original global buffer, so a read past this camera's
// last key continues into the next camera's key 0 (src/Game.cpp:604-612).
int MayaCamera::keyField(int absKey, int ch) const {
	const auto& cams = m_map->mayaCameras;
	int idx = m_camIdx;
	int rel = absKey - m_keyOffset;
	while (rel >= cams[idx].numKeys && idx + 1 < static_cast<int>(cams.size())) {
		rel -= cams[idx].numKeys;
		++idx;
	}
	if (rel >= cams[idx].numKeys) {
		// Past the very last key of the map the original reads out of bounds;
		// hold the final key instead.
		rel = cams[idx].numKeys - 1;
	}
	return cams[idx].keys[cams[idx].numKeys * ch + rel];
}

void MayaCamera::resolveKey(int k) {
	resolvedKey(k, m_agg, m_inherit);
}

void MayaCamera::resolvedKey(int k, int* agg, bool* inherit) const {
	// resetTweenBase (src/MayaCamera.cpp:243-288): load key channels, resolve
	// -2 from the player pose, flag inheritance (roll never inherits).
	static const int chans[5] = { CH_X, CH_Y, CH_Z, CH_PITCH, CH_YAW };
	for (int c = 0; c < 6; ++c) {
		agg[c] = keyField(m_keyOffset + k, c);
		inherit[c] = false;
	}
	for (int i = 0; i < 5; ++i) {
		if (agg[chans[i]] == kInheritSentinel) {
			switch (chans[i]) {
				case CH_X: agg[CH_X] = m_player.x; break;
				case CH_Y: agg[CH_Y] = m_player.y; break;
				case CH_Z: agg[CH_Z] = m_player.z; break;
				case CH_PITCH: agg[CH_PITCH] = m_player.pitch; break;
				case CH_YAW: agg[CH_YAW] = m_player.yaw; break;
			}
			inherit[chans[i]] = true;
		}
	}
}

bool MayaCamera::hasTweens(int k) const {
	// Indices -1/-2 mark channels without tween data (src/MayaCamera.cpp:161-171).
	for (int j = 0; j < 6; ++j) {
		int16_t idx = m_cam->tweenIndices[k * 6 + j];
		if (idx != -1 && idx != -2) {
			return true;
		}
	}
	return false;
}

int MayaCamera::estNumTweens(int absKey) const {
	// src/MayaCamera.cpp:173-179.
	if (absKey + 1 == m_map->totalMayaCameraKeys) {
		return 0;
	}
	return ((keyField(absKey, CH_MS) & 0xFFFF) - 1) / m_cam->sampleRate;
}

int MayaCamera::tweenSample(int k, int ch, int step) const {
	// getTweenData byte fetch (src/MayaCamera.cpp:181-195), rebased into this
	// camera's tween blob via chanOfs + cross-camera base (src/Game.cpp:636).
	int indx = m_cam->tweenIndices[k * 6 + ch];
	if (indx < 0) {
		return 0;
	}
	size_t pos = static_cast<size_t>(m_chanOfs[ch] + m_basePrev[ch] + indx + step);
	if (pos >= m_cam->tweens.size()) {
		return 0;
	}
	return static_cast<int8_t>(m_cam->tweens[pos]);
}

int MayaCamera::getAngleDifference(int a, int b) const {
	// Shortest-arc wrap +/-512 (src/MayaCamera.cpp:151-159).
	if (b - a > 512) {
		b -= 1024;
	} else if (b - a < -512) {
		b += 1024;
	}
	return b - a;
}

void MayaCamera::getKeyOfs(int nextAbsKey, int* delta) const {
	// Deltas from agg toward the next key; angles use shortest arc from
	// agg masked to 10 bits (src/MayaCamera.cpp:197-232). Inherited channels
	// hold their base (delta 0).
	if (m_inherit[CH_X]) delta[CH_X] = 0;
	else delta[CH_X] = static_cast<short>(keyField(nextAbsKey, CH_X) - m_agg[CH_X]);
	if (m_inherit[CH_Y]) delta[CH_Y] = 0;
	else delta[CH_Y] = static_cast<short>(keyField(nextAbsKey, CH_Y) - m_agg[CH_Y]);
	if (m_inherit[CH_Z]) delta[CH_Z] = 0;
	else delta[CH_Z] = static_cast<short>(keyField(nextAbsKey, CH_Z) - m_agg[CH_Z]);
	delta[CH_PITCH] = static_cast<short>(getAngleDifference(m_agg[CH_PITCH] & 0x3FF, keyField(nextAbsKey, CH_PITCH)));
	delta[CH_YAW] = static_cast<short>(getAngleDifference(m_agg[CH_YAW] & 0x3FF, keyField(nextAbsKey, CH_YAW)));
	delta[CH_ROLL] = static_cast<short>(getAngleDifference(m_agg[CH_ROLL] & 0x3FF, keyField(nextAbsKey, CH_ROLL)));
}

void MayaCamera::staticPose() {
	// src/MayaCamera.cpp:79-102.
	m_pose.x = m_agg[CH_X] << 4;
	m_pose.y = m_agg[CH_Y] << 4;
	m_pose.z = m_agg[CH_Z] << 4;
	m_pose.pitch = m_agg[CH_PITCH];
	m_pose.yaw = m_agg[CH_YAW];
	m_pose.roll = m_agg[CH_ROLL];
}

void MayaCamera::interpolate(const int* delta, int num, int den) {
	// src/MayaCamera.cpp:234-241: angles ((agg<<16) + t*delta + 32768)>>16,
	// positions ((agg<<20) + t*(delta<<4) + 32768)>>16.
	int t = (num << 16) / den;
	m_pose.pitch = ((m_agg[CH_PITCH] << 16) + (t * delta[CH_PITCH]) + 32768) >> 16;
	m_pose.yaw = ((m_agg[CH_YAW] << 16) + (t * delta[CH_YAW]) + 32768) >> 16;
	m_pose.roll = ((m_agg[CH_ROLL] << 16) + (t * delta[CH_ROLL]) + 32768) >> 16;
	m_pose.x = ((m_agg[CH_X] << 20) + (t * (delta[CH_X] << 4)) + 32768) >> 16;
	m_pose.y = ((m_agg[CH_Y] << 20) + (t * (delta[CH_Y] << 4)) + 32768) >> 16;
	m_pose.z = ((m_agg[CH_Z] << 20) + (t * (delta[CH_Z] << 4)) + 32768) >> 16;
}

} // namespace newcore
