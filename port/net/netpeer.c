#include <sys/netpeer.h>

#include <ft/fighter.h>
#include <gr/ground.h>
#include <sc/scmanager.h>
#include <sys/netinput.h>
#include <sys/netreplay.h>
#include <sys/netrollback.h>
#include <sys/netsync.h>
#include <sys/utils.h>

#ifdef PORT
extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);
#endif

#if defined(PORT) && !defined(_WIN32)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

#define SYNETPEER_MAGIC 0x53534E50 // SSNP
#define SYNETPEER_VERSION 2
#define SYNETPEER_VERSION_DUAL_LOCAL 3
#define SYNETPEER_MAX_PACKET_FRAMES 16
#define SYNETPEER_FRAME_BYTES 8
#define SYNETPEER_INPUT_HEADER_BYTES (4 + 2 + 2 + 4 + 4 + 4 + 1 + 1 + 1 + 1)
#define SYNETPEER_PACKET_BYTES_V2 ((SYNETPEER_INPUT_HEADER_BYTES) + ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 4)
#define SYNETPEER_PACKET_BYTES_V3                                                                                    \
	((SYNETPEER_INPUT_HEADER_BYTES) + ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 1 + 1 +             \
	 ((SYNETPEER_MAX_PACKET_FRAMES) * (SYNETPEER_FRAME_BYTES)) + 4)
#define SYNETPEER_PACKET_RECV_MAX                                                                                      \
	(((SYNETPEER_PACKET_BYTES_V2) > (SYNETPEER_PACKET_BYTES_V3)) ? (SYNETPEER_PACKET_BYTES_V2) : (SYNETPEER_PACKET_BYTES_V3))
#define SYNETPEER_MAX_REMOTE_PLAYLIST 4
#define SYNETPEER_SECONDARY_SLOT_ABSENT 255
#define SYNETPEER_VALIDATION_INPUT_WINDOW 120
#define SYNETPEER_METADATA_BYTES (11 * 4 + 8 + (MAXCONTROLLERS * 7))
#define SYNETPEER_BOOTSTRAP_PACKET_BYTES (4 + 2 + 2 + 4 + SYNETPEER_METADATA_BYTES + 4)
#define SYNETPEER_CONTROL_PACKET_BYTES (4 + 2 + 2 + 4 + 4)
#define SYNETPEER_TIME_PING_BYTES (4 + 2 + 2 + 4 + 4 + 8 + 4)
#define SYNETPEER_TIME_PONG_BYTES (4 + 2 + 2 + 4 + 4 + 8 + 8 + 8 + 4)
#define SYNETPEER_BATTLE_START_TIME_BYTES (4 + 2 + 2 + 4 + 8 + 8 + 4)
#define SYNETPEER_CLOCK_SYNC_SAMPLES 8
#define SYNETPEER_MIN_START_LEAD_MS 200U
#define SYNETPEER_START_MARGIN_MS 40U
#define SYNETPEER_DEFAULT_INPUT_DELAY 2
#define SYNETPEER_DEFAULT_SESSION_ID 1
#define SYNETPEER_DEFAULT_BOOTSTRAP_SEED 12345
#define SYNETPEER_LOG_INTERVAL 120
#define SYNETPEER_BOOTSTRAP_RETRY_COUNT 300
#define SYNETPEER_BOOTSTRAP_RETRY_USECS 16666
#define SYNETPEER_BARRIER_LOG_INTERVAL 30
#define SYNETPEER_BATTLE_START_REPEAT_FRAMES 30

#define SYNETPEER_PACKET_INPUT 0
#define SYNETPEER_PACKET_MATCH_CONFIG 1
#define SYNETPEER_PACKET_READY 2
#define SYNETPEER_PACKET_START 3
#define SYNETPEER_PACKET_BATTLE_READY 4
#define SYNETPEER_PACKET_BATTLE_START 5
#define SYNETPEER_PACKET_TIME_PING 6
#define SYNETPEER_PACKET_TIME_PONG 7
#define SYNETPEER_PACKET_BATTLE_START_TIME 8

typedef struct SYNetPeerPacketFrame
{
	u32 tick;
	u16 buttons;
	s8 stick_x;
	s8 stick_y;

} SYNetPeerPacketFrame;

static void syNetPeerConfigureRemoteReceiveSlots(void);
static void syNetPeerConfigurePeerSenderSlots(void);
static void syNetPeerConfigureExtraLocalSender(void);
static sb32 syNetPeerValidateRemoteReceiveList(void);
static sb32 syNetPeerValidatePeerSenderList(void);
static sb32 syNetPeerGatherHistoryBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *out_frame_count);
static void syNetPeerStagePacketBundle(s32 target_player, const SYNetPeerPacketFrame *frames, s32 frame_count,
                                       u32 current_tick);

sb32 sSYNetPeerIsEnabled;
sb32 sSYNetPeerIsConfigured;
sb32 sSYNetPeerIsActive;
s32 sSYNetPeerLocalPlayer;
s32 sSYNetPeerRemotePlayer;
u32 sSYNetPeerInputDelay;
static u8 sSYNetPeerRemoteReceiveSlots[SYNETPEER_MAX_REMOTE_PLAYLIST] = { 1 };
static s32 sSYNetPeerRemoteReceiveCount = 1;
static u8 sSYNetPeerPeerSenderSlots[SYNETPEER_MAX_REMOTE_PLAYLIST] = { 1 };
static s32 sSYNetPeerPeerSenderCount = 1;
static s32 sSYNetPeerExtraLocalSenderSlot = -1;
u32 sSYNetPeerSessionID;
u32 sSYNetPeerHighestRemoteTick;
u32 sSYNetPeerPacketsSent;
u32 sSYNetPeerPacketsReceived;
u32 sSYNetPeerPacketsDropped;
u32 sSYNetPeerFramesStaged;
u32 sSYNetPeerLateFrames;
u32 sSYNetPeerInputChecksum;
u32 sSYNetPeerLastLogTick;
u32 sSYNetPeerSendSeq;
u32 sSYNetPeerRecvSeqHighWater;
sb32 sSYNetPeerRecvSeqInitialized;
u32 sSYNetPeerSeqGaps;
u32 sSYNetPeerSeqDuplicates;
u32 sSYNetPeerSeqOutOfOrder;
u32 sSYNetPeerLastPeerAckTick;
u32 sSYNetPeerLastPacketOldestTick;
u32 sSYNetPeerLastPacketNewestTick;
sb32 sSYNetPeerLastPacketTicksValid;
sb32 sSYNetPeerBootstrapIsEnabled;
sb32 sSYNetPeerBootstrapIsHost;
sb32 sSYNetPeerBootstrapMetadataApplied;
sb32 sSYNetPeerBootstrapPeerReady;
sb32 sSYNetPeerBootstrapStartReceived;
u32 sSYNetPeerBootstrapSeed;
SYNetInputReplayMetadata sSYNetPeerBootstrapMetadata;
sb32 sSYNetPeerBattleBarrierEnabled;
sb32 sSYNetPeerBattleLocalReady;
sb32 sSYNetPeerBattlePeerReady;
sb32 sSYNetPeerBattleStartSent;
sb32 sSYNetPeerBattleStartReceived;
sb32 sSYNetPeerBattleBarrierReleased;
u32 sSYNetPeerBattleBarrierWaitFrames;
u32 sSYNetPeerBattleStartRepeatFrames;
u32 sSYNetPeerExecutionHoldFrames;
sb32 sSYNetPeerExecutionBeginLogged;
	sb32 sSYNetPeerClockAlignEnabled;

#ifdef PORT
static u32 sSYNetPeerInputDelayFloor;
static u32 sSYNetPeerInputDelayCeil;
static sb32 sSYNetPeerAdaptiveDelayEnabled;
static sb32 sSYNetPeerAdaptivePrimed;
static u32 sSYNetPeerAdaptivePrevLateFrames;
static u32 sSYNetPeerAdaptivePrevLoadFail;
static u32 sSYNetPeerAdaptiveStableIntervals;

static void syNetPeerResetAdaptiveDelayTracking(void)
{
	sSYNetPeerAdaptivePrimed = FALSE;
	sSYNetPeerAdaptivePrevLateFrames = 0;
	sSYNetPeerAdaptivePrevLoadFail = 0;
	sSYNetPeerAdaptiveStableIntervals = 0;
}

static void syNetPeerMaybeAdaptInputDelay(void)
{
	u32 late;
	u32 lf;
	u32 d_late;
	u32 d_lf;

	if ((sSYNetPeerAdaptiveDelayEnabled == FALSE) || (sSYNetPeerIsActive == FALSE))
	{
		return;
	}
	late = sSYNetPeerLateFrames;
	lf = syNetRollbackGetLoadFailCount();
	if (sSYNetPeerAdaptivePrimed == FALSE)
	{
		sSYNetPeerAdaptivePrimed = TRUE;
		sSYNetPeerAdaptivePrevLateFrames = late;
		sSYNetPeerAdaptivePrevLoadFail = lf;
		return;
	}
	d_late = late - sSYNetPeerAdaptivePrevLateFrames;
	d_lf = lf - sSYNetPeerAdaptivePrevLoadFail;
	sSYNetPeerAdaptivePrevLateFrames = late;
	sSYNetPeerAdaptivePrevLoadFail = lf;

	if ((d_lf > 0U) || (d_late > 8U))
	{
		sSYNetPeerAdaptiveStableIntervals = 0;
		if (sSYNetPeerInputDelay < sSYNetPeerInputDelayCeil)
		{
			sSYNetPeerInputDelay++;
			port_log("SSB64 NetPeer: adaptive delay up -> %u (late_delta=%u lf_delta=%u ceil=%u)\n",
			         sSYNetPeerInputDelay, d_late, d_lf, sSYNetPeerInputDelayCeil);
		}
	}
	else
	{
		sSYNetPeerAdaptiveStableIntervals++;
		if (sSYNetPeerAdaptiveStableIntervals >= 3U)
		{
			sSYNetPeerAdaptiveStableIntervals = 0;
			if (sSYNetPeerInputDelay > sSYNetPeerInputDelayFloor)
			{
				sSYNetPeerInputDelay--;
				port_log("SSB64 NetPeer: adaptive delay down -> %u floor=%u\n",
				         sSYNetPeerInputDelay, sSYNetPeerInputDelayFloor);
			}
		}
	}
}
#endif

#if defined(PORT) && !defined(_WIN32)
s32 sSYNetPeerSocket = -1;
struct sockaddr_in sSYNetPeerBindAddress;
struct sockaddr_in sSYNetPeerPeerAddress;

static u32 sSYNetPeerTimePingSeq;
static u64 sSYNetPeerTimePingT0Sent;
static sb32 sSYNetPeerTimePingAwaitingAck;
static s64 sSYNetPeerClockOffsetSamples[SYNETPEER_CLOCK_SYNC_SAMPLES];
static u32 sSYNetPeerClockRttSamples[SYNETPEER_CLOCK_SYNC_SAMPLES];
static u32 sSYNetPeerClockSyncSampleCount;
static u64 sSYNetPeerBattleStartUnixMs;
static s64 sSYNetPeerBattleStartOffsetMs;
static sb32 sSYNetPeerBattleStartTimeSent;
static sb32 sSYNetPeerBattleStartTimeReceived;
static sb32 sSYNetPeerBarrierDeadlineValid;
static u64 sSYNetPeerBarrierDeadlineUnixMs;

static u64 syNetPeerNowUnixMs(void);
static void syNetPeerWriteU64(u8 **cursor, u64 value);
static u64 syNetPeerReadU64(const u8 **cursor);
static void syNetPeerResetClockAlignState(void);
static void syNetPeerSendTimePingPacket(u32 seq, u64 t0_ms);
static void syNetPeerSendTimePongPacket(u32 seq, u64 h0_echo, u64 c1_ms, u64 c2_ms);
static void syNetPeerSendBattleStartTimePacket(u64 start_unix_ms, s64 offset_host_minus_client_ms);
static void syNetPeerHandleTimePingPacket(const u8 *buffer, s32 size);
static void syNetPeerHandleTimePongPacket(const u8 *buffer, s32 size);
static void syNetPeerHandleBattleStartTimePacket(const u8 *buffer, s32 size);
static s64 syNetPeerMedianS64(s64 *values, u32 count);
static void syNetPeerHostFinishClockSyncAndSendStart(void);
static sb32 syNetPeerCheckBarrierDeadlineReached(void);
#endif

u32 syNetPeerChecksumAccumulateU32(u32 checksum, u32 value)
{
	checksum ^= value;
	checksum *= 16777619U;

	return checksum;
}

u32 syNetPeerChecksumAccumulateFrame(u32 checksum, const SYNetPeerPacketFrame *frame)
{
	checksum = syNetPeerChecksumAccumulateU32(checksum, frame->tick);
	checksum = syNetPeerChecksumAccumulateU32(checksum, frame->buttons);
	checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)frame->stick_x);
	checksum = syNetPeerChecksumAccumulateU32(checksum, (u8)frame->stick_y);

	return checksum;
}

u32 syNetPeerChecksumInputPacket(u32 session_id, u32 ack_tick, u32 packet_seq, u16 wire_version, u8 player, u8 frame_count,
                                 const SYNetPeerPacketFrame *frames, u8 secondary_slot, u8 sec_frame_count,
                                 const SYNetPeerPacketFrame *sec_frames)
{
	u32 checksum = 2166136261U;
	s32 i;

	checksum = syNetPeerChecksumAccumulateU32(checksum, SYNETPEER_MAGIC);
	checksum = syNetPeerChecksumAccumulateU32(checksum, wire_version);
	checksum = syNetPeerChecksumAccumulateU32(checksum, SYNETPEER_PACKET_INPUT);
	checksum = syNetPeerChecksumAccumulateU32(checksum, session_id);
	checksum = syNetPeerChecksumAccumulateU32(checksum, ack_tick);
	checksum = syNetPeerChecksumAccumulateU32(checksum, packet_seq);
	checksum = syNetPeerChecksumAccumulateU32(checksum, player);
	checksum = syNetPeerChecksumAccumulateU32(checksum, frame_count);

	for (i = 0; i < frame_count; i++)
	{
		checksum = syNetPeerChecksumAccumulateFrame(checksum, &frames[i]);
	}
	if ((wire_version >= SYNETPEER_VERSION_DUAL_LOCAL) && (secondary_slot != SYNETPEER_SECONDARY_SLOT_ABSENT))
	{
		checksum = syNetPeerChecksumAccumulateU32(checksum, secondary_slot);
		checksum = syNetPeerChecksumAccumulateU32(checksum, sec_frame_count);
		for (i = 0; i < sec_frame_count; i++)
		{
			checksum = syNetPeerChecksumAccumulateFrame(checksum, &sec_frames[i]);
		}
	}
	return checksum;
}

u32 syNetPeerChecksumBytes(const u8 *bytes, u32 size)
{
	u32 checksum = 2166136261U;
	u32 i;

	for (i = 0; i < size; i++)
	{
		checksum ^= bytes[i];
		checksum *= 16777619U;
	}
	return checksum;
}

void syNetPeerWriteU8(u8 **cursor, u8 value)
{
	*(*cursor)++ = value;
}

void syNetPeerWriteU16(u8 **cursor, u16 value)
{
	*(*cursor)++ = (value >> 8) & 0xFF;
	*(*cursor)++ = value & 0xFF;
}

void syNetPeerWriteU32(u8 **cursor, u32 value)
{
	*(*cursor)++ = (value >> 24) & 0xFF;
	*(*cursor)++ = (value >> 16) & 0xFF;
	*(*cursor)++ = (value >> 8) & 0xFF;
	*(*cursor)++ = value & 0xFF;
}

u8 syNetPeerReadU8(const u8 **cursor)
{
	return *(*cursor)++;
}

u16 syNetPeerReadU16(const u8 **cursor)
{
	u16 value = (u16)(*(*cursor)++) << 8;

	value |= *(*cursor)++;

	return value;
}

u32 syNetPeerReadU32(const u8 **cursor)
{
	u32 value = (u32)(*(*cursor)++) << 24;

	value |= (u32)(*(*cursor)++) << 16;
	value |= (u32)(*(*cursor)++) << 8;
	value |= *(*cursor)++;

	return value;
}

void syNetPeerWriteMetadata(u8 **cursor, const SYNetInputReplayMetadata *metadata)
{
	s32 player;

	syNetPeerWriteU32(cursor, metadata->magic);
	syNetPeerWriteU32(cursor, metadata->version);
	syNetPeerWriteU32(cursor, metadata->scene_kind);
	syNetPeerWriteU32(cursor, metadata->player_count);
	syNetPeerWriteU32(cursor, metadata->stage_kind);
	syNetPeerWriteU32(cursor, metadata->stocks);
	syNetPeerWriteU32(cursor, metadata->time_limit);
	syNetPeerWriteU32(cursor, metadata->item_switch);
	syNetPeerWriteU32(cursor, metadata->item_toggles);
	syNetPeerWriteU32(cursor, metadata->rng_seed);
	syNetPeerWriteU32(cursor, metadata->game_type);
	syNetPeerWriteU8(cursor, metadata->game_rules);
	syNetPeerWriteU8(cursor, metadata->is_team_battle);
	syNetPeerWriteU8(cursor, metadata->handicap);
	syNetPeerWriteU8(cursor, metadata->is_team_attack);
	syNetPeerWriteU8(cursor, metadata->is_stage_select);
	syNetPeerWriteU8(cursor, metadata->damage_ratio);
	syNetPeerWriteU8(cursor, metadata->item_appearance_rate);
	syNetPeerWriteU8(cursor, metadata->is_not_teamshadows);

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		syNetPeerWriteU8(cursor, metadata->player_kinds[player]);
		syNetPeerWriteU8(cursor, metadata->fighter_kinds[player]);
		syNetPeerWriteU8(cursor, metadata->costumes[player]);
		syNetPeerWriteU8(cursor, metadata->teams[player]);
		syNetPeerWriteU8(cursor, metadata->handicaps[player]);
		syNetPeerWriteU8(cursor, metadata->levels[player]);
		syNetPeerWriteU8(cursor, metadata->shades[player]);
	}
}

void syNetPeerReadMetadata(const u8 **cursor, SYNetInputReplayMetadata *metadata)
{
	s32 player;

	metadata->magic = syNetPeerReadU32(cursor);
	metadata->version = syNetPeerReadU32(cursor);
	metadata->scene_kind = syNetPeerReadU32(cursor);
	metadata->player_count = syNetPeerReadU32(cursor);
	metadata->stage_kind = syNetPeerReadU32(cursor);
	metadata->stocks = syNetPeerReadU32(cursor);
	metadata->time_limit = syNetPeerReadU32(cursor);
	metadata->item_switch = syNetPeerReadU32(cursor);
	metadata->item_toggles = syNetPeerReadU32(cursor);
	metadata->rng_seed = syNetPeerReadU32(cursor);
	metadata->game_type = syNetPeerReadU32(cursor);
	metadata->game_rules = syNetPeerReadU8(cursor);
	metadata->is_team_battle = syNetPeerReadU8(cursor);
	metadata->handicap = syNetPeerReadU8(cursor);
	metadata->is_team_attack = syNetPeerReadU8(cursor);
	metadata->is_stage_select = syNetPeerReadU8(cursor);
	metadata->damage_ratio = syNetPeerReadU8(cursor);
	metadata->item_appearance_rate = syNetPeerReadU8(cursor);
	metadata->is_not_teamshadows = syNetPeerReadU8(cursor);

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		metadata->player_kinds[player] = syNetPeerReadU8(cursor);
		metadata->fighter_kinds[player] = syNetPeerReadU8(cursor);
		metadata->costumes[player] = syNetPeerReadU8(cursor);
		metadata->teams[player] = syNetPeerReadU8(cursor);
		metadata->handicaps[player] = syNetPeerReadU8(cursor);
		metadata->levels[player] = syNetPeerReadU8(cursor);
		metadata->shades[player] = syNetPeerReadU8(cursor);
	}
}

sb32 syNetPeerCheckMetadata(const SYNetInputReplayMetadata *metadata)
{
	if ((metadata->magic != SYNETINPUT_REPLAY_MAGIC) ||
		(metadata->version != SYNETINPUT_REPLAY_VERSION) ||
		(metadata->scene_kind != nSCKindVSBattle) ||
		(metadata->player_count == 0) ||
		(metadata->player_count > MAXCONTROLLERS))
	{
		return FALSE;
	}
	return TRUE;
}

#if defined(PORT) && !defined(_WIN32)
void syNetPeerSendBytes(const u8 *buffer, u32 size);

void syNetPeerSleepBootstrapRetry(void)
{
	usleep(SYNETPEER_BOOTSTRAP_RETRY_USECS);
}

const char *syNetPeerFindPortSeparator(const char *text)
{
	const char *separator = NULL;

	while (*text != '\0')
	{
		if (*text == ':')
		{
			separator = text;
		}
		text++;
	}
	return separator;
}

sb32 syNetPeerStringEquals(const char *a, const char *b)
{
	while ((*a != '\0') && (*b != '\0'))
	{
		if (*a != *b)
		{
			return FALSE;
		}
		a++;
		b++;
	}
	return (*a == *b) ? TRUE : FALSE;
}

sb32 syNetPeerParseIPv4Address(const char *text, struct sockaddr_in *out_address)
{
	const char *colon;
	s32 host_length;
	s32 port;
	char host[64];

	if ((text == NULL) || (out_address == NULL))
	{
		return FALSE;
	}
	colon = syNetPeerFindPortSeparator(text);

	if ((colon == NULL) || (colon == text) || (*(colon + 1) == '\0'))
	{
		return FALSE;
	}
	host_length = colon - text;

	if (host_length >= (s32)sizeof(host))
	{
		return FALSE;
	}
	memcpy(host, text, host_length);
	host[host_length] = '\0';

	port = atoi(colon + 1);

	if ((port <= 0) || (port > 65535))
	{
		return FALSE;
	}
	memset(out_address, 0, sizeof(*out_address));
	out_address->sin_family = AF_INET;
	out_address->sin_port = htons((u16)port);

	if ((syNetPeerStringEquals(host, "*") != FALSE) || (syNetPeerStringEquals(host, "0.0.0.0") != FALSE))
	{
		out_address->sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else if (inet_pton(AF_INET, host, &out_address->sin_addr) != 1)
	{
		return FALSE;
	}
	return TRUE;
}

void syNetPeerCloseSocket(void)
{
	if (sSYNetPeerSocket >= 0)
	{
		close(sSYNetPeerSocket);
		sSYNetPeerSocket = -1;
	}
}

sb32 syNetPeerOpenSocket(void)
{
	s32 flags;
	s32 reuse = 1;

	if (sSYNetPeerSocket >= 0)
	{
		return TRUE;
	}
	sSYNetPeerSocket = socket(AF_INET, SOCK_DGRAM, 0);

	if (sSYNetPeerSocket < 0)
	{
		port_log("SSB64 NetPeer: socket failed errno=%d\n", errno);
		return FALSE;
	}
	setsockopt(sSYNetPeerSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	if (bind(sSYNetPeerSocket, (struct sockaddr*)&sSYNetPeerBindAddress, sizeof(sSYNetPeerBindAddress)) != 0)
	{
		port_log("SSB64 NetPeer: bind failed errno=%d\n", errno);
		syNetPeerCloseSocket();
		return FALSE;
	}
	flags = fcntl(sSYNetPeerSocket, F_GETFL, 0);

	if ((flags < 0) || (fcntl(sSYNetPeerSocket, F_SETFL, flags | O_NONBLOCK) != 0))
	{
		port_log("SSB64 NetPeer: nonblocking setup failed errno=%d\n", errno);
		syNetPeerCloseSocket();
		return FALSE;
	}
	return TRUE;
}

static u64 syNetPeerNowUnixMs(void)
{
	struct timespec ts;
	u64 ms;

	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		return 0;
	}
	ms = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
	return ms;
}

static void syNetPeerWriteU64(u8 **cursor, u64 value)
{
	syNetPeerWriteU32(cursor, (u32)(value >> 32));
	syNetPeerWriteU32(cursor, (u32)(value & 0xFFFFFFFFU));
}

static u64 syNetPeerReadU64(const u8 **cursor)
{
	u64 hi = syNetPeerReadU32(cursor);
	u64 lo = syNetPeerReadU32(cursor);

	return (hi << 32) | lo;
}

static void syNetPeerResetClockAlignState(void)
{
	s32 i;

	sSYNetPeerTimePingSeq = 0;
	sSYNetPeerTimePingT0Sent = 0;
	for (i = 0; i < SYNETPEER_CLOCK_SYNC_SAMPLES; i++)
	{
		sSYNetPeerClockOffsetSamples[i] = 0;
		sSYNetPeerClockRttSamples[i] = 0;
	}
	sSYNetPeerClockSyncSampleCount = 0;
	sSYNetPeerBattleStartUnixMs = 0;
	sSYNetPeerBattleStartOffsetMs = 0;
	sSYNetPeerBattleStartTimeSent = FALSE;
	sSYNetPeerBattleStartTimeReceived = FALSE;
	sSYNetPeerBarrierDeadlineValid = FALSE;
	sSYNetPeerBarrierDeadlineUnixMs = 0;
	sSYNetPeerTimePingAwaitingAck = FALSE;
}

static void syNetPeerSendTimePingPacket(u32 seq, u64 t0_ms)
{
	u8 buffer[SYNETPEER_TIME_PING_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_TIME_PING);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, seq);
	syNetPeerWriteU64(&cursor, t0_ms);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_TIME_PING_BYTES);
}

static void syNetPeerSendTimePongPacket(u32 seq, u64 h0_echo, u64 c1_ms, u64 c2_ms)
{
	u8 buffer[SYNETPEER_TIME_PONG_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_TIME_PONG);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, seq);
	syNetPeerWriteU64(&cursor, h0_echo);
	syNetPeerWriteU64(&cursor, c1_ms);
	syNetPeerWriteU64(&cursor, c2_ms);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_TIME_PONG_BYTES);
}

static void syNetPeerSendBattleStartTimePacket(u64 start_unix_ms, s64 offset_host_minus_client_ms)
{
	u8 buffer[SYNETPEER_BATTLE_START_TIME_BYTES];
	u8 *cursor = buffer;
	u32 checksum;
	u64 off_u;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_BATTLE_START_TIME);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU64(&cursor, start_unix_ms);
	off_u = (u64)offset_host_minus_client_ms;
	syNetPeerWriteU64(&cursor, off_u);
	checksum = syNetPeerChecksumBytes(buffer, (u32)(cursor - buffer));
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_BATTLE_START_TIME_BYTES);
}

static s64 syNetPeerMedianS64(s64 *values, u32 count)
{
	s64 tmp[SYNETPEER_CLOCK_SYNC_SAMPLES];
	u32 a;
	u32 b;
	s64 swap;
	s32 mid;

	if ((count == 0) || (count > SYNETPEER_CLOCK_SYNC_SAMPLES))
	{
		return 0;
	}
	for (a = 0; a < count; a++)
	{
		tmp[a] = values[a];
	}
	for (a = 0; a < count; a++)
	{
		for (b = a + 1; b < count; b++)
		{
			if (tmp[b] < tmp[a])
			{
				swap = tmp[a];
				tmp[a] = tmp[b];
				tmp[b] = swap;
			}
		}
	}
	mid = (s32)(count / 2U);
	if ((count % 2U) != 0U)
	{
		return tmp[(u32)mid];
	}
	return (tmp[(u32)(mid - 1)] + tmp[(u32)mid]) / 2;
}

static void syNetPeerHostFinishClockSyncAndSendStart(void)
{
	u64 now_ms;
	u64 start_ms;
	u64 lead_ms;
	u32 max_rtt = 0;
	u32 half_rtt_plus;
	u32 i;
	s64 median_o;

	for (i = 0; i < SYNETPEER_CLOCK_SYNC_SAMPLES; i++)
	{
		if (sSYNetPeerClockRttSamples[i] > max_rtt)
		{
			max_rtt = sSYNetPeerClockRttSamples[i];
		}
	}
	median_o = syNetPeerMedianS64(sSYNetPeerClockOffsetSamples, SYNETPEER_CLOCK_SYNC_SAMPLES);
	now_ms = syNetPeerNowUnixMs();
	lead_ms = SYNETPEER_MIN_START_LEAD_MS;
	half_rtt_plus = (max_rtt / 2U) + SYNETPEER_START_MARGIN_MS;
	if (half_rtt_plus > lead_ms)
	{
		lead_ms = half_rtt_plus;
	}
	start_ms = now_ms + lead_ms;
	sSYNetPeerBattleStartUnixMs = start_ms;
	sSYNetPeerBattleStartOffsetMs = median_o;
	sSYNetPeerBarrierDeadlineUnixMs = start_ms;
	sSYNetPeerBarrierDeadlineValid = TRUE;
	syNetPeerSendBattleStartTimePacket(start_ms, median_o);
	sSYNetPeerBattleStartTimeSent = TRUE;
	sSYNetPeerBattleStartSent = TRUE;
	sSYNetPeerBattleStartReceived = TRUE;
	sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
#ifdef PORT
	port_log("SSB64 NetPeer: barrier schedule host max_rtt=%u median_o=%lld start_ms=%llu lead=%llu\n",
	         max_rtt, (long long)median_o, (unsigned long long)start_ms, (unsigned long long)lead_ms);
#endif
}

static sb32 syNetPeerCheckBarrierDeadlineReached(void)
{
	u64 now_ms;

	if (sSYNetPeerBarrierDeadlineValid == FALSE)
	{
		return FALSE;
	}
	now_ms = syNetPeerNowUnixMs();
	return (now_ms >= sSYNetPeerBarrierDeadlineUnixMs) ? TRUE : FALSE;
}

static void syNetPeerHandleTimePingPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u32 seq;
	u64 h0;
	u64 c1;
	u64 c2;
	u32 checksum;
	u32 expected_checksum;

	if (size != SYNETPEER_TIME_PING_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_TIME_PING_BYTES - 4);
	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	seq = syNetPeerReadU32(&cursor);
	h0 = syNetPeerReadU64(&cursor);
	checksum = syNetPeerReadU32(&cursor);
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_TIME_PING) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		return;
	}
	c1 = syNetPeerNowUnixMs();
	c2 = syNetPeerNowUnixMs();
	syNetPeerSendTimePongPacket(seq, h0, c1, c2);
}

static void syNetPeerHandleTimePongPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u32 seq;
	u64 h0;
	u64 c1;
	u64 c2;
	u64 h3;
	u32 checksum;
	u32 expected_checksum;
	s64 o_sample;
	u32 rtt_ms;

	if (size != SYNETPEER_TIME_PONG_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_TIME_PONG_BYTES - 4);
	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	seq = syNetPeerReadU32(&cursor);
	h0 = syNetPeerReadU64(&cursor);
	c1 = syNetPeerReadU64(&cursor);
	c2 = syNetPeerReadU64(&cursor);
	checksum = syNetPeerReadU32(&cursor);
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_TIME_PONG) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		return;
	}
	if (sSYNetPeerClockSyncSampleCount >= SYNETPEER_CLOCK_SYNC_SAMPLES)
	{
		return;
	}
	if ((seq != sSYNetPeerClockSyncSampleCount) || (h0 != sSYNetPeerTimePingT0Sent))
	{
		return;
	}
	h3 = syNetPeerNowUnixMs();
	o_sample = ((s64)h0 - (s64)c1 + (s64)h3 - (s64)c2) / 2;
	if (h3 >= h0)
	{
		rtt_ms = (u32)(h3 - h0);
	}
	else
	{
		rtt_ms = 0;
	}
	sSYNetPeerClockOffsetSamples[sSYNetPeerClockSyncSampleCount] = o_sample;
	sSYNetPeerClockRttSamples[sSYNetPeerClockSyncSampleCount] = rtt_ms;
	sSYNetPeerTimePingAwaitingAck = FALSE;
	sSYNetPeerClockSyncSampleCount++;
	if (sSYNetPeerClockSyncSampleCount >= SYNETPEER_CLOCK_SYNC_SAMPLES)
	{
		syNetPeerHostFinishClockSyncAndSendStart();
	}
}

static void syNetPeerHandleBattleStartTimePacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u16 version;
	u16 packet_type;
	u32 session_id;
	u64 start_ms;
	u64 offset_u;
	s64 offset_ms;
	u64 deadline_ms;
	u64 now_ms;
	u32 checksum;
	u32 expected_checksum;

	if (size != SYNETPEER_BATTLE_START_TIME_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_BATTLE_START_TIME_BYTES - 4);
	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	start_ms = syNetPeerReadU64(&cursor);
	offset_u = syNetPeerReadU64(&cursor);
	checksum = syNetPeerReadU32(&cursor);
	offset_ms = (s64)offset_u;
	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
	    (packet_type != SYNETPEER_PACKET_BATTLE_START_TIME) || (session_id != sSYNetPeerSessionID) ||
	    (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		return;
	}
	sSYNetPeerBattleStartUnixMs = start_ms;
	sSYNetPeerBattleStartOffsetMs = offset_ms;
	deadline_ms = (u64)((s64)start_ms - offset_ms);
	now_ms = syNetPeerNowUnixMs();
	if (deadline_ms < now_ms)
	{
		deadline_ms = now_ms;
	}
	sSYNetPeerBarrierDeadlineUnixMs = deadline_ms;
	sSYNetPeerBarrierDeadlineValid = TRUE;
	sSYNetPeerBattleStartTimeReceived = TRUE;
	sSYNetPeerBattleStartReceived = TRUE;
	sSYNetPeerBattleStartSent = TRUE;
	sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
#ifdef PORT
	port_log("SSB64 NetPeer: barrier schedule client start_ms=%llu off=%lld deadline_ms=%llu now=%llu\n",
	         (unsigned long long)start_ms, (long long)offset_ms, (unsigned long long)deadline_ms,
	         (unsigned long long)now_ms);
#endif
}

#endif

void syNetPeerMakeBootstrapMetadata(SYNetInputReplayMetadata *metadata)
{
	s32 player;

	memset(metadata, 0, sizeof(*metadata));
	metadata->magic = SYNETINPUT_REPLAY_MAGIC;
	metadata->version = SYNETINPUT_REPLAY_VERSION;
	metadata->scene_kind = nSCKindVSBattle;
	metadata->player_count = 2;
	metadata->stage_kind = nGRKindCastle;
	metadata->stocks = 3;
	metadata->time_limit = SCBATTLE_TIMELIMIT_INFINITE;
	metadata->item_switch = nSCBattleItemSwitchNone;
	metadata->item_toggles = 0;
	metadata->rng_seed = sSYNetPeerBootstrapSeed;
	metadata->game_type = nSCBattleGameTypeRoyal;
	metadata->game_rules = SCBATTLE_GAMERULE_STOCK;
	metadata->is_team_battle = FALSE;
	metadata->handicap = nSCBattleHandicapOff;
	metadata->is_team_attack = FALSE;
	metadata->is_stage_select = FALSE;
	metadata->damage_ratio = 100;
	metadata->item_appearance_rate = nSCBattleItemSwitchNone;
	metadata->is_not_teamshadows = TRUE;

	for (player = 0; player < MAXCONTROLLERS; player++)
	{
		metadata->player_kinds[player] = nFTPlayerKindNot;
		metadata->fighter_kinds[player] = nFTKindNull;
		metadata->costumes[player] = 0;
		metadata->teams[player] = player;
		metadata->handicaps[player] = 9;
		metadata->levels[player] = 3;
		metadata->shades[player] = 0;
	}
	metadata->player_kinds[0] = nFTPlayerKindMan;
	metadata->fighter_kinds[0] = nFTKindMario;
	metadata->player_kinds[1] = nFTPlayerKindMan;
	metadata->fighter_kinds[1] = nFTKindFox;
}

void syNetPeerApplyBootstrapMetadata(const SYNetInputReplayMetadata *metadata)
{
	sSYNetPeerBootstrapMetadata = *metadata;
	syNetReplayApplyBattleMetadata(&sSYNetPeerBootstrapMetadata);
	syUtilsSetRandomSeed(sSYNetPeerBootstrapMetadata.rng_seed);
	gSCManagerSceneData.scene_prev = nSCKindVSMode;
	gSCManagerSceneData.scene_curr = nSCKindVSBattle;
	sSYNetPeerBootstrapMetadataApplied = TRUE;

#ifdef PORT
	port_log("SSB64 NetPeer: bootstrap metadata applied host=%d stage=%u seed=%u players=%u p0=%u/%u p1=%u/%u\n",
	         sSYNetPeerBootstrapIsHost, sSYNetPeerBootstrapMetadata.stage_kind,
	         sSYNetPeerBootstrapMetadata.rng_seed, sSYNetPeerBootstrapMetadata.player_count,
	         sSYNetPeerBootstrapMetadata.player_kinds[0], sSYNetPeerBootstrapMetadata.fighter_kinds[0],
	         sSYNetPeerBootstrapMetadata.player_kinds[1], sSYNetPeerBootstrapMetadata.fighter_kinds[1]);
#endif
}

#if defined(PORT) && !defined(_WIN32)
void syNetPeerSendBytes(const u8 *buffer, u32 size)
{
	sendto(sSYNetPeerSocket, buffer, size, 0,
	       (struct sockaddr*)&sSYNetPeerPeerAddress, sizeof(sSYNetPeerPeerAddress));
}
#endif

void syNetPeerSendControlPacket(u16 packet_type)
{
#if defined(PORT) && !defined(_WIN32)
	u8 buffer[SYNETPEER_CONTROL_PACKET_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, packet_type);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	checksum = syNetPeerChecksumBytes(buffer, cursor - buffer);
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_CONTROL_PACKET_BYTES);
#endif
}

#if defined(PORT) && !defined(_WIN32)
void syNetPeerSendMatchConfigPacket(void)
{
	u8 buffer[SYNETPEER_BOOTSTRAP_PACKET_BYTES];
	u8 *cursor = buffer;
	u32 checksum;

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, SYNETPEER_VERSION);
	syNetPeerWriteU16(&cursor, SYNETPEER_PACKET_MATCH_CONFIG);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteMetadata(&cursor, &sSYNetPeerBootstrapMetadata);
	checksum = syNetPeerChecksumBytes(buffer, cursor - buffer);
	syNetPeerWriteU32(&cursor, checksum);
	syNetPeerSendBytes(buffer, SYNETPEER_BOOTSTRAP_PACKET_BYTES);
}

void syNetPeerHandleControlPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	u32 magic;
	u32 session_id;
	u32 checksum;
	u32 expected_checksum;
	u16 version;
	u16 packet_type;

	if (size != SYNETPEER_CONTROL_PACKET_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_CONTROL_PACKET_BYTES - 4);

	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	checksum = syNetPeerReadU32(&cursor);

	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
		(session_id != sSYNetPeerSessionID) || (checksum != expected_checksum))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (packet_type == SYNETPEER_PACKET_READY)
	{
		sSYNetPeerBootstrapPeerReady = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_START)
	{
		sSYNetPeerBootstrapStartReceived = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_BATTLE_READY)
	{
		if (sSYNetPeerBattlePeerReady == FALSE)
		{
			port_log("SSB64 NetPeer: received BATTLE_READY role=%s tick=%u local=%d remote=%d\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         syNetInputGetTick(), sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer);
		}
		sSYNetPeerBattlePeerReady = TRUE;
	}
	else if (packet_type == SYNETPEER_PACKET_BATTLE_START)
	{
		if (sSYNetPeerBattleStartReceived == FALSE)
		{
			port_log("SSB64 NetPeer: received BATTLE_START role=%s tick=%u local=%d remote=%d\n",
			         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
			         syNetInputGetTick(), sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer);
		}
		sSYNetPeerBattleStartReceived = TRUE;
	}
}

void syNetPeerHandleMatchConfigPacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	SYNetInputReplayMetadata metadata;
	u32 magic;
	u32 session_id;
	u32 checksum;
	u32 expected_checksum;
	u16 version;
	u16 packet_type;

	if (size != SYNETPEER_BOOTSTRAP_PACKET_BYTES)
	{
		return;
	}
	expected_checksum = syNetPeerChecksumBytes(buffer, SYNETPEER_BOOTSTRAP_PACKET_BYTES - 4);

	magic = syNetPeerReadU32(&cursor);
	version = syNetPeerReadU16(&cursor);
	packet_type = syNetPeerReadU16(&cursor);
	session_id = syNetPeerReadU32(&cursor);
	syNetPeerReadMetadata(&cursor, &metadata);
	checksum = syNetPeerReadU32(&cursor);

	if ((magic != SYNETPEER_MAGIC) || (version != SYNETPEER_VERSION) ||
		(packet_type != SYNETPEER_PACKET_MATCH_CONFIG) ||
		(session_id != sSYNetPeerSessionID) || (checksum != expected_checksum) ||
		(syNetPeerCheckMetadata(&metadata) == FALSE))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	if (sSYNetPeerBootstrapIsHost == FALSE)
	{
		syNetPeerApplyBootstrapMetadata(&metadata);
	}
}

void syNetPeerHandleBootstrapPacket(const u8 *buffer, s32 size)
{
	if (size == SYNETPEER_CONTROL_PACKET_BYTES)
	{
		syNetPeerHandleControlPacket(buffer, size);
	}
	else if (size == SYNETPEER_BOOTSTRAP_PACKET_BYTES)
	{
		syNetPeerHandleMatchConfigPacket(buffer, size);
	}
	else sSYNetPeerPacketsDropped++;
}

void syNetPeerReceiveBootstrapPackets(void)
{
	u8 buffer[SYNETPEER_BOOTSTRAP_PACKET_BYTES];

	while (TRUE)
	{
		ssize_t size = recvfrom(sSYNetPeerSocket, buffer, sizeof(buffer), 0, NULL, NULL);

		if (size < 0)
		{
			if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			{
				sSYNetPeerPacketsDropped++;
			}
			break;
		}
		syNetPeerHandleBootstrapPacket(buffer, (s32)size);
	}
}

void syNetPeerRunBootstrap(void)
{
	s32 i;

	if ((sSYNetPeerBootstrapIsEnabled == FALSE) || (sSYNetPeerIsConfigured == FALSE))
	{
		return;
	}
	if (syNetPeerOpenSocket() == FALSE)
	{
		return;
	}
	sSYNetPeerIsActive = TRUE;

	if (sSYNetPeerBootstrapIsHost != FALSE)
	{
		syNetPeerMakeBootstrapMetadata(&sSYNetPeerBootstrapMetadata);
		syNetPeerApplyBootstrapMetadata(&sSYNetPeerBootstrapMetadata);

		for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
		{
			syNetPeerSendMatchConfigPacket();
			syNetPeerReceiveBootstrapPackets();

			if (sSYNetPeerBootstrapPeerReady != FALSE)
			{
				break;
			}
			syNetPeerSleepBootstrapRetry();
		}
		if (sSYNetPeerBootstrapPeerReady == FALSE)
		{
			port_log("SSB64 NetPeer: bootstrap host timed out waiting for READY\n");
			return;
		}
		for (i = 0; i < 30; i++)
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_START);
			syNetPeerSleepBootstrapRetry();
		}
		port_log("SSB64 NetPeer: bootstrap host sent START stage=%u seed=%u\n",
		         sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed);
	}
	else
	{
		for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
		{
			syNetPeerReceiveBootstrapPackets();

			if (sSYNetPeerBootstrapMetadataApplied != FALSE)
			{
				syNetPeerSendControlPacket(SYNETPEER_PACKET_READY);
				break;
			}
			syNetPeerSleepBootstrapRetry();
		}
		if (sSYNetPeerBootstrapMetadataApplied == FALSE)
		{
			port_log("SSB64 NetPeer: bootstrap client timed out waiting for MATCH_CONFIG\n");
			return;
		}
		for (i = 0; i < SYNETPEER_BOOTSTRAP_RETRY_COUNT; i++)
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_READY);
			syNetPeerReceiveBootstrapPackets();

			if (sSYNetPeerBootstrapStartReceived != FALSE)
			{
				break;
			}
			syNetPeerSleepBootstrapRetry();
		}
		if (sSYNetPeerBootstrapStartReceived == FALSE)
		{
			port_log("SSB64 NetPeer: bootstrap client timed out waiting for START\n");
			return;
		}
		port_log("SSB64 NetPeer: bootstrap client received START stage=%u seed=%u\n",
		         sSYNetPeerBootstrapMetadata.stage_kind, sSYNetPeerBootstrapMetadata.rng_seed);
	}
}
#endif

void syNetPeerInitDebugEnv(void)
{
#ifdef PORT
	char *netplay_env = getenv("SSB64_NETPLAY");
	char *local_player_env;
	char *remote_player_env;
	char *delay_env;
	char *session_env;
	char *bind_env;
	char *peer_env;
	char *bootstrap_env;
	char *bootstrap_host_env;
	char *bootstrap_seed_env;

	sSYNetPeerIsEnabled = FALSE;
	sSYNetPeerIsConfigured = FALSE;
	sSYNetPeerLocalPlayer = 0;
	sSYNetPeerRemotePlayer = 1;
	sSYNetPeerInputDelay = SYNETPEER_DEFAULT_INPUT_DELAY;
	sSYNetPeerSessionID = SYNETPEER_DEFAULT_SESSION_ID;
	sSYNetPeerBootstrapIsEnabled = FALSE;
	sSYNetPeerBootstrapIsHost = FALSE;
	sSYNetPeerBootstrapMetadataApplied = FALSE;
	sSYNetPeerBootstrapPeerReady = FALSE;
	sSYNetPeerBootstrapStartReceived = FALSE;
	sSYNetPeerBootstrapSeed = SYNETPEER_DEFAULT_BOOTSTRAP_SEED;
	sSYNetPeerBattleBarrierEnabled = FALSE;
	sSYNetPeerBattleLocalReady = FALSE;
	sSYNetPeerBattlePeerReady = FALSE;
	sSYNetPeerBattleStartSent = FALSE;
	sSYNetPeerBattleStartReceived = FALSE;
	sSYNetPeerBattleBarrierReleased = TRUE;
	sSYNetPeerBattleBarrierWaitFrames = 0;
	sSYNetPeerBattleStartRepeatFrames = 0;
	sSYNetPeerExecutionHoldFrames = 0;
	sSYNetPeerExecutionBeginLogged = FALSE;
	sSYNetPeerClockAlignEnabled = FALSE;

	syNetRollbackInit();

	if ((netplay_env == NULL) || (atoi(netplay_env) == 0))
	{
		return;
	}
	sSYNetPeerIsEnabled = TRUE;

	local_player_env = getenv("SSB64_NETPLAY_LOCAL_PLAYER");
	remote_player_env = getenv("SSB64_NETPLAY_REMOTE_PLAYER");
	delay_env = getenv("SSB64_NETPLAY_DELAY");
	session_env = getenv("SSB64_NETPLAY_SESSION");
	bind_env = getenv("SSB64_NETPLAY_BIND");
	peer_env = getenv("SSB64_NETPLAY_PEER");
	bootstrap_env = getenv("SSB64_NETPLAY_BOOTSTRAP");
	bootstrap_host_env = getenv("SSB64_NETPLAY_HOST");
	bootstrap_seed_env = getenv("SSB64_NETPLAY_SEED");

	if (local_player_env != NULL)
	{
		sSYNetPeerLocalPlayer = atoi(local_player_env);
	}
	if (remote_player_env != NULL)
	{
		sSYNetPeerRemotePlayer = atoi(remote_player_env);
	}
	if (delay_env != NULL)
	{
		s32 delay = atoi(delay_env);

		if (delay >= 0)
		{
			sSYNetPeerInputDelay = delay;
		}
	}
#ifdef PORT
	{
		char *adapt_env;
		char *delay_max_env;

		sSYNetPeerAdaptiveDelayEnabled = FALSE;
		sSYNetPeerInputDelayFloor = sSYNetPeerInputDelay;
		sSYNetPeerInputDelayCeil = 12;
		adapt_env = getenv("SSB64_NETPLAY_ADAPTIVE_DELAY");
		if ((adapt_env != NULL) && (atoi(adapt_env) != 0))
		{
			sSYNetPeerAdaptiveDelayEnabled = TRUE;
		}
		delay_max_env = getenv("SSB64_NETPLAY_DELAY_MAX");
		if ((delay_max_env != NULL) && (atoi(delay_max_env) > 0))
		{
			sSYNetPeerInputDelayCeil = (u32)atoi(delay_max_env);
		}
		if (sSYNetPeerInputDelayCeil < sSYNetPeerInputDelayFloor)
		{
			sSYNetPeerInputDelayCeil = sSYNetPeerInputDelayFloor;
		}
	}
#endif
	if (session_env != NULL)
	{
		s32 session_id = atoi(session_env);

		if (session_id > 0)
		{
			sSYNetPeerSessionID = session_id;
		}
	}
	if ((bootstrap_env != NULL) && (atoi(bootstrap_env) != 0))
	{
		sSYNetPeerBootstrapIsEnabled = TRUE;
	}
	if ((bootstrap_host_env != NULL) && (atoi(bootstrap_host_env) != 0))
	{
		sSYNetPeerBootstrapIsHost = TRUE;
	}
	if (bootstrap_seed_env != NULL)
	{
		s32 seed = atoi(bootstrap_seed_env);

		if (seed > 0)
		{
			sSYNetPeerBootstrapSeed = seed;
		}
	}
	if ((sSYNetPeerLocalPlayer < 0) || (sSYNetPeerLocalPlayer >= MAXCONTROLLERS) ||
		(sSYNetPeerRemotePlayer < 0) || (sSYNetPeerRemotePlayer >= MAXCONTROLLERS) ||
		(sSYNetPeerLocalPlayer == sSYNetPeerRemotePlayer))
	{
		port_log("SSB64 NetPeer: invalid players local=%d remote=%d\n",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer);
		return;
	}
	syNetPeerConfigureRemoteReceiveSlots();
	syNetPeerConfigurePeerSenderSlots();
	syNetPeerConfigureExtraLocalSender();
	if ((syNetPeerValidateRemoteReceiveList() == FALSE) || (syNetPeerValidatePeerSenderList() == FALSE))
	{
		return;
	}
#if !defined(_WIN32)
	if ((syNetPeerParseIPv4Address(bind_env, &sSYNetPeerBindAddress) == FALSE) ||
		(syNetPeerParseIPv4Address(peer_env, &sSYNetPeerPeerAddress) == FALSE))
	{
		port_log("SSB64 NetPeer: invalid bind/peer; expected IPv4 host:port\n");
		return;
	}
	sSYNetPeerIsConfigured = TRUE;
	port_log("SSB64 NetPeer: configured local=%d remote=%d delay=%u session=%u bootstrap=%d host=%d seed=%u bind=%s peer=%s\n",
	         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, sSYNetPeerInputDelay,
	         sSYNetPeerSessionID, sSYNetPeerBootstrapIsEnabled, sSYNetPeerBootstrapIsHost,
	         sSYNetPeerBootstrapSeed, bind_env, peer_env);
	{
		char *sync_start_env = getenv("SSB64_NETPLAY_SYNC_START_MS");

		sSYNetPeerClockAlignEnabled = FALSE;
		if ((sSYNetPeerBootstrapIsEnabled != FALSE) && ((sync_start_env == NULL) || (atoi(sync_start_env) != 0)))
		{
			sSYNetPeerClockAlignEnabled = TRUE;
		}
	}
	syNetPeerRunBootstrap();
#else
	port_log("SSB64 NetPeer: debug UDP netplay is not implemented on Windows yet\n");
#endif
#endif
}

void syNetPeerStartVSSession(void)
{
#if defined(PORT) && !defined(_WIN32)
	if ((sSYNetPeerIsEnabled == FALSE) || (sSYNetPeerIsConfigured == FALSE))
	{
		return;
	}
	if (syNetPeerOpenSocket() == FALSE)
	{
		return;
	}
	sSYNetPeerHighestRemoteTick = 0;
	sSYNetPeerPacketsSent = 0;
	sSYNetPeerPacketsReceived = 0;
	sSYNetPeerPacketsDropped = 0;
	sSYNetPeerFramesStaged = 0;
	sSYNetPeerLateFrames = 0;
	sSYNetPeerInputChecksum = 2166136261U;
	sSYNetPeerLastLogTick = 0;
	sSYNetPeerSendSeq = 0;
	sSYNetPeerRecvSeqInitialized = FALSE;
	sSYNetPeerRecvSeqHighWater = 0;
	sSYNetPeerSeqGaps = 0;
	sSYNetPeerSeqDuplicates = 0;
	sSYNetPeerSeqOutOfOrder = 0;
	sSYNetPeerLastPeerAckTick = 0;
	sSYNetPeerLastPacketTicksValid = FALSE;
	sSYNetPeerIsActive = TRUE;
	sSYNetPeerBattleBarrierEnabled = sSYNetPeerBootstrapIsEnabled;
	sSYNetPeerBattleLocalReady = sSYNetPeerBattleBarrierEnabled;
	sSYNetPeerBattlePeerReady = FALSE;
	sSYNetPeerBattleStartSent = FALSE;
	sSYNetPeerBattleStartReceived = FALSE;
	sSYNetPeerBattleBarrierReleased = (sSYNetPeerBattleBarrierEnabled == FALSE) ? TRUE : FALSE;
	sSYNetPeerBattleBarrierWaitFrames = 0;
	sSYNetPeerBattleStartRepeatFrames = 0;
	sSYNetPeerExecutionHoldFrames = 0;
	sSYNetPeerExecutionBeginLogged = (sSYNetPeerBattleBarrierEnabled == FALSE) ? TRUE : FALSE;

	syNetPeerResetClockAlignState();
#ifdef PORT
	syNetPeerResetAdaptiveDelayTracking();
#endif

	{
		s32 ri;

		syNetInputSetSlotSource(sSYNetPeerLocalPlayer, nSYNetInputSourceLocal);
		for (ri = 0; ri < sSYNetPeerRemoteReceiveCount; ri++)
		{
			syNetInputSetSlotSource((s32)sSYNetPeerRemoteReceiveSlots[ri], nSYNetInputSourceRemotePredicted);
		}
	}

	syNetRollbackStartVSSession();

	port_log("SSB64 NetPeer: VS session start role=%s local=%d remote=%d delay=%u barrier=%d tick=%u recv_n=%d recv=%u,%u,%u,%u peer_snd_n=%d peer_snd=%u,%u,%u,%u extra_local=%d\n",
	         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
	         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, sSYNetPeerInputDelay,
	         sSYNetPeerBattleBarrierEnabled, syNetInputGetTick(),
	         sSYNetPeerRemoteReceiveCount,
	         (sSYNetPeerRemoteReceiveCount > 0) ? (u32)sSYNetPeerRemoteReceiveSlots[0] : 255U,
	         (sSYNetPeerRemoteReceiveCount > 1) ? (u32)sSYNetPeerRemoteReceiveSlots[1] : 255U,
	         (sSYNetPeerRemoteReceiveCount > 2) ? (u32)sSYNetPeerRemoteReceiveSlots[2] : 255U,
	         (sSYNetPeerRemoteReceiveCount > 3) ? (u32)sSYNetPeerRemoteReceiveSlots[3] : 255U,
	         sSYNetPeerPeerSenderCount,
	         (sSYNetPeerPeerSenderCount > 0) ? (u32)sSYNetPeerPeerSenderSlots[0] : 255U,
	         (sSYNetPeerPeerSenderCount > 1) ? (u32)sSYNetPeerPeerSenderSlots[1] : 255U,
	         (sSYNetPeerPeerSenderCount > 2) ? (u32)sSYNetPeerPeerSenderSlots[2] : 255U,
	         (sSYNetPeerPeerSenderCount > 3) ? (u32)sSYNetPeerPeerSenderSlots[3] : 255U,
	         sSYNetPeerExtraLocalSenderSlot);
#endif
}

sb32 syNetPeerCheckBattleExecutionReady(void)
{
	if ((sSYNetPeerIsEnabled == FALSE) || (sSYNetPeerBootstrapIsEnabled == FALSE) ||
		(sSYNetPeerBattleBarrierEnabled == FALSE))
	{
		return TRUE;
	}
	return sSYNetPeerBattleBarrierReleased;
}

sb32 syNetPeerCheckStartBarrierReleased(void)
{
	return syNetPeerCheckBattleExecutionReady();
}

static sb32 syNetPeerU8InList(u8 value, const u8 *list, s32 count)
{
	s32 i;

	for (i = 0; i < count; i++)
	{
		if (list[i] == value)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static sb32 syNetPeerParseCommaControllerSlots(const char *str, u8 *out_slots, s32 *out_count)
{
	const char *p;
	s32 n;
	s32 v;
	sb32 have_digit;

	if ((str == NULL) || (out_slots == NULL) || (out_count == NULL))
	{
		return FALSE;
	}
	n = 0;
	p = str;
	while ((*p != '\0') && (n < SYNETPEER_MAX_REMOTE_PLAYLIST))
	{
		while ((*p == ' ') || (*p == '\t') || (*p == ','))
		{
			p++;
		}
		if (*p == '\0')
		{
			break;
		}
		v = 0;
		have_digit = FALSE;
		while ((*p >= '0') && (*p <= '9'))
		{
			v = (v * 10) + (*p - '0');
			have_digit = TRUE;
			p++;
		}
		if ((have_digit == FALSE) || (v < 0) || (v >= MAXCONTROLLERS))
		{
			return FALSE;
		}
		if (syNetPeerU8InList((u8)v, out_slots, n) != FALSE)
		{
			return FALSE;
		}
		out_slots[n++] = (u8)v;
		while ((*p == ' ') || (*p == '\t'))
		{
			p++;
		}
		if (*p == ',')
		{
			p++;
		}
	}
	*out_count = n;
	return (n > 0) ? TRUE : FALSE;
}

static void syNetPeerConfigureRemoteReceiveSlots(void)
{
	char *env_slots;

	sSYNetPeerRemoteReceiveSlots[0] = (u8)sSYNetPeerRemotePlayer;
	sSYNetPeerRemoteReceiveCount = 1;
	env_slots = getenv("SSB64_NETPLAY_REMOTE_SLOTS");
	if ((env_slots != NULL) && (env_slots[0] != '\0'))
	{
		if (syNetPeerParseCommaControllerSlots(env_slots, sSYNetPeerRemoteReceiveSlots, &sSYNetPeerRemoteReceiveCount) ==
		    FALSE)
		{
			port_log("SSB64 NetPeer: invalid SSB64_NETPLAY_REMOTE_SLOTS; using remote=%d only\n",
			         sSYNetPeerRemotePlayer);
			sSYNetPeerRemoteReceiveSlots[0] = (u8)sSYNetPeerRemotePlayer;
			sSYNetPeerRemoteReceiveCount = 1;
		}
	}
}

static void syNetPeerConfigurePeerSenderSlots(void)
{
	char *env_slots;

	sSYNetPeerPeerSenderSlots[0] = (u8)sSYNetPeerRemotePlayer;
	sSYNetPeerPeerSenderCount = 1;
	env_slots = getenv("SSB64_NETPLAY_PEER_SENDER_SLOTS");
	if ((env_slots != NULL) && (env_slots[0] != '\0'))
	{
		if (syNetPeerParseCommaControllerSlots(env_slots, sSYNetPeerPeerSenderSlots, &sSYNetPeerPeerSenderCount) ==
		    FALSE)
		{
			port_log("SSB64 NetPeer: invalid SSB64_NETPLAY_PEER_SENDER_SLOTS; using remote=%d only\n",
			         sSYNetPeerRemotePlayer);
			sSYNetPeerPeerSenderSlots[0] = (u8)sSYNetPeerRemotePlayer;
			sSYNetPeerPeerSenderCount = 1;
		}
	}
}

static sb32 syNetPeerIsAllowedPeerSenderSlot(u8 slot)
{
	if (slot == (u8)sSYNetPeerLocalPlayer)
	{
		return FALSE;
	}
	return syNetPeerU8InList(slot, sSYNetPeerPeerSenderSlots, sSYNetPeerPeerSenderCount);
}

static void syNetPeerConfigureExtraLocalSender(void)
{
	char *env_extra;
	s32 v;

	sSYNetPeerExtraLocalSenderSlot = -1;
	env_extra = getenv("SSB64_NETPLAY_EXTRA_LOCAL_PLAYER");
	if ((env_extra == NULL) || (env_extra[0] == '\0'))
	{
		return;
	}
	v = atoi(env_extra);
	if ((v < 0) || (v >= MAXCONTROLLERS) || (v == sSYNetPeerLocalPlayer))
	{
		port_log("SSB64 NetPeer: ignoring invalid SSB64_NETPLAY_EXTRA_LOCAL_PLAYER=%d\n", v);
		return;
	}
	if (syNetPeerU8InList((u8)v, sSYNetPeerRemoteReceiveSlots, sSYNetPeerRemoteReceiveCount) != FALSE)
	{
		port_log(
		    "SSB64 NetPeer: SSB64_NETPLAY_EXTRA_LOCAL_PLAYER=%d conflicts with remote receive slots; ignoring extra bundle\n",
		    v);
		return;
	}
	sSYNetPeerExtraLocalSenderSlot = v;
}

static sb32 syNetPeerValidateRemoteReceiveList(void)
{
	s32 i;
	s32 j;

	for (i = 0; i < sSYNetPeerRemoteReceiveCount; i++)
	{
		if (((s32)sSYNetPeerRemoteReceiveSlots[i] < 0) || ((s32)sSYNetPeerRemoteReceiveSlots[i] >= MAXCONTROLLERS) ||
			((s32)sSYNetPeerRemoteReceiveSlots[i] == sSYNetPeerLocalPlayer))
		{
			port_log("SSB64 NetPeer: invalid remote receive slot list\n");
			return FALSE;
		}
		for (j = i + 1; j < sSYNetPeerRemoteReceiveCount; j++)
		{
			if (sSYNetPeerRemoteReceiveSlots[i] == sSYNetPeerRemoteReceiveSlots[j])
			{
				port_log("SSB64 NetPeer: duplicate remote receive slot list\n");
				return FALSE;
			}
		}
	}
	return TRUE;
}

static sb32 syNetPeerValidatePeerSenderList(void)
{
	s32 i;
	s32 j;

	for (i = 0; i < sSYNetPeerPeerSenderCount; i++)
	{
		if (((s32)sSYNetPeerPeerSenderSlots[i] < 0) || ((s32)sSYNetPeerPeerSenderSlots[i] >= MAXCONTROLLERS) ||
			((s32)sSYNetPeerPeerSenderSlots[i] == sSYNetPeerLocalPlayer))
		{
			port_log("SSB64 NetPeer: invalid peer sender slot list\n");
			return FALSE;
		}
		for (j = i + 1; j < sSYNetPeerPeerSenderCount; j++)
		{
			if (sSYNetPeerPeerSenderSlots[i] == sSYNetPeerPeerSenderSlots[j])
			{
				port_log("SSB64 NetPeer: duplicate peer sender slot list\n");
				return FALSE;
			}
		}
	}
	return TRUE;
}

static sb32 syNetPeerGatherHistoryBundle(s32 slot, SYNetPeerPacketFrame *frames, s32 *out_frame_count)
{
	SYNetInputFrame published_frame;
	SYNetInputFrame history_frame;
	u32 latest_tick;
	s32 frame_count;
	s32 back;

	if (syNetInputGetPublishedFrame(slot, &published_frame) == FALSE)
	{
		return FALSE;
	}
	latest_tick = published_frame.tick;
	frame_count = 0;
	for (back = SYNETPEER_MAX_PACKET_FRAMES - 1; back >= 0; back--)
	{
		if ((latest_tick >= (u32)back) &&
			(syNetInputGetHistoryFrame(slot, latest_tick - back, &history_frame) != FALSE))
		{
			frames[frame_count].tick = history_frame.tick + sSYNetPeerInputDelay;
			frames[frame_count].buttons = history_frame.buttons;
			frames[frame_count].stick_x = history_frame.stick_x;
			frames[frame_count].stick_y = history_frame.stick_y;
			frame_count++;
		}
	}
	*out_frame_count = frame_count;
	return TRUE;
}

void syNetPeerBuildPacket(u8 *buffer, u32 *out_size)
{
	SYNetPeerPacketFrame frames[SYNETPEER_MAX_PACKET_FRAMES];
	SYNetPeerPacketFrame sec_frames[SYNETPEER_MAX_PACKET_FRAMES];
	SYNetPeerPacketFrame zero_frame;
	u8 *cursor = buffer;
	u32 checksum;
	s32 frame_count = 0;
	s32 sec_frame_count = 0;
	s32 i;
	u16 wire_version;
	u8 secondary_slot_byte;

	memset(frames, 0, sizeof(frames));
	memset(sec_frames, 0, sizeof(sec_frames));
	memset(&zero_frame, 0, sizeof(zero_frame));

	if (syNetPeerGatherHistoryBundle(sSYNetPeerLocalPlayer, frames, &frame_count) == FALSE)
	{
		*out_size = 0;
		return;
	}
	wire_version = SYNETPEER_VERSION;
	secondary_slot_byte = SYNETPEER_SECONDARY_SLOT_ABSENT;
	if ((sSYNetPeerExtraLocalSenderSlot >= 0) &&
		(syNetPeerGatherHistoryBundle(sSYNetPeerExtraLocalSenderSlot, sec_frames, &sec_frame_count) != FALSE))
	{
		wire_version = SYNETPEER_VERSION_DUAL_LOCAL;
		secondary_slot_byte = (u8)sSYNetPeerExtraLocalSenderSlot;
	}
	checksum = syNetPeerChecksumInputPacket(sSYNetPeerSessionID, sSYNetPeerHighestRemoteTick, sSYNetPeerSendSeq,
	                                       wire_version, (u8)sSYNetPeerLocalPlayer, (u8)frame_count, frames,
	                                       secondary_slot_byte, (u8)sec_frame_count, sec_frames);

	syNetPeerWriteU32(&cursor, SYNETPEER_MAGIC);
	syNetPeerWriteU16(&cursor, wire_version);
	syNetPeerWriteU16(&cursor, 0);
	syNetPeerWriteU32(&cursor, sSYNetPeerSessionID);
	syNetPeerWriteU32(&cursor, sSYNetPeerHighestRemoteTick);
	syNetPeerWriteU32(&cursor, sSYNetPeerSendSeq);
	syNetPeerWriteU8(&cursor, (u8)sSYNetPeerLocalPlayer);
	syNetPeerWriteU8(&cursor, (u8)frame_count);
	syNetPeerWriteU8(&cursor, (u8)sSYNetPeerLocalPlayer);
	syNetPeerWriteU8(&cursor, (u8)sSYNetPeerRemotePlayer);

	for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
	{
		SYNetPeerPacketFrame *frame = (i < frame_count) ? &frames[i] : &zero_frame;

		syNetPeerWriteU32(&cursor, frame->tick);
		syNetPeerWriteU16(&cursor, frame->buttons);
		syNetPeerWriteU8(&cursor, (u8)frame->stick_x);
		syNetPeerWriteU8(&cursor, (u8)frame->stick_y);
	}
	if (wire_version >= SYNETPEER_VERSION_DUAL_LOCAL)
	{
		syNetPeerWriteU8(&cursor, secondary_slot_byte);
		syNetPeerWriteU8(&cursor, (u8)sec_frame_count);
		for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
		{
			SYNetPeerPacketFrame *frame = (i < sec_frame_count) ? &sec_frames[i] : &zero_frame;

			syNetPeerWriteU32(&cursor, frame->tick);
			syNetPeerWriteU16(&cursor, frame->buttons);
			syNetPeerWriteU8(&cursor, (u8)frame->stick_x);
			syNetPeerWriteU8(&cursor, (u8)frame->stick_y);
		}
		*out_size = SYNETPEER_PACKET_BYTES_V3;
	}
	else *out_size = SYNETPEER_PACKET_BYTES_V2;

	syNetPeerWriteU32(&cursor, checksum);
}

void syNetPeerSendLocalInput(void)
{
#if defined(PORT) && !defined(_WIN32)
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];
	u32 size;

	syNetPeerBuildPacket(buffer, &size);

	if (size == 0)
	{
		return;
	}
	if (sendto(sSYNetPeerSocket, buffer, size, 0,
	           (struct sockaddr*)&sSYNetPeerPeerAddress, sizeof(sSYNetPeerPeerAddress)) == (ssize_t)size)
	{
		sSYNetPeerPacketsSent++;
		sSYNetPeerSendSeq++;
	}
#endif
}

static void syNetPeerStagePacketBundle(s32 target_player, const SYNetPeerPacketFrame *frames, s32 frame_count, u32 current_tick)
{
	s32 i;

	for (i = 0; i < frame_count; i++)
	{
#ifdef PORT
		syNetRollbackDebugOnIncomingRemoteFrame((u32 *)&frames[i].tick, (u16 *)&frames[i].buttons,
		                                       (s8 *)&frames[i].stick_x, (s8 *)&frames[i].stick_y);
#endif
		{
			sb32 is_new_remote_tick = (frames[i].tick > sSYNetPeerHighestRemoteTick) ? TRUE : FALSE;

			if ((is_new_remote_tick != FALSE) && (frames[i].tick < current_tick))
			{
				sSYNetPeerLateFrames++;
			}
			if (is_new_remote_tick != FALSE)
			{
				sSYNetPeerHighestRemoteTick = frames[i].tick;
			}
		}
		syNetInputSetRemoteInput(target_player, frames[i].tick, frames[i].buttons, frames[i].stick_x, frames[i].stick_y);
		sSYNetPeerFramesStaged++;
		sSYNetPeerInputChecksum = syNetPeerChecksumAccumulateU32(sSYNetPeerInputChecksum, (u32)target_player);
		sSYNetPeerInputChecksum = syNetPeerChecksumAccumulateFrame(sSYNetPeerInputChecksum, &frames[i]);
	}
}

void syNetPeerHandlePacket(const u8 *buffer, s32 size)
{
	const u8 *cursor = buffer;
	SYNetPeerPacketFrame frames[SYNETPEER_MAX_PACKET_FRAMES];
	SYNetPeerPacketFrame sec_frames[SYNETPEER_MAX_PACKET_FRAMES];
	u32 magic;
	u32 session_id;
	u32 ack_tick;
	u32 packet_seq;
	u32 checksum;
	u32 expected_checksum;
	u16 wire_version;
	u8 player;
	u8 frame_count;
	u8 packet_local_player;
	u8 packet_remote_player;
	u8 secondary_slot;
	u8 sec_frame_count;
	u32 current_tick = syNetInputGetTick();
	s32 i;
	sb32 is_dual;

#if defined(PORT) && !defined(_WIN32)
	if (size == SYNETPEER_TIME_PONG_BYTES)
	{
		syNetPeerHandleTimePongPacket(buffer, size);
		return;
	}
	if (size == SYNETPEER_BATTLE_START_TIME_BYTES)
	{
		syNetPeerHandleBattleStartTimePacket(buffer, size);
		return;
	}
	if (size == SYNETPEER_TIME_PING_BYTES)
	{
		syNetPeerHandleTimePingPacket(buffer, size);
		return;
	}
	if (size == SYNETPEER_CONTROL_PACKET_BYTES)
	{
		syNetPeerHandleControlPacket(buffer, size);
		return;
	}
#endif
	is_dual = FALSE;
	sec_frame_count = 0;
	secondary_slot = SYNETPEER_SECONDARY_SLOT_ABSENT;
	if ((size != SYNETPEER_PACKET_BYTES_V2) && (size != SYNETPEER_PACKET_BYTES_V3))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	memset(frames, 0, sizeof(frames));
	memset(sec_frames, 0, sizeof(sec_frames));

	magic = syNetPeerReadU32(&cursor);
	wire_version = syNetPeerReadU16(&cursor);
	(void)syNetPeerReadU16(&cursor);
	if ((size == SYNETPEER_PACKET_BYTES_V2 && wire_version != SYNETPEER_VERSION) ||
	    (size == SYNETPEER_PACKET_BYTES_V3 && wire_version != SYNETPEER_VERSION_DUAL_LOCAL))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	is_dual = (wire_version >= SYNETPEER_VERSION_DUAL_LOCAL) ? TRUE : FALSE;
	session_id = syNetPeerReadU32(&cursor);
	ack_tick = syNetPeerReadU32(&cursor);
	packet_seq = syNetPeerReadU32(&cursor);
	player = syNetPeerReadU8(&cursor);
	frame_count = syNetPeerReadU8(&cursor);
	packet_local_player = syNetPeerReadU8(&cursor);
	packet_remote_player = syNetPeerReadU8(&cursor);

	if ((magic != SYNETPEER_MAGIC) || (session_id != sSYNetPeerSessionID) || (player != packet_local_player) ||
		(packet_remote_player != (u8)sSYNetPeerLocalPlayer) || (frame_count > SYNETPEER_MAX_PACKET_FRAMES) ||
		(syNetPeerIsAllowedPeerSenderSlot(player) == FALSE))
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
	{
		frames[i].tick = syNetPeerReadU32(&cursor);
		frames[i].buttons = syNetPeerReadU16(&cursor);
		frames[i].stick_x = (s8)syNetPeerReadU8(&cursor);
		frames[i].stick_y = (s8)syNetPeerReadU8(&cursor);
	}
	if (is_dual != FALSE)
	{
		secondary_slot = syNetPeerReadU8(&cursor);
		sec_frame_count = syNetPeerReadU8(&cursor);
		if ((secondary_slot == SYNETPEER_SECONDARY_SLOT_ABSENT) || (sec_frame_count > SYNETPEER_MAX_PACKET_FRAMES) ||
			(syNetPeerIsAllowedPeerSenderSlot(secondary_slot) == FALSE))
		{
			sSYNetPeerPacketsDropped++;
			return;
		}
		for (i = 0; i < SYNETPEER_MAX_PACKET_FRAMES; i++)
		{
			sec_frames[i].tick = syNetPeerReadU32(&cursor);
			sec_frames[i].buttons = syNetPeerReadU16(&cursor);
			sec_frames[i].stick_x = (s8)syNetPeerReadU8(&cursor);
			sec_frames[i].stick_y = (s8)syNetPeerReadU8(&cursor);
		}
	}
	checksum = syNetPeerReadU32(&cursor);
	expected_checksum = syNetPeerChecksumInputPacket(session_id, ack_tick, packet_seq, wire_version, player, frame_count,
	                                                 frames, secondary_slot, sec_frame_count, sec_frames);

	if (checksum != expected_checksum)
	{
		sSYNetPeerPacketsDropped++;
		return;
	}
	sSYNetPeerPacketsReceived++;

	if (sSYNetPeerRecvSeqInitialized == FALSE)
	{
		sSYNetPeerRecvSeqHighWater = packet_seq;
		sSYNetPeerRecvSeqInitialized = TRUE;
	}
	else
	{
		if (packet_seq > sSYNetPeerRecvSeqHighWater)
		{
			sSYNetPeerSeqGaps += packet_seq - sSYNetPeerRecvSeqHighWater - 1U;
			sSYNetPeerRecvSeqHighWater = packet_seq;
		}
		else if (packet_seq == sSYNetPeerRecvSeqHighWater)
		{
			sSYNetPeerSeqDuplicates++;
		}
		else
		{
			sSYNetPeerSeqOutOfOrder++;
		}
	}
	sSYNetPeerLastPeerAckTick = ack_tick;

	if (frame_count > 0)
	{
		u32 oldest_tick_bundle = frames[0].tick;
		u32 newest_tick_bundle = frames[0].tick;

		for (i = 1; i < frame_count; i++)
		{
			if (frames[i].tick < oldest_tick_bundle)
			{
				oldest_tick_bundle = frames[i].tick;
			}
			if (frames[i].tick > newest_tick_bundle)
			{
				newest_tick_bundle = frames[i].tick;
			}
		}
		if ((is_dual != FALSE) && (sec_frame_count > 0))
		{
			s32 j;

			for (j = 0; j < sec_frame_count; j++)
			{
				if (sec_frames[j].tick < oldest_tick_bundle)
				{
					oldest_tick_bundle = sec_frames[j].tick;
				}
				if (sec_frames[j].tick > newest_tick_bundle)
				{
					newest_tick_bundle = sec_frames[j].tick;
				}
			}
		}
		sSYNetPeerLastPacketOldestTick = oldest_tick_bundle;
		sSYNetPeerLastPacketNewestTick = newest_tick_bundle;
		sSYNetPeerLastPacketTicksValid = TRUE;
	}
	else sSYNetPeerLastPacketTicksValid = FALSE;

	syNetPeerStagePacketBundle((s32)player, frames, frame_count, current_tick);
	if ((is_dual != FALSE) && (sec_frame_count > 0))
	{
		syNetPeerStagePacketBundle((s32)secondary_slot, sec_frames, sec_frame_count, current_tick);
	}
}

void syNetPeerReceiveRemoteInput(void)
{
#if defined(PORT) && !defined(_WIN32)
	u8 buffer[SYNETPEER_PACKET_RECV_MAX];

	while (TRUE)
	{
		ssize_t size = recvfrom(sSYNetPeerSocket, buffer, sizeof(buffer), 0, NULL, NULL);

		if (size < 0)
		{
			if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			{
				sSYNetPeerPacketsDropped++;
			}
			break;
		}
		syNetPeerHandlePacket(buffer, (s32)size);
	}
#endif
}

#ifdef PORT
void syNetPeerLogNetSyncValidation(u32 tick)
{
	u32 checksums[MAXCONTROLLERS];
	u32 inp_all = 0;
	u32 fighter_hash;
	u32 map_hash;
	u32 win_begin = 0;
	u32 win_length = tick;

	if ((sSYNetPeerIsActive == FALSE) || (syNetPeerCheckBattleExecutionReady() == FALSE))
	{
		return;
	}
	if (tick >= SYNETPEER_VALIDATION_INPUT_WINDOW)
	{
		win_begin = tick - SYNETPEER_VALIDATION_INPUT_WINDOW;
		win_length = SYNETPEER_VALIDATION_INPUT_WINDOW;
	}
	syNetInputGetHistoryInputValueChecksumWindow(win_begin, win_length, checksums, &inp_all);

	fighter_hash = syNetSyncHashBattleFighters();
	map_hash = syNetSyncHashMapCollisionKinematics();

	port_log(
		"SSB64 NetSync: role=%s lp=%d rp=%d tick=%u hist_win=[%u,%u) all=0x%08X p0=0x%08X p1=0x%08X p2=0x%08X p3=0x%08X figh=0x%08X mph=0x%08X snd_next=%u rcv_hw=%u gap=%u dup=%u ooo=%u puck=%u pko=%u pkn=%u sent=%u recv=%u dropped=%u stg=%u hr=%u late=%u inpchk=0x%08X pkt_valid=%d rb=%u lf=%u delay=%u ring=%u rscan=%u\n",
		(sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		sSYNetPeerLocalPlayer,
		sSYNetPeerRemotePlayer,
		tick,
		win_begin,
		win_begin + win_length,
		inp_all,
		checksums[0],
		checksums[1],
		checksums[2],
		checksums[3],
		fighter_hash,
		map_hash,
		sSYNetPeerSendSeq,
		sSYNetPeerRecvSeqHighWater,
		sSYNetPeerSeqGaps,
		sSYNetPeerSeqDuplicates,
		sSYNetPeerSeqOutOfOrder,
		sSYNetPeerLastPeerAckTick,
		(sSYNetPeerLastPacketTicksValid != FALSE) ? sSYNetPeerLastPacketOldestTick : (~(u32)0),
		(sSYNetPeerLastPacketTicksValid != FALSE) ? sSYNetPeerLastPacketNewestTick : (~(u32)0),
		sSYNetPeerPacketsSent,
		sSYNetPeerPacketsReceived,
		sSYNetPeerPacketsDropped,
		sSYNetPeerFramesStaged,
		sSYNetPeerHighestRemoteTick,
		sSYNetPeerLateFrames,
		sSYNetPeerInputChecksum,
		sSYNetPeerLastPacketTicksValid,
		syNetRollbackGetAppliedResimCount(),
		syNetRollbackGetLoadFailCount(),
		sSYNetPeerInputDelay,
		(u32)SYNETINPUT_HISTORY_LENGTH,
		(u32)SYNETROLLBACK_SCAN_WINDOW);
}
#endif

void syNetPeerLogStats(void)
{
#ifdef PORT
	u32 tick = syNetInputGetTick();

	if ((tick == 0) || ((tick - sSYNetPeerLastLogTick) < SYNETPEER_LOG_INTERVAL))
	{
		return;
	}
	sSYNetPeerLastLogTick = tick;

	syNetPeerMaybeAdaptInputDelay();

	port_log("SSB64 NetPeer: role=%s local=%d remote=%d barrier=%d execution_ready=%d tick=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u snd_next=%u rcv_hw=%u seq_gap=%u seq_dup=%u seq_ooo=%u peer_ack=%u inpchk=0x%08X delay=%u\n",
	         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
	         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer,
	         sSYNetPeerBattleBarrierReleased, syNetPeerCheckBattleExecutionReady(), tick,
	         sSYNetPeerPacketsSent, sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
	         sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames,
	         sSYNetPeerSendSeq, sSYNetPeerRecvSeqHighWater, sSYNetPeerSeqGaps,
	         sSYNetPeerSeqDuplicates, sSYNetPeerSeqOutOfOrder,
	         sSYNetPeerLastPeerAckTick,
	         sSYNetPeerInputChecksum,
	         sSYNetPeerInputDelay);

	syNetPeerLogNetSyncValidation(tick);
#endif
}

void syNetPeerLogExecutionHold(void)
{
#ifdef PORT
	if ((sSYNetPeerExecutionHoldFrames == 1) ||
		((sSYNetPeerExecutionHoldFrames % SYNETPEER_BARRIER_LOG_INTERVAL) == 0))
	{
		port_log("SSB64 NetPeer: execution hold role=%s local=%d remote=%d tick=%u hold=%u barrier_wait=%u peer_ready=%d start_sent=%d start_recv=%d highest_remote=%u late=%u\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerExecutionHoldFrames, sSYNetPeerBattleBarrierWaitFrames,
		         sSYNetPeerBattlePeerReady, sSYNetPeerBattleStartSent,
		         sSYNetPeerBattleStartReceived, sSYNetPeerHighestRemoteTick,
		         sSYNetPeerLateFrames);
	}
#endif
}

void syNetPeerLogExecutionBegin(void)
{
#ifdef PORT
	if (sSYNetPeerExecutionBeginLogged == FALSE)
	{
		sSYNetPeerExecutionBeginLogged = TRUE;
		port_log("SSB64 NetPeer: execution begin role=%s local=%d remote=%d tick=%u hold=%u barrier_wait=%u highest_remote=%u late=%u\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerExecutionHoldFrames, sSYNetPeerBattleBarrierWaitFrames,
		         sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames);
	}
#endif
}

void syNetPeerLogBarrierWait(void)
{
#ifdef PORT
	if ((sSYNetPeerBattleBarrierWaitFrames % SYNETPEER_BARRIER_LOG_INTERVAL) == 0)
	{
		port_log("SSB64 NetPeer: barrier wait role=%s local=%d remote=%d tick=%u local_ready=%d peer_ready=%d start_sent=%d start_recv=%d sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client",
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerBattleLocalReady, sSYNetPeerBattlePeerReady,
		         sSYNetPeerBattleStartSent, sSYNetPeerBattleStartReceived, sSYNetPeerPacketsSent,
		         sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		         sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames);
	}
#endif
}

void syNetPeerReleaseBattleBarrier(const char *reason)
{
	if (sSYNetPeerBattleBarrierReleased == FALSE)
	{
		sSYNetPeerBattleBarrierReleased = TRUE;

#ifdef PORT
#if !defined(_WIN32)
		{
			struct timespec ts;
			u64 ums;

			ums = 0;
			if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
			{
				ums = (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000L);
			}
			port_log(
			    "SSB64 NetPeer: barrier release role=%s reason=%s local=%d remote=%d tick=%u wait=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u unix_ms=%llu\n",
			    (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", reason,
			    sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
			    sSYNetPeerBattleBarrierWaitFrames, sSYNetPeerPacketsSent,
			    sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
			    sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames,
			    (unsigned long long)ums);
		}
#else
		port_log("SSB64 NetPeer: barrier release role=%s reason=%s local=%d remote=%d tick=%u wait=%u sent=%u recv=%u dropped=%u staged=%u highest_remote=%u late=%u\n",
		         (sSYNetPeerBootstrapIsHost != FALSE) ? "host" : "client", reason,
		         sSYNetPeerLocalPlayer, sSYNetPeerRemotePlayer, syNetInputGetTick(),
		         sSYNetPeerBattleBarrierWaitFrames, sSYNetPeerPacketsSent,
		         sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		         sSYNetPeerFramesStaged, sSYNetPeerHighestRemoteTick, sSYNetPeerLateFrames);
#endif
#endif
	}
}

void syNetPeerUpdateStartBarrier(void)
{
#if defined(PORT) && !defined(_WIN32)
	if ((sSYNetPeerBattleBarrierEnabled == FALSE) || (sSYNetPeerBattleBarrierReleased != FALSE))
	{
		return;
	}
	sSYNetPeerBattleBarrierWaitFrames++;
	syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_READY);

	if (sSYNetPeerBattlePeerReady == FALSE)
	{
		syNetPeerLogBarrierWait();
		return;
	}

	if ((sSYNetPeerClockAlignEnabled != FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE))
	{
		if (sSYNetPeerBootstrapIsHost != FALSE)
		{
			if (sSYNetPeerClockSyncSampleCount < SYNETPEER_CLOCK_SYNC_SAMPLES)
			{
				if (sSYNetPeerTimePingAwaitingAck == FALSE)
				{
					sSYNetPeerTimePingT0Sent = syNetPeerNowUnixMs();
					sSYNetPeerTimePingSeq = sSYNetPeerClockSyncSampleCount;
					syNetPeerSendTimePingPacket(sSYNetPeerTimePingSeq, sSYNetPeerTimePingT0Sent);
					sSYNetPeerTimePingAwaitingAck = TRUE;
				}
				else
				{
					syNetPeerSendTimePingPacket(sSYNetPeerTimePingSeq, sSYNetPeerTimePingT0Sent);
				}
			}
			else if (sSYNetPeerBattleStartTimeSent != FALSE)
			{
				if (syNetPeerCheckBarrierDeadlineReached() != FALSE)
				{
					sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
					syNetPeerReleaseBattleBarrier("clock-deadline-host");
				}
			}
		}
		else if (sSYNetPeerBattleStartTimeReceived != FALSE)
		{
			if (syNetPeerCheckBarrierDeadlineReached() != FALSE)
			{
				sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
				syNetPeerReleaseBattleBarrier("clock-deadline-client");
			}
		}
		syNetPeerLogBarrierWait();
		return;
	}

	if (sSYNetPeerBattlePeerReady != FALSE)
	{
		if (sSYNetPeerBootstrapIsHost != FALSE)
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_START);
			sSYNetPeerBattleStartSent = TRUE;

			if (sSYNetPeerBattleStartReceived != FALSE)
			{
				sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
				syNetPeerReleaseBattleBarrier("client-ack");
			}
		}
		else if (sSYNetPeerBattleStartReceived != FALSE)
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_START);
			sSYNetPeerBattleStartSent = TRUE;
			sSYNetPeerBattleStartRepeatFrames = SYNETPEER_BATTLE_START_REPEAT_FRAMES;
			syNetPeerReleaseBattleBarrier("host-start");
		}
	}
	syNetPeerLogBarrierWait();
#endif
}

void syNetPeerUpdateBattleGate(void)
{
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	syNetPeerReceiveRemoteInput();
	syNetPeerUpdateStartBarrier();

	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		sSYNetPeerExecutionHoldFrames++;
		syNetPeerLogExecutionHold();
	}
	else syNetPeerLogExecutionBegin();
}

void syNetPeerUpdate(void)
{
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (sSYNetPeerIsActive == FALSE)
	{
		return;
	}
	syNetPeerUpdateBattleGate();

	if (syNetPeerCheckBattleExecutionReady() == FALSE)
	{
		return;
	}
	if (sSYNetPeerBattleStartRepeatFrames != 0)
	{
#if defined(PORT) && !defined(_WIN32)
		if ((sSYNetPeerClockAlignEnabled != FALSE) && (sSYNetPeerBattleBarrierEnabled != FALSE) &&
		    (sSYNetPeerBootstrapIsHost != FALSE))
		{
			syNetPeerSendBattleStartTimePacket(sSYNetPeerBattleStartUnixMs, sSYNetPeerBattleStartOffsetMs);
		}
		else if ((sSYNetPeerClockAlignEnabled == FALSE) || (sSYNetPeerBattleBarrierEnabled == FALSE))
		{
			syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_START);
		}
#else
		syNetPeerSendControlPacket(SYNETPEER_PACKET_BATTLE_START);
#endif
		sSYNetPeerBattleStartRepeatFrames--;
	}
	syNetPeerSendLocalInput();
	syNetPeerLogStats();
	syNetRollbackUpdate();
}

void syNetPeerStopVSSession(void)
{
#ifdef PORT
	syNetRollbackStopVSSession();
#endif
#if defined(PORT) && !defined(_WIN32)
	if (sSYNetPeerIsActive != FALSE)
	{
		port_log("SSB64 NetPeer: VS session stop sent=%u recv=%u dropped=%u staged=%u late=%u checksum=0x%08X\n",
		         sSYNetPeerPacketsSent, sSYNetPeerPacketsReceived, sSYNetPeerPacketsDropped,
		         sSYNetPeerFramesStaged, sSYNetPeerLateFrames, sSYNetPeerInputChecksum);
	}
	syNetPeerCloseSocket();
#endif
	sSYNetPeerIsActive = FALSE;
}

sb32 syNetPeerIsVSSessionActive(void)
{
	return sSYNetPeerIsActive;
}

s32 syNetPeerGetRemotePlayerSlot(void)
{
	return sSYNetPeerRemotePlayer;
}

u32 syNetPeerGetHighestRemoteTick(void)
{
	return sSYNetPeerHighestRemoteTick;
}

s32 syNetPeerGetRemoteHumanSlotCount(void)
{
	return sSYNetPeerRemoteReceiveCount;
}

sb32 syNetPeerGetRemoteHumanSlotByIndex(s32 index, s32 *out_slot)
{
	if ((index < 0) || (index >= sSYNetPeerRemoteReceiveCount) || (out_slot == NULL))
	{
		return FALSE;
	}
	*out_slot = (s32)sSYNetPeerRemoteReceiveSlots[index];
	return TRUE;
}
