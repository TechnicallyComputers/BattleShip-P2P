#ifndef PORT_MM_BOOTSTRAP_H
#define PORT_MM_BOOTSTRAP_H

#ifdef __cplusplus
extern "C" {
#endif

/** If SSB64_MM_AUTO=1 (and optional CURL), HTTP to match server then setenv SSB64_NETPLAY_*. */
void port_mm_bootstrap_try(void);

/** Optional: POST /v1/heartbeat while SSB64_MM_HEARTBEAT_TICKET and auth envs are set (see docs). */
void port_mm_game_heartbeat_tick(void);

#ifdef __cplusplus
}
#endif

#endif
