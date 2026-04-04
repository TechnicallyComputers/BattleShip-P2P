/**
 * debug_stubs.c — Stubs for debug scene functions and misc remaining symbols.
 *
 * The debug scene code (src/db/) is excluded from the port build, but
 * the scene manager (src/sc/scmanager.c) still references the debug
 * scene entry points in its scene switch table.  These empty stubs
 * allow linking — the debug scenes simply do nothing in the port.
 */

#include <ssb_types.h>
#include <gm/gmsound.h>

/* ========================================================================= */
/*  Debug scene entry points (from src/db/)                                  */
/* ========================================================================= */

void dbBattleStartScene(void)
{
}

void dbCubeStartScene(void)
{
}

void dbFallsStartScene(void)
{
}

void dbMapsStartScene(void)
{
}

/* ========================================================================= */
/*  Misc data symbols                                                        */
/* ========================================================================= */

/*
 * D_8009EDD0_406D0 is an alSoundEffect struct defined in the excluded
 * n_audio library (src/libultra/n_audio/n_env.c).  It is referenced by
 * src/sc/sc1pmode/sc1pgame.c.  Zero-initialised stub.
 */
alSoundEffect D_8009EDD0_406D0 = { 0 };

/*
 * D_NF_00006010 and D_NF_00006450 are file-relative offsets used in
 * src/sc/sc1pmode/sc1pgame.c for camera animation setup.  On N64, the
 * linker places these symbols at absolute addresses matching the offset
 * values, and the code uses &D_NF_00006010 to obtain the address 0x6010.
 *
 * On PC, taking &variable gives a heap/stack address, not the offset.
 * For now, these are defined as intptr_t with the offset as the value.
 * The code in sc1pgame.c that uses (intptr_t)&D_NF_* will need #ifdef
 * PORT adjustments to use the value directly instead of the address.
 *
 * Declared as extern intptr_t in src/sc/sc1pmode/sc1pgame.c.
 */
intptr_t D_NF_00006010 = 0x6010;
intptr_t D_NF_00006450 = 0x6450;
