/**
 * audio_stubs.c — Stub implementations for the N64 audio library.
 *
 * On N64, audio is processed on the RSP using the n_audio microcode.
 * The game's audio system (src/sys/audio.c) calls into the n_audio
 * library for sequenced music playback, sound effect management, and
 * audio mixing.
 *
 * In the PC port, audio will eventually be handled by SDL2.  For now,
 * these stubs allow the game to link and run silently.  Once the asset
 * pipeline is in place, these will be replaced with real implementations
 * that decode and play audio through the port's audio backend.
 */

#include <ssb_types.h>
#include <n_audio/n_libaudio.h>
#include <sys/audio.h>
#include <gm/gmsound.h>

/* ========================================================================= */
/*  Core N64 audio library (n_audio)                                         */
/* ========================================================================= */

void n_alInit(N_ALGlobals *g, ALSynConfig *c)
{
}

void n_alClose(N_ALGlobals *glob)
{
}

Acmd *n_alAudioFrame(Acmd *cmdList, s32 *cmdLen, s16 *outBuf, s32 outLen)
{
	if (cmdLen != NULL)
	{
		*cmdLen = 0;
	}
	return cmdList;
}

/* ========================================================================= */
/*  Compact sequence player (CSP)                                            */
/* ========================================================================= */

void n_alCSPNew(N_ALCSPlayer *seqp, ALSeqpConfig *config)
{
}

void n_alCSPPlay(N_ALCSPlayer *seqp)
{
}

void n_alCSPStop(N_ALCSPlayer *seqp)
{
}

void n_alCSPSetBank(N_ALCSPlayer *seqp, ALBank *b)
{
}

void n_alCSPSetSeq(N_ALCSPlayer *seqp, ALCSeq *seq)
{
}

void n_alCSPSetVol(N_ALCSPlayer *seqp, s16 vol)
{
}

void n_alCSPSetChlFXMix(N_ALCSPlayer *seqp, u8 chan, u8 fxmix)
{
}

void n_alCSPSetChlPriority(N_ALCSPlayer *seqp, u8 chan, u8 priority)
{
}

/* ========================================================================= */
/*  Compact sequence                                                         */
/* ========================================================================= */

void n_alCSeqNew(ALCSeq *seq, u8 *ptr)
{
}

/* ========================================================================= */
/*  Audio utility                                                            */
/* ========================================================================= */

f32 alCents2Ratio(s32 cents)
{
	return 1.0f;
}

/* ========================================================================= */
/*  Unnamed audio functions from src/libultra/n_audio/n_env.c                */
/* ========================================================================= */

/*
 * These functions are defined in the excluded n_audio library.  Their
 * signatures are taken from the call sites in the compiled game code
 * and from the definitions in src/libultra/n_audio/n_env.c.
 */

/* Sets master volume — called from audio.c */
void func_80026070_26C70(u8 vol)
{
}

/* Sets something on a sound effect — called from audio.c */
void func_80026094_26C94(void *sfx, u8 val)
{
}

/* Sets something on a sound effect — called from audio.c */
void func_80026104_26D04(void *sfx, u8 val)
{
}

/* Sets something on a sound effect — called from audio.c */
void func_80026174_26D74(void *sfx, u8 val)
{
}

/* Audio config init — called from syAudioMakeBGMPlayers */
void func_80026204_26E04(void *config)
{
}

/* Pause/restore audio — called from ifCommon */
s32 func_800264A4_270A4(void)
{
	return 0;
}

s32 func_80026594_27194(void)
{
	return 0;
}

/* Audio state reset — called from sc1pintro */
void func_800266A0_272A0(void)
{
}

/* Stop sound effect — called from wpmain */
void func_80026738_27338(alSoundEffect *sfx)
{
}

/* Set sound effect position — called from lbcommon */
void func_800267F4_273F4(alSoundEffect *sfx)
{
}

/* Create/get sound effect by FGM ID — widely used */
alSoundEffect *func_800269C0_275C0(u16 fgm_id)
{
	return NULL;
}

/* Get sound effect data pointer — called from lbcommon */
void *func_80026A10_27610(u16 id)
{
	return NULL;
}

/* ========================================================================= */
/*  Audio settings data                                                      */
/* ========================================================================= */

/*
 * dSYAudioPublicSettings2 and dSYAudioPublicSettings3 are initialised data
 * tables for audio configuration.  They are defined in the excluded n_audio
 * code.  Zero-initialised stubs allow linking.
 */
SYAudioSettings dSYAudioPublicSettings2 = { 0 };
SYAudioSettings dSYAudioPublicSettings3 = { 0 };
