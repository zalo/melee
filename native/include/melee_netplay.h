/* Native-to-native online play (native/netplay.cpp). C API for the game code and the runtime. */
#ifndef MELEE_NETPLAY_H
#define MELEE_NETPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* A session is configured (MELEE_ONLINE_ROLE=host|join in the environment): inputs are exchanged in
 * lockstep, disc and ARAM I/O complete deterministically and the intro movie is skipped. */
int MeleeNativeNetplayActive(void);
/* GameCube port this side plays on: 0 = host, 1 = guest. */
int MeleeNativeNetplayLocalPort(void);
/* Runtime setup from main(): remembers the user directory and the binary digest. */
void MeleeNativeNetplayInit(const char* user_path);
/* Simulation digest for a logic frame (matrix_runtime.c). */
void MeleeNativeNetplayFrameHash(unsigned frame, unsigned hash);
/* One-line status for on-screen display; NULL when there is nothing to show. */
const char* MeleeNativeNetplayOverlayText(void);
int MeleeNativeNetplayOverlayIsWarning(void);
/* One "[netplay-stats] ..." line for the periodic performance block; NULL when not online. */
const char* MeleeNativeNetplayStatsLine(void);

/* In-game lobby (native/netplay.cpp): Z on the character select screen opens an overlay that hosts
 * or joins a LAN game without leaving the screen; after the relaunch an autopilot brings both games
 * back to the character select screen, which is the online lobby from then on. */
void MeleeNativeNetplayScene(int scene);     /* scene changes (keyboard_input.cpp) */
void MeleeNativeNetplayLobbyInput(void* pads); /* from MeleeNativePADRead, before the session poll */
void MeleeNativeNetplayDrawLobby(void);       /* ImGui, from the VI loop */

/* Online Play screen (mnportsettings.c). Discovery runs on a background thread while the
 * screen is open; a completed handshake relaunches the game into the session. */
void MeleeNativeNetplayMenuEnter(void); /* start listening for hosts on the LAN */
void MeleeNativeNetplayMenuLeave(void);
void MeleeNativeNetplayHost(void);      /* announce this device and wait for a joiner */
int MeleeNativeNetplayHosting(void);
int MeleeNativeNetplayHostCount(void);
const char* MeleeNativeNetplayHostLabel(int index); /* "name 10.0.0.5" */
void MeleeNativeNetplayJoin(int index);
const char* MeleeNativeNetplayMenuStatus(void);    /* "Waiting for a player...", errors */
/* Call every frame while the screen is open; relaunches (never returns) once both sides agreed. */
void MeleeNativeNetplayMenuTick(void);
const char* MeleeNativeNetplayLocalAddress(void);  /* this device's LAN address or "" */

#ifdef __cplusplus
}
#endif
#endif
