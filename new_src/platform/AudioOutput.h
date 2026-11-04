#ifndef NEW_PLATFORM_AUDIOOUTPUT_H
#define NEW_PLATFORM_AUDIOOUTPUT_H

#include <cstdint>
#include <vector>

namespace newcore {

// SDL audio device + fixed 10-voice mixer. Every public method locks the
// device (SDL_LockAudioDevice) so the SDL callback never sees a half-written
// voice. PCM passed to play() must stay alive and unchanged until close().
class AudioOutput {
public:
	static constexpr int kVoices = 10;      // src/Sound.h:62
	static constexpr int kRate = 44100;     // native rate of all 5 music tracks
	static constexpr int kChannels = 2;
	static constexpr int kGainMax = 100;    // volume units, src/Sound.cpp:108-113

	~AudioOutput();
	bool open();                            // false = no device; object stays usable as a no-op
	void close();
	bool isOpen() const { return dev_ != 0; }

	// Whole RIFF/WAVE image -> interleaved S16 stereo at kRate. false on any error.
	static bool decodeWav(const std::vector<uint8_t>& file, std::vector<int16_t>& out);

	void play(int voice, const int16_t* pcm, uint32_t frames, bool loop, int gain); // restarts at 0
	void stop(int voice);
	void setGain(int voice, int gain);      // 0..kGainMax
	bool isPlaying(int voice) const;        // false once a one-shot reached its end

private:
	struct Voice {
		const int16_t* pcm = nullptr;
		uint32_t frames = 0;
		uint32_t pos = 0;                   // frame index
		int gain = 0;
		bool loop = false;
		bool playing = false;
	};
	static void callback(void* self, uint8_t* stream, int len);
	void mix(int16_t* out, int frames);     // runs on the SDL audio thread, lock held by SDL

	Voice voices_[kVoices];
	std::vector<int32_t> acc_;              // sized once in open(): obtained.samples * kChannels
	uint32_t dev_ = 0;                      // SDL_AudioDeviceID
	bool subsystem_ = false;                // we called SDL_InitSubSystem(SDL_INIT_AUDIO)
};

} // namespace newcore

#endif // NEW_PLATFORM_AUDIOOUTPUT_H
