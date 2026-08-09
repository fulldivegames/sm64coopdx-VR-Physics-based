#ifndef VR_HAND_INTERACTION_H
#define VR_HAND_INTERACTION_H

#include <PR/ultratypes.h>

#include "types.h"

struct MarioState;
struct Object;

void vr_hand_interaction_update(struct MarioState* mario);
bool vr_hand_interaction_apply_player_anchor(
    struct MarioState* mario
);
bool vr_hand_interaction_is_player_anchor_object(
    struct Object* object
);
bool vr_hand_interaction_apply_held_object_transform(
    struct Object* object
);
bool vr_hand_interaction_is_tracked_held_object(
    struct Object* object
);
bool vr_hand_interaction_blocks_native_held_object_release(
    struct MarioState* mario
);
bool vr_hand_interaction_get_held_object_position(
    struct Object* object,
    Vec3f position
);
bool vr_hand_interaction_get_bowser_controls(
    struct MarioState* mario,
    f32* turnInput,
    bool* gripReleased,
    bool* fullPowerImpulse
);
bool vr_hand_interaction_bowser_spin_active(void);

#endif // VR_HAND_INTERACTION_H
