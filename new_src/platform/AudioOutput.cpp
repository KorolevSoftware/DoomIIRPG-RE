#include "platform/AudioOutput.h"

#include <SDL.h>

#include <algorithm>
#include <cstring>

namespace newcore {

AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::open() {
	if (dev_ != 0) return true;
	// Failures leave the reason in SDL_GetError(); the caller logs it.
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return false;
	subsystem_ = true;

	SDL_AudioSpec want{};
	want.freq = kRate;
	want.format = AUDIO_S16SYS;
	want.channels = kChannels;
	want.samples = 1024;
	want.callback = &AudioOutput::callback;
	want.userdata = this;
	SDL_AudioSpec have{};
	// allowed_changes = 0: SDL converts to the hardware itself, have == want.
	dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (dev_ == 0) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystem_ = false;
		return false;
	}
	acc_.assign(static_cast<size_t>(have.samples) * kChannels, 0);
	SDL_PauseAudioDevice(dev_, 0);
	return true;
}

void AudioOutput::close() {
	if (dev_ != 0) {
		SDL_CloseAudioDevice(dev_);
		dev_ = 0;
	}
	for (Voice& v : voices_) v = Voice{};
	if (subsystem_) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystem_ = false;
	}
}

bool AudioOutput::decodeWav(const std::vector<uint8_t>& file, std::vector<int16_t>& out) {
	SDL_RWops* rw = SDL_RWFromConstMem(file.data(), static_cast<int>(file.size()));
	if (rw == nullptr) return false;
	SDL_AudioSpec spec{};
	Uint8* buf = nullptr;
	Uint32 len = 0;
	if (SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len) == nullptr) return false;

	SDL_AudioCVT cvt;
	const int needed = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
		AUDIO_S16SYS, kChannels, kRate);
	if (needed < 0) {
		SDL_FreeWAV(buf);
		return false;
	}
	if (needed == 0) {
		out.resize(len / 2);
		std::memcpy(out.data(), buf, out.size() * 2);
	} else {
		std::vector<uint8_t> tmp(static_cast<size_t>(len) * cvt.len_mult);
		std::memcpy(tmp.data(), buf, len);
		cvt.buf = tmp.data();
		cvt.len = static_cast<int>(len);
		if (SDL_ConvertAudio(&cvt) < 0) {
			SDL_FreeWAV(buf);
			return false;
		}
		out.resize(static_cast<size_t>(cvt.len_cvt) / 2);
		std::memcpy(out.data(), tmp.data(), out.size() * 2);
	}
	SDL_FreeWAV(buf);
	return true;
}

void AudioOutput::play(int voice, const int16_t* pcm, uint32_t frames, bool loop, int gain) {
	if (voice < 0 || voice >= kVoices) return;
	SDL_LockAudioDevice(dev_);
	voices_[voice] = Voice{pcm, frames, 0, gain, loop, frames != 0};
	SDL_UnlockAudioDevice(dev_);
}

void AudioOutput::stop(int voice) {
	if (voice < 0 || voice >= kVoices) return;
	SDL_LockAudioDevice(dev_);
	voices_[voice] = Voice{};
	SDL_UnlockAudioDevice(dev_);
}

void AudioOutput::setGain(int voice, int gain) {
	if (voice < 0 || voice >= kVoices) return;
	SDL_LockAudioDevice(dev_);
	voices_[voice].gain = gain;
	SDL_UnlockAudioDevice(dev_);
}

bool AudioOutput::isPlaying(int voice) const {
	if (voice < 0 || voice >= kVoices) return false;
	SDL_LockAudioDevice(dev_);
	const bool playing = voices_[voice].playing;
	SDL_UnlockAudioDevice(dev_);
	return playing;
}

void AudioOutput::callback(void* self, uint8_t* stream, int len) {
	static_cast<AudioOutput*>(self)->mix(reinterpret_cast<int16_t*>(stream), len / (2 * kChannels));
}

void AudioOutput::mix(int16_t* out, int frames) {
	// SDL normally asks for exactly have.samples frames; chunk in case it asks for more.
	const int chunkMax = static_cast<int>(acc_.size()) / kChannels;
	if (chunkMax == 0) {
		std::memset(out, 0, static_cast<size_t>(frames) * kChannels * sizeof(int16_t));
		return;
	}
	while (frames > 0) {
		const int n = std::min(frames, chunkMax);
		std::fill(acc_.begin(), acc_.begin() + n * kChannels, 0);
		for (Voice& v : voices_) {
			if (!v.playing) continue;
			for (int f = 0; f < n; ++f) {
				const int16_t* s = v.pcm + static_cast<size_t>(v.pos) * kChannels;
				acc_[f * kChannels] += s[0] * v.gain / kGainMax;
				acc_[f * kChannels + 1] += s[1] * v.gain / kGainMax;
				if (++v.pos == v.frames) {
					if (v.loop) {
						v.pos = 0;
					} else {
						v.playing = false;
						break;
					}
				}
			}
		}
		for (int i = 0; i < n * kChannels; ++i) {
			out[i] = static_cast<int16_t>(std::clamp(acc_[i], -32768, 32767));
		}
		out += n * kChannels;
		frames -= n;
	}
}

} // namespace newcore
