#ifndef NEW_CORE_SOUND_H
#define NEW_CORE_SOUND_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace newcore {

class AudioOutput;

// Port of src/Sound.cpp (docs/original-code/sound.md). Process-wide service
// (ADR 0046): every caller writes Sound::instance().playSound(...), exactly
// where the original wrote app->sound->playSound(...). Safe before startup()
// and with no audio device: every call is then a no-op (isSoundPlaying false).
class Sound {
public:
	static Sound& instance();
	~Sound();

	static constexpr int kNumChannels = 10;      // src/Sound.h:62
	static constexpr int kFirstResId = 1000;     // src/Sound.cpp:180
	static constexpr int kNumSounds = 145;       // src/Sounds.h:7-152
	static constexpr int kDefaultVolume = 80;    // src/Sound.cpp:34-35
	static constexpr int kFlagLoop = 1;          // src/Sound.cpp:439
	static constexpr int kFlagStopAll = 2;       // src/Sound.cpp:396-403
	static constexpr int kPriorityNoRestart = 6; // src/Sound.cpp:367-372

	// Reads "sounds2/<file>" through the caller's archive accessor.
	using Reader = std::function<bool(const std::string& name, std::vector<uint8_t>& out)>;
	void startup(Reader reader);                 // opens the device (silent mode on failure)
	void shutdown();                             // stops, closes the device, frees the cache

	void playSound(int resID, int flags, int priority);   // src/Sound.cpp:306-442
	void soundClick() { playSound(1027, 0, 5); }          // MenuSystem::soundClick, src/MenuSystem.cpp:5874-5876
	void soundStop();                                     // src/Sound.cpp:474-480
	void stopSound(int resID);                            // fadeOut=false arm, src/Sound.cpp:482-504
	bool isSoundPlaying(int resID) const;                 // src/Sound.cpp:506-513

	int soundFxVolume() const { return fxVolume_; }
	int musicVolume() const { return musicVolume_; }
	// Clamp 0..100, then updateVolume() (src/Sound.cpp:515-533,547-561,589-603).
	void setVolumes(int fx, int music);
	void setSoundFxVolume(int v) { setVolumes(v, musicVolume_); }
	void setMusicVolume(int v) { setVolumes(fxVolume_, v); }

	// Legacy public flags. allowSounds gates non-music, allowMusics gates
	// 1067..1071; both are recomputed from the volumes by updateVolume, and
	// callers save/restore allowSounds around map init and cinematic skip.
	bool allowSounds = true;
	bool allowMusics = true;
	bool areSoundsAllowed = false;   // canvas->areSoundsAllowed: config bool, init false (src/Canvas.cpp:188, src/Game.cpp:1986)
	bool inInitMap = false;          // canvas->inInitMap: only EV_PLAYSOUND reads it (src/ScriptThread.cpp:1083)

	static bool isMusic(int resID) { return static_cast<unsigned>(resID - 1067) <= 4; }  // src/Sound.cpp:341

private:
	Sound() = default;

	struct Channel { int resID = -1; int priority = 1; };   // src/Sound.cpp:42-45
	struct Pcm { std::vector<int16_t> samples; bool tried = false; bool ok = false; };

	const Pcm* load(int index);          // decode on first use, cached until shutdown()
	int getFreeSlot(int priority) const; // src/Sound.cpp:445-472
	bool busy(int ch) const;             // AL_PLAYING analog
	void updateVolume();
	int gainFor(int resID) const { return isMusic(resID) ? musicVolume_ : fxVolume_; }

	std::array<Pcm, kNumSounds> pcm_;    // declared before out_: the device closes first on destruction
	std::unique_ptr<AudioOutput> out_;
	Reader reader_;
	Channel channels_[kNumChannels];
	int fxVolume_ = kDefaultVolume;
	int musicVolume_ = kDefaultVolume;
	bool log_ = false;                   // D2R_SOUNDLOG set
};

} // namespace newcore

#endif // NEW_CORE_SOUND_H
