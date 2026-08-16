#ifndef VR_HAND_INTERACTION_H
#define VR_HAND_INTERACTION_H

#include <PR/ultratypes.h>

#include "types.h"

struct MarioState;
struct Object;

void vr_hand_interaction_update(struct MarioState* mario);
void vr_hand_interaction_apply_hand_collision_position(
    u32 hand,
    Vec3f position
);
void vr_hand_interaction_update_headset_collider(
    struct MarioState* mario
);
bool vr_hand_interaction_get_climb_camera_offset(Vec3f offset);
bool vr_hand_interaction_is_physical_climb_active(
    struct MarioState* mario
);
bool vr_hand_interaction_is_physical_pole_climb_active(
    struct MarioState* mario
);
void vr_hand_interaction_apply_moving_pole_displacement(
    struct MarioState* mario
);
bool vr_hand_interaction_is_at_physical_pole_top(
    struct MarioState* mario
);
bool vr_hand_interaction_is_physical_surface_climb_active(
    struct MarioState* mario
);
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
bool vr_hand_interaction_get_late_held_object_position(
    struct Object* object,
    Vec3f position
);
bool vr_hand_interaction_get_bowser_controls(
    struct MarioState* mario,
    f32* turnInput,
    bool* gripReleased,
    bool* fullPowerImpulse,
    s16* releaseYaw,
    bool* releaseYawValid
);
bool vr_hand_interaction_bowser_spin_active(void);
bool vr_special_moves_spawn_fire_flower(
    struct Object* box,
    struct MarioState* owner
);
bool vr_special_moves_fire_flower_active(void);

#endif // VR_HAND_INTERACTION_H
