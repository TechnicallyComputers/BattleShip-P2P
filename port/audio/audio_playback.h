#pragma once

// Audio playback bridge — feeds PCM audio to libultraship's audio player.
// Phase 1: generates silence to prove the LUS audio output path works.

#ifdef __cplusplus
extern "C" {
#endif

// Push one frame of audio to the LUS audio player.
// Currently produces silence; will be wired to the N64 audio synth later.
// Called once per audio tick from the audio coroutine thread.
void portAudioPushSilence(void);

#ifdef __cplusplus
}
#endif
