/**
 * n64_stubs.c — Stub implementations for N64 OS functions and hardware symbols.
 *
 * On N64, these are provided by the libultra SDK and handle threading,
 * RSP/RDP task management, controller polling, and other hardware
 * operations.  In the PC port, the threading model is collapsed to
 * single-threaded, RSP tasks are handled by Fast3D/libultraship, and
 * controllers are managed by libultraship's ControlDeck.
 *
 * These stubs exist to satisfy the linker — the decomp code still calls
 * them, but the calls are effectively no-ops in the port.
 */

#include <ssb_types.h>
#include <PR/os.h>
#include <PR/sptask.h>
#include <PR/os_internal.h>

/* ========================================================================= */
/*  Threading stubs                                                          */
/* ========================================================================= */

/*
 * On N64, the game uses a multi-threaded model with separate threads for
 * the game loop, scheduler, audio, and controller polling.  In the port,
 * all of these run in a single thread — thread creation and management
 * calls become no-ops.
 */

void osCreateThread(OSThread *t, OSId id, void (*entry)(void *), void *arg,
                    void *sp, OSPri p)
{
}

void osStartThread(OSThread *t)
{
}

void osStopThread(OSThread *t)
{
}

void osDestroyThread(OSThread *t)
{
}

void osSetThreadPri(OSThread *t, OSPri p)
{
}

OSPri osGetThreadPri(OSThread *t)
{
	return 0;
}

/* ========================================================================= */
/*  OS initialisation                                                        */
/* ========================================================================= */

void osInitialize(void)
{
}

/* ========================================================================= */
/*  RSP task stubs                                                           */
/* ========================================================================= */

/*
 * RSP task submission is replaced by Fast3D in the port.  Graphics display
 * lists are intercepted by libultraship before they reach these functions.
 */

void osSpTaskLoad(OSTask *tp)
{
}

void osSpTaskStartGo(OSTask *tp)
{
}

void osSpTaskYield(void)
{
}

OSYieldResult osSpTaskYielded(OSTask *tp)
{
	return 0;
}

/* ========================================================================= */
/*  Controller stubs                                                         */
/* ========================================================================= */

/*
 * Controller input is handled by libultraship's ControlDeck, which maps
 * modern gamepad/keyboard input to OSContPad format.
 */

s32 osContStartQuery(OSMesgQueue *mq)
{
	return 0;
}

void osContGetQuery(OSContStatus *data)
{
}

/* ========================================================================= */
/*  I/O stubs                                                                */
/* ========================================================================= */

s32 osDpSetNextBuffer(void *buf, u64 size)
{
	return 0;
}

s32 osEPiLinkHandle(OSPiHandle *handle)
{
	return 0;
}

s32 osAfterPreNMI(void)
{
	return 0;
}

s32 osAiSetFrequency(u32 freq)
{
	return 0;
}

/* ========================================================================= */
/*  Internal OS function stubs                                               */
/* ========================================================================= */

OSThread *__osGetActiveQueue(void)
{
	return NULL;
}

void __osSetWatchLo(u32 val)
{
}

/* ========================================================================= */
/*  Global OS variables                                                      */
/* ========================================================================= */

s32 osTvType = 1;     /* OS_TV_NTSC */
s32 osResetType = 0;  /* Cold reset */

/*
 * OSViMode structs for NTSC and MPAL low-resolution non-interlaced modes.
 * On PC these are never used for actual video mode setting — libultraship
 * manages the window through SDL2.  Zero-initialised to satisfy the linker.
 */
OSViMode osViModeNtscLan1 = { 0 };
OSViMode osViModeMpalLan1 = { 0 };

/* ========================================================================= */
/*  RSP microcode symbols                                                    */
/* ========================================================================= */

/*
 * On N64 these point to the F3DEX2 graphics microcode and the N64 audio
 * microcode loaded onto the RSP.  In the port, Fast3D replaces the RSP
 * for graphics and audio is handled by SDL2.
 *
 * Declared as long long int[] in include/PR/ucode.h and
 * include/n_audio/n_libaudio.h.
 */
long long int gspF3DEX2_fifoTextStart[1] = { 0 };
long long int gspF3DEX2_fifoDataStart[1] = { 0 };
long long int n_aspMainTextStart[1] = { 0 };
long long int n_aspMainDataStart[1] = { 0 };

/* ========================================================================= */
/*  Sprite library stubs                                                     */
/* ========================================================================= */

/*
 * The N64 sprite library (libultra sp/) renders 2D sprites by building
 * GBI display lists.  The decomp's sprite.c depends on drawbitmap which
 * is MIPS assembly only, so we can't compile it.  These stubs allow the
 * game to link — sprite rendering will be reimplemented in the port layer.
 *
 * Declared in include/PR/sp.h.
 */

#include <PR/sp.h>

void spInit(Gfx **glistp)
{
}

void spScissor(s32 xmin, s32 xmax, s32 ymin, s32 ymax)
{
}

Gfx *spDraw(Sprite *sp)
{
	return NULL;
}

void spFinish(Gfx **glistp)
{
}

/* ========================================================================= */
/*  IDO assert                                                               */
/* ========================================================================= */

/*
 * The decomp's include/assert.h defines assert() using __assert(), which
 * is an IDO runtime function.  On PC we just print and abort.
 */

#include <stdio.h>
#include <stdlib.h>

void __assert(const char *expr, const char *file, int line)
{
	fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", expr, file, line);
	abort();
}
