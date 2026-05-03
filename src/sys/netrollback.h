#ifndef _SYNETROLLBACK_H_
#define _SYNETROLLBACK_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

extern void syNetRollbackInit(void);
extern void syNetRollbackStartVSSession(void);
extern void syNetRollbackStopVSSession(void);
extern sb32 syNetRollbackIsActive(void);
extern sb32 syNetRollbackIsResimulating(void);

extern void syNetRollbackAfterBattleUpdate(void);
extern void syNetRollbackUpdate(void);

#ifdef PORT
extern void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y);
extern void syNetRollbackApplyPortSimPacing(u32 refresh_hz);
extern u32 syNetRollbackGetAppliedResimCount(void);
extern u32 syNetRollbackGetLoadFailCount(void);
#endif

#endif /* _SYNETROLLBACK_H_ */
