# Sound subsystem: SDL mixer, legacy channel rules, call-site wiring (G1-G9)

Date: 2026-11-04. ADR: `adr/0046-sound-sdl-mixer-singleton-service.md`.
Facts: `docs/original-code/sound.md` (engine §1-4, gameplay §5, UI §6),
`docs/research/2026-11-04-sound-engine.md`,
`docs/research/2026-11-04-sound-callsites-gameplay.md`,
`docs/research/2026-11-04-sound-callsites-ui.md`.
Closes PLAN.md:211 ("Звук: отсутствует полностью").

## 0. Summary

Two new file pairs and call-site wiring:

| File | Layer | Role |
|---|---|---|
| `new_src/platform/AudioOutput.h/.cpp` | platform | SDL audio device + a 10-voice software mixer (S16 stereo 44100 Hz). Knows nothing about resIDs. |
| `new_src/core/Sound.h/.cpp` | core | Port of `src/Sound.cpp` semantics: resID table, 10 channels with priority rules, flags, volumes, allow flags, PCM cache. Process-wide service `Sound::instance()` (ADR 0046). |

Every `std::fprintf(stderr, "[sound] ...")` placeholder (and the `[combat] fire sound`,
`[monster] alert/pain sound`, `[loot] sound`, `[dialog] sound`, `[script] PLAYSOUND/STOPSOUND`,
`[script] sound 1065` prints) is replaced by a real call. Nothing in `tools/` greps these lines
(checked: `grep -rn "sound" tools/*.py` has no hit), so they are removed; the optional
`D2R_SOUNDLOG=1` trace inside `Sound` replaces them for debugging.

Threading: the game stays single-threaded. The only other thread is SDL's audio callback, which
touches nothing but `AudioOutput::voices_` and the immutable PCM buffers (§3.4).

## 1. Not ported (dead or unused in the original) — do not implement

| Original | Why |
|---|---|
| Fades: `stopSound(id, true)`, `StartFade`, `fadeInProgress`, `fadeVolume` | Started but never advanced; `endFrame` is empty (`src/Sound.cpp:577-587`). The only `stopSound` caller passes `false` (`src/ScriptThread.cpp:1958`). |
| `startFrame` deferred play (`resID/flags/priority` members, `field_0x162`) and the `this->resID == resID` gate | `field_0x162` is never set (`src/Sound.cpp:563-575`); `this->resID` stays 0 (`src/Sound.cpp:350-351`). |
| `field_0x10` / `field_0x4` gates, `openAL_SetSystemVolume` | Constant 100 / never set / call commented out (`src/Sound.cpp:36,77,101-106`). |
| Suspend/resume | Never called (`src/Sound.cpp:115-127`, `src/CAppContainer.cpp:63-69`). |
| `cacheCombatSound`, `freeMonsterSounds`, `cacheSounds` | Stubs / replaced by the lazy cache (`src/Sound.cpp:298-304,539-545`). |
| `playSound` 4th arg `a5`, `playCombatSound` | `a5` unread; `playCombatSound` is a plain forward (`src/Sound.cpp:535-537`). Callers use `playSound`. |
| Paused state in `getFreeSlot` | Nothing pauses a source; "busy" = playing. |
| `sysSoundDelayTime` random sound, `ACTION_TOGSOUND`/`MENU_ENABLE_SOUNDS` | Dead (`src/Canvas.cpp:989-992`, `src/MenuSystem.cpp:3055-3060`, research UI table). |

## 2. `platform/AudioOutput`

### 2.1 Header

```cpp
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
	static constexpr int kRate = 44100;     // native rate of all 5 music tracks (sound.md §1 survey)
	static constexpr int kChannels = 2;
	static constexpr int kGainMax = 100;    // volume units, src/Sound.cpp:108-113

	~AudioOutput();                         // close()
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
#endif
```

### 2.2 Behaviour

- `open()`: `SDL_InitSubSystem(SDL_INIT_AUDIO)` (failure → log, return false). Request
  `freq=kRate, format=AUDIO_S16SYS, channels=2, samples=1024, callback=callback, userdata=this`;
  `SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0)` — `allowed_changes = 0`, so SDL converts to
  the hardware itself and `have` always equals `want`. Failure → log, `SDL_QuitSubSystem`, return
  false. Success → `acc_.assign(have.samples * 2, 0)`, `SDL_PauseAudioDevice(dev_, 0)`.
- `close()`: `SDL_CloseAudioDevice` (joins the callback thread), reset voices, quit the subsystem
  if we inited it. Idempotent.
- `decodeWav`: `SDL_RWFromConstMem` → `SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)` (handles the
  `fmtLen == 18` file `SpiderMastermind_sight.wav`, sound.md §1) →
  `SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, kRate)`;
  if `cvt.needed`: `std::vector<uint8_t> tmp(len * cvt.len_mult)`, copy, `cvt.buf = tmp.data()`,
  `cvt.len = len`, `SDL_ConvertAudio`, result is `cvt.len_cvt` bytes; else the raw bytes. Copy into
  `out` (`len_cvt / 2` samples), `SDL_FreeWAV`. Accepts 8/16-bit, mono/stereo, any rate — the
  archive mix is 8000/11025/22050/44100/48000 Hz (sound.md §1). Mono is duplicated to both
  channels: OpenAL plays a non-positional mono source at the listener equally on both sides
  (source at 0,0,0, `src/Sound.cpp:280-295`).
- `play`: lock; voice = `{pcm, frames, 0, gain, loop, frames != 0}`; unlock.
- `mix(out, frames)`: zero `acc_[0 .. 2*frames)`; for each playing voice, for each output frame:
  `acc[2f+c] += pcm[2*pos+c] * gain / 100`; `++pos`; at `pos == frames`: `loop ? pos = 0 :
  (playing = false, stop this voice)`. Then clamp each sample to `[-32768, 32767]` into `out`.
  Linear gain `vol/100` is the legacy `AL_MAX_GAIN = vol/100` with source gain 1.0
  (`src/Sound.cpp:108-113`). No master gain, no limiter.
- `callback`: `static_cast<AudioOutput*>(self)->mix((int16_t*)stream, len / 4)`.
- Out-of-range `voice` arguments are ignored (no assert needed; `Sound` only passes 0..9).

## 3. `core/Sound`

### 3.1 Header

```cpp
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
	// The future Options/Sound menu uses these (steps of 10 or slider value).
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
	struct Channel { int resID = -1; int priority = 1; };   // src/Sound.cpp:42-45
	struct Pcm { std::vector<int16_t> samples; bool tried = false; bool ok = false; };

	const Pcm* load(int index);          // decode on first use, cached until shutdown()
	int getFreeSlot(int priority) const; // src/Sound.cpp:445-472
	bool busy(int ch) const;             // AL_PLAYING analog
	void updateVolume();
	int gainFor(int resID) const { return isMusic(resID) ? musicVolume_ : fxVolume_; }

	std::unique_ptr<AudioOutput> out_;
	Reader reader_;
	Channel channels_[kNumChannels];
	std::array<Pcm, kNumSounds> pcm_;
	int fxVolume_ = kDefaultVolume;
	int musicVolume_ = kDefaultVolume;
	bool log_ = false;                   // D2R_SOUNDLOG set
};

} // namespace newcore
#endif
```

`Sound.cpp` holds `static const char* const kSoundFiles[kNumSounds]`, a verbatim copy of
`Sounds::RESOURCE_FILE_NAMES` (`src/Sounds.h:8-152`, also tabulated in sound.md §1). In
`startup()` add a Debug self-check: `assert(std::strcmp(kSoundFiles[1067-1000], "Music_Boss.wav") == 0)`,
same for `1000 -> "Arachnotron_active.wav"`, `1086 -> "pistol.wav"`, `1144 -> "zombie_sight3.wav"`.

### 3.2 Lifecycle

- `startup(reader)`: store reader; `log_ = getenv("D2R_SOUNDLOG") != nullptr`; channels reset to
  `{-1, 1}`; `out_ = make_unique<AudioOutput>()`; `if (!out_->open()) out_.reset()`. Log exactly
  one line: `[sound] device 44100 Hz S16 stereo` or `[sound] no audio device (<SDL error>): silent`.
  Then `updateVolume()`.
- Called from `AppContext::startup()` (today an empty stub, `new_src/core/AppContext.cpp:53-55`),
  reader = `[this](const std::string& n, std::vector<uint8_t>& o){ return readResource("sounds2/" + n, o); }`
  (`kResourcePrefix` + `sounds2/` = the legacy `LT_SOUND_RESOURCE` path, `src/JavaStream.cpp:82-90`).
- `shutdown()`: `soundStop()`, `out_.reset()` (closes device → callback thread gone), then clear
  `pcm_`. Called first thing in `AppContext::shutdown()`, before `archive_.reset()`.
- `Window::initialize` drops `SDL_INIT_AUDIO` from `SDL_Init` (`new_src/platform/Window.cpp:39`):
  today a host with no usable audio driver fails the whole `SDL_Init`. Audio is initialised by
  `AudioOutput::open` and its failure is non-fatal.

### 3.3 `playSound(resID, flags, priority)` — exact algorithm (`src/Sound.cpp:330-441`)

```
if resID == -1 || resID == 255 || resID == 1255: return                 // :330-339
if !out_: log; return                                                    // silent mode (SND-D4)
idx = resID - 1000; if idx < 0 || idx >= 145: log once "[sound] bad id"; return   // SND-D3
if isMusic(resID) ? !allowMusics : !allowSounds: return                 // :341-349
reuse = -1; freeSlot = -1
first = first channel with resID == resID (or -1)
if first != -1:
    if !busy(first): reuse = first                                        // :356-366 LABEL_19
    else if priority == 6: return                                         // :367-372
freeSlot = first channel with resID == -1 (or -1)                         // :375-383
// :389-394 (prio 6 || loop) && reuse busy -> return: unreachable, reuse is never busy; omit.
if flags & 2: for every channel: out_->stop(ch); priority = 1             // :396-403 (resID kept)
if reuse == -1:                                                           // :406-426
    slot = freeSlot != -1 ? freeSlot : getFreeSlot(priority)
    if slot == -1: return                                                 // dropped
    pcm = load(idx); if !pcm->ok: return                                  // SND-D3
    channels_[slot] = {resID, priority}
    out_->play(slot, pcm.samples.data(), pcm.samples.size()/2, flags & 1, gainFor(resID))
else:                                                                     // :428-437
    // slot priority NOT updated
    out_->play(reuse, <cached pcm of resID>, ..., flags & 1, gainFor(resID))
```

Notes that MUST be preserved:
- Only the FIRST channel holding `resID` is considered for reuse / the priority-6 test. If it is
  busy and priority != 6, a SECOND copy starts in another channel (overlapping duplicates are
  legal, also for looping music re-requested without flag 2) — sound.md §2 "Consequence".
- On the new-slot path, `load()` happens AFTER `slot` is chosen but before the channel record is
  written, so a failed load leaves the channel untouched.
- `getFreeSlot(p)` (`src/Sound.cpp:445-472`): scan 0..9, return the first channel that has
  `resID == -1` or is not busy. If all 10 are busy, return the busy channel with the lowest
  `priority` that is `< p` AND `< 6` (strictly; first one wins ties: track `best = 6`, update
  only when `priority < p && priority < best`); none → -1. Stealing a busy channel simply calls
  `play()` on it (the voice restarts with the new PCM).
- `busy(ch)` = `out_ && out_->isPlaying(ch)`. A finished one-shot keeps its `resID` and
  `priority` (nothing resets them except `soundStop`/`stopSound`/flag 2, which set priority 1).

### 3.4 Other methods

- `soundStop()`: every channel `out_->stop(ch)`, `priority = 1`; `resID` kept (`src/Sound.cpp:474-480`).
- `stopSound(resID)`: first channel with that resID only: stop, `priority = 1` (`:482-504`, fadeOut=false).
- `isSoundPlaying(resID)`: first channel with that resID → `busy(ch)`; none → false (`:506-513`).
- `updateVolume()` (`:515-533`): for every channel with `resID != -1`: `out_->setGain(ch, gainFor(resID))`;
  then `allowMusics = musicVolume_ != 0; allowSounds = fxVolume_ != 0`. Volume 0 == off; there is
  no separate toggle.
- `load(idx)`: if `!tried`: `tried = true`; read `kSoundFiles[idx]` via reader, `decodeWav`;
  failure → log once `[sound] cannot load <file>`, `ok = false`. Runs on the game thread before
  the voice is pointed at the buffer, so the callback only ever reads finished, immutable vectors.
- Logging when `log_`: `[sound] play <id> fl=<f> pri=<p> -> ch <n>` / `-> reuse ch <n>` /
  `-> dropped` / `-> gated`, `[sound] stop all`, `[sound] stop <id>`. Nothing is printed otherwise
  (besides the startup line and the once-only errors).

Thread-safety contract: the audio thread touches only `AudioOutput::voices_`, `acc_` and the PCM
vectors. `voices_` are written only under `SDL_LockAudioDevice` (SDL holds the same lock while it
runs the callback). PCM vectors are written once before first use and freed only after
`SDL_CloseAudioDevice`. `Sound`'s own fields (`channels_`, volumes, flags) are game-thread only.

### 3.5 Memory / decode decision

Archive survey: 148 WAVs, 39.3 MB uncompressed in the zip. Converted to S16 stereo 44100 Hz the
145 referenced files are ≈63 MB, of which the five music tracks are ≈32.5 MB
(`Music_General` 63.8 s = 11.3 MB, `Music_Title` 58.7 s = 10.4 MB, `Music_Level_End` 41.2 s =
7.3 MB, `Music_Boss` 16.1 s = 2.8 MB, `Music_LevelUp` 4.4 s = 0.8 MB). Pre-decoding everything
would inflate and convert 39 MB at every launch and keep 63 MB resident for sounds most sessions
never play. Decision: **decode on first play, keep until shutdown** (no eviction). A typical
session holds 2-3 music tracks + a few dozen effects (≈30-40 MB). The first play of a music track
costs one inflate + convert of ≈10 MB on the game thread (tens of ms, once) — the original also
decodes synchronously on channel assignment (`src/Sound.cpp:414-420`).

## 4. Configuration persistence (`core/SaveGame`)

The config file v11 already reserves the bytes (`new_src/core/SaveGame.cpp:151-153`, writing
placeholders `true/100/100`).

- `writeConfig(bool empty)`: write `Sound::instance().allowSounds`, `soundFxVolume()`,
  `musicVolume()` (as `uint8_t`) in place of the placeholders — both for the normal and the empty
  config, like `src/Game.cpp:1922-1924` and `:2327-2329`.
- `loadConfig()`: replace `r.skip(1 + 1 + 1 + 1 + 1 + 4)` by reading `bool savedAllow`,
  `int fx = readByte()`, `int music = readByte()`, then skip `1 + 1 + 4`. Apply ONLY after the
  marker check (the existing commit-after-validate block): `snd.areSoundsAllowed = savedAllow;
  snd.setVolumes(fx, music);` — `setVolumes` clamps to 0..100 (config is a trust boundary) and its
  `updateVolume` overwrites `allowSounds`/`allowMusics`, so the saved bool only feeds
  `areSoundsAllowed`, exactly as in the original (`src/Game.cpp:1985-1988,2050`, sound.md §3
  "Persistence").
- No config on disk → defaults 80/80, `areSoundsAllowed = false` (`src/Canvas.cpp:188`): init-map
  script effects stay muted until the first config save. Faithful.

## 5. Call-site wiring

Notation: `S` = `Sound::instance()`; `play(id, fl, pri)`. Every line below replaces the cited
placeholder or adds a call at the cited position. Include `"core/Sound.h"`.

### 5.1 Map load, cinematic skip, game start (G2)

| new_src | Change | Original |
|---|---|---|
| `core/LoadingManager.cpp:139` | `S.soundStop()` | `src/LoadingManager.cpp:70` |
| `core/LoadingManager.cpp:202` | `S.playSound(1069, 1, 3)` | `src/Canvas.cpp:889` |
| `core/LoadingManager.cpp` `startGame`, first statement | `S.soundStop()` (both branches) | `src/MenuSystem.cpp:3583` |
| `core/GameContext.cpp` `tickLoading` phase 1, right after the `if (framesRendered_ == frameAtStateEntry_) return;` | `const bool savedAllow = S.allowSounds; S.inInitMap = true; S.allowSounds = false;` | `src/LoadingManager.cpp:619-621` |
| same function, right after `blockInputTime = gameTime + 200;` | `S.allowSounds = savedAllow; S.inInitMap = false;` then `sys_.game->monsters.armIdleSounds(upTimeMs);` (G5 adds the function; in G2 leave a one-line comment placeholder only if G5 is not merged yet) | `:721`, `:734-736` |
| `core/CinematicCamera.cpp` `skipCinematicNow`, first statements | `const bool savedAllow = S.allowSounds; S.allowSounds = false; S.soundStop();` and `S.allowSounds = savedAllow;` as the last statement | `src/Game.cpp:2510-2512,2534` |

Music is not muted by the init-map window (it is gated by `allowMusics`, `src/Sound.cpp:341-349`):
map00's init script `PLAYSOUND 1068` stays audible.

### 5.2 Scripts (G3) — `domain/game/ScriptVM.cpp`

- `EV_PLAYSOUND` (:978-983): `int id = readUByte(t); int args = readUByte(t);` then
  ```cpp
  Sound& s = Sound::instance();
  if (s.inInitMap && s.areSoundsAllowed) {           // src/ScriptThread.cpp:1083-1088
      const bool saved = s.allowSounds;
      s.allowSounds = true;
      s.playSound(id + 1000, args >> 4, args & 0xF);
      s.allowSounds = saved;
  } else {
      s.playSound(id + 1000, args >> 4, args & 0xF);  // :1090
  }
  ```
  Byte 255 → 1255 → no-op inside `playSound`.
- `EV_STOPSOUND` (:985-988): `Sound::instance().stopSound(id + 1000)` (`src/ScriptThread.cpp:1958`).
- EV_DOOROP lock/unlock (:598, :601): `if (interactive) play(1065, 0, 3)` (`src/ScriptThread.cpp:768,774`).
- `EV_START_ARMORREPAIR` (:1706): `play(characterChoice == 1 ? 1073 : 1072, 0, 3)` keeping the
  1..3 guard (`src/ArmorRepairSystem.cpp:33-38`).

### 5.3 Player, combat, items, world (G4a, G4b)

G4a:

| new_src | Change | Original |
|---|---|---|
| `domain/game/Combat.cpp:690` | `if (snd != -1) play(snd, 0, 4)` | `src/Combat.cpp:307` |
| `domain/game/Combat.cpp:311` | `play(snd, 0, 4)` | `src/Combat.cpp:522` |
| `domain/game/Combat.cpp:992` | `play(1034, 0, 4)` | `src/Combat.cpp:1330` |
| `domain/game/Combat.cpp:425` `playerPainEvent` | `play(isFamiliar ? 1111 : characterChoice == 1 ? 1094 : 1093, 0, 3)`; fix the comment at :422 (divergence fixed) | `src/Player.cpp:631-640` |
| `domain/game/Player.cpp:585` | `play(1124, 0, 3)` | `src/Player.cpp:952` |
| `domain/game/Player.cpp:715` | `play(1034, 0, 3)` | `src/Player.cpp:1572` |
| `domain/game/Player.cpp` `addLevel`, after the stat/health updates | `if (game_ == nullptr \|\| game_->host == nullptr \|\| game_->host->state() != StateId::Menu) { S.soundStop(); play(1070, 0, 5); }`; update the header comment (sound no longer deferred; help dialog still is) | `src/Player.cpp:348-351` |
| `domain/game/Game.h` | new public member `const StateHost* host = nullptr;` (include `core/GameStates.h`); set in `core/Main.cpp` right after `GameContext ctx;`: `game.host = &ctx;` | — |
| `core/GameContext.cpp:285` | `play(cc == 1 ? 1092 : cc == 2 ? 1091 : 1090, 0, 3)` inside the existing 1..3 guard | `src/Player.cpp:720-726` |

G4b:

| new_src | Change | Original |
|---|---|---|
| `domain/game/ItemPickup.cpp:65` (TODO) | `play(1054, 0, 4)` after `removeEntity` | `src/Entity.cpp:277` |
| `domain/game/ItemPickup.cpp:74` | `play(1054, 0, 4)` | same |
| `domain/game/Game.cpp:764` | `play(pushWall ? 1103 : 1020, 0, 3)` | `src/Game.cpp:3480,3483` |
| `domain/game/Game.cpp:775` | `play(1020, 0, 3)` | `src/Player.cpp:1988` |
| `domain/game/Game.cpp` `painProp`, `INTERACT_BARRICADE` arm | `play(1038, 0, 3)` at the frame-swap point; fix the comment above `painProp` | `src/Entity.cpp:376` |
| `domain/game/DoorSystem.cpp` `performDoorEvent` | after the slot fields are filled and BEFORE the `n2 == 0` snap block: `play(n == 1 ? 1028 : 1030, 0, 3)`. Secret walls never reach this function (they use `SpriteLerps::startSecretWall`), which matches the silent `b == true` arm | `src/Game.cpp:1137,1144` (sound precedes the snap at `:1153-1155`) |

### 5.4 Monsters (G5)

- `domain/game/Enums.h` lacks constants this group needs; add next to the existing ones
  (`Enums.h:287-312`): `MONSTER_ZOMBIE = 0`, `MONSTER_IMP = 3` (`src/Enums.h:60-81`) and
  `MSOUND_ALERT1 = 0, MSOUND_ALERT2 = 1, MSOUND_ALERT3 = 2, MSOUND_IDLE = 5, MSOUND_PAIN = 6,
  MSOUND_DEATH = 7` (`src/Enums.h:85-95`).
- `Combat::monsterSound(sub, parm, type)` (`domain/game/Combat.cpp:462-471`) becomes the full
  `Game::getMonsterSound` port (`src/Game.cpp:3771-3845`) and loses `const`:
  1. `rnd = type == MSOUND_ALERT1 ? nextByte() % 3 : 0` (`:3776-3778`; `nextByte()` = `std::rand() & 0xFF`, `new_src/domain/game/Combat.cpp:47-49`).
  2. Pinky parm 0: ALERT1-3 → 1085, ATTACK1/2 → 1082, IDLE → 1026, PAIN → 1084, DEATH → 1083 (`:3780-3800`).
  3. else Mastermind (`BOSS_MASTERMIND` = 14) parm 1: ALERT1-3 → 1116, IDLE → 1024, PAIN → 1025,
     DEATH → 1115, ATTACK1/2 → fall through to 4 (`:3804-3824`).
  4. else `v = tables->monsterSounds[sub*8 + type + rnd]`; result `v != 255 ? v + 1000 : 255`
     (`:3826-3831`); out-of-range index → 255 (rewrite guard; was 0).
  5. After 2-4: IMP && DEATH → `1050 + std::rand() % 2`; ZOMBIE && DEATH → `1139 + std::rand() % 3`
     (`:3836-3842`, `std::rand`, not `nextByte`).
  The two attack call sites keep working unchanged (step 2 already returned 1082 for attacks).
- Alert (`MonsterSystem.cpp:111-114`): replace the print with
  `if (alertSound && env_.host != nullptr && env_.host->state() == StateId::Playing &&
  !env_.combat->active) play(env_.combat->monsterSound(sub, parm, MSOUND_ALERT1), 0, 3)`
  (`src/Game.cpp:804-807`). The original tests `canvas->state == ST_PLAYING`, which excludes
  `ST_COMBAT` (= 5, `src/Canvas.h:72`); the rewrite has no combat state, `Combat::active` is its
  stand-in (`new_src/core/GameContext.cpp:791`, "ST_COMBAT for the whole tick"). The gate matters:
  it also gates the `nextByte` roll. `MonsterSystem::Env` gains
  `const StateHost* host = nullptr;`, filled at `domain/game/Game.cpp:31` with `host`.
- Pain (`MonsterSystem.cpp:370-372`): `play(monsterSound(sub, parm, MSOUND_PAIN), 0, 3)` (`src/Entity.cpp:347-348`).
- Death in `diedMonster`: immediately before `if (giveXP) awardKillXP(*m);` (still the monster
  def, before the corpse def swap): `play(monsterSound(sub, parm, MSOUND_DEATH), 0, 5)`
  (`src/Entity.cpp:403-406`, called with `b2 = true` from `Entity::died` `:496`).
- Script corpses: `corpsifyMonster(Entity*, int x, int y)` gains `bool deathSound`; at its end
  (where the comment says "checkMonsterDeath sound omitted", :478) `if (deathSound)
  play(monsterSound(sub, parm, MSOUND_DEATH), 0, 5)` (the corpse def has the same sub/parm).
  Callers: `ScriptVM.cpp:640` passes `false` (`src/ScriptThread.cpp:837`), `ScriptVM.cpp:1124`
  (EV_MAKE_CORPSE) passes `true` (`src/ScriptThread.cpp:1621`).
- Idle sounds + sight wake, one pass per rendered world frame (amended 2026-11-04 after the G5
  review). Original: `Render::renderBSP` walks the visible leaves back to front and, PER LEAF,
  first runs `addNodeSprites(leaf)` — whose `addSprite` calls the idle hook and may roll
  `nextByte` — then `renderSpriteObject` over that leaf's sorted sprite list, which holds the
  sight wake / alert that may roll `nextByte` (`src/Render.cpp:1752-1761`, hook `:850-861`, idle
  `:3769-3778`, wake `:1576-1583`). The idle hook runs on EVERY world render (it lives in
  `addSprite`, not behind `disableRenderActivate`).
  - **Data** (`domain/game/ViewVisibility.h`): add `std::vector<int> leafStart;` (index into
    `drawnSprites` where each leaf's sprites begin, in renderBSP order) and `int64_t timeMs = 0;`
    (render clock of that frame). `render/World3D.cpp:1413-1419`: inside the
    `for (k = legacyLeaves_.size()-1 ..)` loop, `vis_.leafStart.push_back((int)vis_.drawnSprites.size())`
    before pushing that leaf's sprites (and split rescues); clear `leafStart` wherever
    `drawnSprites` is cleared/reset for a new frame.
  - **Clock**: `core/GameContext.cpp` render, where it copies `scene_.visibility()` into
    `sys_.game->visibility` (~:1354), also sets `sys_.game->visibility.timeMs = upTimeMs` — the
    clock of the frame being rendered, i.e. the original's `app->time` read inside the render
    (`src/Render.cpp:3772,3776`; `app->time = upTimeMs`, `src/LoadingManager.cpp:719`). The idle
    code uses `vis.timeMs`, never the tick's `upTimeMs` (which `tick()` has already advanced,
    `GameContext.cpp:361`). `renderActivate` loses its `int64_t now` parameter.
  - **Pass**: `void MonsterSystem::renderActivate(ViewVisibility& vis, bool wake)`:
    ```
    if (!vis.valid) return
    const bool wakeOk = wake && !vis.cinematic            // ADR 0029 gating, unchanged
    for each leaf segment [leafStart[i], leafStart[i+1] or drawnSprites.size()):
        for s in segment: idle hook (arm order below)      // addNodeSprites
        if wakeOk: for s in segment:                       // renderSpriteObject
            if (sprites->activePropogators() != 0 || lerps->animatingEffects() != 0) continue;
            ...existing wake body (gates, trace, activate(e, true, true, true, false))...
    vis.drawnSprites.clear(); vis.leafStart.clear();  // valid is NOT cleared: MonsterAI.cpp:403 reads it after this pass (kInfoOnScreenStep); empty lists already make a 2nd call a no-op
    ```
    The propagator/effects test is evaluated per sprite and SKIPS that wake (`continue`, the
    per-sprite gate of `src/Render.cpp:1576-1583`); it never stops later leaves' idle hooks.
    Clearing `valid` makes a second call on the same frame a no-op.
  - Idle hook arm order (`src/Render.cpp:829-861`): skip if `mapSpriteInfo[s] & 0x10000`; skip if
    `& 0x10000000`; skip if `& 0x400000`; skip if tile (`& 0xFF`) is 240/245/246/247; skip if
    `& 0xF000000`; entity = `findEntityBySprite(s)`; skip if null or `info & (kInfoCorpse |
    kInfoHidden)` (0x1010000); if `def->eType == ET_MONSTER`: `idleSound(e, vis.timeMs)`.
  - `idleSound(e, now)`: `sub = def->eSubType`; `if (idleTime_[sub] != 0 && idleTime_[sub] <= now)
    { if (trace->distFrom(e, player->viewX, player->viewY) < combat->tileDistances[3])
    play(monsterSound(sub, (int8_t)parm, MSOUND_IDLE), 0, 1); idleTime_[sub] =
    (combat->nextByte() % 10) * 1000 + now + 6000; }` — re-arms even out of range.
  - `MonsterSystem` keeps `int64_t idleTime_[18] = {}` and `armIdleSounds(int64_t now)`:
    `idleTime_[i] = (combat->nextByte() % 10) * 1000 + now + 12000` for i in 0..17
    (`src/LoadingManager.cpp:734-736`), called at the end of the `tickLoading` load (G2 row).
  - **Callers** (`core/GameContext.cpp`):
    - Playing tick (:790): `renderActivate(vis, true)` (position unchanged; covers ST_COMBAT,
      which is not a separate state in the rewrite).
    - Automap tick (:956): `renderActivate(vis, true)` (`src/AutomapController.cpp:23-24`).
    - NEW, at the end of `tick()` after the state dispatch, idle only:
      `if (state_ is Dialog, Camera, Dying, BotDying or Looting) renderActivate(vis, false)`.
      Original world renders in those states: ST_DIALOG `updateView` → `renderScene`
      (`src/Canvas.cpp:920-922`, `src/MovementController.cpp:541-545`); ST_CAMERA
      `updateView` → `MayaCamera::Render` → `Render::render` (`src/Canvas.cpp:954`,
      `src/MayaCamera.cpp:303-309`); ST_DYING (`src/GameStateRunner.cpp:267,274`); ST_BOT_DYING
      (`:290,308,315`); ST_LOOTING (`src/LootingSystem.cpp:59,71,78` `updateView`).
    - NOT run in: Menu (`menuState` has no world render, `src/GameStateRunner.cpp:203-255`; the
      rewrite still draws the world under the menu — that frame is discarded by the next
      consume or overwritten), InterCamera (the `run()` arm only sets repaint flags,
      `src/Canvas.cpp:823-826`), Loading/Saving/TravelMap/MiniGame/front-end states (no map world
      render).

### 5.5 Core interactions (G6)

| new_src | Change | Original |
|---|---|---|
| `core/PlayerActions.cpp:301` | `play(1111, 0, 3)` | `src/PlayingInputHandler.cpp:456` |
| `core/PlayerActions.cpp:353` | `play(1111, 0, 3)` | `:473` |
| `core/PlayerActions.cpp:355` | `play(cc == 1 ? 1073 : 1072, 0, 3)` | `:476-479` |
| `core/LootSession.cpp:85` | `play(1055, 0, 3)` | `src/LootingSystem.cpp:64` |
| `core/Familiar.cpp:104` | `play(1108, 0, 3)` | `src/Player.cpp:2125` |
| `core/Familiar.cpp:143` | `play(1112, 0, 3)` | `src/Player.cpp:2280` |
| `core/Familiar.cpp:171` | `play(1109, 0, 3)` | `src/Player.cpp:746` |
| `core/Familiar.cpp:192` | `play(1032, 0, 3)` | `src/GameStateRunner.cpp:293` |
| `core/ZoomController.cpp:120,129` | `play(1113, 0, 5)` (no isSoundPlaying guard) | `src/ZoomController.cpp:129,147` |

### 5.6 Front end, menus, dialogs (G7)

| new_src | Change | Original |
|---|---|---|
| `core/IntroMovieSession.cpp:65` | `play(1068, 1, 6)`. The original re-requests it every frame; priority 6 makes repeats no-ops and nothing stops it mid-movie, so one call at begin is equivalent. | `src/IntroSequenceManager.cpp:780` |
| `core/GameContext.cpp:700` | `play(1071, 1, 3)` | `src/Canvas.cpp:1404` |
| `core/GameContext.cpp:706` | `S.soundStop()` | `src/IntroSequenceManager.cpp:872` |
| `core/GameContext.cpp:455` | `S.soundStop()` (keep the `loadMapID != 0` gate) | `src/InputEventController.cpp:408-410` |
| `core/MenuSession.cpp:1720` | `play(1125, 0, 3)` | `src/MenuSystem.cpp:2983-2985` |
| `core/MenuSession.cpp` `select`, right after the help/notebook `back(); return;` branch, before `switch` | `play(1086, 0, 5)` (MORE_GAMES does not exist in the rewrite) | `src/MenuSystem.cpp:3002-3013` |
| `core/MenuSession.cpp:1859` | `play(1071, 1, 3)` under the existing gate | `src/MenuSystem.cpp:3087-3090` |
| `core/MenuSession.cpp:1865` | `S.soundStop()` | `src/MenuSystem.cpp:3209-3219` |
| `core/MenuSession.cpp:1934` `back` | `if (menu_ != kMenuMain && !S.isSoundPlaying(1122)) play(1122, 0, 5)` (divergence fixed) | `src/MenuSystem.cpp:578-580` |
| `core/MenuSession.cpp` `handleAction` Forward/Back/TurnLeft/TurnRight | before `moveDir`/`scrollPage*`: `if (numItems_ > 0 && (items_[selectedIndex_].def.flags & 0x100) == 0) S.soundClick()` (block-text rows scroll their text silently; if the rewrite has no 0x100 handling the flag test still applies) | `src/MenuSystem.cpp:311-357` |
| `core/MenuSession.cpp` `moveDir` | keep the help/notebook early `return` (types 5/7 page silently, src/MenuSystem.cpp:516-521 — corrected after G7 review); record `px0 = scrollPixels()` at entry; tail: `if (!isMainMenu(menu_) && menu_ != kMenuComicBook && contentHeight() - viewPx() > 0 && scrollPixels() != px0) play(1065, 0, 5)` | `src/MenuSystem.cpp:461-546` ([GEC] scrollbar refresh at the end of `moveDir`, sound at :539-541) |
| `core/MenuSession.cpp` `showHelp` (menu info button) | interim only — DELETE in G8: Action::MenuInfo comes only from the touch info button, whose sound in the original is the fmButton highlight 1027 (src/MenuSystem.cpp:4796-4805); :2849-2856 is the key path AVK_ITEMS_INFO, which the rewrite does not have | `src/MenuSystem.cpp:4796-4805` |
| `core/CharacterSelectSession.cpp:53,75` | `S.soundClick()` | `src/IntroSequenceManager.cpp:646,655,672` |
| `core/CharacterSelectSession.cpp:67,77` | `play(1086, 0, 5)` | `:662,685` |
| `core/CharacterSelectSession.cpp:83` | `play(1122, 0, 5)` | `:691` |
| `core/ComicBookSession.cpp:86` | `if (buttonTouch(x, y) && !S.isSoundPlaying(1064)) play(1064, 0, 5)` (divergence fixed; keep `buttonTouch` evaluated first) | `src/ComicBook.cpp:734-736` |
| `domain/game/DialogSystem.cpp:169` | `if (style == 2) play(1027, 0, 3)` | `src/DialogSystem.cpp:715-717` |

Research row correction: the 1065 call at `src/MenuSystem.cpp:539-541` is inside `moveDir`
(function spans `:401-546`), i.e. key navigation that moves the scrollbar thumb, not the touch
drag the UI inventory calls it.

### 5.7 Widget press sounds (G8)

The original's `fmButton::SetHighlighted` plays the button's `soundResID` (flags 0, pri 5) on
the highlight rising edge unless already playing (`src/Button.cpp:145-158`). The rewrite maps it
onto the Ui press latch:

- `ui/UiState.h`: add `UiId pressedId = UiId::None;` (press edge of this frame).
- `ui/Ui.cpp`: `beginFrame` sets `state().pressedId = UiId::None`; `hitTest`, where it sets
  `st.activeId = id` on the press edge, also sets `st.pressedId = id`.
- `core/GameContext.cpp`, just before `ui->endFrame()` (:1633): `playPressSound(ui->state().pressedId)`
  — a file-local function:
  ```cpp
  int pressSound(UiId id)   // -1 = silent
  ```
  | UiId | resID | Original |
  |---|---|---|
  | `HudSwitchLeft`, `HudSwitchRight`, `HudWeapon`, `HudPortrait`, `HudShield`, `HudHealth`, `HudKeys`, `HudSoftLeft`, `HudSoftRight` | 1121 | HUD buttons 0-6 `src/Hud.cpp:67-85` (buttons 0/1 are the corner soft keys) |
  | `HudWeaponSel0..14` | 1027 | `src/Hud.cpp:90` |
  | `DialogPageUp`, `DialogPageDown`, `DialogOk`, `DialogChoiceLeft`, `DialogChoiceRight` | 1027 | `src/Canvas.cpp:327-348` |
  | `MenuRow0..8`, `MenuInfo0..8` | 1027 | `src/MenuSystem.cpp:173-256` |
  | `MenuSoftLeft` (back, button 11), `MenuSoftRight` (return to game, button 15) | 1122 | `src/MenuSystem.cpp:193,219` |
  | `CharSlot0..2`, `CharYes`, `CharBack`, `StoryBack`, `StoryMore`, `StorySkip` | 1027 | `src/IntroSequenceManager.cpp:37-47` |
  | `AutomapSoftLeft/Right` (fmButton 19/20, src/Canvas.cpp:355-357), `DialogBody` (button 8, src/Canvas.cpp:348, src/DialogSystem.cpp:491) | 1027 | corrected after G8 review |
| HUD range while `blockInputTime != 0` | -1 | src/Hud.cpp:1238-1239 |
| everything else (`HudSoftCenter`, loot) | -1 | no fmButton sound verified |
  `playPressSound`: `if (snd != -1 && !S.isSoundPlaying(snd)) S.playSound(snd, 0, 5)`.
- Minigames keep their own `highlightedButtonId` edge (G9); they do not use these UiIds.

### 5.8 Minigames (G9)

| new_src | Change | Original |
|---|---|---|
| `minigames/hacking/HackingGame.cpp:208,408` | `S.soundClick()` | `src/HackingGame.cpp:221,225,389` |
| `minigames/hacking/HackingGame.cpp:342` | `if (hl != -1 && hl != lastHighlight_ && !S.isSoundPlaying(1027)) play(1027, 0, 5)` | `src/HackingGame.cpp:95,98` + `src/Button.cpp:150-155` |
| `minigames/hacking/HackingGame.cpp:293` | `play(n == 1 ? 1043 : 1040, 0, 3)` | `src/HackingGame.cpp:622` |
| `minigames/sentrybot/SentryBotGame.cpp:202,208,337` | `S.soundClick()` | `src/SentryBotGame.cpp:234,242,513` |
| `minigames/sentrybot/SentryBotGame.cpp:305` | highlight edge as above (guarded) | `:101-114` |
| `minigames/sentrybot/SentryBotGame.cpp:271` | `play(n == 1 ? 1043 : 1040, 0, 3)` | `:809` |
| `minigames/vending/VendingMachine.cpp:282 / 288` | `play(1043, 0, 3)` / `play(1040, 0, 3)` | `src/VendingMachine.cpp:335,343` |
| `minigames/vending/VendingMachine.cpp:297,303,309,312,555` | `S.soundClick()` | `:353-372,597-599` |
| `minigames/vending/VendingMachine.cpp:384` | highlight edge, guarded | `:81-110` |
| `minigames/common/MiniGameHost.cpp:81` | `play(1071, 1, 3)` | `src/HackingGame.cpp:633`, `src/SentryBotGame.cpp:821`, `src/VendingMachine.cpp:861` |

### 5.9 Skipped — feature not ported in new_src

| Sound | Original | Missing feature |
|---|---|---|
| 1037 gib | `src/Combat.cpp:944` | corpse gib branch (`new_src/domain/game/Combat.cpp:837` deferred) |
| 1032 rocket impact (+shake) | `src/Combat.cpp:1297` | projectile 4 |
| 1135 toilet smash | `src/Combat.cpp:1345` | projectile 13 / thrown toilet |
| 1046 / 1134 refill / toilet pull | `src/PlayingInputHandler.cpp:411,427,437` | toilet/sink interactions |
| 1123 VIOS teleport | `src/Entity.cpp:736` | boss AI |
| 1047 / 1003 boss footsteps | `src/Game.cpp:2931-2934` | boss walk lerp (`new_src/domain/game/SpriteLerps.cpp:259` omitted) |
| 1020 kill-all achievement (n=2) | `src/Player.cpp:1988` via `src/Entity.cpp:417` | `fillMonsterStats` achievement |
| 1054 / 1073 / 1072 armor-repair dialog | `src/DialogSystem.cpp:575-591` | dialog style 13 |
| 1069 after epilogue, soundStop at end ranking / ending credits | `src/IntroSequenceManager.cpp:143-145`, `src/MenuSystem.cpp:2990-2993`, `src/InputEventController.cpp:387-391` | epilogue / end ranking / `endingGame` |
| Options / Sound menu sliders + 1027 steps | `src/MenuSystem.cpp:2304-2343,5818-5867`, `src/InputEventController.cpp:186-271` | Options menu (separate PLAN item; it will call `setSoundFxVolume`/`setMusicVolume`) |
| 1113 sniper dial drag (fmScrollButton, guarded) | `src/Canvas.cpp:241`, `src/Button.cpp:519-526` | dial drag widget (the Z/X steps are wired, §5.5) |
| 1027 target-practice buttons | `src/MiniGameManager.cpp:36,42` | target-practice touch buttons |
| 1027 canvas soft-key buttons 19/20 | `src/Canvas.cpp:355,357` | no UiId for those outside the HUD |

## 6. Deviations

- **SND-D1** SDL software mixer instead of OpenAL: 10 voices, linear gain `vol/100`, every file
  resampled once to S16 stereo 44100 Hz by `SDL_AudioCVT`. Audible result equals OpenAL's
  non-positional playback (no attenuation, `AL_REFERENCE_DISTANCE 5000000`, `src/Sound.cpp:280-295`)
  up to resampler quality.
- **SND-D2** PCM cache per resID decoded on first play and kept until shutdown, instead of one
  re-decoded buffer per channel (`src/Sound.cpp:414-420`). Not observable except for the absence
  of repeat decode hitches.
- **SND-D3** An out-of-range resID or an unloadable file is logged once and ignored; the original
  indexes the table unchecked and calls `app->Error` (`src/Sound.cpp:180,188,272-274`).
- **SND-D4** No audio device (or SDL audio init failure) → silent mode: `playSound`/`stop*` are
  no-ops and `isSoundPlaying` is false. Game logic does not change: the RNG rolls of
  `monsterSound` happen in the callers before `playSound`, so golden captures are unaffected
  (they never depended on audio and `std::rand` is time-seeded, `new_src/core/Main.cpp:42`).
  `SDL_INIT_AUDIO` leaves the fatal `SDL_Init` in `Window`.
- **SND-D5** Widget sounds fire on the Ui press edge (`Ui::hitTest` latch). The original also
  fires when a held finger slides onto another button (fmButton highlight re-evaluation); the
  rewrite's latch does not move with the finger.
- **SND-D6** The `moveDir` 1065 test compares the list's pixel scroll offset instead of the GEC
  scrollbar thumb field `field_0x48_`; it can fire on an offset change too small to move the thumb.
- **SND-D7** Loaded config volumes are clamped to 0..100. Configs written by earlier rewrite
  builds carry the placeholder volumes 100/100 and load as such (one-time difference from the
  80/80 default).
- **SND-D8** Rewrite-only debug aids: F7 plays 1086 pistol, Shift+F7 toggles 1068 looped
  (removable, like F3); env `D2R_SOUNDLOG=1` traces every call.
- **SND-D10** The scene render inside the map-load tail (`src/LoadingManager.cpp:724`, after
  the allowSounds restore at `:721` and BEFORE the idle timers are re-armed at `:734-736`) is not
  modelled: in the original that render can fire an idle sound / `nextByte` roll with the previous
  map's timers. The rewrite arms the timers with no preceding idle pass.
- **SND-D11** RNG-order constraint for future work: in `Entity::died` the `monsterEffects`
  re-stamp and the Lost Soul / Cacodemon poof (`src/Entity.cpp:478-483`, `:491-495`) run before
  `checkMonsterDeath` (`:496`), whose death sound rolls `std::rand` for imps/zombies. Whoever ports
  those blocks into `MonsterSystem::diedMonster` must place them BEFORE the death-sound call.
- **SND-D13** The idle-only pass is gated on the state at the END of the tick, not the state the frame was rendered in: on a Dialog/Camera → Playing transition one frame of idle hooks can be lost (rare; golden identical). Fix if needed: stamp the render state into ViewVisibility next to timeMs.
- **SND-D12** Within one leaf, the wake pass visits sprites in `drawnSprites` order (sprite index),
  not the original's depth-sorted `viewSprites` list built by `addSprite`; leaf order and the
  idle-then-wake split per leaf are exact. Frames rendered between two ticks are overwritten
  unconsumed (only the last frame before a tick runs the pass); idle timers are ≥ 6 s, so only the
  triggering frame can shift.
- **SND-D9** Monster idle sounds iterate `ViewVisibility::drawnSprites` (the sprites the last
  world frame drew) instead of hooking every `addSprite`; same per-frame cadence (`renderActivate`
  runs once per rendered frame, `new_src/core/GameContext.cpp:777-782`).

## 7. Implementation groups

Each group builds (`cmake --build build_new -j 8`) and leaves `grep -rn '\[sound\]' new_src`
shrinking. Reconfigure once in G1 (`cmake -S . -B build_new -DCMAKE_BUILD_TYPE=Debug`, GLOB).

### G1 — engine (AudioOutput + Sound + startup + debug key)
Files: `new_src/platform/AudioOutput.h/.cpp` (new), `new_src/core/Sound.h/.cpp` (new),
`new_src/platform/Window.cpp` (drop `SDL_INIT_AUDIO`), `new_src/core/AppContext.cpp`
(`startup()` → `Sound::instance().startup(reader)`; `shutdown()` first line
`Sound::instance().shutdown()`), `new_src/core/GameLoop.cpp` (F7 / Shift+F7 next to F3:
`ev.key.repeat == 0`; Shift = `ev.key.keysym.mod & KMOD_SHIFT`; Shift+F7:
`isSoundPlaying(1068) ? stopSound(1068) : playSound(1068, 1, 3)`).
Acceptance:
- stdout/stderr shows `[sound] device 44100 Hz S16 stereo` once.
- User: F7 → pistol shot audible; rapid F7 presses overlap (duplicates allowed). Shift+F7 →
  general music starts and loops after ~64 s; Shift+F7 again stops it.
- `D2R_SOUNDLOG=1`: 11 fast F7 presses log channels 0..9, the 11th reuses or steals per §3.3
  (pistol is short; reuse of the first finished channel is expected).
- `SDL_AUDIODRIVER=nonexistent ./DoomIIRPG` → `[sound] no audio device (...): silent`, game runs,
  F7 does nothing, no crash. `DOOM2RPG_CAPTURE_TICKS` golden run unchanged.
- Debug self-check asserts on the name table pass at startup.

### G2 — persistence + load/skip/start windows
Files: `core/SaveGame.cpp` (§4), `core/LoadingManager.cpp` (§5.1 rows 1-3),
`core/GameContext.cpp` `tickLoading` (mute window; the `armIdleSounds` call comes with G5),
`core/CinematicCamera.cpp` (skip window).
Acceptance: with `D2R_SOUNDLOG=1` a map change logs `stop all` and level end logs
`play 1069 fl=1 pri=3`; a fresh slot writes allowSounds/fx/music = 1/80/80 at byte offsets 5..7 (after version int 0-3 and difficulty byte 4)
of the config file (`xxd` on the config in `Doom2rpg.app/`), an existing config with 100/100
loads as 100/100. User: map00 boot plays the general theme from the init script after a config exists;
finishing a level plays the level-end music on the stats screen; skipping a cinematic is silent
during the fast-forward.

### G3 — script opcodes
Files: `domain/game/ScriptVM.cpp` (§5.2).
Acceptance: map00 — the theme starts once (`PLAYSOUND 1068`), and stops at the script's
`STOPSOUND 1068`; locking/unlocking a door via script clicks (1065); armor repair with < 5 shards
says "no use".

### G4a — player and combat
Files: `domain/game/Combat.cpp`, `domain/game/Player.cpp`, `domain/game/Game.h`, `core/Main.cpp`,
`core/GameContext.cpp` (death row) — §5.3 G4a.
Acceptance: firing each weapon plays its sound; taking a hit plays player_pain1 (Major/Sarge) or
player_pain2 (characterChoice 1); dying plays the character's death cry; a level-up stops the
music and plays the level-up jingle; using a medkit plays use_item.

### G4b — items, secrets, glass, doors
Files: `domain/game/ItemPickup.cpp`, `domain/game/Game.cpp`, `domain/game/DoorSystem.cpp` — §5.3 G4b.
Acceptance: walking over an item → item_pickup; opening/closing doors → door_open/door_close
(also auto-close); breaking glass → glass; push-wall secret → secret jingle; script secret → chime.

### G5 — monsters
Files: `domain/game/Enums.h` (constants), `domain/game/Combat.h/.cpp` (`monsterSound` full port), `domain/game/MonsterSystem.h/.cpp`
(alert gate, pain, death, corpsify param, idle timers), `domain/game/Game.cpp:31` (host),
`domain/game/ScriptVM.cpp:640,1124` (corpsify arg), `core/GameContext.cpp` (`renderActivate`
now-arg at :782/:948, `armIdleSounds` in `tickLoading`) — §5.4.
Fix list after the 2026-11-04 review (FAIL):
1. Alert gate adds `&& !env_.combat->active` (§5.4 Alert).
2. `ViewVisibility` gains `leafStart` + `timeMs`; `World3D.cpp:1413-1419` pushes `leafStart`
   per leaf; `GameContext` render stamps `timeMs = upTimeMs` when copying the visibility.
3. `renderActivate(vis, bool wake)` interleaves idle-then-wake per leaf, per-sprite `continue` on
   the propagator/effects gate, uses `vis.timeMs`, clears `drawnSprites`/`leafStart`/`valid`.
4. `GameContext::tick` runs the idle-only pass (`wake = false`) in Dialog, Camera, Dying,
   BotDying, Looting; Playing and Automap keep their `wake = true` calls.
5. No code for SND-D10/D11; leave a one-line comment at `diedMonster`'s death-sound call citing
   SND-D11.
Acceptance: a monster waking up shouts one of its sight sounds (zombies vary between 3);
hurting it plays its pain sound; killing it plays its death sound (imps/zombies vary); a visible
idle monster within 4 tiles grunts every 6-15 s; small pinkies use the small-pinky set.

### G6 — core interactions
Files: `core/PlayerActions.cpp`, `core/LootSession.cpp`, `core/Familiar.cpp`,
`core/ZoomController.cpp` — §5.5.
Acceptance: "no use" grunt on walls; loot crouch plays loot sound once; bot deploy/return/death/
self-destruct sounds; scope zoom steps click.

### G7 — front end, menus, dialogs
Files: `core/IntroMovieSession.cpp`, `core/GameContext.cpp` (backToMain/exitIntro/credits rows),
`core/MenuSession.cpp`, `core/CharacterSelectSession.cpp`, `core/ComicBookSession.cpp`,
`domain/game/DialogSystem.cpp` — §5.6.
Acceptance: intro movie has the general theme; main menu plays the title theme (continuous
across sub-menus, Credits from the main menu keeps it); menu Up/Down clicks, scrolling a long
list adds the scroll swoosh, selecting a row fires the pistol, Back plays switch_exit (not on
the main menu, not stacked when spammed); character select clicks/pistol/back; help dialogs
(style 2) chime 1027.

### G8 — widget press sounds
Files: `ui/UiState.h`, `ui/Ui.cpp`, `core/GameContext.cpp` — §5.7.
Acceptance: clicking a HUD bottom-bar button plays the switch click once per press; weapon-grid
icons, dialog page/OK/Yes/No buttons, menu rows and character-select buttons click (1027);
menu Back/Resume soft keys play switch_exit. Holding the mouse does not repeat.

### G9 — minigames
Files: the four minigame files of §5.8.
Acceptance: cursor clicks in Hacker / Matrix Skip / vending; win/lose jingles; returning from a
main-menu minigame restarts the title theme.

Final check after G9: `grep -rn '\[sound\]\|fire sound\|alert sound\|pain sound\|\[loot\] sound\|\[dialog\] sound\|PLAYSOUND\|STOPSOUND' new_src`
returns only `core/Sound.cpp` log formats and opcode enum names.
