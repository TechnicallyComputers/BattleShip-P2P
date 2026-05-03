#include <sys/netrollback.h>

#include <sys/netinput.h>
#include <sys/netpeer.h>
#include <sys/netsync.h>
#include <sys/objdef.h>
#include <sys/objman.h>

#include <ft/fighter.h>
#include <gm/gmdef.h>
#include <sys/controller.h>

#ifdef PORT
#include <sys/taskman.h>
extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

extern void scVSBattleFuncUpdate(void);

#define SYNETROLLBACK_RING_LENGTH SYNETINPUT_HISTORY_LENGTH
#define SYNETROLLBACK_SCAN_WINDOW 256

typedef struct SYNetRollbackFighterBlob
{
	sb32 is_valid;
	s32 player;
	s32 fkind;
	s32 status_id;
	s32 motion_id;
	s32 percent_damage;
	s32 stock_count;
	s32 lr;
	sb32 ga;
	Vec3f vel_air;
	Vec3f vel_ground;
	f32 vel_damage_ground;
	Vec3f pos_prev;
	Vec3f vel_damage_air;
	u32 hitlag_tics;

} SYNetRollbackFighterBlob;

typedef struct SYNetRollbackRingSlot
{
	u32 tick;
	sb32 is_valid;
	SYNetRollbackFighterBlob fighters[GMCOMMON_PLAYERS_MAX];

} SYNetRollbackRingSlot;

static SYNetRollbackRingSlot sSYNetRollbackRing[SYNETROLLBACK_RING_LENGTH];
static sb32 sSYNetRollbackModuleEnabled;
static sb32 sSYNetRollbackSessionActive;
static u32 sSYNetRollbackResimDepth;
static u32 sSYNetRollbackRollbackCount;
#ifdef PORT
static u32 sSYNetRollbackInjectTick;
static sb32 sSYNetRollbackInjectConsumed;
static u32 sSYNetRollbackLastVerifyHash;
static sb32 sSYNetRollbackForceMismatch;
static u32 sSYNetRollbackForceMismatchPendingTick;
static sb32 sSYNetRollbackMismatchDebug;
static sb32 sSYNetRollbackVerifyStrict;
static u32 sSYNetRollbackLoadFailCount;
static u32 sMismatchAsymLogsRemaining;
#endif

void syNetRollbackInit(void)
{
#ifdef PORT
	char *env_roll;
	char *env_inj;
	char *env_fm;
	char *env_md;
	char *env_vs;

	sSYNetRollbackInjectTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = FALSE;
	env_roll = getenv("SSB64_NETPLAY_ROLLBACK");
	sSYNetRollbackModuleEnabled = TRUE;
	if ((env_roll != NULL) && (atoi(env_roll) == 0))
	{
		sSYNetRollbackModuleEnabled = FALSE;
	}
	env_inj = getenv("SSB64_NETPLAY_ROLLBACK_INJECT_TICK");
	if ((env_inj != NULL) && (sSYNetRollbackModuleEnabled != FALSE))
	{
		s32 v = atoi(env_inj);

		if (v >= 0)
		{
			sSYNetRollbackInjectTick = (u32)v;
			port_log("SSB64 NetRollback: debug inject tamper at remote tick %u\n", sSYNetRollbackInjectTick);
		}
	}
	sSYNetRollbackForceMismatch = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	env_fm = getenv("SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH");
	if ((env_fm != NULL) && (atoi(env_fm) != 0))
	{
		sSYNetRollbackForceMismatch = TRUE;
		if ((sSYNetRollbackInjectTick != ~(u32)0) && (sSYNetRollbackModuleEnabled != FALSE))
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH on wire tick %u (after staging: XOR published history if it equals remote)\n",
			    sSYNetRollbackInjectTick);
		}
		else if (sSYNetRollbackModuleEnabled != FALSE)
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH set but SSB64_NETPLAY_ROLLBACK_INJECT_TICK missing — no one-shot scheduled\n");
		}
	}
	sSYNetRollbackMismatchDebug = FALSE;
	env_md = getenv("SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG");
	if ((env_md != NULL) && (atoi(env_md) != 0))
	{
		sSYNetRollbackMismatchDebug = TRUE;
	}
	sSYNetRollbackVerifyStrict = FALSE;
	env_vs = getenv("SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT");
	if ((env_vs != NULL) && (atoi(env_vs) != 0))
	{
		sSYNetRollbackVerifyStrict = TRUE;
	}
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
#else
	sSYNetRollbackModuleEnabled = FALSE;
#endif
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackLastVerifyHash = 0;
#endif
}

sb32 syNetRollbackIsActive(void)
{
	return (sSYNetRollbackModuleEnabled != FALSE) && (sSYNetRollbackSessionActive != FALSE);
}

sb32 syNetRollbackIsResimulating(void)
{
	return sSYNetRollbackResimDepth != 0;
}

void syNetRollbackStartVSSession(void)
{
	u32 i;

	if (sSYNetRollbackModuleEnabled == FALSE)
	{
		return;
	}
	for (i = 0; i < SYNETROLLBACK_RING_LENGTH; i++)
	{
		sSYNetRollbackRing[i].is_valid = FALSE;
		sSYNetRollbackRing[i].tick = ~(u32)0;
	}
	sSYNetRollbackSessionActive = TRUE;
	sSYNetRollbackResimDepth = 0;
	sSYNetRollbackRollbackCount = 0;
#ifdef PORT
	sSYNetRollbackInjectConsumed = FALSE;
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackLoadFailCount = 0;
	sMismatchAsymLogsRemaining = 16;
#endif
}

void syNetRollbackStopVSSession(void)
{
	sSYNetRollbackSessionActive = FALSE;
	sSYNetRollbackResimDepth = 0;
}

static SYNetRollbackRingSlot *syNetRollbackRingSlotForTick(u32 tick)
{
	return &sSYNetRollbackRing[tick % SYNETROLLBACK_RING_LENGTH];
}

static void syNetRollbackCaptureFighters(SYNetRollbackRingSlot *slot)
{
	GObj *fighter_gobj;
	s32 si;

	for (si = 0; si < GMCOMMON_PLAYERS_MAX; si++)
	{
		slot->fighters[si].is_valid = FALSE;
	}
	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;

		fp = ftGetStruct(fighter_gobj);
		slot_index = fp->player;

		if ((slot_index >= 0) && (slot_index < GMCOMMON_PLAYERS_MAX))
		{
			SYNetRollbackFighterBlob *blob = &slot->fighters[slot_index];

			blob->is_valid = TRUE;
			blob->player = fp->player;
			blob->fkind = fp->fkind;
			blob->status_id = fp->status_id;
			blob->motion_id = fp->motion_id;
			blob->percent_damage = fp->percent_damage;
			blob->stock_count = fp->stock_count;
			blob->lr = fp->lr;
			blob->ga = fp->ga;
			blob->vel_air = fp->physics.vel_air;
			blob->vel_ground = fp->physics.vel_ground;
			blob->vel_damage_ground = fp->physics.vel_damage_ground;
			blob->pos_prev = fp->coll_data.pos_prev;
			blob->vel_damage_air = fp->physics.vel_damage_air;
			blob->hitlag_tics = fp->hitlag_tics;
		}
	}
}

static void syNetRollbackApplyFighters(const SYNetRollbackRingSlot *slot)
{
	GObj *fighter_gobj;

	for (fighter_gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; fighter_gobj != NULL;
	     fighter_gobj = fighter_gobj->link_next)
	{
		FTStruct *fp;
		s32 slot_index;
		const SYNetRollbackFighterBlob *blob;

		fp = ftGetStruct(fighter_gobj);
		slot_index = fp->player;

		if ((slot_index < 0) || (slot_index >= GMCOMMON_PLAYERS_MAX))
		{
			continue;
		}
		blob = &slot->fighters[slot_index];

		if ((blob->is_valid == FALSE) || (blob->player != fp->player) || (blob->fkind != fp->fkind))
		{
			continue;
		}
		fp->status_id = blob->status_id;
		fp->motion_id = blob->motion_id;
		fp->percent_damage = blob->percent_damage;
		fp->stock_count = blob->stock_count;
		fp->lr = blob->lr;
		fp->ga = blob->ga;
		fp->physics.vel_air = blob->vel_air;
		fp->physics.vel_ground = blob->vel_ground;
		fp->physics.vel_damage_ground = blob->vel_damage_ground;
		fp->coll_data.pos_prev = blob->pos_prev;
		fp->physics.vel_damage_air = blob->vel_damage_air;
		fp->hitlag_tics = blob->hitlag_tics;
	}
}

static sb32 syNetRollbackSavePostTick(u32 tick)
{
	SYNetRollbackRingSlot *slot;

	if (syNetRollbackIsActive() == FALSE)
	{
		return FALSE;
	}
	slot = syNetRollbackRingSlotForTick(tick);
	slot->tick = tick;
	slot->is_valid = TRUE;
	syNetRollbackCaptureFighters(slot);
	return TRUE;
}

static sb32 syNetRollbackLoadPostTick(u32 tick)
{
	SYNetRollbackRingSlot *slot;

	slot = syNetRollbackRingSlotForTick(tick);

	if ((slot->is_valid == FALSE) || (slot->tick != tick))
	{
		return FALSE;
	}
	syNetRollbackApplyFighters(slot);
	return TRUE;
}

void syNetRollbackAfterBattleUpdate(void)
{
	u32 post_tick;

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	post_tick = syNetInputGetTick();

	if (post_tick == 0)
	{
		return;
	}
	syNetRollbackSavePostTick(post_tick - 1);
}

static u32 syNetRollbackFindEarliestInputMismatch(u32 frontier_tick)
{
	SYNetInputFrame hist;
	SYNetInputFrame remote;
	u32 begin;
	u32 t;
	s32 remote_player;

	if (frontier_tick == 0)
	{
		return ~(u32)0;
	}
	/* Multi-remote (4P): mismatch scan is intentionally limited to syNetPeerGetRemotePlayerSlot() until
	 * netpeer stages confirmed inputs per remote human slot. */
	remote_player = syNetPeerGetRemotePlayerSlot();

	if ((remote_player < 0) || (remote_player >= MAXCONTROLLERS))
	{
		return ~(u32)0;
	}
	begin = 0;
	if (frontier_tick > SYNETROLLBACK_SCAN_WINDOW)
	{
		begin = frontier_tick - SYNETROLLBACK_SCAN_WINDOW;
	}
	for (t = begin; t < frontier_tick; t++)
	{
		sb32 has_hist;
		sb32 has_remote;

		has_hist = syNetInputGetHistoryFrame(remote_player, t, &hist);
		has_remote = syNetInputGetRemoteHistoryFrame(remote_player, t, &remote);
#ifdef PORT
		if ((sSYNetRollbackMismatchDebug != FALSE) && (has_hist != has_remote))
		{
			if (sMismatchAsymLogsRemaining > 0U)
			{
				port_log(
				    "SSB64 NetRollback: MISMATCH_DEBUG asym tick=%u hist=%d remote=%d frontier=%u\n",
				    t,
				    (has_hist != FALSE) ? 1 : 0,
				    (has_remote != FALSE) ? 1 : 0,
				    frontier_tick);
				sMismatchAsymLogsRemaining--;
			}
		}
#endif
		if (has_hist == FALSE)
		{
			continue;
		}
		if (has_remote == FALSE)
		{
			continue;
		}
		if ((hist.buttons != remote.buttons) || (hist.stick_x != remote.stick_x) || (hist.stick_y != remote.stick_y))
		{
			return t;
		}
	}
	return ~(u32)0;
}

#ifdef PORT
static void syNetRollbackDebugTryApplyPendingForceMismatch(void)
{
	s32 player;
	u32 tick;
	u32 frontier;
	SYNetInputFrame hist;
	SYNetInputFrame remote;

	if (sSYNetRollbackForceMismatch == FALSE)
	{
		return;
	}
	if (sSYNetRollbackForceMismatchPendingTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetRollbackInjectConsumed != FALSE)
	{
		return;
	}
	player = syNetPeerGetRemotePlayerSlot();
	if ((player < 0) || (player >= MAXCONTROLLERS))
	{
		return;
	}
	tick = sSYNetRollbackForceMismatchPendingTick;
	frontier = syNetInputGetTick();

	if (frontier <= tick)
	{
		return;
	}
	if (syNetInputGetHistoryFrame(player, tick, &hist) == FALSE)
	{
		if (frontier > tick + (u32)SYNETINPUT_HISTORY_LENGTH)
		{
			port_log(
			    "SSB64 NetRollback: FORCE_MISMATCH gave up: no published history at tick %u (frontier=%u)\n",
			    tick,
			    frontier);
			sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
			sSYNetRollbackInjectConsumed = TRUE;
		}
		return;
	}
	if (syNetInputGetRemoteHistoryFrame(player, tick, &remote) == FALSE)
	{
		port_log("SSB64 NetRollback: FORCE_MISMATCH gave up: no remote history at tick %u\n", tick);
		sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
		sSYNetRollbackInjectConsumed = TRUE;
		return;
	}
	if ((hist.buttons != remote.buttons) || (hist.stick_x != remote.stick_x) || (hist.stick_y != remote.stick_y))
	{
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH detected published history already differs from remote at tick %u (no XOR)\n",
		    tick);
		sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
		sSYNetRollbackInjectConsumed = TRUE;
		return;
	}

	port_log(
	    "SSB64 NetRollback: FORCE_MISMATCH detected published history == remote at tick %u; XOR 0x1000 into published history only\n",
	    tick);
	syNetInputDebugXorPublishedHistoryButtons(player, tick, 0x1000);
	sSYNetRollbackForceMismatchPendingTick = ~(u32)0;
	sSYNetRollbackInjectConsumed = TRUE;
}
#endif

static void syNetRollbackRunResim(u32 mismatch_tick, u32 target_tick)
{
	u32 t;

	if ((mismatch_tick >= target_tick) || (mismatch_tick == 0))
	{
		return;
	}
	if (syNetRollbackLoadPostTick(mismatch_tick - 1) == FALSE)
	{
#ifdef PORT
		port_log("SSB64 NetRollback: load post tick %u failed (need earlier snapshots)\n", mismatch_tick - 1);
		sSYNetRollbackLoadFailCount++;
#endif
		return;
	}
	syNetInputRollbackPrepareForResim(mismatch_tick);

	sSYNetRollbackResimDepth++;
	for (t = mismatch_tick; t < target_tick; t++)
	{
		syNetInputSetTick(t);
		syNetInputFuncRead();
		scVSBattleFuncUpdate();
	}
	sSYNetRollbackResimDepth--;
}

void syNetRollbackUpdate(void)
{
	u32 frontier;
	u32 mismatch;
	u32 hash_after;
#ifdef PORT
	u32 hash_pre;
#endif

	if (syNetRollbackIsActive() == FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
#ifdef PORT
	syNetRollbackDebugTryApplyPendingForceMismatch();
#endif
	frontier = syNetInputGetTick();
	mismatch = syNetRollbackFindEarliestInputMismatch(frontier);

	if (mismatch == ~(u32)0)
	{
		return;
	}
#ifdef PORT
	hash_pre = syNetSyncHashBattleFighters();
	port_log("SSB64 NetRollback: input mismatch at tick %u frontier=%u rollbacks=%u\n", mismatch, frontier,
	         sSYNetRollbackRollbackCount + 1);
#endif
	syNetRollbackRunResim(mismatch, frontier);
	sSYNetRollbackRollbackCount++;

	hash_after = syNetSyncHashBattleFighters();
#ifdef PORT
	port_log(
	    "SSB64 NetRollback: resim complete figh=0x%08X (pre_resim=0x%08X) mismatch_tick=%u rollbacks=%u\n",
	    hash_after,
	    hash_pre,
	    mismatch,
	    sSYNetRollbackRollbackCount);
	if ((sSYNetRollbackVerifyStrict != FALSE) && (hash_after == hash_pre))
	{
		port_log(
		    "SSB64 NetRollback: VERIFY_STRICT warning: figh unchanged after resim (mismatch_tick=%u frontier=%u)\n",
		    mismatch,
		    frontier);
	}
	if (sSYNetRollbackLastVerifyHash != 0)
	{
		port_log("SSB64 NetRollback: verify delta vs prior rollback figh ref=0x%08X\n", sSYNetRollbackLastVerifyHash);
	}
	sSYNetRollbackLastVerifyHash = hash_after;
#else
	(void)hash_after;
#endif
}

#ifdef PORT
u32 syNetRollbackGetAppliedResimCount(void)
{
	return sSYNetRollbackRollbackCount;
}

u32 syNetRollbackGetLoadFailCount(void)
{
	return sSYNetRollbackLoadFailCount;
}
#endif

void syNetRollbackDebugOnIncomingRemoteFrame(u32 *tick, u16 *buttons, s8 *stick_x, s8 *stick_y)
{
#ifdef PORT
	if (sSYNetRollbackInjectTick == ~(u32)0)
	{
		return;
	}
	if (sSYNetRollbackInjectConsumed != FALSE)
	{
		return;
	}
	if (*tick != sSYNetRollbackInjectTick)
	{
		return;
	}

	if (sSYNetRollbackForceMismatch != FALSE)
	{
		if (sSYNetRollbackForceMismatchPendingTick == *tick)
		{
			return;
		}
		port_log(
		    "SSB64 NetRollback: FORCE_MISMATCH armed at wire tick %u (patch published history after staging if it matches remote)\n",
		    *tick);
		sSYNetRollbackForceMismatchPendingTick = *tick;
		return;
	}

	*buttons ^= 0x1000;
	sSYNetRollbackInjectConsumed = TRUE;
	port_log("SSB64 NetRollback: injected button tamper at tick %u\n", *tick);
#else
	(void)tick;
	(void)buttons;
	(void)stick_x;
	(void)stick_y;
#endif
}

void syNetRollbackApplyPortSimPacing(u32 refresh_hz)
{
#ifdef PORT
	u32 sim_hz;
	char *hz_env;
	u32 K;

	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		syTaskmanSetIntervals(1, 1);
		return;
	}
	sim_hz = 60;
	hz_env = getenv("SSB64_NETPLAY_SIM_HZ");

	if ((hz_env != NULL) && (atoi(hz_env) > 0))
	{
		sim_hz = (u32)atoi(hz_env);
	}
	if (sim_hz < 1)
	{
		sim_hz = 1;
	}
	if (refresh_hz == 0)
	{
		refresh_hz = 60;
	}
	K = (refresh_hz + sim_hz / 2) / sim_hz;

	if (K < 1)
	{
		K = 1;
	}
	if (K > 16)
	{
		K = 16;
	}
	syTaskmanSetIntervals((u16)K, 1);
#else
	(void)refresh_hz;
#endif
}
