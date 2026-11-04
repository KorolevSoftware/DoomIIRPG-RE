# ADR 0046 — Sound: own SDL mixer under the legacy channel rules, `Sound::instance()` service, lazy PCM cache

Date: 2026-11-04. Status: accepted. Spec: `../sound.md`. Facts: `docs/original-code/sound.md`.

## Context

The rewrite has no audio; about 60 call sites print `[sound] N` placeholders. The original
(`src/Sound.cpp`) is a thin layer of 10 OpenAL (buffer, source) pairs with observable rules:
flags bit0 loop / bit1 stop-all, priority stealing, priority 6 = no restart, first-matching-slot
stop/query, music 1067..1071 on a separate volume, 0..100 volumes where 0 = off. The archive
holds 145 referenced WAVs in mixed formats (8/16-bit, mono/stereo, 8000..48000 Hz). The callers
live in every layer: `domain/game` (combat, monsters, player, scripts), `core` (sessions,
loading), `minigames`. No new dependency may be added; SDL2 is already linked.

## Decision

1. **Own mixer on the SDL audio device** (`platform/AudioOutput`): one callback device at S16
   stereo 44100 Hz with `allowed_changes = 0`, 10 voices, integer linear gain `vol/100`, files
   decoded with `SDL_LoadWAV_RW` and converted once with `SDL_AudioCVT`. Every public method
   takes `SDL_LockAudioDevice`; PCM buffers are immutable after decode and outlive the device.
2. **Legacy semantics in `core/Sound`**, a line-by-line port of `playSound`/`getFreeSlot`/
   `soundStop`/`stopSound`/`isSoundPlaying`/`updateVolume`, including the duplicate-copy rule
   (a busy non-priority-6 sound starts a second copy). The original's dead paths (fades,
   deferred `startFrame` play, system volume, suspend) are not ported.
3. **Process-wide service `Sound::instance()`**, mirroring `app->sound->...`. It is started and
   stopped by `AppContext` (`startup`/`shutdown`), reads `sounds2/` through `readResource`, and
   is a safe no-op before startup or without a device.
4. **Lazy PCM cache**: decode on first play, keep until shutdown. ≈63 MB if all 145 were played,
   ≈30-40 MB in a normal session; pre-decoding would cost 39 MB of inflate at every launch.
5. **Volumes persist in the existing config v11 bytes** (`SaveGame`), clamped on load; the
   saved `allowSounds` bool only feeds `areSoundsAllowed`, as in the original.
6. **Audio init leaves `SDL_Init`**: `AudioOutput::open` calls `SDL_InitSubSystem(SDL_INIT_AUDIO)`
   and its failure means silent mode, not a startup failure.
7. **Widget sounds via one press latch**: `UiState::pressedId` set by `Ui::hitTest`, mapped to
   resIDs in one `GameContext` table, instead of per-widget sound fields.

## Consequences

- One include (`core/Sound.h`, no SDL in it) and one line per call site; no Env plumbing through
  ~20 classes. `domain/game` now depends on one more `core/` header (it already includes
  `core/GameStates.h` and `core/GameContext.h`).
- Hidden global state: tests or tools that construct game objects without `AppContext` get a
  silent no-op `Sound`, which is the wanted behaviour.
- Golden captures are unaffected: audio has no effect on frames, and the monster-sound RNG rolls
  happen in the callers whether or not a device exists.
- The first play of a music track decodes ≈10 MB on the game thread (tens of ms, once).
- `Game` gains `const StateHost* host` so monster alert and level-up can apply the original
  `ST_PLAYING` / `!= ST_MENU` gates.
- `[sound]` stderr lines disappear; `D2R_SOUNDLOG=1` brings back a per-call trace.

## Rejected alternatives

- **OpenAL / SDL_mixer**: new dependencies; forbidden by the task.
- **`SDL_QueueAudio` push model**: needs a per-frame mixing pump on the game thread and a latency
  buffer policy; the callback with a lock is simpler and has no pump.
- **`SDL_AudioStream` per voice**: converts on the fly in the callback for no gain; the files are
  small enough to convert once.
- **Pre-decode all 145 at startup**: 39 MB inflate per launch and ≈63 MB resident for sounds
  most sessions never play.
- **Per-channel one-entry cache like the original**: re-decodes 10 MB music whenever a channel
  is reassigned; not observable, only slower.
- **`Sound*` in every Env struct**: ~20 Env/setter changes and wiring in `Main.cpp` for a
  service the original itself reached globally; no testability gain since `Sound` is already a
  no-op without a device.
- **Highlight-edge tracking per widget (fmButton parity)**: needs per-button highlight state
  across frames in a stateless view layer; the press edge is the only difference (SND-D5).
