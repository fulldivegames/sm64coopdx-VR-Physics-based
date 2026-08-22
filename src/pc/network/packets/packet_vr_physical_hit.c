#include "sm64.h"
#include "game/interaction.h"
#include "game/mario.h"
#include "pc/debuglog.h"
#include "pc/network/coopnet/coopnet.h"
#include "pc/network/network.h"
#include "pc/network/network_player.h"

void network_send_vr_player_hit(u8 victimGlobalIndex, s16 attackYaw, u8 attackType) {
    if (!ns_coopnet_vr_public_session() || gNetworkPlayerLocal == NULL) {
        return;
    }

    struct NetworkPlayer* victimNp =
        network_player_from_global_index(victimGlobalIndex);
    if (victimNp == NULL || victimNp == gNetworkPlayerLocal ||
        !victimNp->connected) {
        return;
    }

    struct Packet p = { 0 };
    packet_init(&p, PACKET_VR_PHYSICAL_HIT, true, PLMT_AREA);
    const u8 attackerGlobalIndex = gNetworkPlayerLocal->globalIndex;
    packet_write(&p, (void*)&attackerGlobalIndex, sizeof(attackerGlobalIndex));
    packet_write(&p, &victimGlobalIndex, sizeof(victimGlobalIndex));
    packet_write(&p, &attackYaw, sizeof(attackYaw));
    packet_write(&p, &attackType, sizeof(attackType));
    // CoopNet relays broadcast packets using each peer's own local/global
    // mapping. A directed packet can be misrouted when the server's local
    // slot differs from the destination's global index. Every peer receives
    // this tiny reliable event, and only the named victim applies it below.
    network_send(&p);
}

void network_receive_vr_physical_hit(struct Packet* p) {
    u8 attackerGlobalIndex = UNKNOWN_GLOBAL_INDEX;
    u8 victimGlobalIndex = UNKNOWN_GLOBAL_INDEX;
    s16 attackYaw = 0;
    u8 attackType = VR_PLAYER_ATTACK_PUNCH;
    packet_read(p, &attackerGlobalIndex, sizeof(attackerGlobalIndex));
    packet_read(p, &victimGlobalIndex, sizeof(victimGlobalIndex));
    packet_read(p, &attackYaw, sizeof(attackYaw));
    packet_read(p, &attackType, sizeof(attackType));
    if (p->error || !ns_coopnet_vr_public_session() ||
        gNetworkPlayerLocal == NULL ||
        victimGlobalIndex != gNetworkPlayerLocal->globalIndex ||
        packet_spoofed(p, attackerGlobalIndex)) {
        return;
    }

    struct NetworkPlayer* attackerNp =
        network_player_from_global_index(attackerGlobalIndex);
    if (attackerNp == NULL || !attackerNp->connected ||
        attackerNp->localIndex >= MAX_PLAYERS) {
        return;
    }

    interact_player_vr_attack(
        &gMarioStates[attackerNp->localIndex],
        &gMarioStates[0],
        attackYaw,
        attackType
    );
}
