#ifndef _SYNETPEER_H_
#define _SYNETPEER_H_

#include <PR/ultratypes.h>
#include <ssb_types.h>

extern void syNetPeerInitDebugEnv(void);
extern void syNetPeerStartVSSession(void);
extern sb32 syNetPeerCheckBattleExecutionReady(void);
extern sb32 syNetPeerCheckStartBarrierReleased(void);
extern void syNetPeerUpdateBattleGate(void);
extern void syNetPeerUpdate(void);
extern void syNetPeerStopVSSession(void);
extern sb32 syNetPeerIsVSSessionActive(void);
extern s32 syNetPeerGetRemotePlayerSlot(void);
extern s32 syNetPeerGetRemoteHumanSlotCount(void);
extern sb32 syNetPeerGetRemoteHumanSlotByIndex(s32 index, s32 *out_slot);
extern u32 syNetPeerGetHighestRemoteTick(void);

#endif /* _SYNETPEER_H_ */
