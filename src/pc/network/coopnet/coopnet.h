#ifndef COOPNET_H
#define COOPNET_H
#include <stdbool.h>

enum CoopNetLobbyChannel {
    COOPNET_LOBBY_STANDARD_PUBLIC,
    COOPNET_LOBBY_VR_PUBLIC,
    COOPNET_LOBBY_PRIVATE,
};

#define VR_COOPNET_PROTOCOL_MAGIC 0x56524350U
#define VR_COOPNET_PROTOCOL_VERSION 1U

#ifdef COOPNET

typedef void (*QueryCallbackPtr)(uint64_t aLobbyId, uint64_t aOwnerId, uint16_t aConnections, uint16_t aMaxConnections, const char* aGame, const char* aVersion, const char* aHostName, const char* aMode, const char* aDescription);
typedef void (*QueryFinishCallbackPtr)(void);

extern struct NetworkSystem gNetworkSystemCoopNet;
extern uint64_t gCoopNetDesiredLobby;
extern char gCoopNetPassword[];

bool ns_coopnet_query(QueryCallbackPtr callback, QueryFinishCallbackPtr finishCallback, const char* password, enum CoopNetLobbyChannel channel);
bool ns_coopnet_is_connected(void);
void ns_coopnet_update(void);
void ns_coopnet_set_lobby_channel(enum CoopNetLobbyChannel channel);
enum CoopNetLobbyChannel ns_coopnet_get_lobby_channel(void);
bool ns_coopnet_is_standard_public_session(void);
bool ns_coopnet_vr_gameplay_allowed(void);
bool ns_coopnet_vr_public_session(void);

#else

static inline bool ns_coopnet_is_standard_public_session(void) { return false; }
static inline bool ns_coopnet_vr_gameplay_allowed(void) { return true; }
static inline bool ns_coopnet_vr_public_session(void) { return false; }

#endif
#endif
