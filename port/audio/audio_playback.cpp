// Audio playback bridge — Phase 1: silence pipeline
//
// Generates zeroed PCM and feeds it to libultraship's AudioPlayerPlayFrame()
// to prove the audio output path works end-to-end. The audio coroutine calls
// portAudioPushSilence() once per tick.

#include "audio_playback.h"
#include "port_log.h"

#include <libultraship/bridge/audiobridge.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// SSB64 original output rate is 32 kHz.
// At 60 fps that's ~533 samples/frame. We use the same high/low watermarks
// as Starship to keep the audio player's ring buffer comfortably filled.
static constexpr int32_t SAMPLES_PER_FRAME_HIGH = 560;
static constexpr int32_t SAMPLES_PER_FRAME_LOW  = 528;

// Maximum buffer size in s16 samples (stereo: 2 channels * SAMPLES_HIGH).
static constexpr int32_t MAX_SAMPLES_STEREO = SAMPLES_PER_FRAME_HIGH * 2;

// Persistent silence buffer — zeroed once, reused every frame.
static int16_t sSilenceBuf[MAX_SAMPLES_STEREO];
static bool    sInitialized = false;

extern "C" void portAudioPushSilence(void)
{
    if (!sInitialized) {
        std::memset(sSilenceBuf, 0, sizeof(sSilenceBuf));
        sInitialized = true;
        port_log("SSB64 Audio: silence pipeline active (phase 1)\n");
    }

    int32_t numChannels = GetNumAudioChannels();
    if (numChannels < 1) {
        numChannels = 2; // fallback to stereo
    }

    // Determine how many samples to submit this frame.
    // Mirror the Starship approach: if the player is under-buffered, send the
    // high watermark; otherwise send the low watermark.
    int32_t buffered = AudioPlayerBuffered();
    int32_t desired  = AudioPlayerGetDesiredBuffered();

    int32_t samplesPerFrame;
    if (buffered < desired) {
        samplesPerFrame = SAMPLES_PER_FRAME_HIGH;
    } else {
        samplesPerFrame = SAMPLES_PER_FRAME_LOW;
    }

    // Total s16 values = samples * channels.  Byte length = that * 2.
    size_t totalSamples = (size_t)samplesPerFrame * (size_t)numChannels;
    size_t byteLen      = totalSamples * sizeof(int16_t);

    // Safety: clamp to buffer size.
    if (byteLen > sizeof(sSilenceBuf)) {
        byteLen = sizeof(sSilenceBuf);
    }

    AudioPlayerPlayFrame(reinterpret_cast<const uint8_t*>(sSilenceBuf), byteLen);
}

/* ---------------------------------------------------------------------- */
/*  WAV dump for offline inspection                                       */
/*                                                                        */
/*  Writes the first ~10 seconds of submitted PCM to /tmp/ssb64_dump.wav  */
/*  (interleaved stereo s16 @ 32 kHz).  Open in Audacity to look at the   */
/*  waveform / spectrogram and decide whether the noise is wrong-codec,   */
/*  wrong-buffer-routing, or wrong-rate.                                  */
/* ---------------------------------------------------------------------- */

static FILE *sWavFile = nullptr;
static uint32_t sWavBytesWritten = 0;
static const uint32_t kWavMaxBytes = 32000u * 2u * 2u * 10u; /* 10 s stereo s16 @ 32 kHz */

static void wavWriteHeader(FILE *fp, uint32_t dataBytes)
{
    /* Standard 44-byte RIFF/WAVE/fmt/data header for s16 stereo 32 kHz. */
    const uint16_t channels   = 2;
    const uint32_t sampleRate = 32000;
    const uint16_t bitsPer    = 16;
    const uint16_t blockAlign = channels * (bitsPer / 8);
    const uint32_t byteRate   = sampleRate * blockAlign;
    const uint32_t riffSize   = 36 + dataBytes;

    auto w16 = [&](uint16_t v) { std::fwrite(&v, 1, 2, fp); };
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 1, 4, fp); };

    std::fwrite("RIFF", 1, 4, fp);  w32(riffSize);
    std::fwrite("WAVE", 1, 4, fp);
    std::fwrite("fmt ", 1, 4, fp);  w32(16); /* PCM fmt chunk size */
    w16(1); /* PCM format */
    w16(channels);
    w32(sampleRate);
    w32(byteRate);
    w16(blockAlign);
    w16(bitsPer);
    std::fwrite("data", 1, 4, fp);  w32(dataBytes);
}

static void wavMaybeOpen(void)
{
    if (sWavFile != nullptr || sWavBytesWritten >= kWavMaxBytes) return;
    sWavFile = std::fopen("/tmp/ssb64_dump.wav", "wb");
    if (sWavFile == nullptr) {
        port_log("SSB64 Audio: WAV dump open failed (errno preserved by fopen)\n");
        return;
    }
    /* Placeholder header — finalized when we close. */
    wavWriteHeader(sWavFile, 0);
    port_log("SSB64 Audio: WAV dump opened /tmp/ssb64_dump.wav (will capture %u bytes ~ 10 s)\n",
             (unsigned)kWavMaxBytes);
}

static void wavAppend(const void *buf, size_t bytes)
{
    if (sWavFile == nullptr) return;
    if (sWavBytesWritten >= kWavMaxBytes) return;
    if (bytes > kWavMaxBytes - sWavBytesWritten) {
        bytes = kWavMaxBytes - sWavBytesWritten;
    }
    std::fwrite(buf, 1, bytes, sWavFile);
    sWavBytesWritten += (uint32_t)bytes;
    if (sWavBytesWritten >= kWavMaxBytes) {
        /* Finalize: rewrite header with actual data size, then close. */
        std::fseek(sWavFile, 0, SEEK_SET);
        wavWriteHeader(sWavFile, sWavBytesWritten);
        std::fclose(sWavFile);
        sWavFile = nullptr;
        port_log("SSB64 Audio: WAV dump finalized (%u bytes captured)\n",
                 (unsigned)sWavBytesWritten);
    }
}

extern "C" void portAudioSubmitFrame(const void *buf, int sampleCount)
{
    if (buf == nullptr || sampleCount <= 0) {
        portAudioPushSilence();
        return;
    }

    // n_alAudioFrame produces interleaved stereo s16 PCM.
    // Total bytes = sampleCount * 2 channels * 2 bytes per sample.
    size_t byteLen = (size_t)sampleCount * 4;

    // One-time log: confirm the synthesis pipeline is flowing.
    static bool sFirstSubmit = true;
    static bool sLoggedNonzero = false;
    if (sFirstSubmit) {
        sFirstSubmit = false;
        port_log("SSB64 Audio: first synth frame submitted (sampleCount=%d bytes=%zu)\n",
                 sampleCount, byteLen);
    }
    if (!sLoggedNonzero) {
        const int16_t *s = reinterpret_cast<const int16_t *>(buf);
        size_t count = byteLen / sizeof(int16_t);
        for (size_t i = 0; i < count; i++) {
            if (s[i] != 0) {
                sLoggedNonzero = true;
                port_log("SSB64 Audio: first non-zero sample detected (idx=%zu v=%d)\n",
                         i, (int)s[i]);
                wavMaybeOpen();
                break;
            }
        }
    }
    wavAppend(buf, byteLen);

    AudioPlayerPlayFrame(reinterpret_cast<const uint8_t*>(buf), byteLen);
}
