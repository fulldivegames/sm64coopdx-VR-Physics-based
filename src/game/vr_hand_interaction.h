#ifndef VR_HAND_INTERACTION_H
#define VR_HAND_INTERACTION_H

#include <PR/ultratypes.h>

#include "types.h"

struct MarioState;
struct Object;

void vr_hand_interaction_update(struct MarioState* mario);
void vr_hand_interaction_audio_shutdown(void);
bool vr_hand_interaction_should_hide_body(struct MarioState* mario);
void vr_hand_interaction_update_roomscale_body(
    struct MarioState* mario
);
void vr_hand_interaction_apply_hand_collision_position(
    u32 hand,
    Vec3f position
);
void vr_hand_interaction_apply_big_hands_collision_position(
    u32 hand,
    Vec3f position
);
bool vr_hand_interaction_recover_from_geometry(
    struct MarioState* mario
);
void vr_hand_interaction_update_headset_collider(
    struct MarioState* mario
);

bool vr_hand_interaction_resolve_headset_camera_position(
    Vec3f position
);
bool vr_hand_interaction_validate_headset_damage_contact(
    struct MarioState* mario,
    struct Object* object
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
bool vr_hand_interaction_is_hammer_charge_object(
    struct Object* object
);
bool vr_hand_interaction_is_hammer_suit_shell_object(
    struct Object* object
);
u32 vr_hand_interaction_get_tracked_held_hand(
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
f32 vr_hand_interaction_get_held_object_center_offset(
    struct Object* object
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
enum VrBoxReward {
    VR_BOX_REWARD_ORIGINAL,
    VR_BOX_REWARD_FIRE_FLOWER,
    VR_BOX_REWARD_HAMMER_SUIT,
    VR_BOX_REWARD_SONIC_SHOES,
    VR_BOX_REWARD_BIG_HANDS,
};
enum VrBoxReward vr_special_moves_roll_box_reward(
    struct Object* box,
    struct MarioState* owner
);
bool vr_special_moves_spawn_cheat_fire_flower(void);
bool vr_special_moves_spawn_cheat_hammer_suit(void);
bool vr_special_moves_spawn_cheat_sonic_shoes(void);
bool vr_special_moves_spawn_cheat_big_hands(void);
enum VrCheatSpawnCap {
    VR_CHEAT_SPAWN_WING_CAP,
    VR_CHEAT_SPAWN_VANISH_CAP,
    VR_CHEAT_SPAWN_METAL_CAP,
};
bool vr_special_moves_spawn_cheat_cap(enum VrCheatSpawnCap cap);
bool vr_special_moves_fire_flower_active(void);
bool vr_special_moves_grant_fire_flower(void);
bool vr_special_moves_hammer_suit_active(void);
bool vr_special_moves_grant_hammer_suit(void);
bool vr_special_moves_sonic_shoes_active(void);
bool vr_special_moves_sonic_shoes_prevent_slope_slide(struct MarioState *m);
bool vr_special_moves_grant_sonic_shoes(void);
bool vr_special_moves_big_hands_active(void);
f32 vr_special_moves_big_hands_scale(void);
bool vr_special_moves_grant_big_hands(void);
f32 vr_special_moves_sonic_speed_scale(void);
Gfx* geo_vr_sonic_shoe(
    s32 callContext,
    struct GraphNode* node,
    void* context
);
Gfx* geo_switch_vr_sonic_foot(
    s32 callContext,
    struct GraphNode* node,
    void* context
);
Gfx* geo_vr_fireball_color(
    s32 callContext,
    struct GraphNode* node,
    void* context
);
Gfx* geo_vr_rasengan_color(
    s32 callContext,
    struct GraphNode* node,
    void* context
);
Gfx* geo_vr_rasengan_ring_color(
    s32 callContext,
    struct GraphNode* node,
    void* context
);
Gfx* geo_vr_rasen_shuriken_color(
    s32 callContext,
    struct GraphNode* node,
    void* context
);
Gfx* geo_vr_rasengan_visual_spin(
    s32 callContext,
    struct GraphNode* node,
    void* context
);

#endif // VR_HAND_INTERACTION_H
