#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio/external.h"
#include "behavior_data.h"
#include "characters.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "hardcoded.h"
#include "interaction.h"
#include "mario.h"
#include "object_constants.h"
#include "object_fields.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "sm64.h"
#include "surface_terrains.h"
#include "vr_hand_interaction.h"

#include "pc/configfile.h"
#include "pc/lua/smlua_hooks.h"
#include "pc/network/packets/packet.h"
#include "pc/vr/vr.h"

#ifdef DEBUG
#define VR_INTERACTION_DEBUG(...) printf(__VA_ARGS__)
#else
#define VR_INTERACTION_DEBUG(...) ((void)0)
#endif

#define VR_FIST_ACTIVE_FRAMES 33
#define VR_FIST_SWEEP_SAMPLES 4
#define VR_FIST_BASE_RADIUS 12.0f
#define VR_HAND_COLLISION_RADIUS_MAX 24.0f
#define VR_HAND_COLLISION_RELEASE_MARGIN 3.0f
#define VR_HAND_COLLISION_MAX_SWEEP 300.0f
#define VR_FIST_MAX_SWEEP_DISTANCE 150.0f
#define VR_PUNCH_SOUND_COMBO_RESET_FRAMES 18
#define VR_MOTION_DIVE_PAIR_WINDOW_FRAMES 5
#define VR_GRIP_CLOSE_THRESHOLD 0.55f
#define VR_GRIP_OPEN_THRESHOLD 0.35f
#define VR_GRAB_EXTRA_REACH 16.0f
#define VR_CLIMB_CEILING_EXTRA_REACH 20.0f
#define VR_CLIMB_NATIVE_SWEEP_SAMPLES 4
#define VR_CLIMB_SURFACE_EXTRA_REACH 20.0f
#define VR_CLIMB_SURFACE_HAND_RADIUS_MAX 24.0f
#define VR_CLIMB_SURFACE_CLEARANCE 12.0f
#define VR_CLIMB_SURFACE_EDGE_MARGIN 48.0f
#define VR_CLIMB_SAFE_RELEASE_SAMPLE_FRAMES 3U
#define VR_CLIMB_LEDGE_MIN_RISE 20.0f
#define VR_CLIMB_LEDGE_MAX_HEAD_ABOVE_FLOOR 190.0f
#define VR_CLIMB_LEDGE_MAX_FLOOR_ABOVE_HEAD 24.0f
#define VR_CLIMB_WALL_MAX_NORMAL_Y 0.35f
#define VR_CLIMB_CEILING_MAX_NORMAL_Y -0.10f
#define VR_CLIMB_CEILING_MIN_FEET_SEPARATION 24.0f
#define VR_CLIMB_CEILING_HAND_PLANE_TOLERANCE 8.0f
#define VR_CLIMB_SWING_RELEASE_MIN_SPEED 110.0f
#define VR_CLIMB_REGRAB_COOLDOWN_FRAMES 8
#define VR_PHYSICAL_POLE_SLIDE_MAX_SPEED 6.0f
#define VR_HEADSET_INTERACTION_RADIUS 24.0f
#define VR_HEADSET_INTERACTION_HEIGHT 48.0f
#define VR_THROW_MIN_SPEED 60.0f
#define VR_THROW_VELOCITY_SCALE 0.125f
#define VR_THROW_VELOCITY_MEMORY 0.78f
#define VR_BOWSER_TURN_DEADZONE 0.18f
#define VR_BOWSER_HAND_TURN_FULL_INPUT 0x600
#define VR_BOWSER_HAND_TURN_JITTER 0x60
#define VR_BOWSER_FULL_POWER_ARC 0x1000
#define VR_BOWSER_MIN_HAND_RADIUS_METERS 0.10f
#define VR_FIRE_FLOWER_PICKUP_COUNT 16
#define VR_FIRE_FLOWER_PICKUP_RADIUS 115.0f
#define VR_FIREBALL_CHARGE_FRAMES 75U
#define VR_FIREBALL_TRIGGER_THRESHOLD 0.55f
#define VR_FIREBALL_MIN_THROW_SPEED 60.0f
#define VR_FIREBALL_MAX_LIFETIME 300U

#define VR_ANCHORABLE_INTERACT_TYPES ( \
    INTERACT_BOUNCE_TOP | \
    INTERACT_BOUNCE_TOP2 | \
    INTERACT_BULLY | \
    INTERACT_HIT_FROM_BELOW | \
    INTERACT_HOOT | \
    INTERACT_KOOPA | \
    INTERACT_KOOPA_SHELL | \
    INTERACT_PLAYER | \
    INTERACT_SPINY_WALKING | \
    INTERACT_TEXT \
)

#define VR_FIST_ATTACKABLE_TYPES ( \
    INTERACT_BULLY | \
    INTERACT_BREAKABLE | \
    INTERACT_BOUNCE_TOP | \
    INTERACT_BOUNCE_TOP2 | \
    INTERACT_HIT_FROM_BELOW | \
    INTERACT_KOOPA | \
    INTERACT_SPINY_WALKING \
)

static u8 sVrFistActiveFrames[VR_CONTROLLER_COUNT] = { 0 };
static bool sVrFistPreviousPositionValid[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static Vec3f sVrFistPreviousPosition[VR_CONTROLLER_COUNT] = {
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f }
};
static u8 sVrPunchSoundComboStep = 0;
static u8 sVrPunchSoundComboResetFrames = 0;
static u8 sVrMotionDivePairFrames[VR_CONTROLLER_COUNT] = {
    0,
    0
};
static bool sVrGripPressed[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static struct Object* sVrTrackedHeldObject = NULL;
static u32 sVrTrackedHeldHand = VR_CONTROLLER_COUNT;
static u8 sVrTrackedHeldGripMask = 0;
static Vec3f sVrTrackedHeldPreviousPosition = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrTrackedHeldPosition = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrTrackedHeldVelocity = { 0.0f, 0.0f, 0.0f };
static bool sVrTrackedHeldPositionValid = false;
static u32 sVrTrackedHeldPositionTimestamp = 0;
static bool sVrTrackedReleaseInProgress = false;
static struct Object* sVrTrackedHootObject = NULL;
static u32 sVrTrackedHootHand = VR_CONTROLLER_COUNT;
static struct Object* sVrTrackedAnchorObject = NULL;
static u32 sVrTrackedAnchorHand = VR_CONTROLLER_COUNT;
static Vec3f sVrTrackedAnchorOffset = { 0.0f, 0.0f, 0.0f };

enum VrPhysicalClimbType {
    VR_PHYSICAL_CLIMB_NONE,
    VR_PHYSICAL_CLIMB_POLE,
    VR_PHYSICAL_CLIMB_CEILING,
    VR_PHYSICAL_CLIMB_CHEAT_CEILING,
    VR_PHYSICAL_CLIMB_CHEAT_WALL,
};

static enum VrPhysicalClimbType sVrPhysicalClimbType =
    VR_PHYSICAL_CLIMB_NONE;
static u32 sVrPhysicalClimbHand = VR_CONTROLLER_COUNT;
static struct Object* sVrPhysicalClimbPole = NULL;
static Vec3f sVrPhysicalClimbPolePrevPosition = { 0.0f, 0.0f, 0.0f };
static bool sVrPhysicalClimbPolePrevPositionValid = false;
static struct Surface* sVrPhysicalClimbSurface = NULL;
static Vec3f sVrPhysicalClimbSurfacePoint = {
    0.0f,
    0.0f,
    0.0f
};
static Vec3f sVrPhysicalClimbSurfaceNormal = {
    0.0f,
    0.0f,
    0.0f
};
static bool sVrPhysicalClimbHands[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static bool sVrPhysicalClimbLastPositionValid[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static Vec3f sVrPhysicalClimbLastPosition[VR_CONTROLLER_COUNT] = {
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f }
};
static Vec3f sVrPhysicalClimbContactPosition = {
    0.0f,
    0.0f,
    0.0f
};
static Vec3f sVrPhysicalClimbCameraOffset = {
    0.0f,
    0.0f,
    0.0f
};
static Vec3f sVrPhysicalClimbCameraOffsetPrev = {
    0.0f,
    0.0f,
    0.0f
};
static bool sVrPhysicalClimbSafeReleasePositionValid = false;
static Vec3f sVrPhysicalClimbSafeReleasePosition = {
    0.0f,
    0.0f,
    0.0f
};
static u32 sVrPhysicalClimbSafeReleaseTimestamp = 0;
static u32 sVrPhysicalClimbOffsetTimestamp = 0;
static u8 sVrPhysicalClimbRegrabFrames = 0;
static bool sVrClimbPreviousPositionValid[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static Vec3f sVrClimbPreviousPosition[VR_CONTROLLER_COUNT] = {
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f }
};
static u8 sVrBowserGripMask = 0;
static bool sVrBowserHandYawValid[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static s16 sVrBowserPreviousHandYaw[VR_CONTROLLER_COUNT] = { 0, 0 };
static s32 sVrBowserAccumulatedHandYaw[VR_CONTROLLER_COUNT] = { 0, 0 };
static f32 sVrBowserPhysicalTurnInput = 0.0f;
static bool sVrBowserFullPowerImpulse = false;
static bool sVrBowserReleaseYawValid = false;
static s16 sVrBowserReleaseYaw = 0;
static u32 sVrBowserMotionTimestamp = 0;
static Vec3f sVrBowserFrameVelocity = { 0.0f, 0.0f, 0.0f };
static u8 sVrBowserFrameVelocitySamples = 0;
static bool sVrInteractionTrackingActive = false;
static bool sVrHeadsetColliderActive = false;
static bool sVrFireFlowerPowered = false;
static bool sVrFireballTriggerPressed = false;
static u16 sVrFireballChargeFrames = 0;
static s16 sVrFireFlowerLevel = -1;
static s16 sVrFireFlowerArea = -1;
static struct Object* sVrFireFlowerPickups[VR_FIRE_FLOWER_PICKUP_COUNT] = { NULL };
static struct Object* sVrFireballObject = NULL;
static bool sVrFireballProjectile = false;
static u16 sVrFireballLifetime = 0;
static Vec3f sVrFireballVelocity = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrFireballRememberedVelocity = { 0.0f, 0.0f, 0.0f };
static f32 sVrHeadsetColliderSavedRadius = 50.0f;
static f32 sVrHeadsetColliderSavedHeight = 160.0f;
static f32 sVrHeadsetColliderSavedDownOffset = 0.0f;

struct VrHandCollisionState {
    bool rawPositionValid;
    bool constraintActive;
    Vec3f previousRawPosition;
    Vec3f constraintNormal;
    f32 constraintOriginOffset;
};
static struct VrHandCollisionState
    sVrHandCollision[VR_CONTROLLER_COUNT] = { 0 };

extern u8 gRenderingInterpolated;
extern f32 gRenderingDelta;

struct VrFistSweep {
    Vec3f start;
    Vec3f step;
    f32 minimum[3];
    f32 maximum[3];
};

static f32 vr_hand_interaction_fist_radius(void);

static f32 vr_hand_interaction_surface_distance(
    const struct Surface* surface,
    const Vec3f position
) {
    return surface->normal.x * position[0] +
        surface->normal.y * position[1] +
        surface->normal.z * position[2] +
        surface->originOffset;
}

static bool vr_hand_interaction_surface_is_climbable_exception(
    const struct Surface* surface
) {
    if (surface == NULL || configVrExperimentalClimbableColliders) {
        return false;
    }
    if (surface->type == SURFACE_HANGABLE) {
        return true;
    }
    return surface->object != NULL &&
        (surface->object->oInteractType & INTERACT_POLE) != 0;
}

void vr_hand_interaction_apply_hand_collision_position(
    u32 hand,
    Vec3f position
) {
    if (hand >= VR_CONTROLLER_COUNT || position == NULL ||
        !sVrHandCollision[hand].constraintActive) {
        return;
    }
    const f32 radius = fminf(
        vr_hand_interaction_fist_radius(),
        VR_HAND_COLLISION_RADIUS_MAX
    );
    const f32 distance =
        sVrHandCollision[hand].constraintNormal[0] * position[0] +
        sVrHandCollision[hand].constraintNormal[1] * position[1] +
        sVrHandCollision[hand].constraintNormal[2] * position[2] +
        sVrHandCollision[hand].constraintOriginOffset;
    if (distance >= radius) {
        return;
    }
    const f32 correction = radius - distance;
    for (u32 axis = 0; axis < 3; axis++) {
        position[axis] +=
            sVrHandCollision[hand].constraintNormal[axis] * correction;
    }
}

static void vr_hand_interaction_set_hand_constraint(
    u32 hand,
    const struct Surface* surface
) {
    sVrHandCollision[hand].constraintActive = true;
    sVrHandCollision[hand].constraintNormal[0] = surface->normal.x;
    sVrHandCollision[hand].constraintNormal[1] = surface->normal.y;
    sVrHandCollision[hand].constraintNormal[2] = surface->normal.z;
    sVrHandCollision[hand].constraintOriginOffset = surface->originOffset;
}

static bool vr_hand_interaction_resolve_hand_collision(
    struct MarioState* mario,
    u32 hand,
    Vec3f position
) {
    struct VrHandCollisionState* state = &sVrHandCollision[hand];
    const f32 radius = fminf(
        vr_hand_interaction_fist_radius(),
        VR_HAND_COLLISION_RADIUS_MAX
    );
    Vec3f rawPosition;
    vec3f_copy(rawPosition, position);
    bool collided = false;
    struct Surface* collisionSurface = NULL;

    if (state->constraintActive) {
        const f32 distance =
            state->constraintNormal[0] * rawPosition[0] +
            state->constraintNormal[1] * rawPosition[1] +
            state->constraintNormal[2] * rawPosition[2] +
            state->constraintOriginOffset;
        if (distance < radius + VR_HAND_COLLISION_RELEASE_MARGIN) {
            vr_hand_interaction_apply_hand_collision_position(hand, position);
            collided = distance < radius;
        } else {
            state->constraintActive = false;
        }
    }

    if (state->rawPositionValid) {
        Vec3f sweep = {
            rawPosition[0] - state->previousRawPosition[0],
            rawPosition[1] - state->previousRawPosition[1],
            rawPosition[2] - state->previousRawPosition[2]
        };
        const f32 sweepLength = vec3f_length(sweep);
        if (sweepLength > 0.01f &&
            sweepLength <= VR_HAND_COLLISION_MAX_SWEEP) {
            Vec3f hitPosition;
            struct Surface* hitSurface = NULL;
            find_surface_on_ray(
                state->previousRawPosition,
                sweep,
                &hitSurface,
                hitPosition,
                2.0f
            );
            if (hitSurface != NULL &&
                !vr_hand_interaction_surface_is_climbable_exception(
                    hitSurface
                )) {
                const f32 previousDistance =
                    vr_hand_interaction_surface_distance(
                        hitSurface,
                        state->previousRawPosition
                    );
                if (previousDistance >= -radius) {
                    position[0] = hitPosition[0] +
                        hitSurface->normal.x * radius;
                    position[1] = hitPosition[1] +
                        hitSurface->normal.y * radius;
                    position[2] = hitPosition[2] +
                        hitSurface->normal.z * radius;
                    collisionSurface = hitSurface;
                    collided = true;
                }
            }
        } else if (sweepLength > VR_HAND_COLLISION_MAX_SWEEP) {
            state->constraintActive = false;
        }
    }

    if (!collided) {
        Vec3f wallPosition;
        vec3f_copy(wallPosition, rawPosition);
        struct WallCollisionData wallData = { 0 };
        resolve_and_return_wall_collisions_data(
            wallPosition,
            0.0f,
            radius,
            &wallData
        );
        for (s32 wall = 0; wall < wallData.numWalls; wall++) {
            if (!vr_hand_interaction_surface_is_climbable_exception(
                    wallData.walls[wall]
                )) {
                vec3f_copy(position, wallPosition);
                collisionSurface = wallData.walls[wall];
                collided = true;
                break;
            }
        }
    }

    struct Surface* floor = NULL;
    const f32 floorHeight = find_floor(
        rawPosition[0],
        rawPosition[1] + radius,
        rawPosition[2],
        &floor
    );
    if (floor != NULL &&
        !vr_hand_interaction_surface_is_climbable_exception(floor) &&
        rawPosition[1] < floorHeight + radius &&
        rawPosition[1] > floorHeight - radius * 2.0f) {
        position[1] = floorHeight + radius;
        collisionSurface = floor;
        collided = true;
    }

    struct Surface* ceiling = NULL;
    const f32 ceilingHeight = find_ceil(
        rawPosition[0],
        rawPosition[1] - radius,
        rawPosition[2],
        &ceiling
    );
    if (ceiling != NULL &&
        !vr_hand_interaction_surface_is_climbable_exception(ceiling) &&
        rawPosition[1] > ceilingHeight - radius &&
        rawPosition[1] < ceilingHeight + radius * 2.0f) {
        position[1] = ceilingHeight - radius;
        collisionSurface = ceiling;
        collided = true;
    }

    if (collisionSurface != NULL) {
        vr_hand_interaction_set_hand_constraint(hand, collisionSurface);
        if (mario != NULL &&
            collisionSurface->normal.y < -0.5f &&
            state->rawPositionValid &&
            rawPosition[1] > state->previousRawPosition[1] &&
            mario->vel[1] > 0.0f &&
            (mario->action & ACT_FLAG_AIR) != 0) {
            mario->vel[1] = 0.0f;
        }
    }

    vec3f_copy(state->previousRawPosition, rawPosition);
    state->rawPositionValid = true;
    return collided;
}

static void vr_hand_interaction_sync_climb_collider_to_headset(
    struct MarioState* mario
);

static void vr_hand_interaction_apply_headset_collider(
    struct MarioState* mario,
    const Vec3f headsetPosition
) {
    if (mario == NULL ||
        mario->marioObj == NULL ||
        headsetPosition == NULL) {
        return;
    }

    if (!sVrHeadsetColliderActive) {
        sVrHeadsetColliderSavedRadius =
            mario->marioObj->hitboxRadius;
        sVrHeadsetColliderSavedHeight =
            mario->marioObj->hitboxHeight;
        sVrHeadsetColliderSavedDownOffset =
            mario->marioObj->hitboxDownOffset;
        sVrHeadsetColliderActive = true;
    }

    mario->marioObj->hitboxRadius =
        VR_HEADSET_INTERACTION_RADIUS;
    mario->marioObj->hitboxHeight =
        VR_HEADSET_INTERACTION_HEIGHT;
    mario->marioObj->hitboxDownOffset = 0.0f;
    mario->marioObj->oPosX = headsetPosition[0];
    mario->marioObj->oPosY = headsetPosition[1] -
        VR_HEADSET_INTERACTION_HEIGHT * 0.5f;
    mario->marioObj->oPosZ = headsetPosition[2];
}

static void vr_hand_interaction_clear_tracked_hold(void) {
    sVrTrackedHeldObject = NULL;
    sVrTrackedHeldHand = VR_CONTROLLER_COUNT;
    sVrTrackedHeldGripMask = 0;
    sVrTrackedHeldPositionValid = false;
    sVrTrackedHeldPositionTimestamp = 0;
    sVrTrackedReleaseInProgress = false;
    vec3f_set(sVrTrackedHeldPreviousPosition, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrTrackedHeldPosition, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrTrackedHeldVelocity, 0.0f, 0.0f, 0.0f);
}

static void vr_hand_interaction_clear_hoot_hold(void) {
    sVrTrackedHootObject = NULL;
    sVrTrackedHootHand = VR_CONTROLLER_COUNT;
}

static void vr_hand_interaction_force_release_hoot(
    struct MarioState* mario
) {
    struct Object* object = sVrTrackedHootObject;
    vr_hand_interaction_clear_hoot_hold();

    if (mario != NULL &&
        mario->playerIndex == 0 &&
        mario->action == ACT_RIDING_HOOT &&
        mario->usedObj == object) {
        if (object != NULL) {
            object->oInteractStatus = 0;
            object->oHootMarioReleaseTime = gGlobalTimer;
        }
        mario->usedObj = NULL;
        set_mario_action(mario, ACT_FREEFALL, 0);
    }
}

static void vr_hand_interaction_clear_player_anchor(void) {
    sVrTrackedAnchorObject = NULL;
    sVrTrackedAnchorHand = VR_CONTROLLER_COUNT;
    vec3f_set(sVrTrackedAnchorOffset, 0.0f, 0.0f, 0.0f);
}

static void vr_hand_interaction_clear_physical_climb(void) {
    sVrPhysicalClimbType = VR_PHYSICAL_CLIMB_NONE;
    sVrPhysicalClimbHand = VR_CONTROLLER_COUNT;
    sVrPhysicalClimbPole = NULL;
    sVrPhysicalClimbPolePrevPositionValid = false;
    vec3f_set(sVrPhysicalClimbPolePrevPosition, 0.0f, 0.0f, 0.0f);
    sVrPhysicalClimbSurface = NULL;
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        sVrPhysicalClimbHands[hand] = false;
        sVrPhysicalClimbLastPositionValid[hand] = false;
        vec3f_set(
            sVrPhysicalClimbLastPosition[hand],
            0.0f,
            0.0f,
            0.0f
        );
    }
    vec3f_set(sVrPhysicalClimbContactPosition, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrPhysicalClimbSurfacePoint, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrPhysicalClimbSurfaceNormal, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrPhysicalClimbCameraOffset, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrPhysicalClimbCameraOffsetPrev, 0.0f, 0.0f, 0.0f);
    sVrPhysicalClimbSafeReleasePositionValid = false;
    sVrPhysicalClimbSafeReleaseTimestamp = 0;
    vec3f_set(
        sVrPhysicalClimbSafeReleasePosition,
        0.0f,
        0.0f,
        0.0f
    );
    sVrPhysicalClimbOffsetTimestamp = 0;
}

static void vr_hand_interaction_prepare_climb_offset_update(void) {
    if (sVrPhysicalClimbOffsetTimestamp == gGlobalTimer) {
        return;
    }

    vec3f_copy(
        sVrPhysicalClimbCameraOffsetPrev,
        sVrPhysicalClimbCameraOffset
    );
    sVrPhysicalClimbOffsetTimestamp = gGlobalTimer;
}

bool vr_hand_interaction_get_climb_camera_offset(Vec3f offset) {
    if (offset == NULL ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE ||
        sVrPhysicalClimbHand >= VR_CONTROLLER_COUNT) {
        return false;
    }

    if (gRenderingInterpolated) {
        const f32 delta = clamp(gRenderingDelta, 0.0f, 1.0f);
        for (u32 axis = 0; axis < 3; axis++) {
            offset[axis] =
                sVrPhysicalClimbCameraOffsetPrev[axis] +
                (sVrPhysicalClimbCameraOffset[axis] -
                 sVrPhysicalClimbCameraOffsetPrev[axis]) * delta;
        }
    } else {
        vec3f_copy(offset, sVrPhysicalClimbCameraOffset);
    }
    return true;
}

bool vr_hand_interaction_is_physical_pole_climb_active(
    struct MarioState* mario
) {
    return mario != NULL &&
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE &&
        sVrPhysicalClimbHand < VR_CONTROLLER_COUNT &&
        mario->usedObj == sVrPhysicalClimbPole &&
        (mario->action & ACT_FLAG_ON_POLE) != 0;
}

void vr_hand_interaction_apply_moving_pole_displacement(
    struct MarioState* mario
) {
    struct Object* pole = sVrPhysicalClimbPole;
    if (mario == NULL || mario->marioObj == NULL || pole == NULL ||
        !vr_hand_interaction_is_physical_pole_climb_active(mario)) {
        sVrPhysicalClimbPolePrevPositionValid = false;
        return;
    }
    Vec3f polePosition = { pole->oPosX, pole->oPosY, pole->oPosZ };
    if (!sVrPhysicalClimbPolePrevPositionValid) {
        vec3f_copy(sVrPhysicalClimbPolePrevPosition, polePosition);
        sVrPhysicalClimbPolePrevPositionValid = true;
        return;
    }
    Vec3f displacement;
    for (u32 axis = 0; axis < 3; axis++) {
        displacement[axis] = polePosition[axis] -
            sVrPhysicalClimbPolePrevPosition[axis];
    }
    vec3f_copy(sVrPhysicalClimbPolePrevPosition, polePosition);
    if (!isfinite(displacement[0]) || !isfinite(displacement[1]) ||
        !isfinite(displacement[2])) {
        return;
    }
    // Mario can consume the pole's new collision position before the pole
    // reaches its normal end-of-update graphics sync. Keep the visible model
    // on that same current transform while a physical grip is active.
    obj_update_gfx_pos_and_angle(pole);
    for (u32 axis = 0; axis < 3; axis++) {
        mario->pos[axis] += displacement[axis];
        sVrPhysicalClimbContactPosition[axis] += displacement[axis];
        if (sVrPhysicalClimbSafeReleasePositionValid) {
            sVrPhysicalClimbSafeReleasePosition[axis] += displacement[axis];
        }
        for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
            if (sVrPhysicalClimbLastPositionValid[hand]) {
                sVrPhysicalClimbLastPosition[hand][axis] +=
                    displacement[axis];
            }
        }
    }
    vec3f_copy(&mario->marioObj->oPosX, mario->pos);
    vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
    vr_invalidate_first_person_tracked_world_cache();
}

bool vr_hand_interaction_is_at_physical_pole_top(
    struct MarioState* mario
) {
    struct Object* pole = sVrPhysicalClimbPole;
    Vec3f headsetPosition;
    if (mario == NULL || pole == NULL ||
        !vr_hand_interaction_is_physical_pole_climb_active(mario) ||
        !vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        return false;
    }
    const f32 poleHeight = fmaxf(pole->hitboxHeight, 1.0f);
    const f32 poleTop = pole->oPosY + poleHeight -
        pole->hitboxDownOffset;
    const f32 topWindow = fmaxf(poleHeight * 0.03f, 10.0f);
    return headsetPosition[1] >= poleTop - topWindow;
}

bool vr_hand_interaction_is_physical_climb_active(
    struct MarioState* mario
) {
    if (mario == NULL ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE ||
        sVrPhysicalClimbHand >= VR_CONTROLLER_COUNT) {
        return false;
    }
    return sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE
        ? mario->usedObj == sVrPhysicalClimbPole &&
            (mario->action & ACT_FLAG_ON_POLE) != 0
        : (mario->action & ACT_FLAG_HANGING) != 0;
}

bool vr_hand_interaction_is_physical_surface_climb_active(
    struct MarioState* mario
) {
    return mario != NULL &&
        sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE &&
        sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_POLE &&
        sVrPhysicalClimbHand < VR_CONTROLLER_COUNT &&
        (mario->action & ACT_FLAG_HANGING) != 0;
}

static void vr_hand_interaction_clear_bowser_motion(void) {
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        sVrBowserHandYawValid[hand] = false;
        sVrBowserPreviousHandYaw[hand] = 0;
        sVrBowserAccumulatedHandYaw[hand] = 0;
    }
    sVrBowserPhysicalTurnInput = 0.0f;
    sVrBowserFullPowerImpulse = false;
    sVrBowserReleaseYawValid = false;
    sVrBowserReleaseYaw = 0;
    sVrBowserMotionTimestamp = 0;
    vec3f_set(sVrBowserFrameVelocity, 0.0f, 0.0f, 0.0f);
    sVrBowserFrameVelocitySamples = 0;
}

static void vr_hand_interaction_reset(void) {
    for (u32 hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        sVrFistActiveFrames[hand] = 0;
        sVrFistPreviousPositionValid[hand] = false;
        sVrClimbPreviousPositionValid[hand] = false;
        vec3f_set(
            sVrClimbPreviousPosition[hand],
            0.0f,
            0.0f,
            0.0f
        );
        sVrMotionDivePairFrames[hand] = 0;
        sVrGripPressed[hand] = false;
    }
    sVrPunchSoundComboStep = 0;
    sVrPunchSoundComboResetFrames = 0;
    sVrBowserGripMask = 0;
    sVrPhysicalClimbRegrabFrames = 0;
    vr_hand_interaction_clear_physical_climb();
    vr_hand_interaction_clear_bowser_motion();
    memset(sVrHandCollision, 0, sizeof(sVrHandCollision));
}

static void vr_hand_interaction_update_punch_sound(
    struct MarioState* mario
) {
    if (!configVrMarioPunchSound) {
        sVrPunchSoundComboStep = 0;
        sVrPunchSoundComboResetFrames = 0;
        return;
    }

    play_character_sound(
        mario,
        sVrPunchSoundComboStep == 0
            ? CHAR_SOUND_PUNCH_YAH
            : CHAR_SOUND_PUNCH_WAH
    );

    // Motion punches repeat Mario's one-two voice combo without advancing
    // to the vanilla third (kick) sound.
    sVrPunchSoundComboStep ^= 1;
    sVrPunchSoundComboResetFrames =
        VR_PUNCH_SOUND_COMBO_RESET_FRAMES;
}

static bool vr_hand_interaction_air_action_can_dive(
    u32 action
) {
    switch (action) {
        case ACT_JUMP:
        case ACT_DOUBLE_JUMP:
        case ACT_TRIPLE_JUMP:
        case ACT_BACKFLIP:
        case ACT_FREEFALL:
        case ACT_SIDE_FLIP:
        case ACT_WALL_KICK_AIR:
        case ACT_STEEP_JUMP:
        case ACT_LONG_JUMP:
        case ACT_WATER_JUMP:
        case ACT_SPECIAL_TRIPLE_JUMP:
        case ACT_FLYING_TRIPLE_JUMP:
        case ACT_TOP_OF_POLE_JUMP:
            return true;
        default:
            return false;
    }
}

static bool vr_hand_interaction_try_motion_dive(
    struct MarioState* mario
) {
    if (mario == NULL ||
        mario->controller == NULL ||
        mario->heldObj != NULL ||
        (mario->action & ACT_FLAG_INTANGIBLE) != 0 ||
        (mario->action & ACT_FLAG_INVULNERABLE) != 0) {
        return false;
    }

    if (vr_hand_interaction_air_action_can_dive(
            mario->action
        )) {
        if (!configVrMotionControlledDive) {
            return false;
        }
        return set_mario_action(mario, ACT_DIVE, 0);
    }

    const bool normalRunningAction =
        mario->action == ACT_WALKING ||
        mario->action == ACT_DECELERATING;
    if (!normalRunningAction) {
        return false;
    }

    // If Mario has just run off a ledge, treat the gesture as an aerial
    // dive rather than adding the small upward boost used by a ground dive.
    if ((mario->input & INPUT_OFF_FLOOR) != 0) {
        if (!configVrMotionControlledDive) {
            return false;
        }
        return set_mario_action(mario, ACT_DIVE, 0);
    }

    if (!configVrMotionControlledGroundDive) {
        return false;
    }

    // Match the vanilla running-dive requirements exactly.
    if (mario->forwardVel < 29.0f ||
        mario->controller->stickMag <= 48.0f) {
        return false;
    }

    mario->vel[1] = 20.0f;
    return set_mario_action(mario, ACT_DIVE, 1);
}

static void vr_hand_interaction_register_motion_dive_punch(
    struct MarioState* mario,
    u32 hand
) {
    if ((!configVrMotionControlledDive &&
         !configVrMotionControlledGroundDive) ||
        hand >= VR_CONTROLLER_COUNT) {
        return;
    }

    sVrMotionDivePairFrames[hand] =
        VR_MOTION_DIVE_PAIR_WINDOW_FRAMES;
    if (sVrMotionDivePairFrames[VR_CONTROLLER_LEFT] == 0 ||
        sVrMotionDivePairFrames[VR_CONTROLLER_RIGHT] == 0) {
        return;
    }

    sVrMotionDivePairFrames[VR_CONTROLLER_LEFT] = 0;
    sVrMotionDivePairFrames[VR_CONTROLLER_RIGHT] = 0;

    if (vr_hand_interaction_try_motion_dive(mario)) {
        vr_apply_haptic(
            VR_CONTROLLER_LEFT,
            0.45f,
            0.06f,
            -1.0f
        );
        vr_apply_haptic(
            VR_CONTROLLER_RIGHT,
            0.45f,
            0.06f,
            -1.0f
        );
        VR_INTERACTION_DEBUG(
            "[VR] Two-hand motion dive triggered.\n"
        );
    }
}

static f32 vr_hand_interaction_fist_radius(void) {
    static unsigned int sCachedGloveSize = ~0U;
    static f32 sCachedRadius = VR_FIST_BASE_RADIUS;

    if (sCachedGloveSize != configVrGloveSize) {
        sCachedGloveSize = configVrGloveSize;
        const f32 gloveSize = (f32)clamp(
            sCachedGloveSize,
            25U,
            250U
        );
        sCachedRadius = VR_FIST_BASE_RADIUS * gloveSize / 70.0f;
    }
    return sCachedRadius;
}

static f32 vr_hand_interaction_fist_length(
    f32 fistRadius
) {
    const f32 colliderLength = (f32)clamp(
        configVrPunchColliderLength,
        50U,
        300U
    );

    // At 100%, length equals the old collider diameter. Extra length extends
    // downward from the fist to make short enemies easier to reach without
    // widening punches sideways or changing the visible glove model.
    return fistRadius * 2.0f * colliderLength / 100.0f;
}

bool vr_hand_interaction_is_tracked_held_object(
    struct Object* object
) {
    return object != NULL &&
        object == sVrTrackedHeldObject &&
        sVrTrackedHeldGripMask != 0;
}

bool vr_hand_interaction_blocks_native_held_object_release(
    struct MarioState* mario
) {
    return !sVrTrackedReleaseInProgress &&
        mario != NULL &&
        mario->playerIndex == 0 &&
        mario->heldObj != NULL &&
        mario->heldObj == sVrTrackedHeldObject &&
        sVrTrackedHeldGripMask != 0 &&
        // Damage and knockback use Mario's native forced-drop path. A closed
        // physical grip must not cancel that rule or instantly reclaim Mips,
        // a baby penguin, or another carried actor after Mario is hit.
        mario->hurtCounter == 0 &&
        mario->knockbackTimer == 0;
}

bool vr_hand_interaction_get_held_object_position(
    struct Object* object,
    Vec3f position
) {
    if (position == NULL ||
        !sVrTrackedHeldPositionValid ||
        !vr_hand_interaction_is_tracked_held_object(object)) {
        return false;
    }

    vec3f_copy(position, sVrTrackedHeldPosition);
    return true;
}

bool vr_hand_interaction_get_late_held_object_position(
    struct Object* object,
    Vec3f position
) {
    if (position == NULL ||
        !vr_hand_interaction_is_tracked_held_object(object)) {
        return false;
    }

    struct VrControllerState state;
    Vec3f handPosition;
    if (!vr_get_controller_state(sVrTrackedHeldHand, &state) ||
        !vr_get_controller_world_fist_from_state(
            sVrTrackedHeldHand,
            &state,
            handPosition,
            NULL
        )) {
        return vr_hand_interaction_get_held_object_position(
            object,
            position
        );
    }

    const f32 objectHeight = fmaxf(
        object->hitboxHeight,
        object->hurtboxHeight
    );
    const f32 centerOffset = clamp(
        objectHeight * 0.5f - object->hitboxDownOffset,
        0.0f,
        80.0f
    );
    position[0] = handPosition[0];
    position[1] = handPosition[1] - centerOffset;
    position[2] = handPosition[2];
    return true;
}

bool vr_hand_interaction_apply_held_object_transform(
    struct Object* object
) {
    if (!sVrTrackedHeldPositionValid ||
        !vr_hand_interaction_is_tracked_held_object(object) ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        return false;
    }

    vec3f_copy(&object->oPosX, sVrTrackedHeldPosition);
    vec3f_copy(
        object->header.gfx.prevPos,
        sVrTrackedHeldPreviousPosition
    );
    vec3f_copy(object->header.gfx.pos, sVrTrackedHeldPosition);
    // Gameplay remains native-rate. Rendering interpolates normally, then
    // late-patches the root to the newest tracked pose each OpenXR frame.
    object->header.gfx.node.flags |= GRAPH_RENDER_ACTIVE;
    object->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
    return true;
}

bool vr_hand_interaction_apply_player_anchor(
    struct MarioState* mario
) {
    struct Object* object = sVrTrackedAnchorObject;
    if (mario == NULL ||
        mario->playerIndex != 0 ||
        mario->marioObj == NULL ||
        object == NULL ||
        sVrTrackedAnchorHand >= VR_CONTROLLER_COUNT ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        return false;
    }

    const f32 yawSin = sins(object->oMoveAngleYaw);
    const f32 yawCos = coss(object->oMoveAngleYaw);
    const f32 left = sVrTrackedAnchorOffset[0];
    const f32 forward = sVrTrackedAnchorOffset[2];

    mario->pos[0] = object->oPosX +
        forward * yawSin + left * yawCos;
    mario->pos[1] = object->oPosY +
        sVrTrackedAnchorOffset[1];
    mario->pos[2] = object->oPosZ +
        forward * yawCos - left * yawSin;
    vec3f_set(mario->vel, 0.0f, 0.0f, 0.0f);
    mario->forwardVel = 0.0f;

    vec3f_copy(&mario->marioObj->oPosX, mario->pos);
    vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
    return true;
}

bool vr_hand_interaction_is_player_anchor_object(
    struct Object* object
) {
    return object != NULL &&
        ((object == sVrTrackedAnchorObject &&
          sVrTrackedAnchorHand < VR_CONTROLLER_COUNT) ||
         (object == sVrPhysicalClimbPole &&
          sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE &&
          sVrPhysicalClimbHand < VR_CONTROLLER_COUNT));
}

static void vr_hand_interaction_update_grip(
    u32 hand,
    bool controllerAvailable,
    const struct VrControllerState* state
) {
    if (hand >= VR_CONTROLLER_COUNT ||
        !controllerAvailable ||
        state == NULL) {
        if (hand < VR_CONTROLLER_COUNT) {
            sVrGripPressed[hand] = false;
        }
        return;
    }

    if (state->squeeze >= VR_GRIP_CLOSE_THRESHOLD) {
        sVrGripPressed[hand] = true;
    } else if (state->squeeze <= VR_GRIP_OPEN_THRESHOLD) {
        sVrGripPressed[hand] = false;
    }
}

static bool vr_hand_interaction_is_bowser_hold(
    struct MarioState* mario
) {
    return mario != NULL &&
        mario->playerIndex == 0 &&
        mario->action == ACT_HOLDING_BOWSER &&
        mario->heldObj != NULL &&
        obj_has_behavior(mario->heldObj, bhvBowser);
}

static bool vr_hand_interaction_is_bowser_sequence(
    struct MarioState* mario
) {
    return mario != NULL &&
        mario->playerIndex == 0 &&
        (mario->action == ACT_PICKING_UP_BOWSER ||
         vr_hand_interaction_is_bowser_hold(mario));
}

static void vr_hand_interaction_update_bowser_hand_turn(
    struct MarioState* mario,
    u32 hand,
    bool controllerAvailable,
    const struct VrControllerState* controllerState
) {
    const u8 handBit = (u8)(1U << hand);
    if (!vr_hand_interaction_is_bowser_hold(mario) ||
        (sVrBowserGripMask & handBit) == 0 ||
        !sVrGripPressed[hand] ||
        !controllerAvailable ||
        controllerState == NULL ||
        (!controllerState->gripPoseValid &&
         !controllerState->aimPoseValid)) {
        sVrBowserHandYawValid[hand] = false;
        sVrBowserAccumulatedHandYaw[hand] = 0;
        return;
    }

    if (sVrBowserMotionTimestamp != gGlobalTimer) {
        sVrBowserMotionTimestamp = gGlobalTimer;
        sVrBowserPhysicalTurnInput = 0.0f;
        vec3f_set(sVrBowserFrameVelocity, 0.0f, 0.0f, 0.0f);
        sVrBowserFrameVelocitySamples = 0;
    }

    Vec3f handWorldPosition;
    Vec3f handWorldVelocity;
    if (vr_get_controller_world_fist_from_state(
            hand,
            controllerState,
            handWorldPosition,
            handWorldVelocity)) {
        sVrBowserFrameVelocity[0] += handWorldVelocity[0];
        sVrBowserFrameVelocity[1] += handWorldVelocity[1];
        sVrBowserFrameVelocity[2] += handWorldVelocity[2];
        sVrBowserFrameVelocitySamples++;
        const f32 invSamples =
            1.0f / (f32)sVrBowserFrameVelocitySamples;
        const f32 averageX =
            sVrBowserFrameVelocity[0] * invSamples;
        const f32 averageZ =
            sVrBowserFrameVelocity[2] * invSamples;
        const f32 horizontalSpeedSquared =
            averageX * averageX + averageZ * averageZ;
        if (horizontalSpeedSquared >= 25.0f * 25.0f) {
            // The physical hand-swing tangent is the release direction. It
            // deliberately does not depend on HMD or Mario facing yaw.
            sVrBowserReleaseYaw = atan2s(
                averageZ,
                averageX
            );
            sVrBowserReleaseYawValid = true;
        }
    }

    float headPosition[3];
    if (!vr_get_head_translation(headPosition)) {
        sVrBowserHandYawValid[hand] = false;
        sVrBowserAccumulatedHandYaw[hand] = 0;
        return;
    }

    // Controller and head poses are both expressed in the recentered OpenXR
    // tracking space. Measuring the hand around the head here makes physical
    // Bowser momentum depend only on hand movement, never camera/HMD yaw.
    const float* trackedHandPosition =
        controllerState->gripPoseValid
            ? controllerState->gripPosition
            : controllerState->aimPosition;
    const f32 handX = trackedHandPosition[0] - headPosition[0];
    const f32 handZ = trackedHandPosition[2] - headPosition[2];
    if (handX * handX + handZ * handZ <
        VR_BOWSER_MIN_HAND_RADIUS_METERS *
            VR_BOWSER_MIN_HAND_RADIUS_METERS) {
        sVrBowserHandYawValid[hand] = false;
        sVrBowserAccumulatedHandYaw[hand] = 0;
        return;
    }

    const s16 handYaw = atan2s(handZ, handX);
    if (sVrBowserHandYawValid[hand]) {
        const s16 yawDelta =
            (s16)(handYaw - sVrBowserPreviousHandYaw[hand]);
        const s32 delta = (s32)yawDelta;
        if (delta >= VR_BOWSER_HAND_TURN_JITTER ||
            delta <= -VR_BOWSER_HAND_TURN_JITTER) {
            const f32 handTurnInput = clamp(
                (f32)delta /
                    (f32)VR_BOWSER_HAND_TURN_FULL_INPUT,
                -1.0f,
                1.0f
            );
            if (fabsf(handTurnInput) >
                fabsf(sVrBowserPhysicalTurnInput)) {
                sVrBowserPhysicalTurnInput = handTurnInput;
            }

            // Direction reversals start a new swing. A deliberate 22.5-degree
            // hand arc is one physical turn and guarantees full throw power.
            if ((sVrBowserAccumulatedHandYaw[hand] > 0 && delta < 0) ||
                (sVrBowserAccumulatedHandYaw[hand] < 0 && delta > 0)) {
                sVrBowserAccumulatedHandYaw[hand] = delta;
            } else {
                sVrBowserAccumulatedHandYaw[hand] += delta;
            }
            sVrBowserAccumulatedHandYaw[hand] = clamp(
                sVrBowserAccumulatedHandYaw[hand],
                -VR_BOWSER_FULL_POWER_ARC,
                VR_BOWSER_FULL_POWER_ARC
            );
            if (sVrBowserAccumulatedHandYaw[hand] ==
                    VR_BOWSER_FULL_POWER_ARC ||
                sVrBowserAccumulatedHandYaw[hand] ==
                    -VR_BOWSER_FULL_POWER_ARC) {
                sVrBowserFullPowerImpulse = true;
                sVrBowserAccumulatedHandYaw[hand] = 0;
            }
        }
    }

    sVrBowserPreviousHandYaw[hand] = handYaw;
    sVrBowserHandYawValid[hand] = true;
}

bool vr_hand_interaction_get_bowser_controls(
    struct MarioState* mario,
    f32* turnInput,
    bool* gripReleased,
    bool* fullPowerImpulse,
    s16* releaseYaw,
    bool* releaseYawValid
) {
    if (turnInput != NULL) {
        *turnInput = 0.0f;
    }
    if (gripReleased != NULL) {
        *gripReleased = false;
    }
    if (fullPowerImpulse != NULL) {
        *fullPowerImpulse = false;
    }
    if (releaseYaw != NULL) {
        *releaseYaw = 0;
    }
    if (releaseYawValid != NULL) {
        *releaseYawValid = false;
    }

    if (!vr_is_active() ||
        !configVrMotionControllerInput ||
        !configVrPhysicalGrabbing ||
        !vr_hand_interaction_is_bowser_hold(mario)) {
        sVrBowserGripMask = 0;
        vr_hand_interaction_clear_bowser_motion();
        return false;
    }

    if (sVrBowserGripMask == 0) {
        for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
            if (sVrGripPressed[hand]) {
                sVrBowserGripMask |= (u8)(1U << hand);
            }
        }
        if (sVrBowserGripMask == 0) {
            return false;
        }
        vr_hand_interaction_clear_bowser_motion();
    }

    const u8 previousGripMask = sVrBowserGripMask;
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        const u8 handBit = (u8)(1U << hand);
        if ((sVrBowserGripMask & handBit) != 0 &&
            !sVrGripPressed[hand]) {
            sVrBowserGripMask &= (u8)~handBit;
            sVrBowserHandYawValid[hand] = false;
            sVrBowserAccumulatedHandYaw[hand] = 0;
        }
    }

    if (previousGripMask != 0 && sVrBowserGripMask == 0) {
        if (releaseYaw != NULL) {
            *releaseYaw = sVrBowserReleaseYaw;
        }
        if (releaseYawValid != NULL) {
            *releaseYawValid = sVrBowserReleaseYawValid;
        }
        if (gripReleased != NULL) {
            *gripReleased = true;
        }
        vr_hand_interaction_clear_bowser_motion();
        return true;
    }

    f32 input = sVrBowserPhysicalTurnInput;
    if (mario->controller != NULL) {
        f32 stickInput =
            (f32)mario->controller->extStickX / 127.0f;
        const f32 magnitude = fabsf(stickInput);
        if (magnitude <= VR_BOWSER_TURN_DEADZONE) {
            stickInput = 0.0f;
        } else {
            stickInput = copysignf(
                (magnitude - VR_BOWSER_TURN_DEADZONE) /
                    (1.0f - VR_BOWSER_TURN_DEADZONE),
                stickInput
            );
        }
        if (fabsf(stickInput) > fabsf(input)) {
            input = stickInput;
        }
    }

    if (turnInput != NULL) {
        *turnInput = clamp(input, -1.0f, 1.0f);
    }
    if (fullPowerImpulse != NULL) {
        *fullPowerImpulse = sVrBowserFullPowerImpulse;
    }
    sVrBowserFullPowerImpulse = false;

    return true;
}

bool vr_hand_interaction_bowser_spin_active(void) {
    return sVrBowserGripMask != 0 &&
        vr_is_active() &&
        configVrMotionControllerInput &&
        configVrPhysicalGrabbing &&
        vr_hand_interaction_is_bowser_hold(&gMarioStates[0]);
}

static void vr_hand_interaction_update_held_position(
    struct Object* object,
    const Vec3f handPosition,
    const Vec3f handVelocity
) {
    const f32 objectHeight = fmaxf(
        object->hitboxHeight,
        object->hurtboxHeight
    );
    const f32 centerOffset = clamp(
        objectHeight * 0.5f - object->hitboxDownOffset,
        0.0f,
        80.0f
    );

    Vec3f nextPosition = {
        handPosition[0],
        handPosition[1] - centerOffset,
        handPosition[2]
    };
    if (!sVrTrackedHeldPositionValid) {
        vec3f_copy(sVrTrackedHeldPreviousPosition, nextPosition);
    } else if (sVrTrackedHeldPositionTimestamp != gGlobalTimer) {
        vec3f_copy(
            sVrTrackedHeldPreviousPosition,
            sVrTrackedHeldPosition
        );
    }
    vec3f_copy(sVrTrackedHeldPosition, nextPosition);
    sVrTrackedHeldPositionTimestamp = gGlobalTimer;

    const f32 currentSpeedSquared =
        handVelocity[0] * handVelocity[0] +
        handVelocity[1] * handVelocity[1] +
        handVelocity[2] * handVelocity[2];
    const f32 rememberedSpeedSquared =
        sVrTrackedHeldVelocity[0] * sVrTrackedHeldVelocity[0] +
        sVrTrackedHeldVelocity[1] * sVrTrackedHeldVelocity[1] +
        sVrTrackedHeldVelocity[2] * sVrTrackedHeldVelocity[2];
    const f32 decayedRememberedSpeedSquared =
        rememberedSpeedSquared *
        VR_THROW_VELOCITY_MEMORY *
        VR_THROW_VELOCITY_MEMORY;

    // Keep a short-lived peak rather than only the final release-frame
    // sample. Players naturally decelerate just before their fingers open,
    // and runtimes can report that last sample as nearly stationary.
    if (!sVrTrackedHeldPositionValid ||
        currentSpeedSquared >= decayedRememberedSpeedSquared) {
        sVrTrackedHeldVelocity[0] = handVelocity[0];
        sVrTrackedHeldVelocity[1] = handVelocity[1];
        sVrTrackedHeldVelocity[2] = handVelocity[2];
    } else {
        vec3f_mul(sVrTrackedHeldVelocity, VR_THROW_VELOCITY_MEMORY);
    }
    sVrTrackedHeldPositionValid = true;

    vr_hand_interaction_apply_held_object_transform(object);
}

static bool vr_hand_interaction_requires_native_carry(
    struct Object* object
) {
    // Crazy/Jumping Boxes deliberately take control of Mario for their
    // three-bounce sequence. Treating them like Bob-ombs or ordinary held
    // enemies pins the box to a hand and suppresses the movement that is the
    // object's entire purpose. Keep this behavior-specific so normal enemies
    // remain decoupled from Mario's old forced pickup/backstep motion.
    return object != NULL && obj_has_behavior(object, bhvJumpingBox);
}

static bool vr_hand_interaction_adopt_native_hold(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition,
    const Vec3f handVelocity
) {
    struct Object* object = mario != NULL ? mario->heldObj : NULL;
    if (!configVrPhysicalGrabbing ||
        object == NULL ||
        sVrTrackedHeldObject != NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        !sVrGripPressed[hand] ||
        obj_has_behavior(object, bhvBowser) ||
        vr_hand_interaction_requires_native_carry(object) ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        return false;
    }

    // Dive/punch pickups already established the native held-object and
    // multiplayer state. Adopt that same object instead of grabbing it again.
    sVrTrackedHeldObject = object;
    sVrTrackedHeldHand = hand;
    sVrTrackedHeldGripMask = (u8)(1U << hand);
    vr_hand_interaction_update_held_position(
        object,
        handPosition,
        handVelocity
    );
    vr_apply_haptic(hand, 0.35f, 0.06f, -1.0f);
    VR_INTERACTION_DEBUG(
        "[VR] %s hand adopted Mario's held object.\n",
        hand == VR_CONTROLLER_LEFT ? "Left" : "Right"
    );
    return true;
}

static bool vr_hand_interaction_object_can_be_grabbed(
    struct MarioState* mario,
    struct Object* object
) {
    return mario != NULL &&
        object != NULL &&
        mario->heldObj == NULL &&
        object != mario->marioObj &&
        object->oHeldState == HELD_FREE &&
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) != 0 &&
        object->oIntangibleTimer == 0 &&
        (object->oInteractType & INTERACT_GRABBABLE) != 0 &&
        (object->oInteractionSubtype &
            INT_SUBTYPE_NOT_GRABBABLE) == 0 &&
        // Bowser's tail grab has special rotation, release, and throw rules.
        // Keep the native dive/punch tail interaction until the dedicated
        // physical Bowser-throw step implements those rules deliberately.
        !obj_has_behavior(object, bhvBowser);
}

static bool vr_hand_interaction_hand_is_behind_heavy_grabbable(
    const Vec3f handPosition,
    struct Object* object
) {
    if (object == NULL || handPosition == NULL ||
        (object->oInteractionSubtype & INT_SUBTYPE_GRABS_MARIO) == 0 ||
        obj_has_behavior(object, bhvKingBobomb)) {
        return true;
    }

    const s16 handYaw = atan2s(
        handPosition[2] - object->oPosZ,
        handPosition[0] - object->oPosX
    );
    return abs_angle_diff(handYaw, object->oMoveAngleYaw) >= 0x5555;
}

static bool vr_hand_interaction_grab_overlaps_object(
    const Vec3f handPosition,
    f32 handRadius,
    struct Object* object,
    f32* distanceSquared
);

static bool vr_hand_interaction_grab_overlaps_object(
    const Vec3f handPosition,
    f32 handRadius,
    struct Object* object,
    f32* distanceSquared
) {
    f32 objectRadius = fmaxf(
        object->hitboxRadius,
        object->hurtboxRadius
    );
    f32 objectHeight = fmaxf(
        object->hitboxHeight,
        object->hurtboxHeight
    );
    if (obj_has_behavior(object, bhvKingBobomb)) {
        objectRadius = fmaxf(objectRadius, 180.0f);
        objectHeight = fmaxf(objectHeight, 300.0f);
    }
    if (objectRadius <= 0.0f) {
        objectRadius = 30.0f;
    }
    if (objectHeight <= 0.0f) {
        objectHeight = 60.0f;
    }

    const bool specialHeavyGrab =
        obj_has_behavior(object, bhvKingBobomb) ||
        (object->oInteractionSubtype &
            INT_SUBTYPE_GRABS_MARIO) != 0;
    f32 objectBottom =
        object->oPosY - object->hitboxDownOffset;
    f32 objectTop = objectBottom + objectHeight;
    if (!specialHeavyGrab) {
        const f32 horizontalScale = fmaxf(
            fabsf(object->header.gfx.scale[0]),
            fabsf(object->header.gfx.scale[2])
        );
        const f32 verticalScale =
            fabsf(object->header.gfx.scale[1]);
        objectRadius *= fmaxf(horizontalScale, 1.0f);
        objectTop = objectBottom +
            objectHeight * fmaxf(verticalScale, 1.0f);
        objectBottom += fminf(object->oGraphYOffset, 0.0f);
        objectTop += fmaxf(object->oGraphYOffset, 0.0f);
    }
    const f32 closestY = clamp(
        handPosition[1],
        objectBottom,
        objectTop
    );
    const f32 deltaX = handPosition[0] - object->oPosX;
    const f32 deltaY = handPosition[1] - closestY;
    const f32 deltaZ = handPosition[2] - object->oPosZ;
    const f32 horizontalReach =
        objectRadius + handRadius + VR_GRAB_EXTRA_REACH;
    const f32 verticalReach =
        handRadius + VR_GRAB_EXTRA_REACH;

    if (deltaX * deltaX + deltaZ * deltaZ >
            horizontalReach * horizontalReach ||
        fabsf(deltaY) > verticalReach) {
        return false;
    }

    if (distanceSquared != NULL) {
        *distanceSquared =
            deltaX * deltaX +
            deltaY * deltaY +
            deltaZ * deltaZ;
    }
    return true;
}

static bool vr_hand_interaction_try_collect_cap(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition
) {
    if (mario == NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        handPosition == NULL ||
        gObjectLists == NULL) {
        return false;
    }

    const f32 handRadius = vr_hand_interaction_fist_radius();
    struct ObjectNode* list = &gObjectLists[OBJ_LIST_LEVEL];
    struct ObjectNode* node = list->next;
    while (node != NULL && node != list) {
        struct Object* object = (struct Object*)node;
        node = node->next;

        if ((object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            object->oIntangibleTimer != 0 ||
            (object->oInteractType & INTERACT_CAP) == 0 ||
            (object->oInteractStatus & INT_STATUS_INTERACTED) != 0 ||
            !vr_hand_interaction_grab_overlaps_object(
                handPosition,
                handRadius,
                object,
                NULL
            )) {
            continue;
        }

        if (process_interaction(
                mario,
                INTERACT_CAP,
                object,
                interact_cap
            )) {
            vr_apply_haptic(hand, 0.45f, 0.08f, -1.0f);
            VR_INTERACTION_DEBUG(
                "[VR] Hand contact collected a cap.\n"
            );
            return true;
        }
    }
    return false;
}

void vr_hand_interaction_update_headset_collider(
    struct MarioState* mario
) {
    if (mario == NULL ||
        mario->playerIndex != 0 ||
        mario->marioObj == NULL) {
        return;
    }

    const bool useHeadsetCollider =
        vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        ((mario->action & ACT_FLAG_SWIMMING) != 0 ||
         mario->action == ACT_FLYING ||
         mario->action == ACT_FLYING_TRIPLE_JUMP ||
         vr_hand_interaction_is_physical_climb_active(mario));

    if (!useHeadsetCollider) {
        if (sVrHeadsetColliderActive) {
            mario->marioObj->hitboxRadius =
                sVrHeadsetColliderSavedRadius;
            mario->marioObj->hitboxHeight =
                sVrHeadsetColliderSavedHeight;
            mario->marioObj->hitboxDownOffset =
                sVrHeadsetColliderSavedDownOffset;
            vec3f_copy(&mario->marioObj->oPosX, mario->pos);
            sVrHeadsetColliderActive = false;
        }
        return;
    }

    Vec3f headsetPosition;
    if (!vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        return;
    }

    // This is deliberately only Mario's object-interaction cylinder. The
    // environment/physics collider remains at mario->pos so flight and water
    // movement stay compatible with the original engine and multiplayer.
    vr_hand_interaction_apply_headset_collider(
        mario,
        headsetPosition
    );
}

static struct Object* vr_hand_interaction_find_bowser_tail(
    const Vec3f handPosition
) {
    if (gObjectLists == NULL) {
        return NULL;
    }

    const f32 handRadius = vr_hand_interaction_fist_radius();
    const f32 reach = 100.0f + handRadius + VR_GRAB_EXTRA_REACH;
    const f32 reachSquared = reach * reach;
    struct Object* nearestTail = NULL;
    f32 nearestDistanceSquared = 0.0f;

    struct ObjectNode* list = &gObjectLists[OBJ_LIST_GENACTOR];
    struct ObjectNode* node = list->next;
    while (node != NULL && node != list) {
        struct Object* tail = (struct Object*)node;
        node = node->next;

        struct Object* bowser = tail->parentObj;
        if (!obj_has_behavior(tail, bhvBowserTailAnchor) ||
            (tail->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            bowser == NULL ||
            !obj_has_behavior(bowser, bhvBowser) ||
            bowser->oHeldState != HELD_FREE ||
            (bowser->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            bowser->oAction == 5 ||
            bowser->oAction == 6 ||
            bowser->oAction == 19 ||
            bowser->oAction == 20) {
            continue;
        }

        // The native tail helper intentionally becomes intangible and moves
        // to action 2 while Mario overlaps it. That is the normal state in
        // which a physical glove is close enough to grab, so do not reject it.
        const f32 deltaX = handPosition[0] - tail->oPosX;
        const f32 deltaY = handPosition[1] - tail->oPosY;
        const f32 deltaZ = handPosition[2] - tail->oPosZ;
        const f32 distanceSquared =
            deltaX * deltaX +
            deltaY * deltaY +
            deltaZ * deltaZ;
        if (distanceSquared > reachSquared) {
            continue;
        }

        if (nearestTail == NULL ||
            distanceSquared < nearestDistanceSquared) {
            nearestTail = tail;
            nearestDistanceSquared = distanceSquared;
        }
    }

    return nearestTail;
}

static bool vr_hand_interaction_try_bowser_grab(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition
) {
    if (!configVrPhysicalGrabbing ||
        mario == NULL ||
        mario->heldObj != NULL ||
        hand >= VR_CONTROLLER_COUNT) {
        return false;
    }

    struct Object* tail =
        vr_hand_interaction_find_bowser_tail(handPosition);
    struct Object* bowser = tail != NULL ? tail->parentObj : NULL;
    if (bowser == NULL) {
        return false;
    }

    bool allowInteract = true;
    smlua_call_event_hooks(
        HOOK_ALLOW_INTERACT,
        mario,
        bowser,
        INTERACT_GRABBABLE,
        &allowInteract
    );
    if (!allowInteract) {
        return false;
    }

    mario->faceAngle[1] = bowser->oMoveAngleYaw;
    mario->interactObj = bowser;
    mario->usedObj = bowser;
    if (!set_mario_action(mario, ACT_PICKING_UP_BOWSER, 0)) {
        smlua_call_event_hooks(
            HOOK_ON_INTERACT,
            mario,
            bowser,
            INTERACT_GRABBABLE,
            false
        );
        return false;
    }

    // Keep the native Bowser-held state and networking. Only the physical
    // controller ownership is one-handed from the player's perspective.
    tail->oAction = 2;
    bowser->oIntangibleTimer = 0;
    sVrBowserGripMask = (u8)(1U << hand);
    vr_hand_interaction_clear_bowser_motion();
    if (bowser->oSyncID != 0) {
        network_send_object_reliability(bowser, true);
    }
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        bowser,
        INTERACT_GRABBABLE,
        true
    );
    vr_apply_haptic(hand, 0.55f, 0.09f, -1.0f);
    VR_INTERACTION_DEBUG(
        "[VR] %s hand grabbed Bowser's tail.\n",
        hand == VR_CONTROLLER_LEFT ? "Left" : "Right"
    );
    return true;
}

static struct Object* vr_hand_interaction_find_grab_target(
    struct MarioState* mario,
    const Vec3f handPosition
) {
    if (gObjectLists == NULL) {
        return NULL;
    }

    const f32 handRadius =
        vr_hand_interaction_fist_radius();
    struct Object* nearestObject = NULL;
    f32 nearestDistanceSquared = 0.0f;

    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;

        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;

            f32 distanceSquared;
            if (!vr_hand_interaction_object_can_be_grabbed(
                    mario,
                    object
                ) ||
                !vr_hand_interaction_hand_is_behind_heavy_grabbable(
                    handPosition,
                    object
                ) ||
                !vr_hand_interaction_grab_overlaps_object(
                    handPosition,
                    handRadius,
                    object,
                    &distanceSquared
                )) {
                continue;
            }

            if (nearestObject == NULL ||
                distanceSquared < nearestDistanceSquared) {
                nearestObject = object;
                nearestDistanceSquared = distanceSquared;
            }
        }
    }

    return nearestObject;
}

static bool vr_hand_interaction_climb_is_occupied(void) {
    return sVrTrackedHeldObject != NULL ||
        sVrTrackedHootObject != NULL ||
        sVrTrackedAnchorObject != NULL ||
        sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE ||
        sVrBowserGripMask != 0;
}

static struct Object* vr_hand_interaction_find_pole_target(
    struct MarioState* mario,
    const Vec3f handPosition
) {
    if (mario == NULL ||
        mario->heldObj != NULL ||
        gObjectLists == NULL ||
        vr_hand_interaction_climb_is_occupied()) {
        return NULL;
    }

    const f32 handRadius = vr_hand_interaction_fist_radius();
    struct Object* nearestPole = NULL;
    f32 nearestDistanceSquared = 0.0f;
    struct ObjectNode* list = &gObjectLists[OBJ_LIST_POLELIKE];
    struct ObjectNode* node = list->next;

    while (node != NULL && node != list) {
        struct Object* pole = (struct Object*)node;
        node = node->next;

        f32 distanceSquared;
        if ((pole->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            pole->oIntangibleTimer != 0 ||
            (pole->oInteractType & INTERACT_POLE) == 0 ||
            !vr_hand_interaction_grab_overlaps_object(
                handPosition,
                handRadius,
                pole,
                &distanceSquared
            )) {
            continue;
        }

        if (nearestPole == NULL ||
            distanceSquared < nearestDistanceSquared) {
            nearestPole = pole;
            nearestDistanceSquared = distanceSquared;
        }
    }

    return nearestPole;
}

static void vr_hand_interaction_set_physical_climb_owner(
    u32 hand,
    const Vec3f climbPosition
) {
    if (hand >= VR_CONTROLLER_COUNT || climbPosition == NULL) {
        return;
    }

    sVrPhysicalClimbHands[hand] = true;
    sVrPhysicalClimbHand = hand;
    for (u32 axis = 0; axis < 3; axis++) {
        sVrPhysicalClimbContactPosition[axis] = climbPosition[axis];
        sVrPhysicalClimbLastPosition[hand][axis] = climbPosition[axis];
    }
    sVrPhysicalClimbLastPositionValid[hand] = true;
    vr_invalidate_first_person_tracked_world_cache();
}

static bool vr_hand_interaction_start_pole_climb(
    struct MarioState* mario,
    struct Object* pole,
    u32 hand,
    const Vec3f climbPosition
) {
    if (mario == NULL ||
        mario->marioObj == NULL ||
        pole == NULL ||
        hand >= VR_CONTROLLER_COUNT) {
        return false;
    }

    bool allowInteract = true;
    smlua_call_event_hooks(
        HOOK_ALLOW_INTERACT,
        mario,
        pole,
        INTERACT_POLE,
        &allowInteract
    );
    if (!allowInteract) {
        return false;
    }

    Vec3f previousPosition;
    vec3f_copy(previousPosition, mario->pos);
    mario_stop_riding_and_holding(mario);
    mario->interactObj = pole;
    mario->usedObj = pole;
    mario->pos[0] = pole->oPosX;
    mario->pos[2] = pole->oPosZ;
    vec3f_set(mario->vel, 0.0f, 0.0f, 0.0f);
    mario->forwardVel = 0.0f;
    mario->marioObj->oMarioPoleUnk108 = 0;
    mario->marioObj->oMarioPoleYawVel = 0;
    mario->marioObj->oMarioPolePos =
        mario->pos[1] - pole->oPosY;
    mario->faceAngle[0] = 0;
    vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);

    if (!set_mario_action(mario, ACT_HOLDING_POLE, 0)) {
        vec3f_copy(mario->pos, previousPosition);
        vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
        mario->usedObj = NULL;
        smlua_call_event_hooks(
            HOOK_ON_INTERACT,
            mario,
            pole,
            INTERACT_POLE,
            false
        );
        return false;
    }

    sVrPhysicalClimbType = VR_PHYSICAL_CLIMB_POLE;
    sVrPhysicalClimbPole = pole;
    vec3f_copy(sVrPhysicalClimbPolePrevPosition, &pole->oPosX);
    sVrPhysicalClimbPolePrevPositionValid = true;
    for (u32 axis = 0; axis < 3; axis++) {
        sVrPhysicalClimbCameraOffset[axis] =
            previousPosition[axis] - mario->pos[axis];
    }
    vec3f_copy(
        sVrPhysicalClimbCameraOffsetPrev,
        sVrPhysicalClimbCameraOffset
    );
    vec3f_copy(sVrPhysicalClimbSafeReleasePosition, previousPosition);
    sVrPhysicalClimbSafeReleasePositionValid = true;
    sVrPhysicalClimbSafeReleaseTimestamp = gGlobalTimer;
    sVrPhysicalClimbOffsetTimestamp = gGlobalTimer;
    vr_hand_interaction_set_physical_climb_owner(
        hand,
        climbPosition
    );
    vr_hand_interaction_sync_climb_collider_to_headset(mario);
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        pole,
        INTERACT_POLE,
        true
    );
    vr_apply_haptic(hand, 0.5f, 0.08f, -1.0f);
    VR_INTERACTION_DEBUG("[VR] Physical grip attached to a pole/tree.\n");
    return true;
}

static bool vr_hand_interaction_surface_is_block_or_box(
    struct Surface* surface
) {
    if (surface == NULL) {
        return false;
    }
    struct Object* surfaceObject = surface->object;
    return surfaceObject != NULL &&
        (((surfaceObject->oInteractType & INTERACT_BREAKABLE) != 0) ||
         obj_has_behavior(surfaceObject, bhvBreakableBox) ||
         obj_has_behavior(surfaceObject, bhvExclamationBox) ||
         obj_has_behavior(surfaceObject, bhvHiddenObject) ||
         obj_has_behavior(surfaceObject, bhvPushableMetalBox) ||
         obj_has_behavior(surfaceObject, bhvJumpingBox));
}

static bool vr_hand_interaction_surface_is_climbable_ceiling(
    struct Surface* surface,
    bool allowAnyCeiling
) {
    if (surface == NULL ||
        (surface->flags & SURFACE_FLAG_INTANGIBLE) != 0 ||
        vr_hand_interaction_surface_is_block_or_box(surface)) {
        // Blocks and boxes remain punch/jump targets even with the climb-all
        // cheat enabled. Never turn their temporary collision into a handhold.
        return false;
    }

    return allowAnyCeiling
        ? surface->normal.y <= VR_CLIMB_CEILING_MAX_NORMAL_Y
        : surface->type == SURFACE_HANGABLE;
}

static bool vr_hand_interaction_surface_is_overhead_contact(
    struct MarioState* mario,
    struct Surface* surface,
    f32 contactHeight,
    bool allowAnyCeiling
) {
    if (mario == NULL || !isfinite(contactHeight)) {
        return false;
    }

    if (vr_hand_interaction_surface_is_climbable_ceiling(
            surface,
            allowAnyCeiling
        )) {
        // A ceiling is a handhold only from its underside. In particular,
        // Mario standing on a bridge must not be pulled through its top merely
        // because the engine also exposes a broad hangable ceiling reference.
        return contactHeight >= mario->pos[1] +
            VR_CLIMB_CEILING_MIN_FEET_SEPARATION;
    }

    if (!allowAnyCeiling ||
        surface == NULL ||
        (surface->flags & SURFACE_FLAG_INTANGIBLE) != 0 ||
        surface->normal.y < -VR_CLIMB_CEILING_MAX_NORMAL_Y) {
        return false;
    }

    // Some monkey bars and grates expose only their upward-facing collision
    // triangle, so the engine stores them in the floor partition even though
    // the player approaches them from below. The cheat may treat that solid
    // as a ceiling only when it is well above Mario's feet. This preserves the
    // explicit rule that the actual ground can never become grabbable.
    const f32 minimumOverheadHeight = mario->pos[1] + fmaxf(
        96.0f,
        mario->marioObj != NULL
            ? mario->marioObj->hitboxHeight * 0.55f
            : 96.0f
    );
    return contactHeight >= minimumOverheadHeight;
}

static bool vr_hand_interaction_surface_height_at_xz(
    struct Surface* surface,
    f32 x,
    f32 z,
    f32* height
) {
    if (surface == NULL ||
        height == NULL ||
        fabsf(surface->normal.y) < 0.0001f) {
        return false;
    }

    const f32 x1 = surface->vertex1[0];
    const f32 z1 = surface->vertex1[2];
    const f32 x2 = surface->vertex2[0];
    const f32 z2 = surface->vertex2[2];
    const f32 x3 = surface->vertex3[0];
    const f32 z3 = surface->vertex3[2];
    const f32 edge1 = (x - x2) * (z1 - z2) -
        (x1 - x2) * (z - z2);
    const f32 edge2 = (x - x3) * (z2 - z3) -
        (x2 - x3) * (z - z3);
    const f32 edge3 = (x - x1) * (z3 - z1) -
        (x3 - x1) * (z - z1);
    const bool hasNegative =
        edge1 < -0.5f || edge2 < -0.5f || edge3 < -0.5f;
    const bool hasPositive =
        edge1 > 0.5f || edge2 > 0.5f || edge3 > 0.5f;
    if (hasNegative && hasPositive) {
        return false;
    }

    *height = -(
        surface->normal.x * x +
        surface->normal.z * z +
        surface->originOffset
    ) / surface->normal.y;
    return isfinite(*height);
}

static void vr_hand_interaction_scan_native_hangable_list(
    struct SurfaceNode* node,
    f32 x,
    f32 y,
    f32 z,
    f32 verticalReach,
    struct Surface** nearestSurface,
    f32* nearestHeight,
    f32* nearestDistance
) {
    while (node != NULL) {
        struct Surface* surface = node->surface;
        node = node->next;
        if (surface == NULL ||
            surface->type != SURFACE_HANGABLE ||
            (surface->flags & SURFACE_FLAG_INTANGIBLE) != 0 ||
            y + verticalReach < surface->lowerY ||
            y - verticalReach > surface->upperY) {
            continue;
        }

        f32 height = 0.0f;
        if (!vr_hand_interaction_surface_height_at_xz(
                surface,
                x,
                z,
                &height
            )) {
            continue;
        }

        const f32 distanceBelowSurface = height - y;
        if (distanceBelowSurface >=
                -VR_CLIMB_CEILING_HAND_PLANE_TOLERANCE &&
            distanceBelowSurface <= verticalReach &&
            fabsf(distanceBelowSurface) < *nearestDistance) {
            *nearestSurface = surface;
            *nearestHeight = height;
            *nearestDistance = fabsf(distanceBelowSurface);
        }
    }
}

static bool vr_hand_interaction_find_native_hangable_at_point(
    f32 x,
    f32 y,
    f32 z,
    f32 verticalReach,
    struct Surface** surface,
    f32* height
) {
    if (surface == NULL || height == NULL) {
        return false;
    }

#if EXTENDED_BOUNDS_MODE != 3
    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX ||
        z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return false;
    }
#endif

    const s16 collisionX = (s16)x;
    const s16 collisionZ = (s16)z;
    const s16 cellX = ((collisionX + LEVEL_BOUNDARY_MAX) / CELL_SIZE) &
        NUM_CELLS_INDEX;
    const s16 cellZ = ((collisionZ + LEVEL_BOUNDARY_MAX) / CELL_SIZE) &
        NUM_CELLS_INDEX;
    f32 nearestDistance = verticalReach + 1.0f;
    *surface = NULL;

    // A native hangable can be stored in either floor or ceiling collision
    // according to triangle winding. Inspect every partition directly so an
    // overlapping bridge top or nearby solid cannot hide the hangable face
    // behind find_floor/find_ceil's single nearest-surface result.
    for (s32 partition = SPATIAL_PARTITION_FLOORS;
         partition <= SPATIAL_PARTITION_WALLS;
         partition++) {
        vr_hand_interaction_scan_native_hangable_list(
            gStaticSurfacePartition[cellZ][cellX][partition].next,
            x,
            y,
            z,
            verticalReach,
            surface,
            height,
            &nearestDistance
        );
        vr_hand_interaction_scan_native_hangable_list(
            gDynamicSurfacePartition[cellZ][cellX][partition].next,
            x,
            y,
            z,
            verticalReach,
            surface,
            height,
            &nearestDistance
        );
    }

    return *surface != NULL;
}

static bool vr_hand_interaction_find_native_hangable_contact(
    const Vec3f handPosition,
    const Vec3f previousHandPosition,
    bool previousHandPositionValid,
    f32 verticalReach,
    f32 sampleOffset,
    const f32 sampleDirections[][2],
    u32 sampleDirectionCount,
    struct Surface** ceiling,
    f32* ceilingHeight,
    Vec3f contactPosition
) {
    Vec3f sweep;
    vec3f_set(sweep, 0.0f, 0.0f, 0.0f);
    u32 pathSampleCount = 0;
    if (previousHandPositionValid) {
        for (u32 axis = 0; axis < 3; axis++) {
            sweep[axis] = handPosition[axis] -
                previousHandPosition[axis];
        }
        const f32 sweepLength = vec3f_length(sweep);
        if (isfinite(sweepLength) &&
            sweepLength <= VR_FIST_MAX_SWEEP_DISTANCE) {
            pathSampleCount = VR_CLIMB_NATIVE_SWEEP_SAMPLES;
        }
    }

    for (u32 pathSample = 0;
         pathSample <= pathSampleCount;
         pathSample++) {
        const f32 progress = pathSampleCount > 0
            ? (f32)pathSample / (f32)pathSampleCount
            : 1.0f;
        Vec3f pathPosition;
        if (pathSampleCount > 0) {
            for (u32 axis = 0; axis < 3; axis++) {
                pathPosition[axis] = previousHandPosition[axis] +
                    sweep[axis] * progress;
            }
        } else {
            for (u32 axis = 0; axis < 3; axis++) {
                pathPosition[axis] = handPosition[axis];
            }
        }

        for (u32 sample = 0;
             sample < sampleDirectionCount;
             sample++) {
            const f32 sampleX = pathPosition[0] +
                sampleDirections[sample][0] * sampleOffset;
            const f32 sampleZ = pathPosition[2] +
                sampleDirections[sample][1] * sampleOffset;
            struct Surface* hitSurface = NULL;
            f32 hitHeight = 0.0f;
            if (vr_hand_interaction_find_native_hangable_at_point(
                    sampleX,
                    pathPosition[1],
                    sampleZ,
                    verticalReach,
                    &hitSurface,
                    &hitHeight
                )) {
                *ceiling = hitSurface;
                *ceilingHeight = hitHeight;
                vec3f_set(
                    contactPosition,
                    sampleX,
                    hitHeight,
                    sampleZ
                );
                return true;
            }
        }
    }

    return false;
}

static bool vr_hand_interaction_find_ceiling_contact(
    struct MarioState* mario,
    const Vec3f handPosition,
    const Vec3f previousHandPosition,
    bool previousHandPositionValid,
    bool allowAnyCeiling,
    struct Surface** ceiling,
    f32* ceilingHeight,
    Vec3f contactPosition
) {
    if (ceiling == NULL ||
        ceilingHeight == NULL ||
        contactPosition == NULL) {
        return false;
    }

    *ceiling = NULL;
    const f32 handRadius = vr_hand_interaction_fist_radius();
    const f32 contactHandRadius = fminf(
        handRadius,
        VR_CLIMB_SURFACE_HAND_RADIUS_MAX
    );
    const f32 reach = allowAnyCeiling
        ? contactHandRadius + VR_CLIMB_SURFACE_EXTRA_REACH
        : handRadius + VR_CLIMB_CEILING_EXTRA_REACH;
    const f32 sampleOffset = contactHandRadius * 0.75f;
    static const f32 sampleDirections[9][2] = {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { -1.0f, 0.0f },
        { 0.0f, 1.0f },
        { 0.0f, -1.0f },
        { 0.70710678f, 0.70710678f },
        { -0.70710678f, 0.70710678f },
        { 0.70710678f, -0.70710678f },
        { -0.70710678f, -0.70710678f },
    };
    Vec3f hitPosition;
    struct Surface* hitSurface = NULL;

    // Search native hangables by type before the generic nearest-surface
    // queries. This is the reliable path for bridge undersides, wire netting,
    // monkey bars, and pyramid hangables whose ordinary collision face may
    // otherwise hide the grabbable triangle.
    vr_hand_interaction_find_native_hangable_contact(
        handPosition,
        previousHandPosition,
        previousHandPositionValid,
        reach,
        sampleOffset,
        sampleDirections,
        ARRAY_COUNT(sampleDirections),
        ceiling,
        ceilingHeight,
        contactPosition
    );
    if (*ceiling != NULL &&
        !vr_hand_interaction_surface_is_overhead_contact(
            mario,
            *ceiling,
            *ceilingHeight,
            allowAnyCeiling
        )) {
        *ceiling = NULL;
    }

    // Sample the glove's footprint rather than a single thin ray. Besides
    // catching triangle seams, find_ceil directly queries the collision cell
    // and is considerably cheaper than marching a long vertical ray in tiny
    // increments every gameplay tick.
    for (u32 sample = 0;
         *ceiling == NULL && sample < ARRAY_COUNT(sampleDirections);
         sample++) {
        const f32 sampleX = handPosition[0] +
            sampleDirections[sample][0] * sampleOffset;
        const f32 sampleZ = handPosition[2] +
            sampleDirections[sample][1] * sampleOffset;
        hitSurface = NULL;
        const f32 hitHeight = find_ceil(
            sampleX,
            handPosition[1] - reach,
            sampleZ,
            &hitSurface
        );
        if (hitHeight <= handPosition[1] + reach &&
            vr_hand_interaction_surface_is_overhead_contact(
                mario,
                hitSurface,
                hitHeight,
                allowAnyCeiling
            )) {
            *ceiling = hitSurface;
            *ceilingHeight = hitHeight;
            vec3f_set(contactPosition, sampleX, hitHeight, sampleZ);
            break;
        }

        // A bar may be modeled with only an upward-facing top triangle. Such
        // geometry is invisible to find_ceil, so query the floor partition
        // from just above the glove and accept only native hangables or a
        // cheat-enabled solid that is genuinely overhead.
        hitSurface = NULL;
        const f32 upwardFacingHeight = find_floor(
            sampleX,
            handPosition[1] + reach,
            sampleZ,
            &hitSurface
        );
        if (upwardFacingHeight >= handPosition[1] - reach &&
            upwardFacingHeight <= handPosition[1] + reach &&
            vr_hand_interaction_surface_is_overhead_contact(
                mario,
                hitSurface,
                upwardFacingHeight,
                allowAnyCeiling
            )) {
            *ceiling = hitSurface;
            *ceilingHeight = upwardFacingHeight;
            vec3f_set(
                contactPosition,
                sampleX,
                upwardFacingHeight,
                sampleZ
            );
            break;
        }
    }

    // A fast jump can cover more than the local reach span in one gameplay
    // tick. Sweep the complete tracked path so a held grip cannot tunnel
    // through a thin monkey-bar or ceiling triangle.
    if (*ceiling == NULL && previousHandPositionValid) {
        Vec3f sweepDirection;
        for (u32 axis = 0; axis < 3; axis++) {
            sweepDirection[axis] = handPosition[axis] -
                previousHandPosition[axis];
        }
        const f32 sweepLength = vec3f_length(sweepDirection);
        if (isfinite(sweepLength) &&
            sweepLength > 0.001f &&
            sweepLength <= VR_FIST_MAX_SWEEP_DISTANCE) {
            hitSurface = NULL;
            find_surface_on_ray(
                (Vec3f) {
                    previousHandPosition[0],
                    previousHandPosition[1],
                    previousHandPosition[2]
                },
                sweepDirection,
                &hitSurface,
                hitPosition,
                3.0f
            );
            if (vr_hand_interaction_surface_is_overhead_contact(
                    mario,
                    hitSurface,
                    hitPosition[1],
                    allowAnyCeiling
                )) {
                *ceiling = hitSurface;
                *ceilingHeight = hitPosition[1];
                vec3f_copy(contactPosition, hitPosition);
            }
        }
    }

    // Retain the native geometry fallback for triangle seams, but only while
    // the tracked path is still reasonably close to that ceiling.
    const f32 nativeFallbackReach = reach * 1.5f;
    f32 fallbackHeight = 0.0f;
    if (*ceiling == NULL &&
        mario != NULL &&
        vr_hand_interaction_surface_is_climbable_ceiling(
            mario->ceil,
            allowAnyCeiling
        ) &&
        vr_hand_interaction_surface_height_at_xz(
            mario->ceil,
            handPosition[0],
            handPosition[2],
            &fallbackHeight
        ) &&
        fallbackHeight - handPosition[1] >=
            -VR_CLIMB_CEILING_HAND_PLANE_TOLERANCE &&
        fallbackHeight - handPosition[1] <= nativeFallbackReach &&
        vr_hand_interaction_surface_is_overhead_contact(
            mario,
            mario->ceil,
            fallbackHeight,
            allowAnyCeiling
        )) {
        *ceiling = mario->ceil;
        *ceilingHeight = fallbackHeight;
        vec3f_set(
            contactPosition,
            handPosition[0],
            fallbackHeight,
            handPosition[2]
        );
    }

    if (*ceiling == NULL) {
        return false;
    }

    struct Surface* floor = NULL;
    const f32 floorHeight = find_floor(
        contactPosition[0],
        *ceilingHeight - 160.0f - VR_CLIMB_SURFACE_CLEARANCE,
        contactPosition[2],
        &floor
    );
    if (floor != NULL && *ceilingHeight - floorHeight <= 160.0f) {
        *ceiling = NULL;
        return false;
    }

    return true;
}

static bool vr_hand_interaction_find_vertical_wall_contact(
    const Vec3f handPosition,
    const Vec3f previousHandPosition,
    bool previousHandPositionValid,
    struct Surface** wall,
    Vec3f contactPosition
) {
    if (wall == NULL || contactPosition == NULL) {
        return false;
    }

    *wall = NULL;
    const f32 reach = fminf(
        vr_hand_interaction_fist_radius(),
        VR_CLIMB_SURFACE_HAND_RADIUS_MAX
    ) + VR_CLIMB_SURFACE_EXTRA_REACH;
    struct WallCollisionData collisionData = {
        .x = handPosition[0],
        .y = handPosition[1],
        .z = handPosition[2],
        .offsetY = 0.0f,
        .radius = reach,
    };
    find_wall_collisions(&collisionData);

    f32 nearestDistance = reach + 1.0f;
    for (s32 index = 0; index < collisionData.numWalls; index++) {
        struct Surface* candidate = collisionData.walls[index];
        if (candidate == NULL ||
            (candidate->flags & SURFACE_FLAG_INTANGIBLE) != 0 ||
            vr_hand_interaction_surface_is_block_or_box(candidate) ||
            fabsf(candidate->normal.y) > VR_CLIMB_WALL_MAX_NORMAL_Y) {
            continue;
        }

        const f32 signedDistance =
            candidate->normal.x * handPosition[0] +
            candidate->normal.y * handPosition[1] +
            candidate->normal.z * handPosition[2] +
            candidate->originOffset;
        const f32 distance = fabsf(signedDistance);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            *wall = candidate;
            contactPosition[0] = handPosition[0] -
                candidate->normal.x * signedDistance;
            contactPosition[1] = handPosition[1] -
                candidate->normal.y * signedDistance;
            contactPosition[2] = handPosition[2] -
                candidate->normal.z * signedDistance;
        }
    }

    if (*wall == NULL && previousHandPositionValid) {
        Vec3f sweepDirection;
        Vec3f hitPosition;
        struct Surface* hitSurface = NULL;
        for (u32 axis = 0; axis < 3; axis++) {
            sweepDirection[axis] = handPosition[axis] -
                previousHandPosition[axis];
        }
        const f32 sweepLength = vec3f_length(sweepDirection);
        if (isfinite(sweepLength) &&
            sweepLength > 0.001f &&
            sweepLength <= VR_FIST_MAX_SWEEP_DISTANCE) {
            find_surface_on_ray(
                (Vec3f) {
                    previousHandPosition[0],
                    previousHandPosition[1],
                    previousHandPosition[2]
                },
                sweepDirection,
                &hitSurface,
                hitPosition,
                3.0f
            );
            if (hitSurface != NULL &&
                (hitSurface->flags & SURFACE_FLAG_INTANGIBLE) == 0 &&
                !vr_hand_interaction_surface_is_block_or_box(
                    hitSurface
                ) &&
                fabsf(hitSurface->normal.y) <=
                    VR_CLIMB_WALL_MAX_NORMAL_Y) {
                *wall = hitSurface;
                vec3f_copy(contactPosition, hitPosition);
            }
        }
    }

    return *wall != NULL;
}

static void vr_hand_interaction_set_surface_constraint(
    struct Surface* surface,
    const Vec3f contactPosition,
    const Vec3f playerReferencePosition
) {
    sVrPhysicalClimbSurface = surface;
    for (u32 axis = 0; axis < 3; axis++) {
        sVrPhysicalClimbSurfacePoint[axis] = contactPosition[axis];
    }
    vec3f_set(
        sVrPhysicalClimbSurfaceNormal,
        surface->normal.x,
        surface->normal.y,
        surface->normal.z
    );

    Vec3f playerSide;
    for (u32 axis = 0; axis < 3; axis++) {
        playerSide[axis] = playerReferencePosition[axis] -
            contactPosition[axis];
    }
    // Ceiling normals already point toward the playable space below them.
    // Only wall normals need to be oriented to the side Mario approached.
    if (surface->normal.y > VR_CLIMB_CEILING_MAX_NORMAL_Y &&
        vec3f_dot(sVrPhysicalClimbSurfaceNormal, playerSide) < 0.0f) {
        vec3f_mul(sVrPhysicalClimbSurfaceNormal, -1.0f);
    }
}

static bool vr_hand_interaction_start_ceiling_climb(
    struct MarioState* mario,
    struct Surface* ceiling,
    f32 ceilingHeight,
    const Vec3f ceilingContactPosition,
    bool cheatCeiling,
    u32 hand,
    const Vec3f climbPosition
) {
    if (mario == NULL ||
        mario->marioObj == NULL ||
        ceiling == NULL ||
        (!cheatCeiling && ceiling->type != SURFACE_HANGABLE) ||
        hand >= VR_CONTROLLER_COUNT) {
        return false;
    }

    Vec3f previousPosition;
    vec3f_copy(previousPosition, mario->pos);
    mario_stop_riding_and_holding(mario);
    mario->ceil = ceiling;
    mario->ceilHeight = ceilingHeight;
    if (!cheatCeiling) {
        mario->pos[1] = ceilingHeight - 160.0f;
    }
    vec3f_set(mario->vel, 0.0f, 0.0f, 0.0f);
    mario->forwardVel = 0.0f;
    vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
    if (!set_mario_action(mario, ACT_HANGING, 0)) {
        vec3f_copy(mario->pos, previousPosition);
        vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
        return false;
    }

    sVrPhysicalClimbType = cheatCeiling
        ? VR_PHYSICAL_CLIMB_CHEAT_CEILING
        : VR_PHYSICAL_CLIMB_CEILING;
    sVrPhysicalClimbPole = NULL;
    Vec3f playerReferencePosition;
    vec3f_copy(playerReferencePosition, previousPosition);
    playerReferencePosition[1] +=
        fmaxf(mario->marioObj->hitboxHeight, 160.0f) * 0.5f;
    vr_hand_interaction_set_surface_constraint(
        ceiling,
        ceilingContactPosition,
        playerReferencePosition
    );
    for (u32 axis = 0; axis < 3; axis++) {
        sVrPhysicalClimbCameraOffset[axis] =
            previousPosition[axis] - mario->pos[axis];
    }
    vec3f_copy(
        sVrPhysicalClimbCameraOffsetPrev,
        sVrPhysicalClimbCameraOffset
    );
    vec3f_copy(sVrPhysicalClimbSafeReleasePosition, previousPosition);
    sVrPhysicalClimbSafeReleasePositionValid = true;
    sVrPhysicalClimbSafeReleaseTimestamp = gGlobalTimer;
    sVrPhysicalClimbOffsetTimestamp = gGlobalTimer;
    vr_hand_interaction_set_physical_climb_owner(
        hand,
        climbPosition
    );
    vr_hand_interaction_sync_climb_collider_to_headset(mario);
    mario->input |= INPUT_A_DOWN;
    vr_apply_haptic(hand, 0.5f, 0.08f, -1.0f);
    VR_INTERACTION_DEBUG("[VR] Physical grip attached to a hangable ceiling.\n");
    return true;
}

static bool vr_hand_interaction_start_wall_climb(
    struct MarioState* mario,
    struct Surface* wall,
    const Vec3f wallContactPosition,
    u32 hand,
    const Vec3f climbPosition
) {
    if (mario == NULL ||
        mario->marioObj == NULL ||
        wall == NULL ||
        fabsf(wall->normal.y) > VR_CLIMB_WALL_MAX_NORMAL_Y ||
        hand >= VR_CONTROLLER_COUNT) {
        return false;
    }

    Vec3f previousPosition;
    Vec3f playerReferencePosition;
    vec3f_copy(previousPosition, mario->pos);
    vec3f_copy(playerReferencePosition, previousPosition);
    playerReferencePosition[1] +=
        fmaxf(mario->marioObj->hitboxHeight, 160.0f) * 0.5f;
    mario_stop_riding_and_holding(mario);
    mario->ceil = wall;
    mario->ceilHeight = wallContactPosition[1] + 160.0f;
    vec3f_set(mario->vel, 0.0f, 0.0f, 0.0f);
    mario->forwardVel = 0.0f;
    vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
    if (!set_mario_action(mario, ACT_HANGING, 0)) {
        return false;
    }

    sVrPhysicalClimbType = VR_PHYSICAL_CLIMB_CHEAT_WALL;
    sVrPhysicalClimbPole = NULL;
    vr_hand_interaction_set_surface_constraint(
        wall,
        wallContactPosition,
        playerReferencePosition
    );
    vec3f_set(sVrPhysicalClimbCameraOffset, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrPhysicalClimbCameraOffsetPrev, 0.0f, 0.0f, 0.0f);
    vec3f_copy(sVrPhysicalClimbSafeReleasePosition, previousPosition);
    sVrPhysicalClimbSafeReleasePositionValid = true;
    sVrPhysicalClimbSafeReleaseTimestamp = gGlobalTimer;
    sVrPhysicalClimbOffsetTimestamp = gGlobalTimer;
    vr_hand_interaction_set_physical_climb_owner(
        hand,
        climbPosition
    );
    vr_hand_interaction_sync_climb_collider_to_headset(mario);
    mario->input |= INPUT_A_DOWN;
    vr_apply_haptic(hand, 0.5f, 0.08f, -1.0f);
    VR_INTERACTION_DEBUG("[VR] Cheat climb attached to a vertical wall.\n");
    return true;
}

static bool vr_hand_interaction_try_physical_climb(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition,
    const Vec3f previousHandPosition,
    bool previousHandPositionValid,
    const Vec3f climbPosition,
    bool allowCheatContact
) {
    if (!configVrPhysicalClimbing ||
        mario == NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        vr_hand_interaction_climb_is_occupied()) {
        return false;
    }

    struct Object* pole = vr_hand_interaction_find_pole_target(
        mario,
        handPosition
    );
    if (pole != NULL) {
        return vr_hand_interaction_start_pole_climb(
            mario,
            pole,
            hand,
            climbPosition
        );
    }

    if (mario->heldObj != NULL) {
        return false;
    }

    struct Surface* ceiling = NULL;
    f32 ceilingHeight = 0.0f;
    Vec3f ceilingContactPosition;
    const bool allowCheatSurface =
        configVrCheatSurfaceClimbing && allowCheatContact;
    if (vr_hand_interaction_find_ceiling_contact(
            mario,
            handPosition,
            previousHandPosition,
            previousHandPositionValid,
            allowCheatSurface,
            &ceiling,
            &ceilingHeight,
            ceilingContactPosition
        )) {
        const bool nativeHangable =
            ceiling->type == SURFACE_HANGABLE;
        if (!nativeHangable && !allowCheatSurface) {
            return false;
        }
        const bool cheatCeiling = !nativeHangable;
        return vr_hand_interaction_start_ceiling_climb(
            mario,
            ceiling,
            ceilingHeight,
            ceilingContactPosition,
            cheatCeiling,
            hand,
            climbPosition
        );
    }

    if (!allowCheatSurface) {
        return false;
    }

    struct Surface* wall = NULL;
    Vec3f wallContactPosition;
    if (vr_hand_interaction_find_vertical_wall_contact(
            handPosition,
            previousHandPosition,
            previousHandPositionValid,
            &wall,
            wallContactPosition
        )) {
        return vr_hand_interaction_start_wall_climb(
            mario,
            wall,
            wallContactPosition,
            hand,
            climbPosition
        );
    }

    return false;
}

static bool vr_hand_interaction_try_add_physical_climb_hand(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition,
    const Vec3f climbPosition,
    bool allowCheatContact
) {
    if (!configVrPhysicalClimbing ||
        mario == NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE ||
        sVrPhysicalClimbHands[hand]) {
        return false;
    }

    if (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE) {
        f32 distanceSquared;
        if (sVrPhysicalClimbPole == NULL ||
            (sVrPhysicalClimbPole->activeFlags &
                ACTIVE_FLAG_ACTIVE) == 0 ||
            !vr_hand_interaction_grab_overlaps_object(
                handPosition,
                vr_hand_interaction_fist_radius(),
                sVrPhysicalClimbPole,
                &distanceSquared
            )) {
            return false;
        }
    } else if (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_CEILING ||
               sVrPhysicalClimbType ==
                   VR_PHYSICAL_CLIMB_CHEAT_CEILING) {
        struct Surface* ceiling = NULL;
        f32 ceilingHeight = 0.0f;
        Vec3f ceilingContactPosition;
        const bool cheatCeiling =
            sVrPhysicalClimbType ==
                VR_PHYSICAL_CLIMB_CHEAT_CEILING;
        if (cheatCeiling && !allowCheatContact) {
            return false;
        }
        if (!vr_hand_interaction_find_ceiling_contact(
                mario,
                handPosition,
                sVrClimbPreviousPosition[hand],
                sVrClimbPreviousPositionValid[hand],
                cheatCeiling,
                &ceiling,
                &ceilingHeight,
                ceilingContactPosition
            )) {
            return false;
        }

        // A new monkey-bar contact can lie on an adjacent ceiling triangle.
        // Update the collision reference without moving Mario's native anchor;
        // tracked hand deltas alone own camera translation during the hold.
        mario->ceil = ceiling;
        mario->ceilHeight = ceilingHeight;
        Vec3f playerReferencePosition;
        vec3f_copy(playerReferencePosition, mario->pos);
        playerReferencePosition[1] +=
            fmaxf(mario->marioObj->hitboxHeight, 160.0f) * 0.5f;
        vr_hand_interaction_set_surface_constraint(
            ceiling,
            ceilingContactPosition,
            playerReferencePosition
        );
    } else if (sVrPhysicalClimbType ==
               VR_PHYSICAL_CLIMB_CHEAT_WALL) {
        if (!allowCheatContact) {
            return false;
        }
        struct Surface* wall = NULL;
        Vec3f wallContactPosition;
        if (!vr_hand_interaction_find_vertical_wall_contact(
                handPosition,
                sVrClimbPreviousPosition[hand],
                sVrClimbPreviousPositionValid[hand],
                &wall,
                wallContactPosition
            )) {
            return false;
        }

        Vec3f playerReferencePosition;
        vec3f_copy(playerReferencePosition, mario->pos);
        playerReferencePosition[1] +=
            fmaxf(mario->marioObj->hitboxHeight, 160.0f) * 0.5f;
        vr_hand_interaction_set_surface_constraint(
            wall,
            wallContactPosition,
            playerReferencePosition
        );
        mario->ceil = wall;
        mario->ceilHeight = wallContactPosition[1] + 160.0f;
    } else {
        return false;
    }

    // The newest contact becomes the active anchor. The older hand remains
    // attached, so releasing either controller transfers cleanly rather than
    // dropping Mario or starting the native hand-over-hand animation.
    vr_hand_interaction_set_physical_climb_owner(
        hand,
        climbPosition
    );
    vr_apply_haptic(hand, 0.4f, 0.06f, -1.0f);
    VR_INTERACTION_DEBUG("[VR] Physical climb anchor changed hands.\n");
    return true;
}

static bool vr_hand_interaction_physical_climb_matches_action(
    struct MarioState* mario
) {
    if (mario == NULL) {
        return false;
    }

    if (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE) {
        return (mario->action & ACT_FLAG_ON_POLE) != 0 &&
            mario->usedObj == sVrPhysicalClimbPole;
    }
    if (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_CEILING ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_CHEAT_CEILING ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_CHEAT_WALL) {
        return (mario->action & ACT_FLAG_HANGING) != 0;
    }
    return false;
}

static bool vr_hand_interaction_resolve_safe_release_position(
    Vec3f position
) {
    if (position == NULL) {
        return false;
    }

    struct WallCollisionData collisionData = { 0 };
    resolve_and_return_wall_collisions_data(
        position,
        60.0f,
        50.0f,
        &collisionData
    );
    resolve_and_return_wall_collisions_data(
        position,
        30.0f,
        24.0f,
        &collisionData
    );

    struct Surface* floor = NULL;
    struct Surface* ceiling = NULL;
    const f32 floorHeight = find_floor(
        position[0],
        position[1] + 160.0f,
        position[2],
        &floor
    );
    const f32 ceilingHeight = find_ceil(
        position[0],
        position[1],
        position[2],
        &ceiling
    );

    if (floor != NULL &&
        ceiling != NULL &&
        ceilingHeight - floorHeight < 160.0f) {
        return false;
    }
    if (floor != NULL && position[1] < floorHeight) {
        position[1] = floorHeight;
    }
    if (ceiling != NULL && position[1] + 160.0f > ceilingHeight) {
        position[1] = ceilingHeight - 160.0f;
    }
    if (floor != NULL && position[1] < floorHeight) {
        return false;
    }

    return isfinite(position[0]) &&
        isfinite(position[1]) &&
        isfinite(position[2]);
}

static void vr_hand_interaction_sync_climb_collider_to_headset(
    struct MarioState* mario
) {
    if (mario == NULL ||
        mario->marioObj == NULL ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE ||
        sVrPhysicalClimbHand >= VR_CONTROLLER_COUNT) {
        return;
    }

    Vec3f headsetPosition;
    if (!vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        return;
    }

    // Keep the view on the same side of the grabbed wall/ceiling. Only the
    // temporary climb offset is corrected here, so the HMD never crosses the
    // contact plane even if a controller sample overshoots it.
    if (sVrPhysicalClimbSurface != NULL) {
        Vec3f fromSurface;
        vec3f_dif(
            fromSurface,
            headsetPosition,
            sVrPhysicalClimbSurfacePoint
        );
        const f32 surfaceDistance = vec3f_dot(
            sVrPhysicalClimbSurfaceNormal,
            fromSurface
        );
        Vec3f projectedPosition;
        Vec3f closestSurfacePoint;
        for (u32 axis = 0; axis < 3; axis++) {
            projectedPosition[axis] = headsetPosition[axis] -
                sVrPhysicalClimbSurfaceNormal[axis] *
                    surfaceDistance;
        }
        closest_point_to_triangle(
            sVrPhysicalClimbSurface,
            projectedPosition,
            closestSurfacePoint
        );
        const f32 edgeDeltaX = projectedPosition[0] -
            closestSurfacePoint[0];
        const f32 edgeDeltaY = projectedPosition[1] -
            closestSurfacePoint[1];
        const f32 edgeDeltaZ = projectedPosition[2] -
            closestSurfacePoint[2];
        const f32 edgeDistanceSquared =
            edgeDeltaX * edgeDeltaX +
            edgeDeltaY * edgeDeltaY +
            edgeDeltaZ * edgeDeltaZ;
        const bool besideGrabbedTriangle =
            isfinite(edgeDistanceSquared) &&
            edgeDistanceSquared <=
                VR_CLIMB_SURFACE_EDGE_MARGIN *
                VR_CLIMB_SURFACE_EDGE_MARGIN;

        if (besideGrabbedTriangle &&
            isfinite(surfaceDistance) &&
            surfaceDistance < VR_CLIMB_SURFACE_CLEARANCE) {
            const f32 correction =
                VR_CLIMB_SURFACE_CLEARANCE - surfaceDistance;
            vr_hand_interaction_prepare_climb_offset_update();
            for (u32 axis = 0; axis < 3; axis++) {
                const f32 axisCorrection =
                    sVrPhysicalClimbSurfaceNormal[axis] * correction;
                sVrPhysicalClimbCameraOffset[axis] += axisCorrection;
                headsetPosition[axis] += axisCorrection;
            }
        }
    }

    // Keep the native pole/hanging action anchor fixed. Repositioning
    // mario->pos and then compensating the camera offset created a second
    // feedback loop with floor/wall resolution, visible as bridge jitter.
    // Object interactions only need Mario's interaction object at the HMD;
    // the accumulated camera offset is committed to mario->pos once on release.
    vr_hand_interaction_apply_headset_collider(
        mario,
        headsetPosition
    );

    // This collision-safe fallback is only consumed when a climb ends, where
    // it is resolved once more using the current position. Refreshing it a few
    // times per gameplay second is enough to retain a nearby fallback without
    // repeating several wall/floor/ceiling queries on every climbing tick.
    if (!sVrPhysicalClimbSafeReleasePositionValid ||
        (u32)(gGlobalTimer - sVrPhysicalClimbSafeReleaseTimestamp) >=
            VR_CLIMB_SAFE_RELEASE_SAMPLE_FRAMES) {
        Vec3f releasePosition;
        sVrPhysicalClimbSafeReleaseTimestamp = gGlobalTimer;
        for (u32 axis = 0; axis < 3; axis++) {
            releasePosition[axis] = mario->pos[axis] +
                sVrPhysicalClimbCameraOffset[axis];
        }
        if (vr_hand_interaction_resolve_safe_release_position(
                releasePosition
            )) {
            vec3f_copy(
                sVrPhysicalClimbSafeReleasePosition,
                releasePosition
            );
            sVrPhysicalClimbSafeReleasePositionValid = true;
        }
    }

    vr_invalidate_first_person_tracked_world_cache();
}

static void vr_hand_interaction_commit_physical_climb_offset(
    struct MarioState* mario
) {
    Vec3f offset;
    Vec3f safeReleasePosition;
    const bool safeReleasePositionValid =
        sVrPhysicalClimbSafeReleasePositionValid;
    vec3f_copy(offset, sVrPhysicalClimbCameraOffset);
    vec3f_copy(
        safeReleasePosition,
        sVrPhysicalClimbSafeReleasePosition
    );
    vr_hand_interaction_clear_physical_climb();

    if (mario != NULL && mario->marioObj != NULL) {
        for (u32 axis = 0; axis < 3; axis++) {
            mario->pos[axis] += offset[axis];
        }
        if (!vr_hand_interaction_resolve_safe_release_position(
                mario->pos
            ) && safeReleasePositionValid) {
            vec3f_copy(mario->pos, safeReleasePosition);
        }
        vec3f_copy(&mario->marioObj->oPosX, mario->pos);
        vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
    }
    sVrPhysicalClimbRegrabFrames =
        VR_CLIMB_REGRAB_COOLDOWN_FRAMES;
    vr_invalidate_first_person_tracked_world_cache();
}

static bool vr_hand_interaction_find_ledge_release(
    struct MarioState* mario,
    Vec3f releasePosition,
    struct Surface** releaseFloor
) {
    if (mario == NULL ||
        releasePosition == NULL ||
        releaseFloor == NULL ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE) {
        return false;
    }

    Vec3f headsetPosition;
    if (!vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        return false;
    }

    struct Surface* floor = NULL;
    const f32 floorHeight = find_floor(
        headsetPosition[0],
        headsetPosition[1] +
            VR_CLIMB_LEDGE_MAX_FLOOR_ABOVE_HEAD,
        headsetPosition[2],
        &floor
    );
    if (floor == NULL ||
        floor->normal.y < 0.5f ||
        floorHeight < mario->floorHeight +
            VR_CLIMB_LEDGE_MIN_RISE ||
        floorHeight < headsetPosition[1] -
            VR_CLIMB_LEDGE_MAX_HEAD_ABOVE_FLOOR ||
        floorHeight > headsetPosition[1] +
            VR_CLIMB_LEDGE_MAX_FLOOR_ABOVE_HEAD) {
        return false;
    }

    vec3f_set(
        releasePosition,
        headsetPosition[0],
        floorHeight,
        headsetPosition[2]
    );
    if (!vr_hand_interaction_resolve_safe_release_position(
            releasePosition
        )) {
        return false;
    }

    struct Surface* resolvedFloor = NULL;
    const f32 resolvedFloorHeight = find_floor(
        releasePosition[0],
        releasePosition[1] + 160.0f,
        releasePosition[2],
        &resolvedFloor
    );
    if (resolvedFloor == NULL ||
        resolvedFloor->normal.y < 0.5f ||
        fabsf(resolvedFloorHeight - releasePosition[1]) > 16.0f) {
        return false;
    }

    releasePosition[1] = resolvedFloorHeight;
    *releaseFloor = resolvedFloor;
    return true;
}

static void vr_hand_interaction_release_physical_climb(
    struct MarioState* mario,
    const Vec3f releaseVelocity,
    bool allowSwingRelease
) {
    const u32 hand = sVrPhysicalClimbHand;
    const bool actionMatches =
        vr_hand_interaction_physical_climb_matches_action(mario);
    bool swingRelease = false;
    if (allowSwingRelease &&
        configVrSwingClimbRelease &&
        releaseVelocity != NULL) {
        const f32 speed = sqrtf(
            releaseVelocity[0] * releaseVelocity[0] +
            releaseVelocity[1] * releaseVelocity[1] +
            releaseVelocity[2] * releaseVelocity[2]
        );
        swingRelease = isfinite(speed) &&
            speed >= VR_CLIMB_SWING_RELEASE_MIN_SPEED;
    }
    Vec3f ledgeReleasePosition;
    struct Surface* ledgeReleaseFloor = NULL;
    const bool ledgeRelease =
        actionMatches &&
        !swingRelease &&
        vr_hand_interaction_find_ledge_release(
            mario,
            ledgeReleasePosition,
            &ledgeReleaseFloor
        );
    vr_hand_interaction_commit_physical_climb_offset(mario);

    if (actionMatches) {
        mario->usedObj = NULL;
        if (ledgeRelease) {
            const s16 viewYaw = vr_get_first_person_view_yaw();
            vec3f_copy(mario->pos, ledgeReleasePosition);
            vec3f_set(mario->vel, 0.0f, 0.0f, 0.0f);
            mario->forwardVel = 0.0f;
            mario->floor = ledgeReleaseFloor;
            mario->floorHeight = ledgeReleasePosition[1];
            mario->faceAngle[0] = 0;
            mario->faceAngle[1] = viewYaw;
            mario->faceAngle[2] = 0;
            mario->intendedYaw = viewYaw;
            vec3f_copy(&mario->marioObj->oPosX, mario->pos);
            vec3f_copy(
                mario->marioObj->header.gfx.pos,
                mario->pos
            );
            set_mario_action(mario, ACT_IDLE, 0);
        } else if (swingRelease) {
            const s16 viewYaw = vr_get_first_person_view_yaw();
            mario->faceAngle[0] = 0;
            mario->faceAngle[1] = viewYaw;
            mario->faceAngle[2] = 0;
            mario->intendedYaw = viewYaw;
            set_mario_action(mario, ACT_WALL_KICK_AIR, 0);
        } else {
            // Simply opening the last attached hand is always a clean fall;
            // only a sufficiently fast tracked release becomes a launch.
            set_mario_action(mario, ACT_FREEFALL, 0);
        }
    }
    if (hand < VR_CONTROLLER_COUNT) {
        vr_apply_haptic(hand, 0.15f, 0.04f, -1.0f);
    }
    VR_INTERACTION_DEBUG("[VR] Physical climb grip released.\n");
}

static void vr_hand_interaction_maintain_physical_climb(
    struct MarioState* mario,
    u32 hand,
    bool climbPositionValid,
    const Vec3f climbPosition,
    const Vec3f handVelocity
) {
    if (mario == NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        !sVrPhysicalClimbHands[hand]) {
        return;
    }

    if (climbPositionValid) {
        for (u32 axis = 0; axis < 3; axis++) {
            sVrPhysicalClimbLastPosition[hand][axis] =
                climbPosition[axis];
        }
        sVrPhysicalClimbLastPositionValid[hand] = true;
    }

    if (!climbPositionValid ||
        !sVrGripPressed[hand] ||
        !configVrPhysicalClimbing ||
        ((sVrPhysicalClimbType ==
              VR_PHYSICAL_CLIMB_CHEAT_CEILING ||
          sVrPhysicalClimbType ==
              VR_PHYSICAL_CLIMB_CHEAT_WALL) &&
         !configVrCheatSurfaceClimbing) ||
        (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE &&
         (sVrPhysicalClimbPole == NULL ||
          (sVrPhysicalClimbPole->activeFlags &
              ACTIVE_FLAG_ACTIVE) == 0))) {
        sVrPhysicalClimbHands[hand] = false;
        sVrPhysicalClimbLastPositionValid[hand] = false;
        if (hand != sVrPhysicalClimbHand) {
            return;
        }

        for (u32 otherHand = 0;
             otherHand < VR_CONTROLLER_COUNT;
             otherHand++) {
            if (sVrPhysicalClimbHands[otherHand] &&
                sVrGripPressed[otherHand] &&
                sVrPhysicalClimbLastPositionValid[otherHand]) {
                vr_hand_interaction_set_physical_climb_owner(
                    otherHand,
                    sVrPhysicalClimbLastPosition[otherHand]
                );
                return;
            }
        }

        vr_hand_interaction_release_physical_climb(
            mario,
            climbPositionValid ? handVelocity : NULL,
            climbPositionValid && !sVrGripPressed[hand]
        );
        return;
    }

    if (!vr_hand_interaction_physical_climb_matches_action(mario)) {
        vr_hand_interaction_commit_physical_climb_offset(mario);
        return;
    }

    if (sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_POLE &&
        (mario->input & INPUT_A_PRESSED) != 0) {
        const s16 viewYaw = vr_get_first_person_view_yaw();
        vr_hand_interaction_commit_physical_climb_offset(mario);
        mario->faceAngle[0] = 0;
        mario->faceAngle[1] = viewYaw;
        mario->faceAngle[2] = 0;
        mario->intendedYaw = viewYaw;
        set_mario_action(mario, ACT_WALL_KICK_AIR, 0);
        vr_apply_haptic(hand, 0.3f, 0.05f, -1.0f);
        return;
    }

    if (hand == sVrPhysicalClimbHand) {
        vr_hand_interaction_prepare_climb_offset_update();
        for (u32 axis = 0; axis < 3; axis++) {
            sVrPhysicalClimbCameraOffset[axis] +=
                sVrPhysicalClimbContactPosition[axis] -
                climbPosition[axis];
            // Consume this tracked delta once. Leaving the original contact
            // in place caused the complete displacement to be accumulated
            // again every tick, producing the post/tree position drift.
            sVrPhysicalClimbContactPosition[axis] =
                climbPosition[axis];
        }

        if (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_POLE &&
            mario->controller != NULL &&
            mario->controller->stickY < -16.0f) {
            const f32 slideInput = clamp(
                (-mario->controller->stickY - 16.0f) / 48.0f,
                0.0f,
                1.0f
            );
            const f32 poleBottom = sVrPhysicalClimbPole != NULL
                ? sVrPhysicalClimbPole->oPosY -
                    sVrPhysicalClimbPole->hitboxDownOffset
                : mario->floorHeight;
            const f32 minimumHeight = fmaxf(
                mario->floorHeight,
                poleBottom
            );
            const f32 availableDrop = fmaxf(
                0.0f,
                mario->pos[1] - minimumHeight
            );
            sVrPhysicalClimbCameraOffset[1] -= fminf(
                availableDrop,
                slideInput * VR_PHYSICAL_POLE_SLIDE_MAX_SPEED
            );
        }
        vr_invalidate_first_person_tracked_world_cache();
    }

    if (hand == sVrPhysicalClimbHand) {
        vr_hand_interaction_sync_climb_collider_to_headset(mario);
    }

    // The native climbable remains the gameplay anchor while room-scale HMD
    // and hand tracking can render the torso away from it. Suppress stick
    // locomotion so a closed physical grip behaves as a stationary,
    // weightless one-hand hold. Jump input is deliberately preserved.
    mario->input &= ~INPUT_NONZERO_ANALOG;
    mario->input |= INPUT_ZERO_MOVEMENT;
    if (mario->controller != NULL) {
        mario->controller->stickX = 0.0f;
        mario->controller->stickY = 0.0f;
        mario->controller->stickMag = 0.0f;
    }
    if (sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_POLE) {
        mario->input |= INPUT_A_DOWN;
    }
}

static bool vr_hand_interaction_object_can_be_anchored(
    struct MarioState* mario,
    struct Object* object
) {
    if (mario == NULL ||
        object == NULL ||
        object == mario->marioObj ||
        mario->heldObj != NULL ||
        sVrTrackedHeldObject != NULL ||
        sVrTrackedHootObject != NULL ||
        sVrTrackedAnchorObject != NULL ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
        object->oIntangibleTimer != 0 ||
        (object->oInteractType & INTERACT_GRABBABLE) != 0 ||
        (object->oInteractType &
            VR_ANCHORABLE_INTERACT_TYPES) == 0) {
        return false;
    }

    // INTERACT_TEXT is shared by NPCs and signs. Only animated text actors
    // count as people/NPC anchor targets; this keeps static signs out.
    if ((object->oInteractType & INTERACT_TEXT) != 0 &&
        object->oAnimations == NULL) {
        return false;
    }

    return true;
}

static struct Object* vr_hand_interaction_find_anchor_target(
    struct MarioState* mario,
    const Vec3f handPosition
) {
    if (gObjectLists == NULL) {
        return NULL;
    }

    const f32 handRadius = vr_hand_interaction_fist_radius();
    struct Object* nearestObject = NULL;
    f32 nearestDistanceSquared = 0.0f;

    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;

        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;

            f32 distanceSquared;
            if (!vr_hand_interaction_object_can_be_anchored(
                    mario,
                    object
                ) ||
                !vr_hand_interaction_grab_overlaps_object(
                    handPosition,
                    handRadius,
                    object,
                    &distanceSquared
                )) {
                continue;
            }

            if (nearestObject == NULL ||
                distanceSquared < nearestDistanceSquared) {
                nearestObject = object;
                nearestDistanceSquared = distanceSquared;
            }
        }
    }

    return nearestObject;
}

static bool vr_hand_interaction_try_hoot_hold(
    struct MarioState* mario,
    struct Object* object,
    u32 hand
) {
    if (mario == NULL ||
        object == NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        (object->oInteractType & INTERACT_HOOT) == 0) {
        return false;
    }

    bool allowInteract = true;
    smlua_call_event_hooks(
        HOOK_ALLOW_INTERACT,
        mario,
        object,
        INTERACT_HOOT,
        &allowInteract
    );
    if (!allowInteract) {
        return false;
    }

    struct Object* previousUsedObject = mario->usedObj;
    mario->usedObj = object;
    if (!interact_hoot(mario, INTERACT_HOOT, object)) {
        mario->usedObj = previousUsedObject;
        return false;
    }

    sVrTrackedHootObject = object;
    sVrTrackedHootHand = hand;
    mario->input |= INPUT_A_DOWN;
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        object,
        INTERACT_HOOT,
        true
    );
    vr_apply_haptic(hand, 0.45f, 0.07f, -1.0f);
    VR_INTERACTION_DEBUG(
        "[VR] Physical hold attached to Hoot.\n"
    );
    return true;
}

static bool vr_hand_interaction_start_player_anchor(
    struct MarioState* mario,
    struct Object* object,
    u32 hand
) {
    if (mario == NULL ||
        object == NULL ||
        hand >= VR_CONTROLLER_COUNT ||
        (object->oInteractType & INTERACT_HOOT) != 0) {
        return false;
    }

    bool allowInteract = true;
    smlua_call_event_hooks(
        HOOK_ALLOW_INTERACT,
        mario,
        object,
        object->oInteractType,
        &allowInteract
    );
    if (!allowInteract) {
        return false;
    }

    const f32 deltaX = mario->pos[0] - object->oPosX;
    const f32 deltaZ = mario->pos[2] - object->oPosZ;
    const f32 yawSin = sins(object->oMoveAngleYaw);
    const f32 yawCos = coss(object->oMoveAngleYaw);

    // Store the player offset in the actor's local yaw space so the held
    // position follows both translation and smooth turns of a moving actor.
    mario->interactObj = object;
    mario->usedObj = object;
    if (!set_mario_action(mario, ACT_RIDING_HOOT, 0)) {
        mario->usedObj = NULL;
        return false;
    }

    sVrTrackedAnchorOffset[0] =
        deltaX * yawCos - deltaZ * yawSin;
    sVrTrackedAnchorOffset[1] =
        mario->pos[1] - object->oPosY;
    sVrTrackedAnchorOffset[2] =
        deltaX * yawSin + deltaZ * yawCos;
    sVrTrackedAnchorObject = object;
    sVrTrackedAnchorHand = hand;

    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        object,
        object->oInteractType,
        true
    );
    vr_apply_haptic(hand, 0.45f, 0.07f, -1.0f);
    VR_INTERACTION_DEBUG(
        "[VR] Physical hold anchored to a moving actor.\n"
    );
    return true;
}

static bool vr_hand_interaction_try_actor_hold(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition
) {
    struct Object* object =
        vr_hand_interaction_find_anchor_target(
            mario,
            handPosition
        );
    if (object == NULL) {
        return false;
    }

    if ((object->oInteractType & INTERACT_HOOT) != 0) {
        return vr_hand_interaction_try_hoot_hold(
            mario,
            object,
            hand
        );
    }
    return vr_hand_interaction_start_player_anchor(
        mario,
        object,
        hand
    );
}

static void vr_hand_interaction_release_player_anchor(
    struct MarioState* mario
) {
    const u32 hand = sVrTrackedAnchorHand;
    struct Object* object = sVrTrackedAnchorObject;
    vr_hand_interaction_clear_player_anchor();

    if (mario != NULL &&
        mario->playerIndex == 0 &&
        mario->action == ACT_RIDING_HOOT &&
        mario->usedObj == object) {
        mario->usedObj = NULL;
        set_mario_action(mario, ACT_FREEFALL, 0);
        if (object != NULL) {
            mario->vel[0] = object->oVelX;
            mario->vel[1] = object->oVelY;
            mario->vel[2] = object->oVelZ;
        }
    }

    if (hand < VR_CONTROLLER_COUNT) {
        vr_apply_haptic(hand, 0.15f, 0.04f, -1.0f);
    }
    VR_INTERACTION_DEBUG(
        "[VR] Physical moving-actor hold released.\n"
    );
}

static bool vr_hand_interaction_try_grab(
    struct MarioState* mario,
    u32 hand,
    const Vec3f handPosition,
    const Vec3f handVelocity
) {
    if (!configVrPhysicalGrabbing ||
        mario == NULL ||
        mario->heldObj != NULL ||
        hand >= VR_CONTROLLER_COUNT) {
        return false;
    }

    struct Object* object =
        vr_hand_interaction_find_grab_target(
            mario,
            handPosition
        );
    if (object == NULL) {
        return false;
    }

    bool allowInteract = true;
    smlua_call_event_hooks(
        HOOK_ALLOW_INTERACT,
        mario,
        object,
        INTERACT_GRABBABLE,
        &allowInteract
    );
    if (!allowInteract) {
        return false;
    }

    mario->interactObj = object;
    mario->usedObj = object;
    mario_grab_used_object(mario);
    if (mario->heldObj != object) {
        smlua_call_event_hooks(
            HOOK_ON_INTERACT,
            mario,
            object,
            INTERACT_GRABBABLE,
            false
        );
        return false;
    }

    const bool nativeCarry =
        vr_hand_interaction_requires_native_carry(object);

    // Normal grabbables bypass Mario's pickup animation and follow the hand.
    // Native carry objects retain the Mario-moving action they require.
    mario->input &= ~INPUT_INTERACT_OBJ_GRABBABLE;
    if (nativeCarry) {
        set_mario_action(mario, ACT_CRAZY_BOX_BOUNCE, 0);
    } else {
        sVrTrackedHeldObject = object;
        sVrTrackedHeldHand = hand;
        sVrTrackedHeldGripMask = (u8)(1U << hand);
        vr_hand_interaction_update_held_position(
            object,
            handPosition,
            handVelocity
        );
    }

    if (object->oSyncID != 0) {
        network_send_object_reliability(object, true);
    }
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        object,
        INTERACT_GRABBABLE,
        true
    );
    vr_apply_haptic(hand, 0.45f, 0.07f, -1.0f);
    VR_INTERACTION_DEBUG(
        "[VR] %s hand grabbed an object.\n",
        hand == VR_CONTROLLER_LEFT ? "Left" : "Right"
    );
    return true;
}

static void vr_hand_interaction_release_grab(
    struct MarioState* mario,
    bool allowThrow
) {
    struct Object* object = sVrTrackedHeldObject;
    if (object == NULL) {
        vr_hand_interaction_clear_tracked_hold();
        return;
    }

    Vec3f releasePosition;
    Vec3f releaseVelocity;
    vec3f_copy(releasePosition, sVrTrackedHeldPosition);
    vec3f_copy(releaseVelocity, sVrTrackedHeldVelocity);
    const f32 horizontalSpeed = sqrtf(
        releaseVelocity[0] * releaseVelocity[0] +
        releaseVelocity[2] * releaseVelocity[2]
    );
    const f32 totalSpeed = sqrtf(
        horizontalSpeed * horizontalSpeed +
        releaseVelocity[1] * releaseVelocity[1]
    );
    const bool throwObject = allowThrow &&
        totalSpeed >= VR_THROW_MIN_SPEED;

    const u32 releaseAction = mario != NULL ? mario->action : 0;
    if (mario != NULL && mario->heldObj == object) {
        if (mario->marioBodyState != NULL &&
            sVrTrackedHeldPositionValid) {
            vec3f_copy(
                mario->marioBodyState->heldObjLastPosition,
                releasePosition
            );
        }

        sVrTrackedReleaseInProgress = true;
        if (throwObject) {
            mario_throw_held_object(mario);
        } else {
            mario_drop_held_object(mario);
        }
        sVrTrackedReleaseInProgress = false;

        // A native dive/punch pickup may still be in a holding animation when
        // the physical grip releases it. Leave that animation immediately so
        // it cannot re-enter a hold state with a null object.
        if (mario->heldObj == NULL) {
            switch (releaseAction) {
                case ACT_PICKING_UP:
                case ACT_DIVE_PICKING_UP:
                case ACT_HOLD_IDLE:
                case ACT_HOLD_HEAVY_IDLE:
                    set_mario_action(mario, ACT_IDLE, 0);
                    break;
                case ACT_HOLD_WALKING:
                case ACT_HOLD_HEAVY_WALKING:
                case ACT_HOLD_DECELERATING:
                    set_mario_action(mario, ACT_WALKING, 0);
                    break;
                case ACT_HOLD_JUMP:
                case ACT_HOLD_FREEFALL:
                    set_mario_action(mario, ACT_FREEFALL, 0);
                    break;
            }
        }
    }

    if ((object->activeFlags & ACTIVE_FLAG_ACTIVE) != 0 &&
        sVrTrackedHeldPositionValid) {
        vec3f_copy(&object->oPosX, releasePosition);
        vec3f_copy(object->header.gfx.pos, releasePosition);
        object->header.gfx.node.flags |= GRAPH_RENDER_ACTIVE;
        object->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;

        if (throwObject && horizontalSpeed > 0.0001f) {
            const s16 throwYaw = atan2s(
                releaseVelocity[2],
                releaseVelocity[0]
            );
            object->oMoveAngleYaw = throwYaw;
            object->oFaceAngleYaw = throwYaw;
            object->oForwardVel = clamp(
                horizontalSpeed * VR_THROW_VELOCITY_SCALE,
                15.0f,
                60.0f
            );
            object->oVelY = clamp(
                releaseVelocity[1] *
                    VR_THROW_VELOCITY_SCALE +
                    5.0f,
                -20.0f,
                50.0f
            );
        }

        if (object->oSyncID != 0) {
            network_send_object_reliability(object, true);
        }
    }

    if (sVrTrackedHeldHand < VR_CONTROLLER_COUNT) {
        vr_apply_haptic(
            sVrTrackedHeldHand,
            throwObject ? 0.30f : 0.15f,
            0.04f,
            -1.0f
        );
    }
    VR_INTERACTION_DEBUG(
        "[VR] Tracked object %s.\n",
        throwObject ? "thrown" : "released"
    );
    vr_hand_interaction_clear_tracked_hold();
}

static bool vr_hand_interaction_object_is_attackable(
    struct MarioState* mario,
    struct Object* object
) {
    return mario != NULL &&
        object != NULL &&
        object != mario->marioObj &&
        object != mario->heldObj &&
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) != 0 &&
        object->oIntangibleTimer == 0 &&
        (object->oInteractType & VR_FIST_ATTACKABLE_TYPES) != 0 &&
        (object->oInteractStatus & INT_STATUS_INTERACTED) == 0;
}

static bool vr_hand_interaction_sweep_overlaps_object(
    const struct VrFistSweep* sweep,
    f32 fistRadius,
    f32 fistLength,
    struct Object* object
) {
    const f32 objectRadius = fmaxf(
        object->hitboxRadius,
        object->hurtboxRadius
    );
    const f32 objectHeight = fmaxf(
        object->hitboxHeight,
        object->hurtboxHeight
    );
    if (objectRadius <= 0.0f || objectHeight <= 0.0f) {
        return false;
    }

    const f32 collisionRadius = objectRadius + fistRadius;
    const f32 collisionRadiusSquared =
        collisionRadius * collisionRadius;
    const f32 objectBottom =
        object->oPosY - object->hitboxDownOffset;
    const f32 objectTop = objectBottom + objectHeight;

    // Reject distant objects before running the sampled sweep. A motion
    // punch remains active for several frames, so this broad phase avoids
    // doing five interpolated collision tests for nearly every object in
    // the level while preserving the exact existing hit result.
    if (object->oPosX < sweep->minimum[0] - collisionRadius ||
        object->oPosX > sweep->maximum[0] + collisionRadius ||
        object->oPosZ < sweep->minimum[2] - collisionRadius ||
        object->oPosZ > sweep->maximum[2] + collisionRadius ||
        objectTop < sweep->minimum[1] - fistLength ||
        objectBottom > sweep->maximum[1] + fistRadius) {
        return false;
    }

    for (u32 sample = 0;
         sample <= VR_FIST_SWEEP_SAMPLES;
         sample++) {
        const f32 x = sweep->start[0] + sweep->step[0] * sample;
        const f32 y = sweep->start[1] + sweep->step[1] * sample;
        const f32 z = sweep->start[2] + sweep->step[2] * sample;
        const f32 deltaX = x - object->oPosX;
        const f32 deltaZ = z - object->oPosZ;
        const f32 fistTop = y + fistRadius;
        const f32 fistBottom = fistTop - fistLength;

        if (deltaX * deltaX + deltaZ * deltaZ <=
                collisionRadiusSquared &&
            fistTop >= objectBottom &&
            fistBottom <= objectTop) {
            return true;
        }
    }

    return false;
}

static bool vr_hand_interaction_attack_object(
    struct MarioState* mario,
    u32 hand,
    const struct VrFistSweep* sweep,
    const Vec3f velocity,
    struct Object* object,
    f32 fistRadius,
    f32 fistLength
) {
    if (!vr_hand_interaction_object_is_attackable(mario, object) ||
        !vr_hand_interaction_sweep_overlaps_object(
            sweep,
            fistRadius,
            fistLength,
            object
        )) {
        return false;
    }

    if ((mario->action & ACT_FLAG_SWIMMING) != 0 &&
        (object->oInteractType & INTERACT_BREAKABLE) != 0 &&
        !configVrCheatUnderwaterBoxPunching) {
        return false;
    }

    bool allowInteract = true;
    smlua_call_event_hooks(
        HOOK_ALLOW_INTERACT,
        mario,
        object,
        object->oInteractType,
        &allowInteract
    );
    if (!allowInteract) {
        return false;
    }

    const f32 horizontalSpeedSquared =
        velocity[0] * velocity[0] +
        velocity[2] * velocity[2];
    if (horizontalSpeedSquared > 0.0001f) {
        object->oMoveAngleYaw = atan2s(
            velocity[2],
            velocity[0]
        );
    }

    // Bullies normally receive their impulse inside Mario's body-collision
    // handler. Supply the same magnitude here without moving Mario's body.
    if ((object->oInteractType & INTERACT_BULLY) != 0 &&
        object->hitboxRadius > 0.0f) {
        object->oForwardVel = 3392.0f / object->hitboxRadius;
    }

    mario->interactObj = object;
    attack_object(mario, object, INT_PUNCH);
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        object,
        object->oInteractType,
        true
    );

    play_sound(
        SOUND_ACTION_HIT_2,
        object->header.gfx.cameraToObject
    );
    vr_apply_haptic(hand, 0.55f, 0.06f, -1.0f);

    VR_INTERACTION_DEBUG(
        "[VR] %s fist hit an object.\n",
        hand == VR_CONTROLLER_LEFT ? "Left" : "Right"
    );
    return true;
}

static bool vr_hand_interaction_process_lists(
    struct MarioState* mario,
    u32 hand,
    const Vec3f start,
    const Vec3f end,
    const Vec3f velocity
) {
    if (gObjectLists == NULL) {
        return false;
    }

    // These settings cannot change during a single fist sweep. Compute them
    // once instead of repeating the clamps and divisions for every object.
    const f32 fistRadius = vr_hand_interaction_fist_radius();
    const f32 fistLength =
        vr_hand_interaction_fist_length(fistRadius);
    struct VrFistSweep sweep;
    for (u32 axis = 0; axis < 3; axis++) {
        sweep.start[axis] = start[axis];
        sweep.step[axis] =
            (end[axis] - start[axis]) /
            (f32)VR_FIST_SWEEP_SAMPLES;
        sweep.minimum[axis] = fminf(start[axis], end[axis]);
        sweep.maximum[axis] = fmaxf(start[axis], end[axis]);
    }

    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;

        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;

            if (vr_hand_interaction_attack_object(
                    mario,
                    hand,
                    &sweep,
                    velocity,
                    object,
                    fistRadius,
                    fistLength
                )) {
                return true;
            }
        }
    }

    return false;
}

static void vr_hand_interaction_collect_coins_at_headset(
    struct MarioState* mario
) {
    if (mario == NULL ||
        gObjectLists == NULL ||
        (sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE &&
         (mario->action & ACT_FLAG_SWIMMING) == 0)) {
        return;
    }

    Vec3f headsetPosition;
    if (!vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        return;
    }

    const f32 headsetBottom = headsetPosition[1] -
        VR_HEADSET_INTERACTION_HEIGHT * 0.5f;
    const f32 headsetTop = headsetBottom +
        VR_HEADSET_INTERACTION_HEIGHT;

      // Object collision is normally generated before Mario's late-frame HMD
      // collider update. Scan only while physically climbing or swimming,
      // giving the headset a small, safe collectible extension without moving
    // Mario's environment collider or triggering enemies, hazards, and warps.
    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;

        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;

              const bool coin =
                  (object->oInteractType & INTERACT_COIN) != 0;
              const bool oneUp =
                  obj_has_behavior(object, bhv1upWalking) ||
                  obj_has_behavior(object, bhv1upRunningAway) ||
                  obj_has_behavior(object, bhv1upSliding) ||
                  obj_has_behavior(object, bhv1Up) ||
                  obj_has_behavior(object, bhv1upJumpOnApproach) ||
                  obj_has_behavior(object, bhvHidden1up) ||
                  obj_has_behavior(object, bhvHidden1upInPole);

              if ((object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
                  object->oIntangibleTimer != 0 ||
                  (!coin && !oneUp) ||
                  (object->oInteractStatus & INT_STATUS_INTERACTED) != 0) {
                continue;
            }

            const f32 dx = object->oPosX - headsetPosition[0];
            const f32 dz = object->oPosZ - headsetPosition[2];
            const f32 combinedRadius =
                VR_HEADSET_INTERACTION_RADIUS +
                MAX(object->hitboxRadius, 0.0f);
            if (dx * dx + dz * dz >
                combinedRadius * combinedRadius) {
                continue;
            }

            const f32 objectBottom =
                object->oPosY - object->hitboxDownOffset;
            const f32 objectTop =
                objectBottom + MAX(object->hitboxHeight, 0.0f);
            if (headsetBottom > objectTop || headsetTop < objectBottom) {
                continue;
            }

              if (coin) {
                  // Yellow, red, and blue coins all use INTERACT_COIN, so the
                  // same native interaction retains their values, counters,
                  // red-coin star logic, sound, and networking.
                  process_interaction(
                      mario,
                      INTERACT_COIN,
                      object,
                      interact_coin
                  );
              } else {
                  play_sound(
                      SOUND_GENERAL_COLLECT_1UP,
                      gGlobalSoundSource
                  );
                  mario->numLives++;
                  object->activeFlags = ACTIVE_FLAG_DEACTIVATED;
                  if (gLevelValues.mushroom1UpHeal) {
                      mario->healCounter = 31;
                      mario->hurtCounter = 0;
                  }
                  network_send_collect_item(object);
              }
        }
    }
}

static bool vr_hand_interaction_point_overlaps_object(
    const Vec3f position,
    f32 radius,
    f32 height,
    struct Object* object
) {
    if (position == NULL || object == NULL) {
        return false;
    }

    const f32 objectRadius = fmaxf(
        object->hitboxRadius,
        object->hurtboxRadius
    );
    const f32 objectHeight = fmaxf(
        object->hitboxHeight,
        object->hurtboxHeight
    );
    if (objectRadius <= 0.0f || objectHeight <= 0.0f) {
        return false;
    }

    const f32 dx = object->oPosX - position[0];
    const f32 dz = object->oPosZ - position[2];
    const f32 combinedRadius = radius + objectRadius;
    if (dx * dx + dz * dz > combinedRadius * combinedRadius) {
        return false;
    }

    const f32 pointBottom = position[1] - height * 0.5f;
    const f32 pointTop = pointBottom + height;
    const f32 objectBottom =
        object->oPosY - object->hitboxDownOffset;
    const f32 objectTop = objectBottom + objectHeight;
    return pointBottom <= objectTop && pointTop >= objectBottom;
}

static bool vr_hand_interaction_is_collectible_star(
    struct Object* object
) {
    return object != NULL &&
        (object->oInteractType & INTERACT_STAR_OR_KEY) != 0 &&
        // Preserve the original requirement that Bowser's keys are collected
        // by Mario rather than remotely touched with a tracked extremity.
        !obj_has_behavior(object, bhvBowserKey);
}

static bool vr_hand_interaction_process_star_contacts(
    struct MarioState* mario,
    const Vec3f handPositions[VR_CONTROLLER_COUNT],
    const bool handPositionValid[VR_CONTROLLER_COUNT]
) {
    if (mario == NULL || gObjectLists == NULL) {
        return false;
    }

    const f32 handRadius = vr_hand_interaction_fist_radius();
    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;

        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;

            if ((object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
                object->oIntangibleTimer != 0 ||
                (object->oInteractStatus & INT_STATUS_INTERACTED) != 0 ||
                !vr_hand_interaction_is_collectible_star(object)) {
                continue;
            }

            bool touching = false;
            u32 touchedHand = VR_CONTROLLER_COUNT;
            for (u32 hand = 0;
                 !touching && hand < VR_CONTROLLER_COUNT;
                 hand++) {
                if (handPositionValid != NULL &&
                    handPositions != NULL &&
                    handPositionValid[hand] &&
                    vr_hand_interaction_point_overlaps_object(
                        handPositions[hand],
                        handRadius,
                        handRadius * 2.0f,
                        object
                    )) {
                    touching = true;
                    touchedHand = hand;
                }
            }
            if (!touching) {
                continue;
            }

            if (process_interaction(
                    mario,
                    INTERACT_STAR_OR_KEY,
                    object,
                    interact_star_or_key
                )) {
                if (touchedHand < VR_CONTROLLER_COUNT) {
                    vr_apply_haptic(
                        touchedHand,
                        0.45f,
                        0.08f,
                        -1.0f
                    );
                }
                return true;
            }
        }
    }
    return false;
}

static bool vr_hand_interaction_process_head_contacts(
    struct MarioState* mario,
    const Vec3f headsetPosition
) {
    if (mario == NULL ||
        headsetPosition == NULL ||
        gObjectLists == NULL) {
        return false;
    }

    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;

        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;

            const bool collectibleStar =
                vr_hand_interaction_is_collectible_star(object);
            if ((object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
                object->oIntangibleTimer != 0 ||
                (object->oInteractStatus & INT_STATUS_INTERACTED) != 0 ||
                (!collectibleStar &&
                 (object->oInteractType & INTERACT_DAMAGE) == 0) ||
                !vr_hand_interaction_point_overlaps_object(
                    headsetPosition,
                    VR_HEADSET_INTERACTION_RADIUS,
                    VR_HEADSET_INTERACTION_HEIGHT,
                    object
                )) {
                continue;
            }

            // Match the native interaction priority: a real star wins over a
            // simultaneous damage contact. Bowser keys remain body-only.
            if (collectibleStar &&
                process_interaction(
                    mario,
                    INTERACT_STAR_OR_KEY,
                    object,
                    interact_star_or_key
                )) {
                return true;
            }
            if ((object->oInteractType & INTERACT_DAMAGE) != 0 &&
                process_interaction(
                    mario,
                    INTERACT_DAMAGE,
                    object,
                    interact_damage
                )) {
                return true;
            }
        }
    }
    return false;
}

static void vr_special_moves_delete_object(struct Object** object) {
    if (object != NULL && *object != NULL) {
        if (((*object)->activeFlags & ACTIVE_FLAG_ACTIVE) != 0) {
            obj_mark_for_deletion(*object);
        }
        *object = NULL;
    }
}

static void vr_special_moves_clear_fireball(void) {
    vr_special_moves_delete_object(&sVrFireballObject);
    sVrFireballProjectile = false;
    sVrFireballChargeFrames = 0;
    sVrFireballLifetime = 0;
    vec3f_set(sVrFireballVelocity, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrFireballRememberedVelocity, 0.0f, 0.0f, 0.0f);
}

static void vr_special_moves_reset_power(void) {
    sVrFireFlowerPowered = false;
    sVrFireballTriggerPressed = false;
    vr_special_moves_clear_fireball();
}

bool vr_special_moves_fire_flower_active(void) {
    return configVrSpecialFireFlower && sVrFireFlowerPowered;
}

bool vr_special_moves_grant_fire_flower(void) {
    if (!configVrSpecialFireFlower || !vr_is_active() ||
        gMarioStates[0].marioObj == NULL) {
        return false;
    }
    sVrFireFlowerPowered = true;
    sVrFireFlowerLevel = gCurrLevelNum;
    sVrFireFlowerArea = gCurrAreaIndex;
    return true;
}

Gfx* geo_vr_fireball_color(
    s32 callContext,
    UNUSED struct GraphNode* node,
    UNUSED void* context
) {
    if (callContext != GEO_CONTEXT_RENDER ||
        gCurGraphNodeObject == NULL) {
        return NULL;
    }
    Gfx* displayList = alloc_display_list(2 * sizeof(Gfx));
    if (displayList == NULL) {
        return NULL;
    }
    struct Object* object = (struct Object*)gCurGraphNodeObject;
    const u8 alpha = (u8)clamp(
        object->oOpacity,
        0,
        255
    );
    // Warmer than the stock red flame (255, 50, 0) without becoming yellow.
    gDPSetEnvColor(&displayList[0], 255, 105, 0, alpha);
    gSPEndDisplayList(&displayList[1]);
    return displayList;
}

bool vr_special_moves_spawn_fire_flower(
    struct Object* box,
    struct MarioState* owner
) {
    if (!configVrSpecialFireFlower || !vr_is_active() || box == NULL ||
        owner == NULL || owner->playerIndex != 0 || random_float() >= 0.5f) {
        return false;
    }

    for (u32 i = 0; i < VR_FIRE_FLOWER_PICKUP_COUNT; i++) {
        struct Object* pickup = sVrFireFlowerPickups[i];
        if (pickup != NULL &&
            (pickup->activeFlags & ACTIVE_FLAG_ACTIVE) != 0) {
            continue;
        }

        pickup = spawn_object(box, MODEL_VR_FIRE_FLOWER, bhvStaticObject);
        if (pickup == NULL) {
            return false;
        }
        pickup->oPosY += 65.0f;
        pickup->oOpacity = 255;
        obj_scale(pickup, 0.75f);
        sVrFireFlowerPickups[i] = pickup;
        return true;
    }
    return false;
}

static void vr_special_moves_update_pickups(struct MarioState* mario) {
    if (mario == NULL || mario->marioObj == NULL) {
        return;
    }
    for (u32 i = 0; i < VR_FIRE_FLOWER_PICKUP_COUNT; i++) {
        struct Object* pickup = sVrFireFlowerPickups[i];
        if (pickup == NULL) {
            continue;
        }
        if ((pickup->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
            sVrFireFlowerPickups[i] = NULL;
            continue;
        }

        pickup->oFaceAngleYaw += 0x600;
        pickup->oPosY = pickup->oHomeY + 65.0f +
            sins((s16)(gGlobalTimer * 0x500)) * 10.0f;
        obj_update_gfx_pos_and_angle(pickup);
        if (dist_between_objects(pickup, mario->marioObj) <=
            VR_FIRE_FLOWER_PICKUP_RADIUS) {
            vr_special_moves_grant_fire_flower();
            vr_apply_haptic(VR_CONTROLLER_LEFT, 0.45f, 0.10f, -1.0f);
            vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.45f, 0.10f, -1.0f);
            vr_special_moves_delete_object(&sVrFireFlowerPickups[i]);
        }
    }
}

static bool vr_special_moves_projectile_hits_enemy(
    struct MarioState* mario,
    struct Object* projectile
) {
    if (mario == NULL || projectile == NULL || gObjectLists == NULL) {
        return false;
    }
    for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        for (struct ObjectNode* node = list->next;
             node != NULL && node != list;
             node = node->next) {
            struct Object* target = (struct Object*)node;
            const u32 enemyTypes = INTERACT_DAMAGE | INTERACT_BULLY |
                INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 |
                INTERACT_KOOPA | INTERACT_SPINY_WALKING;
            if (target == projectile || target == mario->marioObj ||
                (target->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
                target->oIntangibleTimer != 0 ||
                (target->oInteractType & enemyTypes) == 0) {
                continue;
            }
            const f32 dx = projectile->oPosX - target->oPosX;
            const f32 dz = projectile->oPosZ - target->oPosZ;
            const f32 targetBottom = target->oPosY - target->hitboxDownOffset;
            const f32 targetTop = targetBottom + fmaxf(target->hitboxHeight, 80.0f);
            const f32 radius = fmaxf(target->hitboxRadius, 35.0f) + 24.0f;
            if (dx * dx + dz * dz > radius * radius ||
                projectile->oPosY < targetBottom - 24.0f ||
                projectile->oPosY > targetTop + 24.0f) {
                continue;
            }

            mario->interactObj = target;
            attack_object(mario, target, INT_PUNCH);
            struct Object* explosion = spawn_object(
                projectile,
                MODEL_EXPLOSION,
                bhvExplosion
            );
            if (explosion != NULL) {
                obj_scale(explosion, 0.2f);
            }
            play_sound(SOUND_GENERAL_BOWSER_BOMB_EXPLOSION,
                projectile->header.gfx.cameraToObject);
            return true;
        }
    }
    return false;
}

static void vr_special_moves_update_projectile(struct MarioState* mario) {
    struct Object* projectile = sVrFireballObject;
    if (!sVrFireballProjectile || projectile == NULL) {
        return;
    }
    if ((projectile->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
        ++sVrFireballLifetime > VR_FIREBALL_MAX_LIFETIME) {
        vr_special_moves_clear_fireball();
        return;
    }

    projectile->oPosX += sVrFireballVelocity[0];
    projectile->oPosY += sVrFireballVelocity[1];
    projectile->oPosZ += sVrFireballVelocity[2];
    sVrFireballVelocity[1] -= 2.0f;
    projectile->oAnimState = (s32)((gGlobalTimer >> 1) & 7U);

    struct WallCollisionData wall = {
        .x = projectile->oPosX,
        .y = projectile->oPosY,
        .z = projectile->oPosZ,
        .offsetY = 0.0f,
        .radius = 20.0f,
    };
    if (find_wall_collisions(&wall) > 0 && wall.walls[0] != NULL) {
        const f32 nx = wall.walls[0]->normal.x;
        const f32 nz = wall.walls[0]->normal.z;
        const f32 dot = sVrFireballVelocity[0] * nx +
            sVrFireballVelocity[2] * nz;
        sVrFireballVelocity[0] -= 2.0f * dot * nx;
        sVrFireballVelocity[2] -= 2.0f * dot * nz;
        projectile->oPosX = wall.x;
        projectile->oPosZ = wall.z;
    }

    struct Surface* floor = NULL;
    const f32 floorHeight = find_floor(
        projectile->oPosX,
        projectile->oPosY + 80.0f,
        projectile->oPosZ,
        &floor
    );
    if (floor != NULL && projectile->oPosY < floorHeight + 18.0f) {
        projectile->oPosY = floorHeight + 18.0f;
        sVrFireballVelocity[1] = 8.0f;
        sVrFireballVelocity[0] *= 0.985f;
        sVrFireballVelocity[2] *= 0.985f;
    }
    obj_update_gfx_pos_and_angle(projectile);

    if (vr_special_moves_projectile_hits_enemy(mario, projectile)) {
        vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.65f, 0.08f, -1.0f);
        vr_special_moves_clear_fireball();
    }
}

static bool vr_special_moves_update_fireball_hand(
    struct MarioState* mario,
    const struct VrControllerState* state,
    const Vec3f position,
    const Vec3f velocity,
    bool handBusy
) {
    const bool triggerPressed = state != NULL &&
        state->trigger >= VR_FIREBALL_TRIGGER_THRESHOLD;
    const bool canCharge = vr_special_moves_fire_flower_active() &&
        !handBusy && sVrGripPressed[VR_CONTROLLER_RIGHT] &&
        mario != NULL && mario->heldObj == NULL &&
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE;

    if (triggerPressed && !sVrFireballTriggerPressed && canCharge &&
        !sVrFireballProjectile) {
        sVrFireballObject = spawn_object(
            mario->marioObj,
            MODEL_VR_FIREBALL,
            bhvStaticObject
        );
        sVrFireballChargeFrames = 0;
        vec3f_set(sVrFireballRememberedVelocity, 0.0f, 0.0f, 0.0f);
    }

    if (triggerPressed && canCharge && sVrFireballObject != NULL &&
        !sVrFireballProjectile) {
        sVrFireballChargeFrames = (u16)min(
            sVrFireballChargeFrames + 1U,
            VR_FIREBALL_CHARGE_FRAMES
        );
        const f32 progress = (f32)sVrFireballChargeFrames /
            (f32)VR_FIREBALL_CHARGE_FRAMES;
        for (u32 axis = 0; axis < 3; axis++) {
            (&sVrFireballObject->oPosX)[axis] = position[axis];
        }
        sVrFireballObject->oAnimState =
            (s32)((gGlobalTimer >> 1) & 7U);
        sVrFireballObject->oOpacity = (s32)(51.0f + progress * 204.0f);
        obj_scale(sVrFireballObject, 0.05f + progress * 0.55f);
        obj_update_gfx_pos_and_angle(sVrFireballObject);

        const f32 speedSq = velocity[0] * velocity[0] +
            velocity[1] * velocity[1] + velocity[2] * velocity[2];
        const f32 rememberedSq =
            sVrFireballRememberedVelocity[0] * sVrFireballRememberedVelocity[0] +
            sVrFireballRememberedVelocity[1] * sVrFireballRememberedVelocity[1] +
            sVrFireballRememberedVelocity[2] * sVrFireballRememberedVelocity[2];
        if (speedSq >= rememberedSq * VR_THROW_VELOCITY_MEMORY *
            VR_THROW_VELOCITY_MEMORY) {
            for (u32 axis = 0; axis < 3; axis++) {
                sVrFireballRememberedVelocity[axis] = velocity[axis];
            }
        } else {
            vec3f_mul(sVrFireballRememberedVelocity, VR_THROW_VELOCITY_MEMORY);
        }
    }

    if (!triggerPressed && sVrFireballTriggerPressed &&
        sVrFireballObject != NULL && !sVrFireballProjectile) {
        const f32 speedSq =
            sVrFireballRememberedVelocity[0] * sVrFireballRememberedVelocity[0] +
            sVrFireballRememberedVelocity[1] * sVrFireballRememberedVelocity[1] +
            sVrFireballRememberedVelocity[2] * sVrFireballRememberedVelocity[2];
        if (sVrFireballChargeFrames >= VR_FIREBALL_CHARGE_FRAMES &&
            speedSq >= VR_FIREBALL_MIN_THROW_SPEED * VR_FIREBALL_MIN_THROW_SPEED) {
            for (u32 axis = 0; axis < 3; axis++) {
                sVrFireballVelocity[axis] =
                    sVrFireballRememberedVelocity[axis] *
                    VR_THROW_VELOCITY_SCALE;
            }
            sVrFireballObject->oOpacity = 255;
            obj_scale(sVrFireballObject, 0.6f);
            sVrFireballProjectile = true;
            sVrFireballLifetime = 0;
        } else {
            vr_special_moves_clear_fireball();
        }
    }
    if ((!canCharge || !configVrSpecialFireFlower) &&
        !sVrFireballProjectile && sVrFireballObject != NULL) {
        vr_special_moves_clear_fireball();
    }
    sVrFireballTriggerPressed = triggerPressed;
    return sVrFireballObject != NULL && !sVrFireballProjectile;
}

void vr_hand_interaction_update(struct MarioState* mario) {
    // Remote Mario states run through the same interaction function. They
    // must not reset the local player's tracked fist state.
    if (mario == NULL || mario->playerIndex != 0) {
        return;
    }

    if (!configVrSpecialFireFlower || !vr_is_active() ||
        (sVrFireFlowerPowered &&
         (sVrFireFlowerLevel != gCurrLevelNum ||
          sVrFireFlowerArea != gCurrAreaIndex))) {
        vr_special_moves_reset_power();
    }
    vr_special_moves_update_pickups(mario);
    vr_special_moves_update_projectile(mario);

    const bool headsetTrackingAvailable =
        vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        mario->marioObj != NULL;
    Vec3f headsetPosition;
    const bool headsetPositionValid =
        headsetTrackingAvailable &&
        vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        );
    if (headsetPositionValid &&
        (mario->action & ACT_FLAG_INTANGIBLE) == 0) {
        if (vr_hand_interaction_process_head_contacts(
                mario,
                headsetPosition
            )) {
            return;
        }
    }

    const bool trackingAvailable =
        headsetTrackingAvailable &&
        configVrMotionControllerInput;
    const bool canStartInteraction =
        trackingAvailable &&
        (mario->action & ACT_FLAG_INTANGIBLE) == 0;
    if (!trackingAvailable) {
        if (sVrFireballObject != NULL && !sVrFireballProjectile) {
            vr_special_moves_clear_fireball();
        }
        sVrFireballTriggerPressed = false;
        if (sVrInteractionTrackingActive ||
            sVrTrackedHeldObject != NULL ||
            sVrTrackedAnchorObject != NULL ||
            sVrTrackedHootObject != NULL ||
            sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE) {
            if (sVrTrackedHeldObject != NULL) {
                vr_hand_interaction_release_grab(mario, false);
            }
            if (sVrTrackedAnchorObject != NULL) {
                vr_hand_interaction_release_player_anchor(mario);
            }
            if (sVrPhysicalClimbType !=
                VR_PHYSICAL_CLIMB_NONE) {
                vr_hand_interaction_release_physical_climb(
                    mario,
                    NULL,
                    false
                );
            }
            vr_hand_interaction_force_release_hoot(mario);
            vr_hand_interaction_reset();
        }
        sVrInteractionTrackingActive = false;
        return;
    }
    sVrInteractionTrackingActive = true;

    if (sVrTrackedHeldObject != NULL &&
        mario->heldObj == sVrTrackedHeldObject &&
        (mario->hurtCounter != 0 || mario->knockbackTimer != 0)) {
        // Some native damage paths finish setting the hurt state after their
        // normal drop opportunity. Force the tracked object through the same
        // native drop path on the first damaged frame, even if grip stays
        // closed. Re-grabbing still requires a fresh grip edge.
        vr_hand_interaction_release_grab(mario, false);
    }

    if (sVrTrackedHeldObject != NULL &&
        (mario->heldObj != sVrTrackedHeldObject ||
         (sVrTrackedHeldObject->activeFlags &
            ACTIVE_FLAG_ACTIVE) == 0)) {
        if (mario->heldObj == sVrTrackedHeldObject) {
            mario->heldObj = NULL;
        }
        vr_hand_interaction_clear_tracked_hold();
    }

    if (sVrTrackedHeldObject != NULL &&
        mario->heldObj == sVrTrackedHeldObject &&
        (sVrTrackedHeldObject->oInteractionSubtype &
            INT_SUBTYPE_DROP_IMMEDIATELY) != 0) {
        // Mips, Ukiki, and the baby penguin use this native flag to end a
        // scripted hold. Physical ownership must honor it even though VR
        // bypasses Mario's ACT_HOLD_IDLE animation/state.
        sVrTrackedHeldObject->oInteractionSubtype &=
            ~INT_SUBTYPE_DROP_IMMEDIATELY;
        vr_hand_interaction_release_grab(mario, false);
    }

    if (!vr_hand_interaction_is_bowser_sequence(mario)) {
        sVrBowserGripMask = 0;
        vr_hand_interaction_clear_bowser_motion();
    }

    if (sVrTrackedHootObject != NULL &&
        ((sVrTrackedHootObject->activeFlags &
            ACTIVE_FLAG_ACTIVE) == 0 ||
         mario->action != ACT_RIDING_HOOT ||
         mario->usedObj != sVrTrackedHootObject)) {
        vr_hand_interaction_force_release_hoot(mario);
    }

    if (sVrTrackedAnchorObject != NULL &&
        ((sVrTrackedAnchorObject->activeFlags &
            ACTIVE_FLAG_ACTIVE) == 0 ||
         mario->action != ACT_RIDING_HOOT ||
         mario->usedObj != sVrTrackedAnchorObject)) {
        vr_hand_interaction_release_player_anchor(mario);
    }

    if (sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE &&
        !vr_hand_interaction_physical_climb_matches_action(mario)) {
        // A native action (including a pole jump) already took ownership.
        // Preserve the physical camera position without overwriting that
        // new action or its launch velocity.
        vr_hand_interaction_commit_physical_climb_offset(mario);
    }

    if (!configVrPhysicalGrabbing) {
        if (sVrTrackedHeldObject != NULL) {
            vr_hand_interaction_release_grab(mario, false);
        }
        if (sVrTrackedAnchorObject != NULL) {
            vr_hand_interaction_release_player_anchor(mario);
        }
        vr_hand_interaction_clear_hoot_hold();
    }
    if (!configVrPhysicalClimbing &&
        sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE) {
        vr_hand_interaction_release_physical_climb(
            mario,
            NULL,
            false
        );
    }

    if (canStartInteraction &&
        (sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE ||
         (mario->action & ACT_FLAG_SWIMMING) != 0)) {
        vr_hand_interaction_collect_coins_at_headset(mario);
    }

    if (!configVrMarioPunchSound) {
        sVrPunchSoundComboStep = 0;
        sVrPunchSoundComboResetFrames = 0;
    } else if (sVrPunchSoundComboResetFrames > 0) {
        sVrPunchSoundComboResetFrames--;
        if (sVrPunchSoundComboResetFrames == 0) {
            sVrPunchSoundComboStep = 0;
        }
    }

    if (sVrPhysicalClimbRegrabFrames > 0) {
        sVrPhysicalClimbRegrabFrames--;
    }

    Vec3f collectibleHandPositions[VR_CONTROLLER_COUNT];
    bool collectibleHandPositionValid[VR_CONTROLLER_COUNT] = {
        false,
        false
    };
    for (u32 hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        if (!configVrMotionControlledDive &&
            !configVrMotionControlledGroundDive) {
            sVrMotionDivePairFrames[hand] = 0;
        } else if (sVrMotionDivePairFrames[hand] > 0) {
            sVrMotionDivePairFrames[hand]--;
        }
    }

    for (u32 hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        struct VrControllerState state = { 0 };
        const bool controllerAvailable =
            vr_get_controller_state(hand, &state);
        const bool gripWasPressed = sVrGripPressed[hand];
        vr_hand_interaction_update_grip(
            hand,
            controllerAvailable,
            &state
        );

        Vec3f position;
        Vec3f velocity;
        vec3f_set(position, 0.0f, 0.0f, 0.0f);
        vec3f_set(velocity, 0.0f, 0.0f, 0.0f);
        const bool positionValid =
            controllerAvailable &&
            vr_get_controller_world_fist_raw_from_state(
                hand,
                &state,
                position,
                velocity
            );
        if (positionValid) {
            vr_hand_interaction_resolve_hand_collision(
                mario,
                hand,
                position
            );
        } else {
            sVrHandCollision[hand].rawPositionValid = false;
            sVrHandCollision[hand].constraintActive = false;
        }
        if (positionValid) {
            vec3f_copy(
                collectibleHandPositions[hand],
                position
            );
            collectibleHandPositionValid[hand] = true;
        }
        Vec3f climbPosition;
        vec3f_set(climbPosition, 0.0f, 0.0f, 0.0f);
        const bool climbPositionValid =
            positionValid &&
            vr_get_controller_climb_fist(
                position,
                climbPosition
            );
        const bool handIsHoldingCap =
            vr_is_controller_holding_cap(hand);
        const bool handIsHoldingFireFlower =
            vr_is_controller_holding_fire_flower(hand);
        const bool handIsHoldingVrItem =
            handIsHoldingCap || handIsHoldingFireFlower;
        const bool fireballHandBusy =
            handIsHoldingVrItem ||
            sVrTrackedHeldObject != NULL ||
            sVrPhysicalClimbHands[hand] ||
            sVrTrackedHootHand == hand ||
            sVrTrackedAnchorHand == hand ||
            (sVrBowserGripMask & (u8)(1U << hand)) != 0;
        const bool handIsChargingFireball =
            hand == VR_CONTROLLER_RIGHT && positionValid &&
            vr_special_moves_update_fireball_hand(
                mario,
                &state,
                position,
                velocity,
                fireballHandBusy
            );
        if (handIsHoldingVrItem) {
            sVrMotionDivePairFrames[hand] = 0;
        }

        if (positionValid && canStartInteraction &&
            !handIsHoldingVrItem) {
            vr_hand_interaction_try_collect_cap(
                mario,
                hand,
                position
            );
        }

        if (vr_hand_interaction_is_bowser_hold(mario) &&
            sVrBowserGripMask != 0 &&
            !gripWasPressed &&
            sVrGripPressed[hand] &&
            positionValid) {
            sVrBowserGripMask |= (u8)(1U << hand);
            sVrBowserHandYawValid[hand] = false;
            sVrBowserAccumulatedHandYaw[hand] = 0;
        }

        if (positionValid &&
            sVrTrackedHeldObject == NULL &&
            mario->heldObj != NULL &&
            sVrGripPressed[hand]) {
            vr_hand_interaction_adopt_native_hold(
                mario,
                hand,
                position,
                velocity
            );
        }

        if ((sVrBowserGripMask & (u8)(1U << hand)) != 0) {
            vr_hand_interaction_update_bowser_hand_turn(
                mario,
                hand,
                controllerAvailable,
                &state
            );
        }

        if ((sVrTrackedHeldGripMask & (u8)(1U << hand)) != 0) {
            if (!positionValid ||
                !sVrGripPressed[hand]) {
                sVrTrackedHeldGripMask &= (u8)~(1U << hand);
                if (sVrTrackedHeldGripMask == 0) {
                    vr_hand_interaction_release_grab(mario, positionValid);
                } else if (sVrTrackedHeldHand == hand) {
                    sVrTrackedHeldHand = hand == VR_CONTROLLER_LEFT
                        ? VR_CONTROLLER_RIGHT : VR_CONTROLLER_LEFT;
                }
            } else if (sVrTrackedHeldHand == hand) {
                vr_hand_interaction_update_held_position(
                    sVrTrackedHeldObject,
                    position,
                    velocity
                );
            }
        } else if (sVrPhysicalClimbHands[hand]) {
            vr_hand_interaction_maintain_physical_climb(
                mario,
                hand,
                climbPositionValid,
                climbPosition,
                velocity
            );
        } else if (sVrTrackedHootHand == hand) {
            if (!positionValid ||
                !sVrGripPressed[hand]) {
                vr_hand_interaction_clear_hoot_hold();
            } else {
                // Hoot's native hanging action expects A to remain held.
                // Supply that action input from the physical grip only while
                // the same tracked hand remains closed.
                mario->input |= INPUT_A_DOWN;
            }
        } else if (sVrTrackedAnchorHand == hand) {
            if (!positionValid ||
                !sVrGripPressed[hand]) {
                vr_hand_interaction_release_player_anchor(
                    mario
                );
            }
        } else if (canStartInteraction &&
                   !handIsHoldingVrItem &&
                   !handIsChargingFireball &&
                   sVrGripPressed[hand] &&
                   positionValid &&
                   climbPositionValid) {
            bool interactionStarted = false;
            if (sVrPhysicalClimbType !=
                VR_PHYSICAL_CLIMB_NONE) {
                interactionStarted =
                    vr_hand_interaction_try_add_physical_climb_hand(
                        mario,
                        hand,
                        position,
                        climbPosition,
                        !gripWasPressed
                    );
            }
            if (!interactionStarted &&
                configVrPhysicalGrabbing &&
                sVrPhysicalClimbType ==
                    VR_PHYSICAL_CLIMB_NONE) {
                interactionStarted =
                    vr_hand_interaction_try_bowser_grab(
                        mario,
                        hand,
                        position
                    );
            }
            if (!interactionStarted &&
                !gripWasPressed &&
                configVrPhysicalGrabbing &&
                sVrPhysicalClimbType ==
                    VR_PHYSICAL_CLIMB_NONE) {
                interactionStarted =
                    vr_hand_interaction_try_grab(
                        mario,
                        hand,
                        position,
                        velocity
                    );
            }
            if (!interactionStarted &&
                sVrPhysicalClimbRegrabFrames == 0 &&
                sVrPhysicalClimbType ==
                    VR_PHYSICAL_CLIMB_NONE) {
                // A held grip can auto-catch only native poles, trees, and
                // hangables as the controller enters reach. Cheat-only wall
                // and ceiling contacts require the fresh grip edge passed
                // below, preventing invisible nearby geometry from latching.
                interactionStarted =
                    vr_hand_interaction_try_physical_climb(
                        mario,
                        hand,
                        position,
                        sVrClimbPreviousPosition[hand],
                        sVrClimbPreviousPositionValid[hand],
                        climbPosition,
                        !gripWasPressed
                    );
            }
            if (!interactionStarted &&
                !gripWasPressed &&
                configVrPhysicalGrabbing &&
                sVrPhysicalClimbType ==
                    VR_PHYSICAL_CLIMB_NONE) {
                vr_hand_interaction_try_actor_hold(
                    mario,
                    hand,
                    position
                );
            }
        }

        if (positionValid) {
            vec3f_copy(
                sVrClimbPreviousPosition[hand],
                position
            );
            sVrClimbPreviousPositionValid[hand] = true;
        } else {
            sVrClimbPreviousPositionValid[hand] = false;
        }

        const bool punchStarted =
            vr_consume_physical_punch(hand);
        const bool handIsHoldingObject =
            handIsHoldingCap ||
            handIsChargingFireball ||
            (sVrTrackedHeldGripMask & (u8)(1U << hand)) != 0 ||
            sVrPhysicalClimbHands[hand] ||
            sVrTrackedHootHand == hand ||
            sVrTrackedAnchorHand == hand ||
            (sVrBowserGripMask & (u8)(1U << hand)) != 0;

        if (punchStarted &&
            canStartInteraction &&
            configVrPhysicalPunching &&
            !handIsHoldingObject) {
            vr_hand_interaction_update_punch_sound(mario);
            vr_hand_interaction_register_motion_dive_punch(
                mario,
                hand
            );
        }

        if (!canStartInteraction ||
            !positionValid ||
            !configVrPhysicalPunching ||
            handIsHoldingObject) {
            sVrFistActiveFrames[hand] = 0;
            sVrFistPreviousPositionValid[hand] = false;
            continue;
        }

        if (punchStarted) {
            sVrFistActiveFrames[hand] =
                VR_FIST_ACTIVE_FRAMES;
        }

        if (sVrFistActiveFrames[hand] > 0) {
            Vec3f sweepStart;
            vec3f_copy(sweepStart, position);
            if (sVrFistPreviousPositionValid[hand]) {
                Vec3f displacement;
                vec3f_dif(
                    displacement,
                    position,
                    sVrFistPreviousPosition[hand]
                );
                const f32 displacementSquared =
                    displacement[0] * displacement[0] +
                    displacement[1] * displacement[1] +
                    displacement[2] * displacement[2];
                if (displacementSquared <=
                    VR_FIST_MAX_SWEEP_DISTANCE *
                        VR_FIST_MAX_SWEEP_DISTANCE) {
                    vec3f_copy(
                        sweepStart,
                        sVrFistPreviousPosition[hand]
                    );
                }
            }

            const bool hit = vr_hand_interaction_process_lists(
                mario,
                hand,
                sweepStart,
                position,
                velocity
            );
            if (hit) {
                sVrFistActiveFrames[hand] = 0;
            } else {
                sVrFistActiveFrames[hand]--;
            }
        }

        vec3f_copy(
            sVrFistPreviousPosition[hand],
            position
        );
        sVrFistPreviousPositionValid[hand] = true;
    }

    if (canStartInteraction) {
        vr_hand_interaction_process_star_contacts(
            mario,
            collectibleHandPositions,
            collectibleHandPositionValid
        );
    }
}
