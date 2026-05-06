/**
 * audio_dump_plugin.c — Mupen64Plus audio plugin that dumps PCM to WAV
 *
 * Captures every audio buffer the N64 audio interface (AI) submits and
 * appends the byte-swapped s16 stereo PCM to /tmp/emu_audio.wav.  No
 * actual playback — purely a recording sink for debugging.
 *
 * Format (per N64 AI):
 *   - 16-bit signed stereo PCM, big-endian in RDRAM (we byteswap to LE)
 *   - sample rate computed from AI_DACRATE_REG: rate = VI_clock / (DACRATE+1)
 *
 * Output WAV header is a placeholder (size=0); finalized on PluginShutdown
 * with the actual data length.  Open the result in Audacity to compare
 * with /tmp/ssb64_dump.wav from the port.
 *
 * Env vars:
 *   M64P_AUDIO_DUMP_PATH   output WAV path (default: /tmp/emu_audio.wav)
 *   M64P_AUDIO_DUMP_LIMIT  max bytes to capture (default: 10 s @ 32kHz stereo = 1280000)
 */
#include "m64p_plugin_api.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#define CALL   __cdecl
#else
#define EXPORT __attribute__((visibility("default")))
#define CALL
#endif

/* ========================================================================= */
/*  Audio plugin info struct (not in shared header — keep local)             */
/* ========================================================================= */

typedef struct {
    uint8_t  *RDRAM;
    uint8_t  *DMEM;
    uint8_t  *IMEM;
    uint32_t *MI_INTR_REG;
    uint32_t *AI_DRAM_ADDR_REG;
    uint32_t *AI_LEN_REG;
    uint32_t *AI_CONTROL_REG;
    uint32_t *AI_STATUS_REG;
    uint32_t *AI_DACRATE_REG;
    uint32_t *AI_BITRATE_REG;
    void (*CheckInterrupts)(void);
} AUDIO_INFO;

/* ========================================================================= */
/*  State                                                                    */
/* ========================================================================= */

static AUDIO_INFO sAi;
static FILE      *sWav        = NULL;
static char       sPath[1024] = "/tmp/emu_audio.wav";
static uint32_t   sBytesWritten = 0;
static uint32_t   sBytesLimit   = 32000u * 4u * 10u; /* 10 s s16 stereo @ 32 kHz */
static uint32_t   sSampleRate   = 32000;             /* updated by AiDacrateChanged */
static int        sFinalized    = 0;
static int        sInitialized  = 0;

/* NTSC VI clock per mupen64plus */
#define NTSC_VI_CLOCK 48681812

/* ========================================================================= */
/*  WAV helpers                                                              */
/* ========================================================================= */

static void wav_write_header(FILE *fp, uint32_t data_bytes, uint32_t sample_rate)
{
    const uint16_t channels   = 2;
    const uint16_t bits       = 16;
    const uint16_t block_align = channels * (bits / 8);
    const uint32_t byte_rate   = sample_rate * block_align;
    const uint32_t riff_size   = 36 + data_bytes;

    fwrite("RIFF", 1, 4, fp);
    fwrite(&riff_size, 1, 4, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    {
        uint32_t fmt_size = 16; uint16_t fmt = 1;
        fwrite(&fmt_size, 1, 4, fp);
        fwrite(&fmt, 1, 2, fp);
        fwrite(&channels, 1, 2, fp);
        fwrite(&sample_rate, 1, 4, fp);
        fwrite(&byte_rate, 1, 4, fp);
        fwrite(&block_align, 1, 2, fp);
        fwrite(&bits, 1, 2, fp);
    }
    fwrite("data", 1, 4, fp);
    fwrite(&data_bytes, 1, 4, fp);
}

static void wav_open_if_needed(void)
{
    if (sWav != NULL || sFinalized) return;
    {
        const char *env = getenv("M64P_AUDIO_DUMP_PATH");
        if (env && *env) {
            strncpy(sPath, env, sizeof(sPath) - 1);
            sPath[sizeof(sPath) - 1] = '\0';
        }
    }
    {
        const char *lim = getenv("M64P_AUDIO_DUMP_LIMIT");
        if (lim && *lim) {
            sBytesLimit = (uint32_t)strtoul(lim, NULL, 10);
        }
    }
    sWav = fopen(sPath, "wb");
    if (!sWav) {
        fprintf(stderr, "[audio_dump] failed to open %s\n", sPath);
        sFinalized = 1;
        return;
    }
    /* Placeholder header — will be rewritten on shutdown / limit hit. */
    wav_write_header(sWav, 0, sSampleRate);
    fprintf(stderr, "[audio_dump] writing %s (limit=%u bytes, rate=%u Hz)\n",
            sPath, (unsigned)sBytesLimit, (unsigned)sSampleRate);
}

static void wav_finalize(void)
{
    if (!sWav || sFinalized) return;
    fseek(sWav, 0, SEEK_SET);
    wav_write_header(sWav, sBytesWritten, sSampleRate);
    fclose(sWav);
    sWav = NULL;
    sFinalized = 1;
    fprintf(stderr, "[audio_dump] finalized: %u bytes captured (rate=%u)\n",
            (unsigned)sBytesWritten, (unsigned)sSampleRate);
}

/* ========================================================================= */
/*  Plugin lifecycle                                                         */
/* ========================================================================= */

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle handle,
                                     void *context,
                                     m64p_debug_callback debug_cb)
{
    (void)handle; (void)context; (void)debug_cb;
    if (sInitialized) return M64ERR_ALREADY_INIT;
    sInitialized = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!sInitialized) return M64ERR_NOT_INIT;
    wav_finalize();
    sInitialized = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type,
                                        int *version, int *api_version,
                                        const char **name, int *capabilities)
{
    if (type)         *type         = M64PLUGIN_AUDIO;
    if (version)      *version      = 0x010000;
    if (api_version)  *api_version  = 0x020000;
    if (name)         *name         = "Audio Dump Plugin (PCM → WAV)";
    if (capabilities) *capabilities = 0;
    return M64ERR_SUCCESS;
}

/* ========================================================================= */
/*  Audio plugin API                                                         */
/* ========================================================================= */

EXPORT int CALL InitiateAudio(AUDIO_INFO Audio_Info)
{
    sAi = Audio_Info;
    return 1; /* nonzero = success */
}

EXPORT int CALL RomOpen(void)
{
    sBytesWritten = 0;
    sFinalized    = 0;
    if (sWav) { fclose(sWav); sWav = NULL; }
    return 1;
}

EXPORT void CALL RomClosed(void)
{
    wav_finalize();
}

EXPORT void CALL AiDacrateChanged(int SystemType)
{
    /* SystemType: 0=NTSC, 1=PAL, 2=MPAL.  Only NTSC clock encoded here;
     * PAL/MPAL would use slightly different VI_CLOCK constants. */
    uint32_t dacrate = sAi.AI_DACRATE_REG ? *sAi.AI_DACRATE_REG : 0;
    if (dacrate == 0) return;
    sSampleRate = NTSC_VI_CLOCK / (dacrate + 1);
    fprintf(stderr, "[audio_dump] AiDacrateChanged: dacrate=%u → sample_rate=%u Hz\n",
            (unsigned)dacrate, (unsigned)sSampleRate);
    /* If we already opened the WAV with a stale rate, finalize and warn — the
     * dump will be silent on rate change.  In practice SSB64 picks one rate. */
}

EXPORT void CALL AiLenChanged(void)
{
    uint32_t addr;
    uint32_t len;
    uint32_t i;

    if (sFinalized) return;
    if (!sAi.AI_DRAM_ADDR_REG || !sAi.AI_LEN_REG || !sAi.RDRAM) return;

    addr = (*sAi.AI_DRAM_ADDR_REG) & 0xFFFFFF;
    len  = (*sAi.AI_LEN_REG) & 0x3FFFF; /* AI_LEN is 18 bits */
    if (len == 0) return;

    wav_open_if_needed();
    if (!sWav) return;

    /* Cap to remaining budget. */
    if (sBytesWritten + len > sBytesLimit) {
        len = sBytesLimit - sBytesWritten;
    }
    if (len == 0) {
        wav_finalize();
        return;
    }

    /* RDRAM holds 16-bit PCM samples in big-endian byte order on real
     * hardware.  Mupen64Plus stores RDRAM in native endianness with
     * 32-bit-word byte swap (typical for HLE configurations).  We need
     * to byteswap each 16-bit sample so the resulting WAV is little-endian. */
    {
        const uint8_t *src = sAi.RDRAM + addr;
        for (i = 0; i + 1 < len; i += 2) {
            /* Each 16-bit sample: source bytes are at offsets that, after the
             * standard m64p 32-bit byte-swap, look like XOR-3 indexed bytes.
             * Read s16 BE assuming m64p stores RDRAM with word-swap. */
            uint8_t hi = src[(i + 0) ^ 3];
            uint8_t lo = src[(i + 1) ^ 3];
            int16_t s = (int16_t)((hi << 8) | lo);
            fwrite(&s, 1, 2, sWav);
        }
    }
    sBytesWritten += len;
    if (sBytesWritten >= sBytesLimit) {
        wav_finalize();
    }
}

EXPORT void CALL ProcessAList(void)
{
    /* HLE handles audio task processing.  Nothing to do here. */
}

/* Some core builds expect this even though we have nothing to do per VI. */
EXPORT void CALL AiUpdate(int Wait)
{
    (void)Wait;
}

/* ========================================================================= */
/*  Stubs that mupen64plus core requires from an audio plugin                */
/* ========================================================================= */

EXPORT void CALL ChangeWindow(void)         {}
EXPORT void CALL VolumeMute(void)           {}
EXPORT void CALL VolumeUp(void)             {}
EXPORT void CALL VolumeDown(void)           {}
EXPORT int  CALL VolumeGetLevel(void)       { return 100; }
EXPORT void CALL VolumeSetLevel(int level)  { (void)level; }
EXPORT const char * CALL VolumeGetString(void) { return "100%"; }
EXPORT void CALL SetSpeedFactor(int percent) { (void)percent; }
