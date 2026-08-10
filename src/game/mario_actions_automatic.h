#ifndef MARIO_ACTIONS_AUTOMATIC_H
#define MARIO_ACTIONS_AUTOMATIC_H

#include <PR/ultratypes.h>

#include "types.h"

s32 mario_execute_automatic_action(struct MarioState *m);
void mario_pop_bubble(struct MarioState* m);
u8 vr_get_cannon_vision_fade_alpha(void);
bool vr_get_cannon_vision_guidance(
    s16* screenAngle,
    u8* alpha
);

#endif // MARIO_ACTIONS_AUTOMATIC_H
