#include "core/Sound.h"

#include "platform/AudioOutput.h"

#include <SDL.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace newcore {

namespace {
// Verbatim copy of Sounds::RESOURCE_FILE_NAMES (src/Sounds.h:8-152).
const char* const kSoundFiles[Sound::kNumSounds] = {
	"Arachnotron_active.wav",  // 0
	"Arachnotron_death.wav",
	"Arachnotron_sight.wav",
	"Arachnotron_walk.wav",
	"Archvile_active.wav",
	"Archvile_attack.wav",
	"Archvile_death.wav",
	"Archvile_pain.wav",
	"Archvile_sight.wav",
	"BFG.wav",
	"block_drop.wav", // 10
	"block_pick.wav",
	"Cacodemon_death.wav",
	"Cacodemon_sight.wav",
	"chaingun.wav",
	"chainsaw.wav",
	"ChainsawGoblin_attack.wav",
	"ChainsawGoblin_death.wav",
	"ChainsawGoblin_pain.wav",
	"ChainsawGoblin_sight.wav",
	"chime.wav", // 20
	"claw.wav",
	"Cyberdemon_death.wav",
	"Cyberdemon_sight.wav",
	"demon_active.wav",
	"demon_pain.wav",
	"demon_small_active.wav",
	"Dialog_Help.wav",
	"door_close.wav",
	"door_close_slow.wav",
	"door_open.wav", // 30
	"door_open_slow.wav",
	"explosion.wav",
	"fireball.wav",
	"fireball_impact.wav",
	"footstep1.wav",
	"footstep2.wav",
	"gib.wav",
	"glass.wav",
	"hack_clear.wav",
	"hack_fail.wav", // 40
	"hack_failed.wav",
	"hack_submit.wav",
	"hack_success.wav",
	"hack_successful.wav",
	"HolyWaterPistol.wav",
	"HolyWaterPistol_refill.wav",
	"hoof.wav",
	"impClaw.wav",
	"Imp_active.wav",
	"Imp_death1.wav", // 50
	"Imp_death2.wav",
	"Imp_sight1.wav",
	"Imp_sight2.wav",
	"item_pickup.wav",
	"loot.wav",
	"LostSoul_attack.wav",
	"Maglev_arrive.wav",
	"Maglev_depart.wav",
	"malfunction.wav",
	"Mancubus_attack.wav", // 60
	"Mancubus_death.wav",
	"Mancubus_pain.wav",
	"Mancubus_sight.wav",
	"menu_open.wav",
	"menu_scroll.wav",
	"monster_pain.wav",
	"Music_Boss.wav",
	"Music_General.wav",
	"Music_Level_End.wav",
	"Music_LevelUp.wav", // 70
	"Music_Title.wav",
	"no_use.wav",
	"no_use2.wav",
	"Pinkinator_attack.wav",
	"Pinkinator_death.wav",
	"Pinkinator_pain.wav",
	"Pinkinator_sight.wav",
	"Pinkinator_spawn.wav",
	"Pinky_attack.wav",
	"Pinky_death.wav", // 80
	"Pinky_sight.wav",
	"Pinky_small_attack.wav",
	"Pinky_small_death.wav",
	"Pinky_small_pain.wav",
	"Pinky_small_sight.wav",
	"pistol.wav",
	"plasma.wav",
	"platform_start.wav",
	"platform_stop.wav",
	"player_death1.wav", // 90
	"player_death2.wav",
	"player_death3.wav",
	"player_pain1.wav",
	"player_pain2.wav",
	"Revenant_active.wav",
	"Revenant_attack.wav",
	"Revenant_death.wav",
	"Revenant_punch.wav",
	"Revenant_sight.wav",
	"Revenant_swing.wav", // 100
	"rocketLauncher.wav",
	"rocket_explode.wav",
	"secret.wav",
	"Sentinel_attack.wav",
	"Sentinel_death.wav",
	"Sentinel_pain.wav",
	"Sentinel_sight.wav",
	"SentryBot_activate.wav",
	"SentryBot_death.wav",
	"SentryBot_discard.wav", // 110
	"SentryBot_pain.wav",
	"SentryBot_return.wav",
	"slider.wav",
	"Soulcube.wav",
	"SpiderMastermind_death.wav",
	"SpiderMastermind_sight.wav",
	"supershotgun.wav",
	"supershotgun_close.wav",
	"supershotgun_load.wav",
	"supershotgun_open.wav", // 120
	"switch.wav",
	"switch_exit.wav",
	"teleport.wav",
	"use_item.wav",
	"vending_sale.wav",
	"vent.wav",
	"VIOS_attack1.wav",
	"VIOS_attack2.wav",
	"VIOS_death.wav",
	"VIOS_pain.wav", // 130
	"VIOS_sight.wav",
	"weapon_pickup.wav",
	"Weapon_Sniper_Scope.wav",
	"Weapon_Toilet_Pull.wav",
	"Weapon_Toilet_Smash.wav",
	"Weapon_Toilet_Throw.wav",
	"Weapon_Toilet_Throw2.wav",
	"zombie_active.wav",
	"zombie_death1.wav",
	"zombie_death2.wav", // 140
	"zombie_death3.wav",
	"zombie_sight1.wav",
	"zombie_sight2.wav",
	"zombie_sight3.wav",
};
} // namespace

Sound& Sound::instance() {
	static Sound sound;
	return sound;
}

Sound::~Sound() = default;

void Sound::startup(Reader reader) {
	assert(std::strcmp(kSoundFiles[1067 - kFirstResId], "Music_Boss.wav") == 0);
	assert(std::strcmp(kSoundFiles[1000 - kFirstResId], "Arachnotron_active.wav") == 0);
	assert(std::strcmp(kSoundFiles[1086 - kFirstResId], "pistol.wav") == 0);
	assert(std::strcmp(kSoundFiles[1144 - kFirstResId], "zombie_sight3.wav") == 0);

	reader_ = std::move(reader);
	log_ = std::getenv("D2R_SOUNDLOG") != nullptr;
	for (Channel& c : channels_) c = Channel{};
	out_ = std::make_unique<AudioOutput>();
	if (out_->open()) {
		std::fprintf(stdout, "[sound] device 44100 Hz S16 stereo\n");
	} else {
		std::fprintf(stderr, "[sound] no audio device (%s): silent\n", SDL_GetError());
		out_.reset();
	}
	updateVolume();
}

void Sound::shutdown() {
	soundStop();
	out_.reset();   // closes the device: the callback thread is gone before the PCM is freed
	for (Pcm& p : pcm_) p = Pcm{};
}

const Sound::Pcm* Sound::load(int index) {
	Pcm& p = pcm_[index];
	if (!p.tried) {
		p.tried = true;
		std::vector<uint8_t> file;
		p.ok = reader_ && reader_(kSoundFiles[index], file) && AudioOutput::decodeWav(file, p.samples);
		if (!p.ok) {
			p.samples.clear();
			std::fprintf(stderr, "[sound] cannot load %s\n", kSoundFiles[index]);
		}
	}
	return &p;
}

bool Sound::busy(int ch) const {
	return out_ && out_->isPlaying(ch);
}

int Sound::getFreeSlot(int priority) const {
	int best = kPriorityNoRestart;
	int slot = -1;
	for (int ch = 0; ch < kNumChannels; ++ch) {
		if (channels_[ch].resID == -1 || !busy(ch)) return ch;
		const int p = channels_[ch].priority;
		if (p < priority && p < best) {
			best = p;
			slot = ch;
		}
	}
	return slot;
}

void Sound::playSound(int resID, int flags, int priority) {
	if (resID == -1 || resID == 255 || resID == 1255) return;   // src/Sound.cpp:330-339
	if (!out_) {
		if (log_) std::fprintf(stderr, "[sound] play %d fl=%d pri=%d -> silent\n", resID, flags, priority);
		return;
	}
	const int idx = resID - kFirstResId;
	if (idx < 0 || idx >= kNumSounds) {
		static bool logged = false;
		if (!logged) {
			logged = true;
			std::fprintf(stderr, "[sound] bad id %d\n", resID);
		}
		return;
	}
	if (isMusic(resID) ? !allowMusics : !allowSounds) {   // src/Sound.cpp:341-349
		if (log_) std::fprintf(stderr, "[sound] play %d fl=%d pri=%d -> gated\n", resID, flags, priority);
		return;
	}

	// Only the FIRST channel holding resID decides reuse / priority-6 (src/Sound.cpp:353-372).
	int reuse = -1;
	for (int ch = 0; ch < kNumChannels; ++ch) {
		if (channels_[ch].resID != resID) continue;
		if (!busy(ch)) {
			reuse = ch;
		} else if (priority == kPriorityNoRestart) {
			if (log_) std::fprintf(stderr, "[sound] play %d fl=%d pri=%d -> dropped\n", resID, flags, priority);
			return;
		}
		break;
	}
	int freeSlot = -1;
	for (int ch = 0; ch < kNumChannels; ++ch) {
		if (channels_[ch].resID == -1) {
			freeSlot = ch;
			break;
		}
	}

	if (flags & kFlagStopAll) {   // src/Sound.cpp:396-403, resID kept
		for (int ch = 0; ch < kNumChannels; ++ch) {
			out_->stop(ch);
			channels_[ch].priority = 1;
		}
	}

	const bool loop = (flags & kFlagLoop) != 0;
	if (reuse == -1) {   // src/Sound.cpp:406-426
		const int slot = freeSlot != -1 ? freeSlot : getFreeSlot(priority);
		if (slot == -1) {
			if (log_) std::fprintf(stderr, "[sound] play %d fl=%d pri=%d -> dropped\n", resID, flags, priority);
			return;
		}
		const Pcm* pcm = load(idx);
		if (!pcm->ok) return;
		channels_[slot] = Channel{resID, priority};
		out_->play(slot, pcm->samples.data(), static_cast<uint32_t>(pcm->samples.size() / 2), loop, gainFor(resID));
		if (log_) std::fprintf(stderr, "[sound] play %d fl=%d pri=%d -> ch %d\n", resID, flags, priority, slot);
	} else {   // src/Sound.cpp:428-437, slot priority not updated
		const Pcm* pcm = load(idx);
		out_->play(reuse, pcm->samples.data(), static_cast<uint32_t>(pcm->samples.size() / 2), loop, gainFor(resID));
		if (log_) std::fprintf(stderr, "[sound] play %d fl=%d pri=%d -> reuse ch %d\n", resID, flags, priority, reuse);
	}
}

void Sound::soundStop() {
	if (log_) std::fprintf(stderr, "[sound] stop all\n");
	for (int ch = 0; ch < kNumChannels; ++ch) {
		if (out_) out_->stop(ch);
		channels_[ch].priority = 1;
	}
}

void Sound::stopSound(int resID) {
	if (log_) std::fprintf(stderr, "[sound] stop %d\n", resID);
	for (int ch = 0; ch < kNumChannels; ++ch) {
		if (channels_[ch].resID == resID) {
			if (out_) out_->stop(ch);
			channels_[ch].priority = 1;
			return;
		}
	}
}

bool Sound::isSoundPlaying(int resID) const {
	for (int ch = 0; ch < kNumChannels; ++ch) {
		if (channels_[ch].resID == resID) return busy(ch);
	}
	return false;
}

void Sound::setVolumes(int fx, int music) {
	fxVolume_ = std::clamp(fx, 0, 100);
	musicVolume_ = std::clamp(music, 0, 100);
	updateVolume();
}

void Sound::updateVolume() {   // src/Sound.cpp:515-533
	if (out_) {
		for (int ch = 0; ch < kNumChannels; ++ch) {
			if (channels_[ch].resID != -1) out_->setGain(ch, gainFor(channels_[ch].resID));
		}
	}
	allowMusics = musicVolume_ != 0;
	allowSounds = fxVolume_ != 0;
}

} // namespace newcore
