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
#include "obj_behaviors.h"
#include "rendering_graph_node.h"
#include "sound_init.h"
#include "sm64.h"
#include "surface_terrains.h"
#include "vr_hand_interaction.h"

#include "pc/configfile.h"
#include "pc/djui/djui.h"
#include "pc/lua/smlua_hooks.h"
#include "pc/network/network.h"
#include "pc/network/network_player.h"
#include "pc/network/coopnet/coopnet.h"
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
#define VR_HAND_COLLISION_MAX_CONTROLLER_SEPARATION 30.5f
#define VR_FIST_MAX_SWEEP_DISTANCE 150.0f
#define VR_PUNCH_SOUND_COMBO_RESET_FRAMES 18
#define VR_MOTION_DIVE_PAIR_WINDOW_FRAMES 5
#define VR_GRIP_CLOSE_THRESHOLD 0.55f
#define VR_GRIP_OPEN_THRESHOLD 0.35f
#define VR_GRAB_EXTRA_REACH 16.0f
#define VR_JRB_SPIKE_GRAB_EXTRA_REACH 56.0f
#define VR_OBJECT_GRAB_EXTRA_REACH \
    (VR_GRAB_EXTRA_REACH + VR_FIST_BASE_RADIUS * 8.0f)
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
#define VR_CLIMB_HANDOFF_GRACE_FRAMES 3
#define VR_PHYSICAL_POLE_SLIDE_MAX_SPEED 6.0f
#define VR_HEADSET_INTERACTION_RADIUS 24.0f
#define VR_HEADSET_INTERACTION_HEIGHT 48.0f
#define VR_THROW_MIN_SPEED 60.0f
#define VR_THROW_VELOCITY_SCALE 0.125f
#define VR_FIREBALL_LAUNCH_VELOCITY_SCALE \
    (VR_THROW_VELOCITY_SCALE * 2.0f)
#define VR_THROW_VELOCITY_MEMORY 0.78f
#define VR_BOWSER_TURN_DEADZONE 0.18f
#define VR_BOWSER_HAND_TURN_FULL_INPUT 0x600
#define VR_BOWSER_HAND_TURN_JITTER 0x60
#define VR_BOWSER_FULL_POWER_ARC 0x1000
#define VR_BOWSER_MIN_HAND_RADIUS_METERS 0.10f
#define VR_FIRE_FLOWER_PICKUP_COUNT 16
#define VR_FIRE_FLOWER_PICKUP_RADIUS 115.0f
#define VR_FIRE_FLOWER_HEAD_PICKUP_RADIUS 58.0f
#define VR_FIRE_FLOWER_HAND_PICKUP_RADIUS 52.0f
#define VR_FIRE_FLOWER_GRAVITY 0.65f
#define VR_FIRE_FLOWER_FALL_SPEED 7.0f
#define VR_FIRE_FLOWER_DURATION_FRAMES 1800U
#define VR_FIRE_FLOWER_PICKUP_GRACE_FRAMES 12U
#define VR_FIREBALL_FORM_DELAY_FRAMES 6U
#define VR_CHARGE_TENTHS_MIN 5U
#define VR_CHARGE_TENTHS_MAX 80U
#define VR_FIREBALL_TRIGGER_THRESHOLD 0.55f
#define VR_FIREBALL_MIN_THROW_SPEED 60.0f
#define VR_FIREBALL_MAX_LIFETIME 180U
#define VR_FIREBALL_PROJECTILE_COUNT 8U
#define VR_HAMMER_SUIT_DURATION_FRAMES 1800U
#define VR_HAMMER_CHARGE_FRAMES 30U
#define VR_HAMMER_PROJECTILE_COUNT 12U
#define VR_HAMMER_VOLLEY_COUNT 3U
#define VR_HAMMER_MAX_LIFETIME 150U
#define VR_HAMMER_GRAVITY 1.75f
#define VR_HAMMER_MIN_HORIZONTAL_SPEED 38.0f
#define VR_HAMMER_MAX_HORIZONTAL_SPEED 72.0f
#define VR_HAMMER_LAUNCH_ARC_BIAS 12.0f
#define VR_HAMMER_CONTACT_PADDING 32.0f
#define VR_HAMMER_MELEE_RADIUS 30.0f
#define VR_HAMMER_PICKUP_GRAVITY 0.65f
#define VR_HAMMER_PICKUP_FALL_SPEED 7.0f
#define VR_RASENGAN_IMPACT_GROW_FRAMES 10U
#define VR_RASENGAN_IMPACT_MAX_FRAMES 90U
// Two rotated, double-sided shells contribute four translucent surfaces.
// Alpha 42 per surface composes to approximately 50% visible opacity.
#define VR_RASENGAN_MAX_OPACITY 56
#define VR_RASENGAN_RING_MAX_OPACITY 205
#define VR_RASENGAN_HAND_SCALE 0.1125f
#define VR_RASENGAN_MODEL_RADIUS 64.0f
#define VR_RASENGAN_CONTACT_RADIUS 18.0f
#define VR_RASENGAN_BOBOMB_CARRY_FRAMES 30U
#define VR_RASENGAN_IMPACT_PADDING 12.0f
#define VR_RASENGAN_SWIRL_TARGET_RADIANS 18.849556f
#define VR_RASENGAN_SWIRL_MIN_RADIUS 12.0f
#define VR_RASENGAN_SWIRL_MAX_RADIUS 280.0f
#define VR_RASENGAN_SWIRL_MIN_STEP 0.003f
#define VR_RASENGAN_SWIRL_MAX_STEP 1.20f
#define VR_RASENGAN_LEFT_GRIP_THRESHOLD 0.45f
#define VR_RASENGAN_RIGHT_OPEN_THRESHOLD 0.50f
#define VR_RASENGAN_READY_LIFETIME_FRAMES 180U
#define VR_RASENGAN_READY_FADE_FRAMES 30U
#define VR_RASEN_SHURIKEN_READY_LIFETIME_FRAMES 300U
#define VR_RASEN_SHURIKEN_MAX_FLIGHT_FRAMES 300U
#define VR_RASEN_SHURIKEN_EXPLOSION_HOLD_FRAMES 45U
#define VR_RASEN_SHURIKEN_EXPLOSION_FADE_FRAMES 15U
#define VR_RASEN_SHURIKEN_EXPLOSION_FRAMES \
    (VR_RASEN_SHURIKEN_EXPLOSION_HOLD_FRAMES + \
     VR_RASEN_SHURIKEN_EXPLOSION_FADE_FRAMES)
#define VR_RASEN_SHURIKEN_FLIGHT_SPEED 67.5f
#define VR_RASEN_SHURIKEN_MIN_THROW_SPEED 55.0f
#define VR_RASEN_SHURIKEN_EXPLOSION_SCALE \
    (VR_RASENGAN_HAND_SCALE * 40.0f)
#define VR_RASEN_SHURIKEN_EXPLOSION_RADIUS \
    (VR_RASENGAN_MODEL_RADIUS * VR_RASEN_SHURIKEN_EXPLOSION_SCALE)
#define VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT 64U

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
static u8 sVrPhysicalPlayerHitCooldown[VR_CONTROLLER_COUNT] = { 0 };
static u8 sVrSpecialPlayerHitCooldown[MAX_PLAYERS] = { 0 };
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
static u8 sVrPhysicalClimbHandoffGraceFrames = 0;
static u32 sVrPhysicalClimbHandoffGraceTimestamp = 0;
static bool sVrPhysicalClimbPendingSwingRelease = false;
static bool sVrPhysicalClimbPendingVelocityValid = false;
static Vec3f sVrPhysicalClimbPendingVelocity = {
    0.0f,
    0.0f,
    0.0f
};
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
static struct Object* sVrHeadsetColliderObject = NULL;
static bool sVrFireFlowerPowered = false;
static u16 sVrFireFlowerTimer = 0;
// This remains finite when the no-timer cheat keeps the power active.
static u16 sVrFireFlowerMusicTimer = 0;
static u16 sVrFireballChargeFrames = 0;
static s16 sVrFireFlowerLevel = -1;
static s16 sVrFireFlowerArea = -1;
static struct Object* sVrFireFlowerPickups[VR_FIRE_FLOWER_PICKUP_COUNT] = { NULL };
static f32 sVrFireFlowerPickupVelocityY[VR_FIRE_FLOWER_PICKUP_COUNT] = { 0.0f };
static bool sVrFireFlowerPickupLanded[VR_FIRE_FLOWER_PICKUP_COUNT] = { false };
static u16 sVrFireFlowerPickupAge[VR_FIRE_FLOWER_PICKUP_COUNT] = { 0 };
static struct Object* sVrRewardRolledBox = NULL;
static u32 sVrRewardRolledBoxTimestamp = 0;
static enum VrBoxReward sVrRolledBoxReward = VR_BOX_REWARD_ORIGINAL;
static struct Object* sVrFireballChargeObject = NULL;
static struct Object*
    sVrFireballProjectiles[VR_FIREBALL_PROJECTILE_COUNT] = { NULL };
static u16
    sVrFireballProjectileLifetime[VR_FIREBALL_PROJECTILE_COUNT] = { 0 };
static Vec3f
    sVrFireballProjectileVelocity[VR_FIREBALL_PROJECTILE_COUNT] = { { 0 } };
static bool
    sVrFireballProjectileLaunchBoost[VR_FIREBALL_PROJECTILE_COUNT] = { false };
static Vec3f sVrFireballRememberedVelocity = { 0.0f, 0.0f, 0.0f };
static bool sVrHammerSuitPowered = false;
static u16 sVrHammerSuitTimer = 0;
static u16 sVrHammerSuitMusicTimer = 0;
static s16 sVrHammerSuitLevel = -1;
static s16 sVrHammerSuitArea = -1;
static struct Object* sVrHammerSuitShellObject = NULL;
static struct Object* sVrHammerSuitPickupObject = NULL;
static f32 sVrHammerSuitPickupVelocityY = 0.0f;
static bool sVrHammerSuitPickupLanded = false;
static u16 sVrHammerSuitPickupAge = 0;
static struct Object* sVrHammerChargeObject = NULL;
static u16 sVrHammerChargeFrames = 0;
static Vec3f sVrHammerRememberedVelocity = { 0.0f, 0.0f, 0.0f };
static struct Object* sVrHammerMeleeContact = NULL;
static struct Object* sVrHammerProjectiles[VR_HAMMER_PROJECTILE_COUNT] = {
    NULL
};
static Vec3f sVrHammerProjectileVelocity[VR_HAMMER_PROJECTILE_COUNT] = {
    { 0.0f, 0.0f, 0.0f }
};
static u16 sVrHammerProjectileLifetime[VR_HAMMER_PROJECTILE_COUNT] = {
    0
};
static struct Object* sVrRasenganObject = NULL;
static struct Object* sVrRasenganTarget = NULL;
static u16 sVrRasenganChargeFrames = 0;
static u16 sVrRasenganImpactFrames = 0;
static u16 sVrRasenganReadyLifetimeFrames = 0;
static Vec3f sVrRasenganImpactVelocity = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrRasenganPreviousOrbitVector = { 0.0f, 0.0f, 0.0f };
static bool sVrRasenganPreviousOrbitValid = false;
static f32 sVrRasenganSwirlRadians = 0.0f;
static u16 sVrRasenganGestureFrames = 0;
static u16 sVrRasenShurikenChargeFrames = 0;
static bool sVrRasenShurikenReady = false;
static Vec3f sVrRasenganRememberedVelocity = { 0.0f, 0.0f, 0.0f };
static struct Object* sVrRasenShurikenProjectile = NULL;
static Vec3f sVrRasenShurikenVelocity = { 0.0f, 0.0f, 0.0f };
static u16 sVrRasenShurikenProjectileFrames = 0;
static u16 sVrRasenShurikenExplosionFrames = 0;
static struct Object* sVrRasenShurikenHitTargets[
    VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT
] = { NULL };
static u16 sVrRasenShurikenHitFirstFrame[
    VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT
] = { 0 };
static u16 sVrRasenShurikenHitLastFrame[
    VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT
] = { 0 };
static bool sVrRasenShurikenHitContinuous[
    VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT
] = { false };
static f32 sVrHeadsetColliderSavedRadius = 50.0f;
static f32 sVrHeadsetColliderSavedHeight = 160.0f;
static f32 sVrHeadsetColliderSavedDownOffset = 0.0f;
struct VrHandCollisionState {
    bool rawPositionValid;
    bool constraintActive;
    struct Object* constraintObject;
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
static bool vr_special_moves_try_rasengan_hit(
    struct MarioState* mario,
    u32 hand,
    const Vec3f velocity,
    struct Object* object
);

static u16 vr_special_moves_charge_frames(unsigned int tenths) {
    tenths = max(VR_CHARGE_TENTHS_MIN,
        min(tenths, VR_CHARGE_TENTHS_MAX));
    return (u16)(tenths * 3U);
}

static u16 vr_special_moves_fireball_ready_frames(void) {
    return VR_FIREBALL_FORM_DELAY_FRAMES +
        vr_special_moves_charge_frames(configVrFireballChargeTime);
}

static u16 vr_special_moves_rasengan_ready_frames(void) {
    return vr_special_moves_charge_frames(configVrRasenganChargeTime);
}

static u16 vr_special_moves_rasen_shuriken_ready_frames(void) {
    return vr_special_moves_charge_frames(
        configVrRasenShurikenChargeTime
    );
}

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
    if (surface == NULL ||
        configVrExperimentalClimbableColliders) {
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
    if (sVrHandCollision[hand].constraintObject != NULL &&
        (sVrHandCollision[hand].constraintObject->activeFlags &
            ACTIVE_FLAG_ACTIVE) == 0) {
        sVrHandCollision[hand].constraintActive = false;
        sVrHandCollision[hand].constraintObject = NULL;
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
    // Collision may visually hold a glove at a surface, but it must never
    // become detached from the physical controller. If satisfying the stale
    // plane would leave the glove over roughly one foot from the live pose,
    // release the constraint immediately and let the render-rate controller
    // matrix pull the glove back—even when that means crossing geometry.
    if (correction > VR_HAND_COLLISION_MAX_CONTROLLER_SEPARATION) {
        sVrHandCollision[hand].constraintActive = false;
        sVrHandCollision[hand].constraintObject = NULL;
        return;
    }
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
    sVrHandCollision[hand].constraintObject = surface->object;
    sVrHandCollision[hand].constraintNormal[0] = surface->normal.x;
    sVrHandCollision[hand].constraintNormal[1] = surface->normal.y;
    sVrHandCollision[hand].constraintNormal[2] = surface->normal.z;
    sVrHandCollision[hand].constraintOriginOffset =
        surface->originOffset;
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

    // Dynamic collision surfaces can disappear on the same frame that a
    // punch breaks their object. Never keep constraining a tracked hand to
    // the stale plane after its block has been deactivated.
    if (state->constraintActive && state->constraintObject != NULL &&
        (state->constraintObject->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        state->constraintActive = false;
        state->constraintObject = NULL;
        state->rawPositionValid = false;
    }

    if (state->constraintActive) {
        const f32 distance =
            state->constraintNormal[0] * rawPosition[0] +
            state->constraintNormal[1] * rawPosition[1] +
            state->constraintNormal[2] * rawPosition[2] +
            state->constraintOriginOffset;
        if (distance < radius + VR_HAND_COLLISION_RELEASE_MARGIN) {
            vr_hand_interaction_apply_hand_collision_position(
                hand,
                position
            );
            collided = distance < radius;
        } else {
            state->constraintActive = false;
            state->constraintObject = NULL;
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
            state->constraintObject = NULL;
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

    // Never let a collision plane leave a glove behind its controller. The
    // visible glove and gameplay fist both snap back after roughly one foot,
    // rather than waiting until the hand is several feet from the headset.
    if (collided || state->constraintActive) {
        const f32 dx = position[0] - rawPosition[0];
        const f32 dy = position[1] - rawPosition[1];
        const f32 dz = position[2] - rawPosition[2];
        if (dx * dx + dy * dy + dz * dz >
            VR_HAND_COLLISION_MAX_CONTROLLER_SEPARATION *
                VR_HAND_COLLISION_MAX_CONTROLLER_SEPARATION) {
            vec3f_copy(position, rawPosition);
            state->constraintActive = false;
            state->constraintObject = NULL;
            collisionSurface = NULL;
            collided = false;
        }
    }

    if (collisionSurface != NULL) {
        vr_hand_interaction_set_hand_constraint(
            hand,
            collisionSurface
        );
        if (mario != NULL &&
            collisionSurface->normal.y < -0.5f &&
            state->rawPositionValid &&
            rawPosition[1] > state->previousRawPosition[1] &&
            mario->vel[1] > 0.0f &&
            (mario->action & ACT_FLAG_AIR) != 0) {
            // An upward fist collision acts like Mario reaching a ceiling:
            // stop the ascent at the hand contact instead of allowing the
            // body/head to continue through the block above it.
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
static bool vr_hand_interaction_point_overlaps_object(
    const Vec3f position,
    f32 radius,
    f32 height,
    struct Object* object
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

    if (sVrHeadsetColliderActive &&
        sVrHeadsetColliderObject != mario->marioObj) {
        if (sVrHeadsetColliderObject != NULL) {
            sVrHeadsetColliderObject->hitboxRadius =
                sVrHeadsetColliderSavedRadius;
            sVrHeadsetColliderObject->hitboxHeight =
                sVrHeadsetColliderSavedHeight;
            sVrHeadsetColliderObject->hitboxDownOffset =
                sVrHeadsetColliderSavedDownOffset;
        }
        sVrHeadsetColliderActive = false;
        sVrHeadsetColliderObject = NULL;
    }

    if (!sVrHeadsetColliderActive) {
        sVrHeadsetColliderSavedRadius =
            mario->marioObj->hitboxRadius;
        sVrHeadsetColliderSavedHeight =
            mario->marioObj->hitboxHeight;
        sVrHeadsetColliderSavedDownOffset =
            mario->marioObj->hitboxDownOffset;
        sVrHeadsetColliderActive = true;
        sVrHeadsetColliderObject = mario->marioObj;
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
    sVrPhysicalClimbHandoffGraceFrames = 0;
    sVrPhysicalClimbHandoffGraceTimestamp = 0;
    sVrPhysicalClimbPendingSwingRelease = false;
    sVrPhysicalClimbPendingVelocityValid = false;
    vec3f_set(sVrPhysicalClimbPendingVelocity, 0.0f, 0.0f, 0.0f);
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

    // Move the native anchor and every cached tracked contact together. If
    // only Mario moved, the controller delta would immediately cancel the
    // platform displacement and leave the headset behind the moving pole.
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
        ((object == sVrTrackedHeldObject &&
          sVrTrackedHeldGripMask != 0) ||
         object == sVrFireballChargeObject ||
         object == sVrHammerChargeObject ||
         (object == sVrRasenganObject &&
          sVrRasenganTarget == NULL));
}

bool vr_hand_interaction_is_hammer_charge_object(
    struct Object* object
) {
    return object != NULL && object == sVrHammerChargeObject;
}

u32 vr_hand_interaction_get_tracked_held_hand(
    struct Object* object
) {
    if (object == NULL || object != sVrTrackedHeldObject ||
        sVrTrackedHeldGripMask == 0 ||
        sVrTrackedHeldHand >= VR_CONTROLLER_COUNT) {
        return VR_CONTROLLER_COUNT;
    }
    return sVrTrackedHeldHand;
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
        object != sVrTrackedHeldObject ||
        sVrTrackedHeldGripMask == 0) {
        return false;
    }

    vec3f_copy(position, sVrTrackedHeldPosition);
    return true;
}

f32 vr_hand_interaction_get_held_object_center_offset(
    struct Object* object
) {
    if (object == NULL) {
        return 0.0f;
    }

    // Baby penguins use a tiny 40-unit gameplay hitbox even though their
    // animated model is much taller. Centering that hitbox on the fist puts
    // the glove through the torso and can fold the head visually into the
    // body. Hold it by its upper body while preserving native held/delivery
    // state and event behavior.
    if (obj_has_behavior(object, bhvPenguinBaby) ||
        obj_has_behavior(object, bhvSmallPenguin)) {
        return 58.0f;
    }

    const f32 objectHeight = fmaxf(
        object->hitboxHeight,
        object->hurtboxHeight
    );
    return clamp(
        objectHeight * 0.5f - object->hitboxDownOffset,
        0.0f,
        80.0f
    );
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
    if (object == sVrHammerChargeObject) {
        if (!vr_get_controller_state(VR_CONTROLLER_RIGHT, &state) ||
            !vr_get_controller_world_fist_from_state(
                VR_CONTROLLER_RIGHT,
                &state,
                handPosition,
                NULL
            )) {
            vec3f_copy(position, &object->oPosX);
            return true;
        }
        handPosition[1] += 2.0f *
            (f32)clamp(configVrGloveSize, 25U, 250U) / 70.0f;
        vec3f_copy(position, handPosition);
        return true;
    }

    if (object == sVrFireballChargeObject ||
        (object == sVrRasenganObject &&
         sVrRasenganTarget == NULL)) {
        if (!vr_get_controller_state(VR_CONTROLLER_RIGHT, &state) ||
            !vr_get_controller_world_palm_from_state(
                VR_CONTROLLER_RIGHT,
                &state,
                handPosition
            )) {
            vec3f_copy(position, &object->oPosX);
            return true;
        }
        vec3f_copy(position, handPosition);
        return true;
    }

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

    const f32 centerOffset =
        vr_hand_interaction_get_held_object_center_offset(object);
    position[0] = handPosition[0];
    position[1] = handPosition[1] - centerOffset;
    position[2] = handPosition[2];
    return true;
}

bool vr_hand_interaction_apply_held_object_transform(
    struct Object* object
) {
    if (!sVrTrackedHeldPositionValid ||
        object != sVrTrackedHeldObject ||
        sVrTrackedHeldGripMask == 0 ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        return false;
    }

    vec3f_copy(&object->oPosX, sVrTrackedHeldPosition);
    vec3f_copy(
        object->header.gfx.prevPos,
        sVrTrackedHeldPreviousPosition
    );
    vec3f_copy(object->header.gfx.pos, sVrTrackedHeldPosition);
    // A physically held actor is positioned entirely by the tracked hand.
    // Clear residual native velocity so its behavior cannot tug the gameplay
    // root between hand updates; release velocity still comes from the
    // controller sample stored by vr_hand_interaction_update_held_position.
    object->oVelX = 0.0f;
    object->oVelY = 0.0f;
    object->oVelZ = 0.0f;
    object->oForwardVel = 0.0f;
    // Gameplay stays at the native simulation rate. Rendering interpolates
    // the object's animation normally, then late-patches its root transform
    // to the newest tracked hand pose once per submitted OpenXR frame.
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
            // The tangent of the physical hand swing is the actual release
            // direction. Keeping this separate from HMD and Mario yaw avoids
            // mirrored or backwards throws when the player looks elsewhere.
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
        // A native dive-grab can enter this action before the physical-tail
        // path has selected a hand. Adopt every closed grip so a deliberate
        // two-handed grab remains two-handed through release.
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

    // Releasing one controller from a two-handed hold transfers Bowser's
    // weight to the remaining hand. The native throw begins only when the
    // final participating grip opens.
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
    const f32 centerOffset =
        vr_hand_interaction_get_held_object_center_offset(object);

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
    if (object == NULL || handPosition == NULL) {
        return false;
    }

    const bool kingBobomb = obj_has_behavior(object, bhvKingBobomb);
    if (!kingBobomb &&
        (object->oInteractionSubtype & INT_SUBTYPE_GRABS_MARIO) == 0) {
        return true;
    }

    const s16 handYaw = atan2s(
        handPosition[2] - object->oPosZ,
        handPosition[0] - object->oPosX
    );
    // Heavy actors that grab Mario, including King Bob-omb, expose only a
    // rear grab zone. A 120-degree rear cone prevents front/side grabs while
    // remaining practical with tracked-hand contact.
    return abs_angle_diff(handYaw, object->oMoveAngleYaw) >= 0x5555;
}

static bool vr_hand_interaction_grab_overlaps_object(
    const Vec3f handPosition,
    f32 handRadius,
    f32 extraReach,
    struct Object* object,
    f32* distanceSquared
);

static bool vr_hand_interaction_grab_overlaps_object(
    const Vec3f handPosition,
    f32 handRadius,
    f32 extraReach,
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
    const bool kingBobomb = obj_has_behavior(object, bhvKingBobomb);
    if (kingBobomb) {
        // King Bob-omb's native interaction volume is intentionally broad
        // enough for his grab-Mario attack. It is not an appropriate hand
        // pickup volume: taking the maximum here allowed that native volume
        // to reach around his sides and into the front. Use a dedicated,
        // visible-body-sized pickup cylinder and the rear-cone test below.
        objectRadius = 145.0f;
        objectHeight = 285.0f;
        // The global physical-grab extension is intentionally generous for
        // small enemies. Do not let it extend through this boss's body.
        extraReach = fminf(extraReach, 24.0f);
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
        objectRadius + handRadius + extraReach;
    const f32 verticalReach =
        handRadius + extraReach;

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
                VR_GRAB_EXTRA_REACH,
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
            if (sVrHeadsetColliderObject != NULL) {
                sVrHeadsetColliderObject->hitboxRadius =
                    sVrHeadsetColliderSavedRadius;
                sVrHeadsetColliderObject->hitboxHeight =
                    sVrHeadsetColliderSavedHeight;
                sVrHeadsetColliderObject->hitboxDownOffset =
                    sVrHeadsetColliderSavedDownOffset;
            }
            if (sVrHeadsetColliderObject == mario->marioObj) {
                vec3f_copy(&mario->marioObj->oPosX, mario->pos);
            }
            sVrHeadsetColliderActive = false;
            sVrHeadsetColliderObject = NULL;
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

static bool vr_hand_interaction_roomscale_action_allowed(
    struct MarioState* mario
) {
    if (mario == NULL ||
        (mario->action & ACT_FLAG_INTANGIBLE) != 0 ||
        mario->freeze >= 2 ||
        vr_hand_interaction_is_physical_climb_active(mario)) {
        return false;
    }
    switch (mario->action & ACT_GROUP_MASK) {
        case ACT_GROUP_STATIONARY:
        case ACT_GROUP_MOVING:
        case ACT_GROUP_AIRBORNE:
        case ACT_GROUP_SUBMERGED:
            return true;
        default:
            return false;
    }
}

void vr_hand_interaction_update_roomscale_body(
    struct MarioState* mario
) {
    if (mario == NULL || mario->marioObj == NULL ||
        !vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON) {
        vr_reset_roomscale_body_tracking();
        return;
    }
    if (!vr_hand_interaction_roomscale_action_allowed(mario)) {
        return;
    }

    Vec3f requestedDisplacement;
    if (!vr_get_roomscale_body_displacement(requestedDisplacement)) {
        return;
    }
    const f32 requestedLength = sqrtf(
        requestedDisplacement[0] * requestedDisplacement[0] +
        requestedDisplacement[2] * requestedDisplacement[2]
    );
    if (requestedLength < 0.001f) {
        return;
    }

    // Limit each gameplay sample while retaining unconsumed room movement.
    // This prevents a fast headset sample from tunnelling through thin walls;
    // any remainder is resolved on following samples.
    const f32 appliedLength = fminf(requestedLength, 36.0f);
    Vec3f step = {
        requestedDisplacement[0] * appliedLength / requestedLength,
        0.0f,
        requestedDisplacement[2] * appliedLength / requestedLength
    };
    const s32 substeps = max((s32)ceilf(appliedLength / 12.0f), 1);
    step[0] /= (f32)substeps;
    step[2] /= (f32)substeps;

    Vec3f startPosition;
    Vec3f resolvedPosition;
    vec3f_copy(startPosition, mario->pos);
    vec3f_copy(resolvedPosition, mario->pos);
    struct Surface* resolvedFloor = mario->floor;
    f32 resolvedFloorHeight = mario->floorHeight;
    const bool grounded =
        (mario->action & (ACT_FLAG_AIR | ACT_FLAG_SWIMMING)) == 0;

    for (s32 substep = 0; substep < substeps; substep++) {
        Vec3f candidate = {
            resolvedPosition[0] + step[0],
            resolvedPosition[1],
            resolvedPosition[2] + step[2]
        };
        struct WallCollisionData lowerWalls = { 0 };
        struct WallCollisionData upperWalls = { 0 };
        resolve_and_return_wall_collisions_data(
            candidate,
            30.0f,
            24.0f,
            &lowerWalls
        );
        resolve_and_return_wall_collisions_data(
            candidate,
            60.0f,
            50.0f,
            &upperWalls
        );

        struct Surface* floor = NULL;
        const f32 floorHeight = find_floor(
            candidate[0],
            candidate[1] + 100.0f,
            candidate[2],
            &floor
        );
        struct Surface* ceiling = NULL;
        const f32 ceilingHeight = find_ceil(
            candidate[0],
            candidate[1] - 80.0f,
            candidate[2],
            &ceiling
        );
        if (floor == NULL ||
            (grounded && floorHeight > candidate[1] + 100.0f) ||
            (ceiling != NULL &&
             ceilingHeight - fmaxf(candidate[1], floorHeight) < 160.0f)) {
            break;
        }

        if (grounded && fabsf(floorHeight - candidate[1]) <= 100.0f) {
            candidate[1] = floorHeight;
        }
        vec3f_copy(resolvedPosition, candidate);
        resolvedFloor = floor;
        resolvedFloorHeight = floorHeight;
        if (upperWalls.numWalls > 0) {
            mario_update_wall(mario, &upperWalls);
        }
    }

    Vec3f actualDisplacement = {
        resolvedPosition[0] - startPosition[0],
        0.0f,
        resolvedPosition[2] - startPosition[2]
    };
    if (fabsf(actualDisplacement[0]) < 0.001f &&
        fabsf(actualDisplacement[2]) < 0.001f) {
        return;
    }

    vec3f_copy(mario->pos, resolvedPosition);
    mario->floor = resolvedFloor;
    mario->floorHeight = resolvedFloorHeight;
    if (grounded &&
        resolvedFloor != NULL &&
        resolvedFloorHeight < mario->pos[1] - 100.0f) {
        set_mario_action(mario, ACT_FREEFALL, 0);
    }
    vec3f_copy(&mario->marioObj->oPosX, mario->pos);
    vec3f_copy(mario->marioObj->header.gfx.pos, mario->pos);
    // Preserve normal render interpolation. The matching tracking-space
    // compensation is interpolated over this same gameplay frame, so the
    // authoritative hitbox moves now without making the visible body snap.
    vr_commit_roomscale_body_displacement(actualDisplacement);
}

bool vr_hand_interaction_validate_headset_damage_contact(
    struct MarioState* mario,
    struct Object* object
) {
    if (!sVrHeadsetColliderActive ||
        mario == NULL ||
        object == NULL ||
        sVrHeadsetColliderObject != mario->marioObj) {
        return true;
    }

    Vec3f headsetPosition;
    if (!vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        return false;
    }

    return vr_hand_interaction_point_overlaps_object(
        headsetPosition,
        VR_HEADSET_INTERACTION_RADIUS,
        VR_HEADSET_INTERACTION_HEIGHT,
        object
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
                    VR_OBJECT_GRAB_EXTRA_REACH,
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

    // Player objects are not guaranteed to be linked into a normal actor
    // list. Mods such as Grab Anybody can explicitly opt a player into the
    // native grabbable contract; scan only those opted-in player objects and
    // leave ordinary INTERACT_PLAYER objects untouched.
    for (s32 playerIndex = 1; playerIndex < MAX_PLAYERS; playerIndex++) {
        struct MarioState* targetMario = &gMarioStates[playerIndex];
        struct Object* object = targetMario->marioObj;
        f32 distanceSquared;
        if (!is_player_active(targetMario) ||
            !vr_hand_interaction_object_can_be_grabbed(mario, object) ||
            !vr_hand_interaction_grab_overlaps_object(
                handPosition,
                handRadius,
                VR_OBJECT_GRAB_EXTRA_REACH,
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

    return nearestObject;
}

static bool vr_hand_interaction_climb_is_occupied(void) {
    return sVrTrackedHeldObject != NULL ||
        sVrTrackedHootObject != NULL ||
        sVrTrackedAnchorObject != NULL ||
        sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE ||
        sVrBowserGripMask != 0;
}

static f32 vr_hand_interaction_pole_extra_reach(
    struct Object* pole
) {
    return gCurrLevelNum == LEVEL_JRB &&
        pole != NULL && obj_has_behavior(pole, bhvPoleGrabbing)
            ? VR_JRB_SPIKE_GRAB_EXTRA_REACH
            : VR_GRAB_EXTRA_REACH;
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
        const f32 extraReach =
            vr_hand_interaction_pole_extra_reach(pole);
        if ((pole->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            pole->oIntangibleTimer != 0 ||
            (pole->oInteractType & INTERACT_POLE) == 0 ||
            !vr_hand_interaction_grab_overlaps_object(
                handPosition,
                handRadius,
                extraReach,
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
    sVrPhysicalClimbHandoffGraceFrames = 0;
    sVrPhysicalClimbHandoffGraceTimestamp = 0;
    sVrPhysicalClimbPendingSwingRelease = false;
    sVrPhysicalClimbPendingVelocityValid = false;
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
    vec3f_copy(
        sVrPhysicalClimbPolePrevPosition,
        &pole->oPosX
    );
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
        ns_coopnet_vr_gameplay_allowed() &&
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
                vr_hand_interaction_pole_extra_reach(
                    sVrPhysicalClimbPole
                ),
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
        // Physical climbing already commits its accumulated camera/HMD
        // displacement into Mario here. Consume the matching room-scale
        // tracking remainder so the general body-follow path cannot apply it
        // a second time on the first frame after release.
        Vec3f roomscaleDisplacement;
        if (vr_get_roomscale_body_displacement(
                roomscaleDisplacement
            )) {
            vr_commit_roomscale_body_displacement(
                roomscaleDisplacement
            );
        }
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

static void vr_hand_interaction_schedule_physical_climb_release(
    const Vec3f releaseVelocity,
    bool allowSwingRelease
) {
    sVrPhysicalClimbHandoffGraceFrames =
        VR_CLIMB_HANDOFF_GRACE_FRAMES;
    sVrPhysicalClimbHandoffGraceTimestamp = gGlobalTimer;
    sVrPhysicalClimbPendingSwingRelease = allowSwingRelease;
    sVrPhysicalClimbPendingVelocityValid =
        releaseVelocity != NULL;
    if (releaseVelocity != NULL) {
        for (u32 axis = 0; axis < 3; axis++) {
            sVrPhysicalClimbPendingVelocity[axis] =
                releaseVelocity[axis];
        }
    } else {
        vec3f_set(
            sVrPhysicalClimbPendingVelocity,
            0.0f,
            0.0f,
            0.0f
        );
    }
}

static void vr_hand_interaction_update_physical_climb_handoff(
    struct MarioState* mario
) {
    if (sVrPhysicalClimbHandoffGraceFrames == 0 ||
        sVrPhysicalClimbType == VR_PHYSICAL_CLIMB_NONE) {
        return;
    }

    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        if (sVrPhysicalClimbHands[hand] &&
            sVrGripPressed[hand]) {
            sVrPhysicalClimbHandoffGraceFrames = 0;
            sVrPhysicalClimbHandoffGraceTimestamp = 0;
            sVrPhysicalClimbPendingSwingRelease = false;
            sVrPhysicalClimbPendingVelocityValid = false;
            return;
        }
    }

    // Let both controllers finish processing the frame that opened the last
    // hand. This gives the other hand three simulation frames (about
    // 0.1 seconds) to take ownership before a fall or swing-jump is applied.
    if (sVrPhysicalClimbHandoffGraceTimestamp == gGlobalTimer) {
        return;
    }

    sVrPhysicalClimbHandoffGraceFrames--;
    if (sVrPhysicalClimbHandoffGraceFrames == 0) {
        vr_hand_interaction_release_physical_climb(
            mario,
            sVrPhysicalClimbPendingVelocityValid
                ? sVrPhysicalClimbPendingVelocity
                : NULL,
            sVrPhysicalClimbPendingSwingRelease
        );
        return;
    }

    // The native pole/hang action remains the temporary anchor during the
    // handoff window. Keep analog locomotion from turning a harmless hand
    // switch into an accidental detach.
    mario->input &= ~INPUT_NONZERO_ANALOG;
    mario->input |= INPUT_ZERO_MOVEMENT;
    if (mario->controller != NULL) {
        mario->controller->stickX = 0.0f;
        mario->controller->stickY = 0.0f;
        mario->controller->stickMag = 0.0f;
    }
    vr_hand_interaction_sync_climb_collider_to_headset(mario);
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
         (!ns_coopnet_vr_gameplay_allowed() ||
          !configVrCheatSurfaceClimbing)) ||
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

        const bool handOpened =
            climbPositionValid &&
            !sVrGripPressed[hand] &&
            configVrPhysicalClimbing;
        if (handOpened) {
            vr_hand_interaction_schedule_physical_climb_release(
                handVelocity,
                true
            );
        } else {
            vr_hand_interaction_release_physical_climb(
                mario,
                climbPositionValid ? handVelocity : NULL,
                false
            );
        }
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
        (object->oInteractType & INTERACT_GRABBABLE) != 0) {
        return false;
    }

    // Moving-actor anchoring is deliberately narrower than native object
    // grabbing. Hoot and Klepto are the only non-carry actors that physical
    // hands may hold; players, NPCs, and ordinary enemies must retain their
    // original interaction behavior.
    return (object->oInteractType & INTERACT_HOOT) != 0 ||
        obj_has_behavior(object, bhvKlepto);
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
                    VR_OBJECT_GRAB_EXTRA_REACH,
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

static bool vr_hand_interaction_sweep_overlaps_player(
    const struct VrFistSweep* sweep,
    f32 fistRadius,
    f32 fistLength,
    struct MarioState* player
) {
    if (sweep == NULL || player == NULL || player->marioObj == NULL) {
        return false;
    }
    struct Object* object = player->marioObj;
    f32 modelScale = object->header.gfx.scale[1];
    if (modelScale < 0.25f) {
        modelScale = 1.0f;
    }
    const f32 playerRadius = fmaxf(
        fmaxf(object->hitboxRadius, object->hurtboxRadius),
        64.0f * modelScale
    );
    // Remote room-scale motion can move the rendered Mario away from the
    // simulation root. Test the fist against the transform the player sees.
    const f32 playerX = object->header.gfx.pos[0];
    const f32 playerBottom = object->header.gfx.pos[1];
    const f32 playerZ = object->header.gfx.pos[2];
    const f32 playerTop = playerBottom + 225.0f * modelScale;
    const f32 collisionRadius = playerRadius + fistRadius;
    const f32 collisionRadiusSquared = collisionRadius * collisionRadius;

    if (playerX < sweep->minimum[0] - collisionRadius ||
        playerX > sweep->maximum[0] + collisionRadius ||
        playerZ < sweep->minimum[2] - collisionRadius ||
        playerZ > sweep->maximum[2] + collisionRadius ||
        playerTop < sweep->minimum[1] - fistLength ||
        playerBottom > sweep->maximum[1] + fistRadius) {
        return false;
    }

    for (u32 sample = 0; sample <= VR_FIST_SWEEP_SAMPLES; sample++) {
        const f32 x = sweep->start[0] + sweep->step[0] * sample;
        const f32 y = sweep->start[1] + sweep->step[1] * sample;
        const f32 z = sweep->start[2] + sweep->step[2] * sample;
        const f32 dx = x - playerX;
        const f32 dz = z - playerZ;
        if (dx * dx + dz * dz <= collisionRadiusSquared &&
            y + fistRadius >= playerBottom &&
            y + fistRadius - fistLength <= playerTop) {
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
        (!ns_coopnet_vr_gameplay_allowed() ||
         !configVrCheatUnderwaterBoxPunching)) {
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

    if (vr_special_moves_try_rasengan_hit(
            mario,
            hand,
            velocity,
            object
        )) {
        return true;
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

    // Physical fists can synchronize a break before the box's own behavior
    // gets a reliable local-attacker signal. Roll the reward at the actual
    // local hit and let the behavior consume this cached result afterward.
    if (obj_has_behavior(object, bhvBreakableBox) ||
        obj_has_behavior(object, bhvBreakableBoxSmall) ||
        obj_has_behavior(object, bhvJumpingBox)) {
        vr_special_moves_roll_box_reward(object, mario);
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

    // The isolated VR-public channel guarantees this packet is understood by
    // both standalone and PC VR. Let the victim's owning peer apply native
    // PVP damage/knockback after this physical hand sweep proves contact.
    if (ns_coopnet_vr_public_session() &&
        sVrPhysicalPlayerHitCooldown[hand] == 0) {
        for (s32 playerIndex = 1;
             playerIndex < MAX_PLAYERS;
             playerIndex++) {
            struct MarioState* victim = &gMarioStates[playerIndex];
            struct Object* victimObject = victim->marioObj;
            if (!is_player_active(victim) || victimObject == NULL ||
                !gNetworkPlayers[playerIndex].connected ||
                !gNetworkPlayers[playerIndex].currAreaSyncValid ||
                !vr_hand_interaction_sweep_overlaps_player(
                    &sweep,
                    fistRadius,
                    fistLength,
                    victim
                )) {
                continue;
            }

            const s16 attackYaw = atan2s(
                victim->pos[2] - mario->pos[2],
                victim->pos[0] - mario->pos[0]
            );
            network_send_vr_player_hit(
                gNetworkPlayers[playerIndex].globalIndex,
                attackYaw,
                VR_PLAYER_ATTACK_PUNCH
            );
            sVrPhysicalPlayerHitCooldown[hand] = 10;
            play_sound(
                SOUND_ACTION_HIT_2,
                victimObject->header.gfx.cameraToObject
            );
            vr_apply_haptic(hand, 0.55f, 0.06f, -1.0f);
            return true;
        }
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
         (mario->action & ACT_FLAG_SWIMMING) == 0 &&
         mario->action != ACT_FLYING &&
         mario->action != ACT_FLYING_TRIPLE_JUMP)) {
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
    // collider update. Scan while physically climbing, swimming, or flying,
    // and only for coins/1-Ups, giving the headset a small extension without
    // moving Mario's environment collider or triggering hazards and warps.
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

static void vr_special_moves_clear_fireball_charge(void) {
    vr_special_moves_delete_object(&sVrFireballChargeObject);
    sVrFireballChargeFrames = 0;
    vec3f_set(sVrFireballRememberedVelocity, 0.0f, 0.0f, 0.0f);
}

static void vr_special_moves_clear_rasengan(void) {
    vr_special_moves_delete_object(&sVrRasenganObject);
    sVrRasenganTarget = NULL;
    sVrRasenganChargeFrames = 0;
    sVrRasenganImpactFrames = 0;
    sVrRasenganReadyLifetimeFrames = 0;
    vec3f_set(sVrRasenganImpactVelocity, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrRasenganPreviousOrbitVector, 0.0f, 0.0f, 0.0f);
    sVrRasenganPreviousOrbitValid = false;
    sVrRasenganSwirlRadians = 0.0f;
    sVrRasenganGestureFrames = 0;
    sVrRasenShurikenChargeFrames = 0;
    sVrRasenShurikenReady = false;
    vec3f_set(sVrRasenganRememberedVelocity, 0.0f, 0.0f, 0.0f);
}

static void vr_special_moves_clear_rasen_shuriken_projectile(void) {
    vr_special_moves_delete_object(&sVrRasenShurikenProjectile);
    vec3f_set(sVrRasenShurikenVelocity, 0.0f, 0.0f, 0.0f);
    sVrRasenShurikenProjectileFrames = 0;
    sVrRasenShurikenExplosionFrames = 0;
    for (u32 i = 0; i < VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT; i++) {
        sVrRasenShurikenHitTargets[i] = NULL;
        sVrRasenShurikenHitFirstFrame[i] = 0;
        sVrRasenShurikenHitLastFrame[i] = 0;
        sVrRasenShurikenHitContinuous[i] = false;
    }
}

static void vr_special_moves_clear_fireball_projectile(u32 slot) {
    if (slot >= VR_FIREBALL_PROJECTILE_COUNT) {
        return;
    }
    vr_special_moves_delete_object(&sVrFireballProjectiles[slot]);
    sVrFireballProjectileLifetime[slot] = 0;
    vec3f_set(
        sVrFireballProjectileVelocity[slot],
        0.0f,
        0.0f,
        0.0f
    );
    sVrFireballProjectileLaunchBoost[slot] = false;
}

static u32 vr_special_moves_allocate_fireball_projectile(void) {
    u32 oldestSlot = 0;
    u16 oldestLifetime = 0;
    for (u32 slot = 0; slot < VR_FIREBALL_PROJECTILE_COUNT; slot++) {
        struct Object* projectile = sVrFireballProjectiles[slot];
        if (projectile == NULL ||
            (projectile->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
            vr_special_moves_clear_fireball_projectile(slot);
            return slot;
        }
        if (sVrFireballProjectileLifetime[slot] >= oldestLifetime) {
            oldestLifetime = sVrFireballProjectileLifetime[slot];
            oldestSlot = slot;
        }
    }

    // Eight simultaneous shots is already far beyond normal charged play. If the
    // pool is saturated, recycle only the oldest shot instead of blocking a
    // new charge or allowing an unbounded object allocation.
    vr_special_moves_clear_fireball_projectile(oldestSlot);
    return oldestSlot;
}

static void vr_special_moves_clear_hammer_charge(void) {
    vr_special_moves_delete_object(&sVrHammerChargeObject);
    sVrHammerChargeFrames = 0;
    sVrHammerMeleeContact = NULL;
    vec3f_set(sVrHammerRememberedVelocity, 0.0f, 0.0f, 0.0f);
}

static void vr_special_moves_clear_hammer_projectile(u32 slot) {
    if (slot >= VR_HAMMER_PROJECTILE_COUNT) {
        return;
    }
    vr_special_moves_delete_object(&sVrHammerProjectiles[slot]);
    vec3f_set(
        sVrHammerProjectileVelocity[slot],
        0.0f,
        0.0f,
        0.0f
    );
    sVrHammerProjectileLifetime[slot] = 0;
}

static void vr_special_moves_reset_hammer_suit(void) {
    if (sVrHammerSuitMusicTimer > 0 &&
        (gMarioStates[0].flags & MARIO_SPECIAL_CAPS) == 0) {
        stop_cap_music();
    }
    sVrHammerSuitPowered = false;
    sVrHammerSuitTimer = 0;
    sVrHammerSuitMusicTimer = 0;
    vr_special_moves_delete_object(&sVrHammerSuitShellObject);
    vr_special_moves_clear_hammer_charge();
    for (u32 slot = 0; slot < VR_HAMMER_PROJECTILE_COUNT; slot++) {
        vr_special_moves_clear_hammer_projectile(slot);
    }
}

static u32 vr_special_moves_allocate_hammer_projectile(void) {
    u32 oldestSlot = 0;
    u16 oldestLifetime = 0;
    for (u32 slot = 0; slot < VR_HAMMER_PROJECTILE_COUNT; slot++) {
        struct Object* projectile = sVrHammerProjectiles[slot];
        if (projectile == NULL ||
            (projectile->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
            vr_special_moves_clear_hammer_projectile(slot);
            return slot;
        }
        if (sVrHammerProjectileLifetime[slot] >= oldestLifetime) {
            oldestLifetime = sVrHammerProjectileLifetime[slot];
            oldestSlot = slot;
        }
    }
    vr_special_moves_clear_hammer_projectile(oldestSlot);
    return oldestSlot;
}

static void vr_special_moves_reset_power(void) {
    if (sVrFireFlowerMusicTimer > 0 &&
        (gMarioStates[0].flags & MARIO_SPECIAL_CAPS) == 0) {
        stop_cap_music();
    }
    sVrFireFlowerPowered = false;
    sVrFireFlowerTimer = 0;
    sVrFireFlowerMusicTimer = 0;
    vr_special_moves_clear_fireball_charge();
    for (u32 slot = 0; slot < VR_FIREBALL_PROJECTILE_COUNT; slot++) {
        vr_special_moves_clear_fireball_projectile(slot);
    }
}

static bool vr_special_moves_online_allowed(void) {
    // No abilities in the title/file-select scene, and no custom gameplay in
    // flat-screen public CoopNet rooms. Solo play, direct/private sessions and
    // the isolated VR-public channel remain supported.
    return configVrSpecialMovesEnabled &&
        !gDjuiInMainMenu &&
        ns_coopnet_vr_gameplay_allowed() &&
        gCurrentArea != NULL &&
        gMarioStates[0].marioObj != NULL &&
        is_player_active(&gMarioStates[0]);
}

static bool vr_special_moves_try_player_hit(
    struct MarioState* attacker,
    const Vec3f position,
    f32 attackRadius,
    u8 attackType
) {
    if (attacker == NULL || position == NULL ||
        !ns_coopnet_vr_public_session() ||
        !vr_special_moves_online_allowed()) {
        return false;
    }

    for (s32 playerIndex = 1; playerIndex < MAX_PLAYERS; playerIndex++) {
        struct MarioState* victim = &gMarioStates[playerIndex];
        struct Object* victimObject = victim->marioObj;
        if (!is_player_active(victim) || victimObject == NULL ||
            !gNetworkPlayers[playerIndex].connected ||
            !gNetworkPlayers[playerIndex].currAreaSyncValid ||
            sVrSpecialPlayerHitCooldown[playerIndex] != 0) {
            continue;
        }

        f32 modelScale = victimObject->header.gfx.scale[1];
        if (modelScale < 0.25f) {
            modelScale = 1.0f;
        }
        const f32 victimRadius = 52.0f * modelScale + attackRadius;
        const f32 victimBottom = victim->pos[1];
        const f32 victimTop = victimBottom + 225.0f * modelScale;
        const f32 dx = position[0] - victim->pos[0];
        const f32 dz = position[2] - victim->pos[2];
        if (dx * dx + dz * dz > victimRadius * victimRadius ||
            position[1] + attackRadius < victimBottom ||
            position[1] - attackRadius > victimTop) {
            continue;
        }

        const s16 attackYaw = atan2s(
            victim->pos[2] - attacker->pos[2],
            victim->pos[0] - attacker->pos[0]
        );
        network_send_vr_player_hit(
            gNetworkPlayers[playerIndex].globalIndex,
            attackYaw,
            attackType
        );
        sVrSpecialPlayerHitCooldown[playerIndex] =
            attackType == VR_PLAYER_ATTACK_RASEN_SHURIKEN ? 45 : 20;
        play_sound(
            SOUND_ACTION_HIT_2,
            victimObject->header.gfx.cameraToObject
        );
        return true;
    }
    return false;
}

bool vr_special_moves_fire_flower_active(void) {
    return configVrSpecialFireFlower &&
        sVrFireFlowerPowered &&
        vr_special_moves_online_allowed();
}

bool vr_special_moves_grant_fire_flower(void) {
    if (!configVrSpecialFireFlower || !vr_is_active() ||
        !vr_special_moves_online_allowed() ||
        gMarioStates[0].marioObj == NULL) {
        return false;
    }
    vr_special_moves_reset_hammer_suit();
    sVrFireFlowerPowered = true;
    sVrFireFlowerTimer = VR_FIRE_FLOWER_DURATION_FRAMES;
    sVrFireFlowerMusicTimer = VR_FIRE_FLOWER_DURATION_FRAMES;
    sVrFireFlowerLevel = gCurrLevelNum;
    sVrFireFlowerArea = gCurrAreaIndex;
    if (configVrSpecialFireFlowerMusic &&
        (gMarioStates[0].flags &
         (MARIO_METAL_CAP | MARIO_VANISH_CAP)) == 0) {
        play_cap_music(
            SEQUENCE_ARGS(4, gLevelValues.wingCapSequence)
        );
    }
    return true;
}

static void vr_special_moves_update_fire_flower_music(
    struct MarioState* mario
) {
    if (!sVrFireFlowerPowered || sVrFireFlowerMusicTimer == 0 ||
        mario == NULL) {
        return;
    }

    const u32 specialCaps = mario->flags & MARIO_SPECIAL_CAPS;
    if (!configVrSpecialFireFlowerMusic) {
        if (specialCaps == 0) {
            stop_cap_music();
        }
        sVrFireFlowerMusicTimer--;
        return;
    }
    // Metal and Vanish Cap own the cap-music channel while active. Wing Cap
    // shares Powerful Mario with the Fire Flower, so the de-duplicated call
    // simply keeps the current track running without overlap or a restart.
    if ((specialCaps & (MARIO_METAL_CAP | MARIO_VANISH_CAP)) == 0 &&
        (sVrFireFlowerMusicTimer > 60 ||
         (specialCaps & MARIO_WING_CAP) != 0)) {
        play_cap_music(
            SEQUENCE_ARGS(4, gLevelValues.wingCapSequence)
        );
    }

    sVrFireFlowerMusicTimer--;
    if (specialCaps == 0 && sVrFireFlowerMusicTimer == 60) {
        fadeout_cap_music();
    } else if (specialCaps == 0 && sVrFireFlowerMusicTimer == 0) {
        stop_cap_music();
    }
}

Gfx* geo_vr_fireball_color(
    s32 callContext,
    struct GraphNode* node,
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
    struct GraphNodeGenerated* generated =
        (struct GraphNodeGenerated*)node;
    generated->fnNode.node.flags =
        (generated->fnNode.node.flags & 0xFF) |
        (LAYER_TRANSPARENT << 8);
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

Gfx* geo_vr_rasengan_color(
    s32 callContext,
    struct GraphNode* node,
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
    struct GraphNodeGenerated* generated =
        (struct GraphNodeGenerated*)node;
    generated->fnNode.node.flags =
        (generated->fnNode.node.flags & 0xFF) |
        (LAYER_TRANSPARENT << 8);
    const bool charged = object == sVrRasenganObject &&
        (sVrRasenShurikenReady ||
         sVrRasenganChargeFrames >=
            vr_special_moves_rasengan_ready_frames());
    const u8 pulseStep = (u8)(gGlobalTimer & 7U);
    const u8 glow = charged
        ? (u8)((pulseStep <= 4U ? pulseStep : 8U - pulseStep) * 6U)
        : 0U;
    gDPSetEnvColor(
        &displayList[0],
        (u8)min(210U + glow, 255U),
        (u8)min(235U + glow / 2U, 255U),
        255,
        (u8)clamp(object->oOpacity, 0, 255)
    );
    gSPEndDisplayList(&displayList[1]);
    return displayList;
}

Gfx* geo_vr_rasengan_ring_color(
    s32 callContext,
    struct GraphNode* node,
    UNUSED void* context
) {
    if (callContext != GEO_CONTEXT_RENDER || node == NULL ||
        gCurGraphNodeObject == NULL) {
        return NULL;
    }
    Gfx* displayList = alloc_display_list(2 * sizeof(Gfx));
    if (displayList == NULL) {
        return NULL;
    }
    struct Object* object = (struct Object*)gCurGraphNodeObject;
    struct GraphNodeGenerated* generated =
        (struct GraphNodeGenerated*)node;
    generated->fnNode.node.flags =
        (generated->fnNode.node.flags & 0xFF) |
        (LAYER_TRANSPARENT << 8);

    f32 reveal = 1.0f;
    if (generated->parameter >= 1 && generated->parameter <= 3) {
        const bool showAll =
            sVrRasenShurikenReady ||
            object == sVrRasenShurikenProjectile;
        if (!showAll) {
            const f32 fadeStart =
                (f32)(generated->parameter - 1) * 15.0f;
            reveal = clamp(
                ((f32)sVrRasenShurikenChargeFrames - fadeStart) /
                    15.0f,
                0.0f,
                1.0f
            );
        }
    }
    const u8 alpha = (u8)clamp(
        (s32)roundf(
            (f32)object->oOpacity *
            (f32)VR_RASENGAN_RING_MAX_OPACITY * reveal /
            (f32)VR_RASENGAN_MAX_OPACITY
        ),
        0,
        VR_RASENGAN_RING_MAX_OPACITY
    );
    // Keep the tornado crown visibly blue-white without tinting it as deeply
    // as the energy sphere. Alpha follows the parent so explosion rings fade
    // out with the bubble instead of popping away.
    gDPSetPrimColor(&displayList[0], 0, 0, 235, 245, 255, alpha);
    gSPEndDisplayList(&displayList[1]);
    return displayList;
}

Gfx* geo_vr_rasengan_visual_spin(
    s32 callContext,
    struct GraphNode* node,
    UNUSED void* context
) {
    if (callContext != GEO_CONTEXT_RENDER || node == NULL ||
        node->next == NULL || gCurGraphNodeObject == NULL) {
        return NULL;
    }

    struct GraphNodeGenerated* generated =
        (struct GraphNodeGenerated*)node;
    struct GraphNodeRotation* rotation =
        (struct GraphNodeRotation*)node->next;
    struct Object* object = (struct Object*)gCurGraphNodeObject;
    f32 speedMultiplier = 1.0f;
    if (object == sVrRasenShurikenProjectile ||
        sVrRasenShurikenReady) {
        speedMultiplier = 2.0f;
    } else if (sVrRasenShurikenChargeFrames > 0) {
        const f32 chargeProgress = clamp(
            (f32)sVrRasenShurikenChargeFrames /
                (f32)MAX(vr_special_moves_rasen_shuriken_ready_frames(), 1),
            0.0f,
            1.0f
        );
        speedMultiplier = 1.0f + chargeProgress;
    }

    // Integrate phase at the headset render cadence. Multiplying absolute
    // time by a changing charge-speed multiplier produces a large phase jump
    // and an apparent direction reversal at the ready-state transition.
    const f32 renderTime = (f32)gGlobalTimer +
        (gRenderingInterpolated
            ? clamp(gRenderingDelta, 0.0f, 1.0f)
            : 0.0f);
    static f32 sVisualTime = -1.0f;
    static f32 sPrimaryPhase = 0.0f;
    static f32 sSecondaryPhase = 0.0f;
    if (sVisualTime < 0.0f || renderTime < sVisualTime ||
        renderTime - sVisualTime > 2.0f) {
        sPrimaryPhase = renderTime * 2304.0f;
        sSecondaryPhase = renderTime * 1664.0f;
    } else if (renderTime > sVisualTime) {
        const f32 elapsed = renderTime - sVisualTime;
        sPrimaryPhase += elapsed * 2304.0f * speedMultiplier;
        sSecondaryPhase += elapsed * 1664.0f * speedMultiplier;
    }
    sVisualTime = renderTime;
    const s16 primary = (s16)(s32)sPrimaryPhase;
    const s16 secondary = (s16)(s32)sSecondaryPhase;

    switch (generated->parameter) {
        case 0:
            vec3s_set(rotation->rotation,
                primary, secondary, (s16)-primary);
            break;
        case 1:
            vec3s_set(rotation->rotation,
                (s16)-secondary, primary,
                (s16)(primary + 0x2000));
            break;
        case 2:
            vec3s_set(rotation->rotation,
                (s16)(0x1000 + secondary), primary,
                (s16)(primary / 2));
            break;
        case 3:
            vec3s_set(rotation->rotation,
                (s16)(0x3800 - primary),
                (s16)(0x1F00 + secondary),
                (s16)-primary);
            break;
        case 4:
            vec3s_set(rotation->rotation,
                (s16)(-0x2700 + secondary),
                (s16)(0x4300 - primary), primary);
            break;
        case 5:
            // Billboard first, then spin the supplied Rasen-Shuriken PNG in
            // its own plane around the centered Rasengan.
            vec3s_set(rotation->rotation, 0, 0, primary);
            break;
        case 6:
            vec3s_set(rotation->rotation,
                (s16)(primary + 0x1800),
                (s16)(-secondary),
                (s16)(secondary + 0x3000));
            break;
        case 7:
            vec3s_set(rotation->rotation,
                (s16)(-primary),
                (s16)(secondary + 0x2800),
                (s16)(primary / 2 + 0x5000));
            break;
        case 8:
            vec3s_set(rotation->rotation,
                (s16)(secondary + 0x4000),
                (s16)(primary + 0x1000),
                (s16)(-secondary));
            break;
    }
    return NULL;
}

Gfx* geo_vr_rasen_shuriken_color(
    s32 callContext,
    struct GraphNode* node,
    UNUSED void* context
) {
    if (callContext != GEO_CONTEXT_RENDER || node == NULL ||
        gCurGraphNodeObject == NULL) {
        return NULL;
    }

    Gfx* displayList = alloc_display_list(2 * sizeof(Gfx));
    if (displayList == NULL) {
        return NULL;
    }

    struct GraphNodeGenerated* generated =
        (struct GraphNodeGenerated*)node;
    generated->fnNode.node.flags =
        (generated->fnNode.node.flags & 0xFF) |
        (LAYER_TRANSPARENT << 8);

    u8 alpha = 255;
    struct Object* object = (struct Object*)gCurGraphNodeObject;
    if (object != sVrRasenShurikenProjectile &&
        !sVrRasenShurikenReady) {
        const f32 chargeProgress = clamp(
            (f32)sVrRasenShurikenChargeFrames /
                (f32)MAX(vr_special_moves_rasen_shuriken_ready_frames(), 1),
            0.0f,
            1.0f
        );
        alpha = (u8)roundf(chargeProgress * 255.0f);
    }

    gDPSetEnvColor(&displayList[0], 255, 255, 255, alpha);
    gSPEndDisplayList(&displayList[1]);
    return displayList;
}

static bool vr_special_moves_spawn_pickup(
    struct Object* parent,
    f32 x,
    f32 y,
    f32 z,
    f32 velocityY
) {
    if (parent == NULL) {
        return false;
    }
    for (u32 i = 0; i < VR_FIRE_FLOWER_PICKUP_COUNT; i++) {
        struct Object* pickup = sVrFireFlowerPickups[i];
        if (pickup != NULL &&
            (pickup->activeFlags & ACTIVE_FLAG_ACTIVE) != 0) {
            continue;
        }

        pickup = spawn_object(parent, MODEL_VR_FIRE_FLOWER, bhvStaticObject);
        if (pickup == NULL) {
            return false;
        }
        pickup->oPosX = x;
        pickup->oPosY = y;
        pickup->oPosZ = z;
        pickup->oOpacity = 255;
        obj_scale(pickup, 0.375f);
        obj_update_gfx_pos_and_angle(pickup);
        sVrFireFlowerPickups[i] = pickup;
        sVrFireFlowerPickupVelocityY[i] = velocityY;
        sVrFireFlowerPickupLanded[i] = false;
        sVrFireFlowerPickupAge[i] = 0;
        return true;
    }
    return false;
}

static bool vr_special_moves_spawn_hammer_pickup(
    struct Object* parent,
    f32 x,
    f32 y,
    f32 z,
    f32 velocityY
) {
    if (parent == NULL || (sVrHammerSuitPickupObject != NULL &&
        (sVrHammerSuitPickupObject->activeFlags & ACTIVE_FLAG_ACTIVE) != 0)) {
        return false;
    }

    sVrHammerSuitPickupObject = spawn_object(
        parent,
        MODEL_VR_HAMMER,
        bhvStaticObject
    );
    if (sVrHammerSuitPickupObject == NULL) {
        return false;
    }
    sVrHammerSuitPickupObject->oPosX = x;
    sVrHammerSuitPickupObject->oPosY = y;
    sVrHammerSuitPickupObject->oPosZ = z;
    sVrHammerSuitPickupObject->oFaceAnglePitch = 0;
    sVrHammerSuitPickupObject->oInteractType = 0;
    obj_scale(sVrHammerSuitPickupObject, 0.35f);
    obj_update_gfx_pos_and_angle(sVrHammerSuitPickupObject);
    sVrHammerSuitPickupVelocityY = velocityY;
    sVrHammerSuitPickupLanded = false;
    sVrHammerSuitPickupAge = 0;
    return true;
}

enum VrBoxReward vr_special_moves_roll_box_reward(
    struct Object* box,
    struct MarioState* owner
) {
    if (box == sVrRewardRolledBox &&
        (u32)(gGlobalTimer - sVrRewardRolledBoxTimestamp) <= 1U) {
        return sVrRolledBoxReward;
    }

    sVrRewardRolledBox = box;
    sVrRewardRolledBoxTimestamp = gGlobalTimer;
    sVrRolledBoxReward = VR_BOX_REWARD_ORIGINAL;
    if (!vr_is_active() || !vr_special_moves_online_allowed() ||
        box == NULL || owner == NULL || owner->playerIndex != 0 ||
        owner->marioObj == NULL) {
        return sVrRolledBoxReward;
    }

    enum VrBoxReward choices[3] = { VR_BOX_REWARD_ORIGINAL };
    u32 choiceCount = 1;
    if (configVrSpecialFireFlower) {
        choices[choiceCount++] = VR_BOX_REWARD_FIRE_FLOWER;
    }
    if (configVrSpecialHammerSuit) {
        choices[choiceCount++] = VR_BOX_REWARD_HAMMER_SUIT;
    }
    const enum VrBoxReward choice = choices[random_u16() % choiceCount];
    bool spawned = false;
    if (choice == VR_BOX_REWARD_FIRE_FLOWER) {
        spawned = vr_special_moves_spawn_pickup(
            owner->marioObj,
            box->oPosX,
            box->oPosY + 65.0f,
            box->oPosZ,
            20.0f
        );
    } else if (choice == VR_BOX_REWARD_HAMMER_SUIT) {
        spawned = vr_special_moves_spawn_hammer_pickup(
            owner->marioObj,
            box->oPosX,
            box->oPosY + 65.0f,
            box->oPosZ,
            20.0f
        );
    }
    if (choice == VR_BOX_REWARD_ORIGINAL || spawned) {
        sVrRolledBoxReward = choice;
    }
    return sVrRolledBoxReward;
}

bool vr_special_moves_spawn_cheat_fire_flower(void) {
    struct MarioState* mario = &gMarioStates[0];
    if (!vr_is_active() ||
        !vr_special_moves_online_allowed() ||
        mario->marioObj == NULL) {
        return false;
    }

    // A cheat-menu spawn should never silently fail because the Special
    // Moves master switch was off. It creates a normal collectible pickup;
    // because gameplay is paused behind the menu, gravity begins only after
    // the player closes the menu.
    configVrSpecialFireFlower = true;
    return vr_special_moves_spawn_pickup(
        mario->marioObj,
        mario->pos[0],
        mario->pos[1] + 480.0f,
        mario->pos[2],
        0.0f
    );
}

static void vr_special_moves_update_pickups(struct MarioState* mario) {
    if (mario == NULL || mario->marioObj == NULL) {
        return;
    }
    Vec3f headsetPosition;
    const bool headsetValid =
        vr_get_stabilized_headset_world_position(headsetPosition, false);
    Vec3f handPositions[VR_CONTROLLER_COUNT];
    bool handValid[VR_CONTROLLER_COUNT] = { false, false };
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        struct VrControllerState state = { 0 };
        Vec3f velocity;
        handValid[hand] =
            vr_get_controller_state(hand, &state) &&
            vr_get_controller_world_fist_raw_from_state(
                hand,
                &state,
                handPositions[hand],
                velocity
            );
    }

    for (u32 i = 0; i < VR_FIRE_FLOWER_PICKUP_COUNT; i++) {
        struct Object* pickup = sVrFireFlowerPickups[i];
        if (pickup == NULL) {
            continue;
        }
        if ((pickup->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
            sVrFireFlowerPickups[i] = NULL;
            sVrFireFlowerPickupVelocityY[i] = 0.0f;
            sVrFireFlowerPickupLanded[i] = false;
            sVrFireFlowerPickupAge[i] = 0;
            continue;
        }

        if (sVrFireFlowerPickupAge[i] < 0xFFFFU) {
            sVrFireFlowerPickupAge[i]++;
        }

        pickup->oFaceAngleYaw += 0x600;
        if (sVrFireFlowerPickupLanded[i]) {
            struct Surface* support = NULL;
            const f32 supportHeight = find_floor(
                pickup->oPosX,
                pickup->oPosY + 80.0f,
                pickup->oPosZ,
                &support
            );
            if (support == NULL ||
                fabsf(pickup->oPosY - supportHeight) > 2.0f) {
                // The cork box can disappear after briefly being detected as
                // support. Resume gravity instead of leaving the flower aloft.
                sVrFireFlowerPickupLanded[i] = false;
                sVrFireFlowerPickupVelocityY[i] = 0.0f;
            } else {
                pickup->oPosY = supportHeight;
            }
        }
        if (!sVrFireFlowerPickupLanded[i]) {
            pickup->oPosY += sVrFireFlowerPickupVelocityY[i];
            sVrFireFlowerPickupVelocityY[i] = fmaxf(
                sVrFireFlowerPickupVelocityY[i] -
                    VR_FIRE_FLOWER_GRAVITY,
                -VR_FIRE_FLOWER_FALL_SPEED
            );

            struct Surface* floor = NULL;
            const f32 floorHeight = find_floor(
                pickup->oPosX,
                pickup->oPosY + 80.0f,
                pickup->oPosZ,
                &floor
            );
            // Never treat the top of the box as a landing surface while the
            // flower is still rising out of it.
            if (sVrFireFlowerPickupVelocityY[i] <= 0.0f &&
                floor != NULL && pickup->oPosY <= floorHeight) {
                pickup->oPosY = floorHeight;
                sVrFireFlowerPickupVelocityY[i] = 0.0f;
                sVrFireFlowerPickupLanded[i] = true;
            }
        }
        obj_update_gfx_pos_and_angle(pickup);

        // A box can be broken while its center is already inside Mario's
        // pickup radius. Let the reward visibly emerge before body, head, or
        // hand collection becomes active.
        if (sVrFireFlowerPickupAge[i] <
            VR_FIRE_FLOWER_PICKUP_GRACE_FRAMES) {
            continue;
        }

        const f32 px = pickup->oPosX;
        const f32 py = pickup->oPosY + 28.0f;
        const f32 pz = pickup->oPosZ;
        const f32 bodyDx = px - mario->marioObj->oPosX;
        const f32 bodyDy = py -
            (mario->marioObj->oPosY + 45.0f);
        const f32 bodyDz = pz - mario->marioObj->oPosZ;
        bool collected = bodyDx * bodyDx + bodyDy * bodyDy +
            bodyDz * bodyDz <=
            VR_FIRE_FLOWER_PICKUP_RADIUS *
                VR_FIRE_FLOWER_PICKUP_RADIUS;
        u32 collectingHand = VR_CONTROLLER_COUNT;
        if (!collected && headsetValid) {
            const f32 dx = px - headsetPosition[0];
            const f32 dy = py - headsetPosition[1];
            const f32 dz = pz - headsetPosition[2];
            collected = dx * dx + dy * dy + dz * dz <=
                VR_FIRE_FLOWER_HEAD_PICKUP_RADIUS *
                    VR_FIRE_FLOWER_HEAD_PICKUP_RADIUS;
        }
        for (u32 hand = 0;
             !collected && hand < VR_CONTROLLER_COUNT;
             hand++) {
            if (!handValid[hand]) {
                continue;
            }
            const f32 dx = px - handPositions[hand][0];
            const f32 dy = py - handPositions[hand][1];
            const f32 dz = pz - handPositions[hand][2];
            if (dx * dx + dy * dy + dz * dz <=
                VR_FIRE_FLOWER_HAND_PICKUP_RADIUS *
                    VR_FIRE_FLOWER_HAND_PICKUP_RADIUS) {
                collected = true;
                collectingHand = hand;
            }
        }

        if (collected) {
            vr_special_moves_grant_fire_flower();
            if (collectingHand < VR_CONTROLLER_COUNT) {
                vr_apply_haptic(
                    collectingHand,
                    0.45f,
                    0.10f,
                    -1.0f
                );
            } else {
                vr_apply_haptic(
                    VR_CONTROLLER_LEFT,
                    0.30f,
                    0.08f,
                    -1.0f
                );
                vr_apply_haptic(
                    VR_CONTROLLER_RIGHT,
                    0.30f,
                    0.08f,
                    -1.0f
                );
            }
            vr_special_moves_delete_object(&sVrFireFlowerPickups[i]);
            sVrFireFlowerPickupVelocityY[i] = 0.0f;
            sVrFireFlowerPickupLanded[i] = false;
            sVrFireFlowerPickupAge[i] = 0;
        }
    }
}

static bool vr_special_moves_target_is_whomp(struct Object* target) {
    return target != NULL &&
        (obj_has_behavior(target, bhvSmallWhomp) ||
         obj_has_behavior(target, bhvWhompKingBoss));
}

bool vr_special_moves_spawn_cheat_cap(enum VrCheatSpawnCap cap) {
    struct MarioState* mario = &gMarioStates[0];
    if (!vr_special_moves_online_allowed() ||
        mario->marioObj == NULL || mario->character == NULL) {
        return false;
    }

    s32 model = mario->character->capModelId;
    const BehaviorScript* behavior = bhvVanishCap;
    switch (cap) {
        case VR_CHEAT_SPAWN_WING_CAP:
            model = mario->character->capWingModelId;
            behavior = bhvWingCap;
            break;
        case VR_CHEAT_SPAWN_METAL_CAP:
            model = mario->character->capMetalModelId;
            behavior = bhvMetalCap;
            break;
        case VR_CHEAT_SPAWN_VANISH_CAP:
            break;
        default:
            return false;
    }

    struct Object* pickup = spawn_object(mario->marioObj, model, behavior);
    if (pickup == NULL) {
        return false;
    }
    pickup->oPosX = mario->pos[0];
    pickup->oPosY = mario->pos[1] + 480.0f;
    pickup->oPosZ = mario->pos[2];
    pickup->oVelY = 0.0f;
    obj_update_gfx_pos_and_angle(pickup);
    return true;
}

bool vr_special_moves_hammer_suit_active(void) {
    return configVrSpecialHammerSuit && sVrHammerSuitPowered &&
        vr_special_moves_online_allowed();
}

bool vr_special_moves_grant_hammer_suit(void) {
    if (!configVrSpecialHammerSuit || !vr_is_active() ||
        !vr_special_moves_online_allowed() ||
        gMarioStates[0].marioObj == NULL) {
        return false;
    }
    vr_special_moves_reset_power();
    sVrHammerSuitPowered = true;
    sVrHammerSuitTimer = VR_HAMMER_SUIT_DURATION_FRAMES;
    sVrHammerSuitMusicTimer = VR_HAMMER_SUIT_DURATION_FRAMES;
    sVrHammerSuitLevel = gCurrLevelNum;
    sVrHammerSuitArea = gCurrAreaIndex;
    if ((gMarioStates[0].flags &
         (MARIO_METAL_CAP | MARIO_VANISH_CAP)) == 0) {
        play_cap_music(
            SEQUENCE_ARGS(4, gLevelValues.wingCapSequence)
        );
    }
    return true;
}

static void vr_special_moves_update_hammer_suit_music(
    struct MarioState* mario
) {
    if (!sVrHammerSuitPowered || sVrHammerSuitMusicTimer == 0 ||
        mario == NULL) {
        return;
    }

    const u32 specialCaps = mario->flags & MARIO_SPECIAL_CAPS;
    // Metal and Vanish Cap own the cap-music channel. Wing Cap and Hammer
    // Suit both use Powerful Mario, so replaying this sequence only keeps the
    // existing shared track alive instead of starting an overlapping copy.
    if ((specialCaps & (MARIO_METAL_CAP | MARIO_VANISH_CAP)) == 0 &&
        (sVrHammerSuitMusicTimer > 60 ||
         (specialCaps & MARIO_WING_CAP) != 0)) {
        play_cap_music(
            SEQUENCE_ARGS(4, gLevelValues.wingCapSequence)
        );
    }

    sVrHammerSuitMusicTimer--;
    if (specialCaps == 0 && sVrHammerSuitMusicTimer == 60) {
        fadeout_cap_music();
    } else if (specialCaps == 0 && sVrHammerSuitMusicTimer == 0) {
        stop_cap_music();
    }
}

bool vr_special_moves_spawn_cheat_hammer_suit(void) {
    struct MarioState* mario = &gMarioStates[0];
    if (!vr_special_moves_online_allowed() ||
        !configVrSpecialHammerSuit || mario->marioObj == NULL) {
        return false;
    }
    vr_special_moves_delete_object(&sVrHammerSuitPickupObject);
    return vr_special_moves_spawn_hammer_pickup(
        mario->marioObj,
        mario->pos[0],
        mario->pos[1] + 480.0f,
        mario->pos[2],
        0.0f
    );
}

static void vr_special_moves_update_hammer_suit_pickup(
    struct MarioState* mario
) {
    struct Object* pickup = sVrHammerSuitPickupObject;
    if (pickup == NULL || mario == NULL) {
        return;
    }
    if ((pickup->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        sVrHammerSuitPickupObject = NULL;
        sVrHammerSuitPickupAge = 0;
        return;
    }

    if (sVrHammerSuitPickupAge < 0xFFFFU) {
        sVrHammerSuitPickupAge++;
    }

    if (sVrHammerSuitPickupLanded) {
        struct Surface* support = NULL;
        const f32 supportHeight = find_floor(
            pickup->oPosX,
            pickup->oPosY + 80.0f,
            pickup->oPosZ,
            &support
        );
        if (support == NULL ||
            fabsf(pickup->oPosY - (supportHeight + 38.0f)) > 2.0f) {
            // A broken box can briefly be reported as support. Resume the
            // fall when that temporary geometry disappears.
            sVrHammerSuitPickupLanded = false;
            sVrHammerSuitPickupVelocityY = 0.0f;
        } else {
            pickup->oPosY = supportHeight + 38.0f;
        }
    }
    if (!sVrHammerSuitPickupLanded) {
        pickup->oPosY += sVrHammerSuitPickupVelocityY;
        sVrHammerSuitPickupVelocityY = fmaxf(
            sVrHammerSuitPickupVelocityY - VR_HAMMER_PICKUP_GRAVITY,
            -VR_HAMMER_PICKUP_FALL_SPEED
        );
        struct Surface* floor = NULL;
        const f32 floorHeight = find_floor(
            pickup->oPosX,
            pickup->oPosY + 80.0f,
            pickup->oPosZ,
            &floor
        );
        // Match the Fire Flower: emerge from a box before gravity is allowed
        // to settle the pickup onto the supporting surface.
        if (sVrHammerSuitPickupVelocityY <= 0.0f && floor != NULL &&
            pickup->oPosY <= floorHeight + 38.0f) {
            pickup->oPosY = floorHeight + 38.0f;
            sVrHammerSuitPickupVelocityY = 0.0f;
            sVrHammerSuitPickupLanded = true;
        }
    }
    pickup->oFaceAngleYaw += 0x300;
    pickup->oFaceAngleRoll += 0x180;
    obj_update_gfx_pos_and_angle(pickup);

    if (sVrHammerSuitPickupAge < VR_FIRE_FLOWER_PICKUP_GRACE_FRAMES) {
        return;
    }

    const f32 px = pickup->oPosX;
    const f32 py = pickup->oPosY;
    const f32 pz = pickup->oPosZ;
    const f32 bodyDx = px - mario->marioObj->oPosX;
    const f32 bodyDy = py - (mario->marioObj->oPosY + 45.0f);
    const f32 bodyDz = pz - mario->marioObj->oPosZ;
    bool collected = bodyDx * bodyDx + bodyDy * bodyDy +
        bodyDz * bodyDz <=
        VR_FIRE_FLOWER_PICKUP_RADIUS * VR_FIRE_FLOWER_PICKUP_RADIUS;
    u32 collectingHand = VR_CONTROLLER_COUNT;

    Vec3f headsetPosition;
    if (!collected && vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        )) {
        const f32 dx = px - headsetPosition[0];
        const f32 dy = py - headsetPosition[1];
        const f32 dz = pz - headsetPosition[2];
        collected = dx * dx + dy * dy + dz * dz <=
            VR_FIRE_FLOWER_HEAD_PICKUP_RADIUS *
                VR_FIRE_FLOWER_HEAD_PICKUP_RADIUS;
    }
    for (u32 hand = 0;
         !collected && hand < VR_CONTROLLER_COUNT;
         hand++) {
        struct VrControllerState state = { 0 };
        Vec3f handPosition;
        Vec3f handVelocity;
        if (!vr_get_controller_state(hand, &state) ||
            !vr_get_controller_world_fist_raw_from_state(
                hand,
                &state,
                handPosition,
                handVelocity
            )) {
            continue;
        }
        const f32 dx = px - handPosition[0];
        const f32 dy = py - handPosition[1];
        const f32 dz = pz - handPosition[2];
        if (dx * dx + dy * dy + dz * dz <=
            VR_FIRE_FLOWER_HAND_PICKUP_RADIUS *
                VR_FIRE_FLOWER_HAND_PICKUP_RADIUS) {
            collected = true;
            collectingHand = hand;
        }
    }

    if (collected) {
        if (vr_special_moves_grant_hammer_suit()) {
            if (collectingHand < VR_CONTROLLER_COUNT) {
                vr_apply_haptic(collectingHand, 0.45f, 0.10f, -1.0f);
            } else {
                vr_apply_haptic(VR_CONTROLLER_LEFT, 0.30f, 0.08f, -1.0f);
                vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.30f, 0.08f, -1.0f);
            }
            vr_special_moves_delete_object(&sVrHammerSuitPickupObject);
            sVrHammerSuitPickupAge = 0;
        }
    }
}

static void vr_special_moves_update_hammer_suit_shell(
    struct MarioState* mario
) {
    if (!vr_special_moves_hammer_suit_active() || mario == NULL ||
        mario->marioObj == NULL) {
        vr_special_moves_delete_object(&sVrHammerSuitShellObject);
        return;
    }
    if (sVrHammerSuitShellObject == NULL) {
        sVrHammerSuitShellObject = spawn_object(
            mario->marioObj,
            MODEL_VR_HAMMER_SHELL,
            bhvStaticObject
        );
        if (sVrHammerSuitShellObject == NULL) {
            return;
        }
        sVrHammerSuitShellObject->oInteractType = 0;
    }

    // Drive both interpolation endpoints from Mario's corresponding rendered
    // body samples. Copying the shell's own previous transform made it trail
    // Mario by a simulation update and produced the same intermittent jitter
    // that physically held actors used to show in third person.
    const s16 yaw = mario->marioObj->header.gfx.angle[1];
    const s16 previousYaw =
        mario->marioObj->header.gfx.prevAngle[1];
    sVrHammerSuitShellObject->oPosX =
        mario->marioObj->header.gfx.pos[0] - sins(yaw) * 8.0f;
    sVrHammerSuitShellObject->oPosY =
        mario->marioObj->header.gfx.pos[1] + 68.0f;
    sVrHammerSuitShellObject->oPosZ =
        mario->marioObj->header.gfx.pos[2] - coss(yaw) * 8.0f;
    sVrHammerSuitShellObject->oFaceAnglePitch = -0x4000;
    sVrHammerSuitShellObject->oFaceAngleYaw = yaw;
    sVrHammerSuitShellObject->oFaceAngleRoll = 0;
    obj_scale(sVrHammerSuitShellObject, 0.44f);
    obj_update_gfx_pos_and_angle(sVrHammerSuitShellObject);
    sVrHammerSuitShellObject->header.gfx.prevPos[0] =
        mario->marioObj->header.gfx.prevPos[0] -
        sins(previousYaw) * 8.0f;
    sVrHammerSuitShellObject->header.gfx.prevPos[1] =
        mario->marioObj->header.gfx.prevPos[1] + 68.0f;
    sVrHammerSuitShellObject->header.gfx.prevPos[2] =
        mario->marioObj->header.gfx.prevPos[2] -
        coss(previousYaw) * 8.0f;
    sVrHammerSuitShellObject->header.gfx.prevAngle[0] = -0x4000;
    sVrHammerSuitShellObject->header.gfx.prevAngle[1] = previousYaw;
    sVrHammerSuitShellObject->header.gfx.prevAngle[2] = 0;
}

static bool vr_special_moves_point_is_behind_target(
    struct Object* target,
    f32 x,
    f32 z
) {
    if (target == NULL) {
        return false;
    }
    const s16 hitYaw = atan2s(z - target->oPosZ, x - target->oPosX);
    // Use the rear hemisphere. The old 120-degree cone was too narrow for
    // wide Whomp geometry and could reject a hand visibly touching its back.
    return abs_angle_diff(hitYaw, target->oMoveAngleYaw) >= 0x4000;
}

static void vr_special_moves_get_enemy_contact_bounds(
    struct Object* target,
    f32* radius,
    f32* height
) {
    const bool kingWhomp =
        target != NULL && obj_has_behavior(target, bhvWhompKingBoss);
    if (target != NULL && obj_has_behavior(target, bhvKingBobomb)) {
        // Keep special-move contact on the visible back rather than allowing
        // the old full-body sphere to trigger from beside or above the boss.
        // This does not alter King Bob-omb's native grab/dive hitbox. The
        // impact visual uses separate full-body bounds below.
        *radius = 150.0f;
        *height = 300.0f;
        return;
    }
    if (vr_special_moves_target_is_whomp(target)) {
        // Whomps are surface objects and deliberately have no ordinary
        // attack hitbox. Supply bounds around their full rendered geometry,
        // including the horizontal belly-flop pose.
        *radius = kingWhomp ? 520.0f : 260.0f;
        *height = kingWhomp ? 900.0f : 450.0f;
        return;
    }
    *radius = fmaxf(
        fmaxf(target->hitboxRadius, target->hurtboxRadius),
        20.0f
    );
    *height = fmaxf(
        fmaxf(target->hitboxHeight, target->hurtboxHeight),
        40.0f
    );
}

static void vr_special_moves_release_mario_from_grabber(
    struct MarioState* mario,
    struct Object* target
) {
    if (mario == NULL || target == NULL) {
        return;
    }
    target->oInteractStatus &= ~INT_STATUS_GRABBED_MARIO;
    target->usingObj = NULL;
    if (mario->action == ACT_GRABBED &&
        (mario->usedObj == target || mario->heldByObj == target)) {
        mario->usedObj = NULL;
        mario->heldByObj = NULL;
        mario->vel[1] = fmaxf(mario->vel[1], 10.0f);
        mario->forwardVel = 0.0f;
        set_mario_action(mario, ACT_FREEFALL, 0);
    }
}

static void vr_special_moves_apply_rear_target_hit(
    struct MarioState* mario,
    struct Object* target,
    f32 hitX,
    f32 hitZ
) {
    if (mario == NULL || target == NULL) {
        return;
    }
    const s16 awayYaw = atan2s(
        target->oPosZ - hitZ,
        target->oPosX - hitX
    );
    mario->interactObj = target;
    if (obj_has_behavior(target, bhvKingBobomb)) {
        // Use the boss's native thrown state so landing counts exactly one
        // hit and retains its normal three-hit fight and defeat sequence.
        vr_special_moves_release_mario_from_grabber(mario, target);
        target->oMoveAngleYaw = awayYaw;
        target->oAction = 4;
        target->oSubAction = 0;
        target->oMoveFlags &= ~OBJ_MOVE_MASK_ON_GROUND;
        target->oForwardVel = fmaxf(target->oForwardVel, 52.0f);
        target->oVelY = fmaxf(target->oVelY, 35.0f);
    } else if (obj_has_behavior(target, bhvWhompKingBoss)) {
        target->oHealth = max(target->oHealth - 1, 0);
        target->oForwardVel = 0.0f;
        target->oVelY = 0.0f;
        target->oAction = target->oHealth == 0 ? 8 : 2;
        target->oSubAction = 0;
        play_sound(
            SOUND_OBJ2_WHOMP_SOUND_SHORT,
            target->header.gfx.cameraToObject
        );
    } else if (obj_has_behavior(target, bhvSmallWhomp)) {
        target->oNumLootCoins = max(target->oNumLootCoins, 5);
        obj_spawn_loot_yellow_coins(
            target,
            target->oNumLootCoins,
            20.0f
        );
        target->oForwardVel = 0.0f;
        target->oVelY = 0.0f;
        target->oAction = 8;
        target->oSubAction = 0;
    }
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        target,
        target->oInteractType,
        true
    );
    network_send_object(target);
}

static bool vr_special_moves_rasengan_target_is_eligible(
    struct MarioState* mario,
    struct Object* object
);
static struct Object* vr_special_moves_resolve_rasengan_target(
    struct Object* object
);
static void vr_special_moves_apply_rasen_shuriken_damage(
    struct MarioState* mario,
    struct Object* explosion,
    struct Object* object
);

static bool vr_special_moves_projectile_hits_enemy(
    struct MarioState* mario,
    struct Object* projectile,
    bool explosiveImpact,
    bool hammerImpact
) {
    if (mario == NULL || projectile == NULL || gObjectLists == NULL) {
        return false;
    }
    if (vr_special_moves_try_player_hit(
            mario,
            &projectile->oPosX,
            hammerImpact ? 45.0f : 32.0f,
            hammerImpact
                ? VR_PLAYER_ATTACK_HAMMER
                : VR_PLAYER_ATTACK_FIREBALL
        )) {
        return true;
    }
    for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        for (struct ObjectNode* node = list->next;
             node != NULL && node != list;
             node = node->next) {
            struct Object* target = (struct Object*)node;
            const u32 enemyTypes = INTERACT_DAMAGE | INTERACT_BULLY |
                INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 |
                INTERACT_KOOPA | INTERACT_SPINY_WALKING |
                INTERACT_MR_BLIZZARD | INTERACT_CLAM_OR_BUBBA;
            const bool kingBobomb =
                obj_has_behavior(target, bhvKingBobomb) &&
                target->oAction == 2;
            const bool kingWhomp =
                obj_has_behavior(target, bhvWhompKingBoss) &&
                target->oAction >= 2 && target->oAction < 8;
            const bool protectedBoss =
                obj_has_behavior(target, bhvBowser) ||
                obj_has_behavior(target, bhvBowserBodyAnchor) ||
                obj_has_behavior(target, bhvBowserTailAnchor) ||
                (obj_has_behavior(target, bhvWhompKingBoss) &&
                    !hammerImpact);
            const bool grabbableEnemy =
                obj_has_behavior(target, bhvBobomb) ||
                obj_has_behavior(target, bhvChuckya) ||
                obj_has_behavior(target, bhvHeaveHo) ||
                kingBobomb || (hammerImpact && kingWhomp);
            const bool regularWhomp =
                obj_has_behavior(target, bhvSmallWhomp) &&
                target->oAction < 8;
            if (target == projectile || target == mario->marioObj ||
                (target->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
                continue;
            }
            if (hammerImpact) {
                // Hammer Suit follows the supported special-move enemy list,
                // but Mr. Blizzard is intentionally immune: melting or
                // exploding a snowman with a physical hammer is not a
                // sensible Hammer Suit interaction.
                if (obj_has_behavior(target, bhvMrBlizzard)) {
                    continue;
                }
                if (!vr_special_moves_rasengan_target_is_eligible(
                        mario,
                        target
                    )) {
                    continue;
                }
            } else if (protectedBoss ||
                (target->oIntangibleTimer != 0 && !regularWhomp) ||
                ((target->oInteractType & enemyTypes) == 0 &&
                 !grabbableEnemy && !regularWhomp)) {
                continue;
            }
            const f32 dx = projectile->oPosX - target->oPosX;
            const f32 dz = projectile->oPosZ - target->oPosZ;
            const f32 targetBottom = target->oPosY - target->hitboxDownOffset;
            f32 contactRadius = 0.0f;
            f32 targetHeight = 0.0f;
            vr_special_moves_get_enemy_contact_bounds(
                target,
                &contactRadius,
                &targetHeight
            );
            const f32 targetTop = targetBottom + targetHeight;
            const f32 contactPadding = hammerImpact
                ? VR_HAMMER_CONTACT_PADDING
                : 24.0f;
            const f32 radius = fmaxf(contactRadius, 35.0f) +
                contactPadding;
            if (dx * dx + dz * dz > radius * radius ||
                projectile->oPosY < targetBottom - contactPadding ||
                projectile->oPosY > targetTop + contactPadding) {
                continue;
            }

            const bool rearTarget = regularWhomp || kingBobomb ||
                (hammerImpact && kingWhomp);
            if (rearTarget) {
                if (!vr_special_moves_point_is_behind_target(
                        target,
                        projectile->oPosX,
                        projectile->oPosZ
                    )) {
                    continue;
                }
            }

            if (hammerImpact) {
                vr_special_moves_apply_rasen_shuriken_damage(
                    mario,
                    projectile,
                    vr_special_moves_resolve_rasengan_target(target)
                );
                play_sound(
                    SOUND_ACTION_HIT_2,
                    projectile->header.gfx.cameraToObject
                );
                return true;
            }

            if (rearTarget) {
                vr_special_moves_apply_rear_target_hit(
                    mario,
                    target,
                    projectile->oPosX,
                    projectile->oPosZ
                );
            }

            if (!rearTarget) {
                mario->interactObj = target;
                attack_object(
                    mario,
                    target,
                    obj_has_behavior(target, bhvGoomba) &&
                        target->oGoombaSize == GOOMBA_SIZE_HUGE
                        ? INT_GROUND_POUND_OR_TWIRL
                        : INT_PUNCH
                );
                // Match a normal Bob-omb blast's damage status. This
                // preserves native enemy reaction/death logic.
                target->oInteractStatus |= INT_STATUS_TOUCHED_BOB_OMB;
            }
            if (!rearTarget && obj_has_behavior(target, bhvMrBlizzard)) {
                // Jumping Mr. Blizzards do not consume the generic Bob-omb
                // status used by most enemies. Give this enemy an explicit
                // one-hit Fire Flower defeat. Its behavior keeps it defeated
                // for 30 seconds before restoring the correct snowman type.
                if (target->oNumLootCoins > 0) {
                    obj_spawn_loot_yellow_coins(
                        target,
                        target->oNumLootCoins,
                        20.0f
                    );
                }
                target->oAction = MR_BLIZZARD_ACT_DEATH;
                target->prevObj = target->oMrBlizzardHeldObj = NULL;
                target->oMrBlizzardTimer = 30 * 30;
                network_send_object(target);
            }
            if (explosiveImpact) {
                struct Object* explosion = spawn_object(
                    projectile,
                    MODEL_EXPLOSION,
                    bhvExplosion
                );
                if (explosion != NULL) {
                    obj_scale(explosion, 0.2f);
                }
                play_sound(
                    SOUND_GENERAL_BOWSER_BOMB_EXPLOSION,
                    projectile->header.gfx.cameraToObject
                );
            } else {
                play_sound(
                    SOUND_ACTION_HIT_2,
                    projectile->header.gfx.cameraToObject
                );
            }
            return true;
        }
    }
    return false;
}

static bool vr_special_moves_hammer_melee_contact(
    struct MarioState* mario,
    const Vec3f hammerHead
) {
    struct Object* contact = NULL;
    if (mario == NULL || hammerHead == NULL || gObjectLists == NULL) {
        sVrHammerMeleeContact = NULL;
        return false;
    }

    const f32 gloveScale =
        (f32)clamp(configVrGloveSize, 25U, 250U) / 70.0f;
    const f32 hammerRadius = VR_HAMMER_MELEE_RADIUS * gloveScale;
    if (vr_special_moves_try_player_hit(
            mario,
            hammerHead,
            hammerRadius,
            VR_PLAYER_ATTACK_HAMMER
        )) {
        return true;
    }
    for (s32 listIndex = 0;
         listIndex < NUM_OBJ_LISTS && contact == NULL;
         listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        for (struct ObjectNode* node = list->next;
             node != NULL && node != list;
             node = node->next) {
            struct Object* target = (struct Object*)node;
            if (!vr_special_moves_rasengan_target_is_eligible(
                    mario,
                    target
                )) {
                continue;
            }

            f32 targetRadius = 0.0f;
            f32 targetHeight = 0.0f;
            vr_special_moves_get_enemy_contact_bounds(
                target,
                &targetRadius,
                &targetHeight
            );
            if (obj_has_behavior(target, bhvChainChomp)) {
                // Chain Chomp's native interaction bounds are intentionally
                // small and do not match its visible head. Enlarge only the
                // Hammer Suit head contact; ordinary fist/punch collision is
                // deliberately left unchanged.
                targetRadius = fmaxf(targetRadius, 160.0f);
                targetHeight = fmaxf(targetHeight, 300.0f);
            }
            const f32 targetBottom =
                target->oPosY - target->hitboxDownOffset;
            const f32 targetTop = targetBottom + targetHeight;
            const f32 dx = hammerHead[0] - target->oPosX;
            const f32 dz = hammerHead[2] - target->oPosZ;
            const f32 combinedRadius =
                fmaxf(targetRadius, 20.0f) + hammerRadius;
            if (dx * dx + dz * dz >
                    combinedRadius * combinedRadius ||
                hammerHead[1] + hammerRadius < targetBottom ||
                hammerHead[1] - hammerRadius > targetTop) {
                continue;
            }

            const bool rearTarget =
                obj_has_behavior(target, bhvKingBobomb) ||
                vr_special_moves_target_is_whomp(target);
            if (rearTarget &&
                !vr_special_moves_point_is_behind_target(
                    target,
                    hammerHead[0],
                    hammerHead[2]
                )) {
                continue;
            }
            contact = target;
            break;
        }
    }

    if (contact == NULL) {
        sVrHammerMeleeContact = NULL;
        return false;
    }
    if (contact == sVrHammerMeleeContact) {
        return false;
    }
    sVrHammerMeleeContact = contact;

    bool allowInteract = true;
    const bool rearTarget =
        obj_has_behavior(contact, bhvKingBobomb) ||
        vr_special_moves_target_is_whomp(contact);
    if (!rearTarget) {
        smlua_call_event_hooks(
            HOOK_ALLOW_INTERACT,
            mario,
            contact,
            contact->oInteractType,
            &allowInteract
        );
    }
    if (!allowInteract) {
        return false;
    }

    mario->interactObj = contact;
    if (rearTarget) {
        vr_special_moves_apply_rear_target_hit(
            mario,
            contact,
            hammerHead[0],
            hammerHead[2]
        );
    } else {
        const bool breakable =
            obj_has_behavior(contact, bhvBreakableBox) ||
            obj_has_behavior(contact, bhvBreakableBoxSmall) ||
            obj_has_behavior(contact, bhvJumpingBox);
        if (breakable) {
            vr_special_moves_roll_box_reward(contact, mario);
        }
        attack_object(
            mario,
            contact,
            obj_has_behavior(contact, bhvGoomba) &&
                contact->oGoombaSize == GOOMBA_SIZE_HUGE
                ? INT_GROUND_POUND_OR_TWIRL
                : INT_PUNCH
        );
        if (obj_has_behavior(contact, bhvBobomb)) {
            // The Hammer Suit's physical hammer head is its own melee
            // contact. Feed only this contact through the Bob-omb's native
            // touched/interacted path; ordinary VR punch behavior is left
            // unchanged.
            contact->oInteractStatus |=
                INT_STATUS_INTERACTED | INT_STATUS_TOUCHED_BOB_OMB;
        }
        smlua_call_event_hooks(
            HOOK_ON_INTERACT,
            mario,
            contact,
            contact->oInteractType,
            true
        );
        network_send_object(contact);
    }
    play_sound(SOUND_ACTION_HIT_2, contact->header.gfx.cameraToObject);
    vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.55f, 0.06f, -1.0f);
    return true;
}

static void vr_special_moves_update_projectiles(struct MarioState* mario) {
    for (u32 slot = 0; slot < VR_FIREBALL_PROJECTILE_COUNT; slot++) {
        struct Object* projectile = sVrFireballProjectiles[slot];
        if (projectile == NULL) {
            continue;
        }
        if ((projectile->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            ++sVrFireballProjectileLifetime[slot] >
                VR_FIREBALL_MAX_LIFETIME) {
            vr_special_moves_clear_fireball_projectile(slot);
            continue;
        }

        Vec3f* velocity = &sVrFireballProjectileVelocity[slot];
        projectile->oPosX += (*velocity)[0];
        projectile->oPosY += (*velocity)[1];
        projectile->oPosZ += (*velocity)[2];
        (*velocity)[1] -= 2.0f;
        projectile->oAnimState = (s32)((gGlobalTimer >> 1) & 7U);

        struct WallCollisionData wall = {
            .x = projectile->oPosX,
            .y = projectile->oPosY,
            .z = projectile->oPosZ,
            .offsetY = 0.0f,
            .radius = 20.0f,
        };
        if (find_wall_collisions(&wall) > 0 &&
            wall.walls[0] != NULL) {
            const f32 nx = wall.walls[0]->normal.x;
            const f32 nz = wall.walls[0]->normal.z;
            const f32 dot = (*velocity)[0] * nx +
                (*velocity)[2] * nz;
            (*velocity)[0] -= 2.0f * dot * nx;
            (*velocity)[2] -= 2.0f * dot * nz;
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
        if (floor != NULL &&
            projectile->oPosY < floorHeight + 18.0f) {
            projectile->oPosY = floorHeight + 18.0f;
            (*velocity)[1] = 8.0f;
            if (sVrFireballProjectileLaunchBoost[slot]) {
                // The hand throw gets a stronger initial shot, then returns
                // to the established skipping speed on its first landing.
                (*velocity)[0] *= 0.5f;
                (*velocity)[2] *= 0.5f;
                sVrFireballProjectileLaunchBoost[slot] = false;
            }
            (*velocity)[0] *= 0.985f;
            (*velocity)[2] *= 0.985f;
        }
        obj_update_gfx_pos_and_angle(projectile);

        if (vr_special_moves_projectile_hits_enemy(
                mario,
                projectile,
                true,
                false
            )) {
            vr_apply_haptic(
                VR_CONTROLLER_RIGHT,
                0.65f,
                0.08f,
                -1.0f
            );
            vr_special_moves_clear_fireball_projectile(slot);
        }
    }
}

static bool vr_special_moves_rasengan_target_is_protected(
    struct Object* target
) {
    return target == NULL ||
        obj_has_behavior(target, bhvChainChompChainPart) ||
        obj_has_behavior(target, bhvBowser) ||
        obj_has_behavior(target, bhvBowserBodyAnchor) ||
        obj_has_behavior(target, bhvBowserTailAnchor) ||
        obj_has_behavior(target, bhvBigBullyWithMinions) ||
        obj_has_behavior(target, bhvBigChillBully) ||
        obj_has_behavior(target, bhvEyerokBoss) ||
        obj_has_behavior(target, bhvEyerokHand) ||
        obj_has_behavior(target, bhvWigglerHead) ||
        obj_has_behavior(target, bhvWigglerBody) ||
        obj_has_behavior(target, bhvBalconyBigBoo) ||
        obj_has_behavior(target, bhvMerryGoRoundBigBoo) ||
        obj_has_behavior(target, bhvGhostHuntBigBoo);
}

static bool vr_special_moves_rasengan_target_is_eligible(
    struct MarioState* mario,
    struct Object* object
) {
    const bool whompTarget = vr_special_moves_target_is_whomp(object);
    if (mario == NULL || object == NULL ||
        object == mario->marioObj || object == mario->heldObj ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
        (object->oIntangibleTimer != 0 && !whompTarget)) {
        return false;
    }

    const u32 enemyTypes = INTERACT_DAMAGE | INTERACT_BULLY |
        INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 |
        INTERACT_KOOPA | INTERACT_SPINY_WALKING |
        INTERACT_MR_BLIZZARD | INTERACT_CLAM_OR_BUBBA |
        INTERACT_BREAKABLE;
    const bool eligibleType =
        (object->oInteractType & enemyTypes) != 0 ||
        obj_has_behavior(object, bhvBobomb) ||
        obj_has_behavior(object, bhvChuckya) ||
        obj_has_behavior(object, bhvHeaveHo) ||
        obj_has_behavior(object, bhvChainChomp) ||
        obj_has_behavior(object, bhvPokeyBodyPart) ||
        obj_has_behavior(object, bhvBreakableBox) ||
        obj_has_behavior(object, bhvBreakableBoxSmall) ||
        obj_has_behavior(object, bhvJumpingBox) ||
        obj_has_behavior(object, bhvExclamationBox) ||
        (obj_has_behavior(object, bhvSmallWhomp) &&
            object->oAction < 8) ||
        (obj_has_behavior(object, bhvWhompKingBoss) &&
            object->oAction >= 2 && object->oAction < 8) ||
        (obj_has_behavior(object, bhvKingBobomb) &&
            object->oAction == 2);
    return eligibleType &&
        !vr_special_moves_rasengan_target_is_protected(object);
}

static bool vr_special_moves_rasengan_is_behind_target(
    struct Object* object
) {
    if (object == NULL || sVrRasenganObject == NULL) {
        return false;
    }
    return vr_special_moves_point_is_behind_target(
        object,
        sVrRasenganObject->oPosX,
        sVrRasenganObject->oPosZ
    );
}

static struct Object* vr_special_moves_resolve_rasengan_target(
    struct Object* object
) {
    if (object != NULL &&
        obj_has_behavior(object, bhvPokeyBodyPart) &&
        object->parentObj != NULL &&
        obj_has_behavior(object->parentObj, bhvPokey)) {
        return object->parentObj;
    }
    return object;
}

static void vr_special_moves_get_rasengan_target_bounds(
    struct Object* target,
    f32* centerY,
    f32* radius
) {
    if (target == NULL || centerY == NULL || radius == NULL) {
        return;
    }

    if (obj_has_behavior(target, bhvPokey)) {
        f32 bottom = target->oPosY;
        f32 top = target->oPosY + 80.0f;
        f32 horizontalRadius = 50.0f;
        for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
            struct ObjectNode* list = &gObjectLists[listIndex];
            struct ObjectNode* node = list->next;
            while (node != NULL && node != list) {
                struct Object* part = (struct Object*)node;
                node = node->next;
                if ((part->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
                    !obj_has_behavior(part, bhvPokeyBodyPart) ||
                    part->parentObj != target) {
                    continue;
                }
                const f32 partRadius = fmaxf(
                    fmaxf(part->hitboxRadius, part->hurtboxRadius),
                    45.0f
                );
                const f32 dx = part->oPosX - target->oPosX;
                const f32 dz = part->oPosZ - target->oPosZ;
                horizontalRadius = fmaxf(
                    horizontalRadius,
                    sqrtf(dx * dx + dz * dz) + partRadius
                );
                bottom = fminf(bottom, part->oPosY - 45.0f);
                top = fmaxf(top, part->oPosY + 65.0f);
            }
        }
        *centerY = (bottom + top) * 0.5f;
        *radius = fmaxf(horizontalRadius, (top - bottom) * 0.5f);
        return;
    }

    if (obj_has_behavior(target, bhvKingBobomb)) {
        // Enclose the complete rendered boss after a valid back contact even
        // though the activation zone itself is intentionally much smaller.
        *centerY = target->oPosY + 200.0f;
        *radius = 260.0f;
        return;
    }

    if (vr_special_moves_target_is_whomp(target)) {
        f32 contactRadius = 0.0f;
        f32 contactHeight = 0.0f;
        vr_special_moves_get_enemy_contact_bounds(
            target,
            &contactRadius,
            &contactHeight
        );
        *centerY = target->oPosY + contactHeight * 0.5f;
        *radius = fmaxf(contactRadius, contactHeight * 0.5f);
        return;
    }

    const f32 height = fmaxf(
        fmaxf(target->hitboxHeight, target->hurtboxHeight),
        80.0f
    );
    *centerY = target->oPosY - target->hitboxDownOffset + height * 0.5f;
    *radius = fmaxf(
        fmaxf(target->hitboxRadius, target->hurtboxRadius),
        height * 0.5f
    );
}

static void vr_special_moves_rasengan_attack_pokey(
    struct MarioState* mario,
    struct Object* pokey
) {
    for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;
        while (node != NULL && node != list) {
            struct Object* part = (struct Object*)node;
            node = node->next;
            if ((part->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
                !obj_has_behavior(part, bhvPokeyBodyPart) ||
                part->parentObj != pokey) {
                continue;
            }
            attack_object(mario, part, INT_PUNCH);
            smlua_call_event_hooks(
                HOOK_ON_INTERACT,
                mario,
                part,
                part->oInteractType,
                true
            );
            network_send_object(part);
        }
    }
    network_send_object(pokey);
}

static bool vr_special_moves_try_rasengan_hit(
    struct MarioState* mario,
    u32 hand,
    const Vec3f velocity,
    struct Object* object
) {
    if (!vr_special_moves_online_allowed() ||
        !configVrSpecialRasengan || hand != VR_CONTROLLER_RIGHT ||
        sVrRasenganObject == NULL || sVrRasenganTarget != NULL ||
        sVrRasenganChargeFrames <
            vr_special_moves_rasengan_ready_frames() ||
        !vr_special_moves_rasengan_target_is_eligible(mario, object)) {
        return false;
    }

    if ((obj_has_behavior(object, bhvKingBobomb) ||
         obj_has_behavior(object, bhvSmallWhomp) ||
         obj_has_behavior(object, bhvWhompKingBoss)) &&
        !vr_special_moves_rasengan_is_behind_target(object)) {
        return false;
    }

    sVrRasenganTarget = vr_special_moves_resolve_rasengan_target(object);
    sVrRasenganImpactFrames = 0;
    for (u32 axis = 0; axis < 3; axis++) {
        sVrRasenganImpactVelocity[axis] = velocity[axis];
    }
    const f32 horizontalSpeed = sqrtf(
        velocity[0] * velocity[0] + velocity[2] * velocity[2]
    );
    if (horizontalSpeed < 1.0f) {
        const s16 yaw = vr_get_first_person_view_yaw();
        sVrRasenganImpactVelocity[0] = sins(yaw) * 120.0f;
        sVrRasenganImpactVelocity[2] = coss(yaw) * 120.0f;
    }
    if (!vr_special_moves_target_is_whomp(sVrRasenganTarget)) {
        sVrRasenganTarget->oMoveAngleYaw = atan2s(
            sVrRasenganImpactVelocity[2],
            sVrRasenganImpactVelocity[0]
        );
    }
    if (!vr_special_moves_target_is_whomp(sVrRasenganTarget)) {
        sVrRasenganTarget->oForwardVel = fmaxf(
            sVrRasenganTarget->oForwardVel,
            42.0f
        );
        sVrRasenganTarget->oVelY = fmaxf(
            sVrRasenganTarget->oVelY,
            15.0f
        );
    }
    play_sound(
        SOUND_OBJ2_BOWSER_ROAR,
        object->header.gfx.cameraToObject
    );
    vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.95f, 0.16f, -1.0f);
    return true;
}

static bool vr_special_moves_check_rasengan_contact(
    struct MarioState* mario,
    const Vec3f velocity
) {
    if (mario == NULL || gObjectLists == NULL ||
        sVrRasenganObject == NULL || sVrRasenganTarget != NULL ||
        sVrRasenganChargeFrames <
            vr_special_moves_rasengan_ready_frames() ||
        sVrRasenShurikenChargeFrames > 0 ||
        sVrRasenShurikenReady) {
        return false;
    }

    const f32 sphereX = sVrRasenganObject->oPosX;
    const f32 sphereY = sVrRasenganObject->oPosY;
    const f32 sphereZ = sVrRasenganObject->oPosZ;
    if (vr_special_moves_try_player_hit(
            mario,
            &sVrRasenganObject->oPosX,
            VR_RASENGAN_CONTACT_RADIUS,
            VR_PLAYER_ATTACK_RASENGAN
        )) {
        vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.95f, 0.16f, -1.0f);
        vr_special_moves_clear_rasengan();
        return true;
    }
    for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;
        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;
            // Most objects in a level are nowhere near the held sphere. This
            // coarse squared-distance gate avoids behavior and hitbox checks
            // for those objects without changing close-contact accuracy.
            const f32 coarseX = sphereX - object->oPosX;
            const f32 coarseY = sphereY - object->oPosY;
            const f32 coarseZ = sphereZ - object->oPosZ;
            if (coarseX * coarseX + coarseZ * coarseZ >
                    640.0f * 640.0f ||
                fabsf(coarseY) > 640.0f) {
                continue;
            }
            if (!vr_special_moves_rasengan_target_is_eligible(
                    mario,
                    object
                )) {
                continue;
            }

            f32 objectRadius = 0.0f;
            f32 objectHeight = 0.0f;
            vr_special_moves_get_enemy_contact_bounds(
                object,
                &objectRadius,
                &objectHeight
            );
            if (objectRadius <= 0.0f || objectHeight <= 0.0f) {
                continue;
            }
            const f32 objectBottom =
                object->oPosY - object->hitboxDownOffset;
            const f32 objectTop = objectBottom + objectHeight;
            const f32 combinedRadius =
                objectRadius + VR_RASENGAN_CONTACT_RADIUS;
            const f32 deltaX = sphereX - object->oPosX;
            const f32 deltaZ = sphereZ - object->oPosZ;
            if (deltaX * deltaX + deltaZ * deltaZ >
                    combinedRadius * combinedRadius ||
                sphereY + VR_RASENGAN_CONTACT_RADIUS < objectBottom ||
                sphereY - VR_RASENGAN_CONTACT_RADIUS > objectTop) {
                continue;
            }

            bool allowInteract = true;
            const bool explicitRearTarget =
                obj_has_behavior(object, bhvKingBobomb) ||
                vr_special_moves_target_is_whomp(object);
            if (!explicitRearTarget) {
                smlua_call_event_hooks(
                    HOOK_ALLOW_INTERACT,
                    mario,
                    object,
                    object->oInteractType,
                    &allowInteract
                );
            }
            if (allowInteract && vr_special_moves_try_rasengan_hit(
                    mario,
                    VR_CONTROLLER_RIGHT,
                    velocity,
                    object
                )) {
                return true;
            }
        }
    }
    return false;
}

static void vr_special_moves_update_rasengan_impact(
    struct MarioState* mario
) {
    if (sVrRasenganObject == NULL || sVrRasenganTarget == NULL) {
        return;
    }
    if ((sVrRasenganObject->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
        (sVrRasenganTarget->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        vr_special_moves_clear_rasengan();
        return;
    }

    struct Object* target = sVrRasenganTarget;
    sVrRasenganImpactFrames++;
    const bool bobombTarget = obj_has_behavior(target, bhvBobomb);
    const bool chainChompTarget = obj_has_behavior(target, bhvChainChomp);
    const bool kingBobombTarget = obj_has_behavior(target, bhvKingBobomb);
    const bool kingWhompTarget = obj_has_behavior(target, bhvWhompKingBoss);
    const bool regularWhompTarget =
        obj_has_behavior(target, bhvSmallWhomp);
    const bool pokeyTarget = obj_has_behavior(target, bhvPokey);
    const u16 pushFrames = bobombTarget
        ? VR_RASENGAN_BOBOMB_CARRY_FRAMES
        : VR_RASENGAN_IMPACT_GROW_FRAMES;
    if (sVrRasenganImpactFrames <= pushFrames &&
        !kingWhompTarget && !regularWhompTarget) {
        const f32 horizontalLength = sqrtf(
            sVrRasenganImpactVelocity[0] *
                sVrRasenganImpactVelocity[0] +
            sVrRasenganImpactVelocity[2] *
                sVrRasenganImpactVelocity[2]
        );
        if (horizontalLength > 0.001f) {
            const f32 pushX =
                sVrRasenganImpactVelocity[0] /
                horizontalLength * 12.0f;
            const f32 pushZ =
                sVrRasenganImpactVelocity[2] /
                horizontalLength * 12.0f;
            target->oPosX += pushX;
            target->oPosZ += pushZ;
            target->oVelX = pushX;
            target->oVelZ = pushZ;
            target->header.gfx.pos[0] = target->oPosX;
            target->header.gfx.pos[2] = target->oPosZ;
        }
    }
    f32 targetCenterY = target->oPosY;
    f32 targetRadius = 40.0f;
    vr_special_moves_get_rasengan_target_bounds(
        target,
        &targetCenterY,
        &targetRadius
    );
    sVrRasenganObject->oPosX = target->oPosX;
    sVrRasenganObject->oPosY = targetCenterY;
    sVrRasenganObject->oPosZ = target->oPosZ;
    // Per-render GeoLayout rotation supplies headset-rate visual motion.
    // Keep the simulation transform neutral so a 30 Hz parent rotation does
    // not introduce a visible hitch underneath it.
    sVrRasenganObject->oFaceAnglePitch = 0;
    sVrRasenganObject->oFaceAngleYaw = 0;
    sVrRasenganObject->oFaceAngleRoll = 0;
    const f32 growth = clamp(
        (f32)sVrRasenganImpactFrames /
            (f32)VR_RASENGAN_IMPACT_GROW_FRAMES,
        0.0f,
        1.0f
    );
    const f32 enclosingScale = fmaxf(
        VR_RASENGAN_HAND_SCALE,
        (targetRadius + VR_RASENGAN_IMPACT_PADDING) /
            VR_RASENGAN_MODEL_RADIUS
    );
    obj_scale(
        sVrRasenganObject,
        VR_RASENGAN_HAND_SCALE +
            growth * (enclosingScale - VR_RASENGAN_HAND_SCALE)
    );
    sVrRasenganObject->oOpacity = VR_RASENGAN_MAX_OPACITY;
    obj_update_gfx_pos_and_angle(sVrRasenganObject);

    if (sVrRasenganImpactFrames <= VR_RASENGAN_IMPACT_GROW_FRAMES) {
        if (!kingWhompTarget && !regularWhompTarget) {
            target->oMoveAngleYaw = atan2s(
                sVrRasenganImpactVelocity[2],
                sVrRasenganImpactVelocity[0]
            );
        }
        if (!kingWhompTarget && !regularWhompTarget) {
            target->oForwardVel = fmaxf(target->oForwardVel, 42.0f);
            target->oVelY = fmaxf(target->oVelY, 10.0f);
        }
        if (!bobombTarget &&
            sVrRasenganImpactFrames == VR_RASENGAN_IMPACT_GROW_FRAMES) {
            mario->interactObj = target;
            if (pokeyTarget) {
                vr_special_moves_rasengan_attack_pokey(mario, target);
            } else if (kingBobombTarget) {
                // Use King Bob-omb's native thrown action so landing removes
                // exactly one health point and preserves his three-hit fight.
                vr_special_moves_release_mario_from_grabber(mario, target);
                target->oAction = 4;
                target->oSubAction = 0;
                target->oMoveFlags &= ~OBJ_MOVE_MASK_ON_GROUND;
                target->oForwardVel = fmaxf(target->oForwardVel, 52.0f);
                target->oVelY = fmaxf(target->oVelY, 35.0f);
            } else if (kingWhompTarget) {
                // A rear Rasengan is one native boss hit. Preserve the
                // three-hit health counter and the original defeat dialog.
                target->oHealth = max(target->oHealth - 1, 0);
                target->oForwardVel = 0.0f;
                target->oVelY = 0.0f;
                target->oFaceAnglePitch = 0;
                target->oAngleVelPitch = 0;
                target->oAction = target->oHealth == 0 ? 8 : 2;
                target->oSubAction = 0;
                play_sound(
                    SOUND_OBJ2_WHOMP_SOUND_SHORT,
                    target->header.gfx.cameraToObject
                );
            } else if (regularWhompTarget) {
                target->oNumLootCoins = max(target->oNumLootCoins, 5);
                obj_spawn_loot_yellow_coins(
                    target,
                    target->oNumLootCoins,
                    20.0f
                );
                target->oForwardVel = 0.0f;
                target->oVelY = 0.0f;
                target->oFaceAnglePitch = 0;
                target->oAngleVelPitch = 0;
                target->oAction = 8;
                target->oSubAction = 0;
            } else {
                const bool breakableTarget =
                    obj_has_behavior(target, bhvBreakableBox) ||
                    obj_has_behavior(target, bhvBreakableBoxSmall) ||
                    obj_has_behavior(target, bhvJumpingBox);
                if (breakableTarget) {
                    vr_special_moves_roll_box_reward(target, mario);
                }
                attack_object(
                    mario,
                    target,
                    obj_has_behavior(target, bhvGoomba) &&
                        target->oGoombaSize == GOOMBA_SIZE_HUGE
                        ? INT_GROUND_POUND_OR_TWIRL
                        : INT_PUNCH
                );
                target->oInteractStatus |= INT_STATUS_TOUCHED_BOB_OMB;
                if (!chainChompTarget && !breakableTarget &&
                    !obj_has_behavior(target, bhvExclamationBox)) {
                    target->oHealth = 0;
                }
            }
            smlua_call_event_hooks(
                HOOK_ON_INTERACT,
                mario,
                target,
                target->oInteractType,
                true
            );
            network_send_object(target);
            if (kingBobombTarget || kingWhompTarget ||
                regularWhompTarget || pokeyTarget) {
                vr_special_moves_clear_rasengan();
                return;
            }
        }
    }
    if (bobombTarget &&
        sVrRasenganImpactFrames == VR_RASENGAN_BOBOMB_CARRY_FRAMES) {
        // Carry the Bob-omb inside the expanding sphere for one second, then
        // hand control back to its native explosion path. This preserves its
        // coin/respawner/network behavior without detonating on first touch.
        mario->interactObj = target;
        target->oBobombFuseLit = 1;
        target->oAction = BOBOMB_ACT_EXPLODE;
        target->oPrevAction = BOBOMB_ACT_EXPLODE;
        target->oTimer = 5;
        target->oInteractStatus |= INT_STATUS_TOUCHED_BOB_OMB;
        smlua_call_event_hooks(
            HOOK_ON_INTERACT,
            mario,
            target,
            target->oInteractType,
            true
        );
        network_send_object(target);
        vr_special_moves_clear_rasengan();
        return;
    }
    if (sVrRasenganImpactFrames >= VR_RASENGAN_IMPACT_MAX_FRAMES) {
        vr_special_moves_clear_rasengan();
    }
}

static void vr_special_moves_launch_hammer_volley(
    struct MarioState* mario,
    const Vec3f position,
    const Vec3f handVelocity
) {
    if (mario == NULL || mario->marioObj == NULL) {
        return;
    }

    const f32 rawSpeed = sqrtf(
        handVelocity[0] * handVelocity[0] +
        handVelocity[1] * handVelocity[1] +
        handVelocity[2] * handVelocity[2]
    ) * VR_THROW_VELOCITY_SCALE;
    const f32 launchSpeed = clamp(
        rawSpeed,
        VR_HAMMER_MIN_HORIZONTAL_SPEED,
        VR_HAMMER_MAX_HORIZONTAL_SPEED
    );
    Vec3f baseVelocity;
    if (rawSpeed > 1.0f) {
        const f32 velocityScale = launchSpeed /
            (rawSpeed / VR_THROW_VELOCITY_SCALE);
        for (u32 axis = 0; axis < 3; axis++) {
            baseVelocity[axis] = handVelocity[axis] * velocityScale;
        }
    } else {
        const s16 launchYaw = vr_get_first_person_view_yaw();
        vec3f_set(
            baseVelocity,
            sins(launchYaw) * launchSpeed,
            0.0f,
            coss(launchYaw) * launchSpeed
        );
    }
    // Preserve the player's complete 3D throw vector. A small lift supplies
    // the classic hammer arc, while gravity immediately takes over instead
    // of letting the projectile float like the Rasen-Shuriken.
    baseVelocity[1] += VR_HAMMER_LAUNCH_ARC_BIAS;

    // The complete volley begins as one bunch in the glove. On release these
    // explicit roles separate it front-to-back without any sideways fan:
    // hammer 1 lands near, hammer 2 in the middle, and hammer 3 far. All three
    // share one vertical arc so their flight times stay approximately equal.
    static const f32 sHammerVolleyHorizontalScale[
        VR_HAMMER_VOLLEY_COUNT
    ] = { 0.72f, 1.00f, 1.28f };
    for (u32 volley = 0; volley < VR_HAMMER_VOLLEY_COUNT; volley++) {
        const u32 slot = vr_special_moves_allocate_hammer_projectile();
        struct Object* projectile = spawn_object(
            mario->marioObj,
            MODEL_VR_HAMMER,
            bhvStaticObject
        );
        if (projectile == NULL) {
            continue;
        }
        sVrHammerProjectiles[slot] = projectile;
        // All three leave the same hand-held bunch. Their slightly different
        // forward speeds separate them front-to-back only after release, so
        // none appears to spawn independently out in front of the glove.
        projectile->oPosX = position[0];
        projectile->oPosY = position[1];
        projectile->oPosZ = position[2];
        projectile->oInteractType = 0;
        projectile->oFaceAnglePitch = (s16)(volley * 0x2800);
        projectile->oFaceAngleRoll = (s16)(volley * 0x1800);
        obj_scale(projectile, 0.58f);
        obj_update_gfx_pos_and_angle(projectile);

        const f32 volleySpeedScale =
            sHammerVolleyHorizontalScale[volley];
        sVrHammerProjectileVelocity[slot][0] =
            baseVelocity[0] * volleySpeedScale;
        sVrHammerProjectileVelocity[slot][1] =
            baseVelocity[1];
        sVrHammerProjectileVelocity[slot][2] =
            baseVelocity[2] * volleySpeedScale;
        sVrHammerProjectileLifetime[slot] = 0;
    }
    play_sound(
        SOUND_ACTION_THROW,
        mario->marioObj->header.gfx.cameraToObject
    );
    vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.65f, 0.10f, -1.0f);
}

static void vr_special_moves_update_hammer_projectiles(
    struct MarioState* mario
) {
    for (u32 slot = 0; slot < VR_HAMMER_PROJECTILE_COUNT; slot++) {
        struct Object* projectile = sVrHammerProjectiles[slot];
        if (projectile == NULL) {
            continue;
        }
        if ((projectile->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
            ++sVrHammerProjectileLifetime[slot] >
                VR_HAMMER_MAX_LIFETIME) {
            vr_special_moves_clear_hammer_projectile(slot);
            continue;
        }

        Vec3f* velocity = &sVrHammerProjectileVelocity[slot];
        projectile->oPosX += (*velocity)[0];
        projectile->oPosY += (*velocity)[1];
        projectile->oPosZ += (*velocity)[2];
        (*velocity)[1] -= VR_HAMMER_GRAVITY;
        projectile->oFaceAnglePitch += 0x1800;
        projectile->oFaceAngleRoll += 0x1000;

        if (vr_special_moves_projectile_hits_enemy(
                mario,
                projectile,
                false,
                true
            )) {
            vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.45f, 0.06f, -1.0f);
            vr_special_moves_clear_hammer_projectile(slot);
            continue;
        }

        bool hitGeometry = false;
        struct WallCollisionData wall = {
            .x = projectile->oPosX,
            .y = projectile->oPosY,
            .z = projectile->oPosZ,
            .offsetY = 0.0f,
            .radius = 24.0f,
        };
        if (find_wall_collisions(&wall) > 0) {
            hitGeometry = true;
        }
        struct Surface* floor = NULL;
        const f32 floorHeight = find_floor(
            projectile->oPosX,
            projectile->oPosY + 50.0f,
            projectile->oPosZ,
            &floor
        );
        if (floor != NULL && projectile->oPosY <= floorHeight + 12.0f) {
            hitGeometry = true;
        }
        struct Surface* ceiling = NULL;
        const f32 ceilingHeight = find_ceil(
            projectile->oPosX,
            projectile->oPosY - 30.0f,
            projectile->oPosZ,
            &ceiling
        );
        if (ceiling != NULL &&
            projectile->oPosY >= ceilingHeight - 12.0f) {
            hitGeometry = true;
        }
        if (hitGeometry) {
            play_sound(
                SOUND_ACTION_HIT_2,
                projectile->header.gfx.cameraToObject
            );
            vr_special_moves_clear_hammer_projectile(slot);
            continue;
        }
        obj_update_gfx_pos_and_angle(projectile);
    }
}

static void vr_special_moves_apply_rasen_shuriken_damage(
    struct MarioState* mario,
    struct Object* explosion,
    struct Object* object
) {
    if (mario == NULL || explosion == NULL || object == NULL) {
        return;
    }

    const bool rearTarget =
        obj_has_behavior(object, bhvKingBobomb) ||
        vr_special_moves_target_is_whomp(object);
    if (rearTarget) {
        vr_special_moves_apply_rear_target_hit(
            mario,
            object,
            explosion->oPosX,
            explosion->oPosZ
        );
        return;
    }
    if (obj_has_behavior(object, bhvPokey)) {
        vr_special_moves_rasengan_attack_pokey(mario, object);
        return;
    }
    if (obj_has_behavior(object, bhvBreakableBox) ||
        obj_has_behavior(object, bhvBreakableBoxSmall) ||
        obj_has_behavior(object, bhvJumpingBox)) {
        vr_special_moves_roll_box_reward(object, mario);
    }
    mario->interactObj = object;
    attack_object(
        mario,
        object,
        obj_has_behavior(object, bhvGoomba) &&
            object->oGoombaSize == GOOMBA_SIZE_HUGE
            ? INT_GROUND_POUND_OR_TWIRL
            : INT_PUNCH
    );
    object->oInteractStatus |= INT_STATUS_TOUCHED_BOB_OMB;
    if (!obj_has_behavior(object, bhvChainChomp) &&
        (object->oInteractType & INTERACT_BREAKABLE) == 0) {
        object->oHealth = max(object->oHealth - 1, 0);
    }
    smlua_call_event_hooks(
        HOOK_ON_INTERACT,
        mario,
        object,
        object->oInteractType,
        true
    );
    network_send_object(object);
}

static void vr_special_moves_rasen_shuriken_damage_area(
    struct MarioState* mario,
    struct Object* explosion
) {
    if (mario == NULL || explosion == NULL || gObjectLists == NULL) {
        return;
    }
    const f32 radius = VR_RASEN_SHURIKEN_EXPLOSION_RADIUS;
    const u16 explosionFrame = sVrRasenShurikenExplosionFrames;
    for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;
        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;
            if (object == explosion ||
                !vr_special_moves_rasengan_target_is_eligible(
                    mario,
                    object
                )) {
                continue;
            }
            f32 objectRadius = 0.0f;
            f32 objectHeight = 0.0f;
            vr_special_moves_get_enemy_contact_bounds(
                object,
                &objectRadius,
                &objectHeight
            );
            const f32 dx = object->oPosX - explosion->oPosX;
            const f32 dz = object->oPosZ - explosion->oPosZ;
            const f32 bottom = object->oPosY - object->hitboxDownOffset;
            const f32 top = bottom + objectHeight;
            if (dx * dx + dz * dz >
                    (radius + objectRadius) *
                    (radius + objectRadius) ||
                explosion->oPosY + radius < bottom ||
                explosion->oPosY - radius > top) {
                continue;
            }

            const bool rearTarget =
                obj_has_behavior(object, bhvKingBobomb) ||
                vr_special_moves_target_is_whomp(object);
            if (rearTarget &&
                !vr_special_moves_point_is_behind_target(
                    object,
                    explosion->oPosX,
                    explosion->oPosZ
                )) {
                continue;
            }

            struct Object* damageTarget = object;
            if (obj_has_behavior(object, bhvPokeyBodyPart) &&
                object->parentObj != NULL &&
                obj_has_behavior(object->parentObj, bhvPokey)) {
                damageTarget = object->parentObj;
            }

            s32 hitSlot = -1;
            s32 freeSlot = -1;
            for (u32 i = 0;
                 i < VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT;
                 i++) {
                if (sVrRasenShurikenHitTargets[i] == damageTarget) {
                    hitSlot = (s32)i;
                    break;
                }
                if (freeSlot < 0 && sVrRasenShurikenHitTargets[i] == NULL) {
                    freeSlot = (s32)i;
                }
            }
            if (hitSlot < 0) {
                if (freeSlot < 0) {
                    continue;
                }
                hitSlot = freeSlot;
                sVrRasenShurikenHitTargets[hitSlot] = damageTarget;
                sVrRasenShurikenHitFirstFrame[hitSlot] = explosionFrame;
                sVrRasenShurikenHitLastFrame[hitSlot] = explosionFrame;
                sVrRasenShurikenHitContinuous[hitSlot] =
                    explosionFrame == 1U;
                vr_special_moves_apply_rasen_shuriken_damage(
                    mario,
                    explosion,
                    damageTarget
                );
            } else {
                if (sVrRasenShurikenHitLastFrame[hitSlot] + 1U !=
                    explosionFrame) {
                    sVrRasenShurikenHitContinuous[hitSlot] = false;
                }
                sVrRasenShurikenHitLastFrame[hitSlot] = explosionFrame;
            }

            if (explosionFrame == VR_RASEN_SHURIKEN_EXPLOSION_FRAMES &&
                sVrRasenShurikenHitContinuous[hitSlot] &&
                sVrRasenShurikenHitFirstFrame[hitSlot] == 1U) {
                vr_special_moves_apply_rasen_shuriken_damage(
                    mario,
                    explosion,
                    damageTarget
                );
                sVrRasenShurikenHitContinuous[hitSlot] = false;
            }
        }
    }
}

static void vr_special_moves_rasen_shuriken_explode(
    struct MarioState* mario
) {
    if (sVrRasenShurikenProjectile == NULL ||
        sVrRasenShurikenExplosionFrames != 0) {
        return;
    }
    sVrRasenShurikenExplosionFrames = 1;
    for (u32 i = 0; i < VR_RASEN_SHURIKEN_TRACKED_TARGET_COUNT; i++) {
        sVrRasenShurikenHitTargets[i] = NULL;
        sVrRasenShurikenHitFirstFrame[i] = 0;
        sVrRasenShurikenHitLastFrame[i] = 0;
        sVrRasenShurikenHitContinuous[i] = false;
    }
    vec3f_set(sVrRasenShurikenVelocity, 0.0f, 0.0f, 0.0f);
    obj_set_model(sVrRasenShurikenProjectile, MODEL_VR_RASENGAN);
    obj_scale(
        sVrRasenShurikenProjectile,
        VR_RASEN_SHURIKEN_EXPLOSION_SCALE
    );
    sVrRasenShurikenProjectile->oOpacity = VR_RASENGAN_MAX_OPACITY;
    play_sound(
        SOUND_GENERAL2_BOBOMB_EXPLOSION,
        sVrRasenShurikenProjectile->header.gfx.cameraToObject
    );
    vr_special_moves_rasen_shuriken_damage_area(
        mario,
        sVrRasenShurikenProjectile
    );
}

static bool vr_special_moves_rasen_shuriken_hits_enemy(
    struct MarioState* mario,
    struct Object* projectile
) {
    if (mario == NULL || projectile == NULL || gObjectLists == NULL) {
        return false;
    }
    if (vr_special_moves_try_player_hit(
            mario,
            &projectile->oPosX,
            35.0f,
            VR_PLAYER_ATTACK_RASEN_SHURIKEN
        )) {
        return true;
    }
    for (s32 listIndex = 0; listIndex < NUM_OBJ_LISTS; listIndex++) {
        struct ObjectNode* list = &gObjectLists[listIndex];
        struct ObjectNode* node = list->next;
        while (node != NULL && node != list) {
            struct Object* object = (struct Object*)node;
            node = node->next;
            if (!vr_special_moves_rasengan_target_is_eligible(
                    mario,
                    object
                )) {
                continue;
            }
            f32 objectRadius = 0.0f;
            f32 objectHeight = 0.0f;
            vr_special_moves_get_enemy_contact_bounds(
                object,
                &objectRadius,
                &objectHeight
            );
            objectRadius += 20.0f;
            const f32 dx = projectile->oPosX - object->oPosX;
            const f32 dz = projectile->oPosZ - object->oPosZ;
            const f32 bottom = object->oPosY - object->hitboxDownOffset;
            if (dx * dx + dz * dz <= objectRadius * objectRadius &&
                projectile->oPosY + 20.0f >= bottom &&
                projectile->oPosY - 20.0f <= bottom + objectHeight &&
                (!(obj_has_behavior(object, bhvKingBobomb) ||
                   vr_special_moves_target_is_whomp(object)) ||
                 vr_special_moves_point_is_behind_target(
                    object,
                    projectile->oPosX,
                    projectile->oPosZ
                 ))) {
                return true;
            }
        }
    }
    return false;
}

static void vr_special_moves_update_rasen_shuriken_projectile(
    struct MarioState* mario
) {
    struct Object* projectile = sVrRasenShurikenProjectile;
    if (projectile == NULL) {
        return;
    }
    if ((projectile->activeFlags & ACTIVE_FLAG_ACTIVE) == 0 ||
        ++sVrRasenShurikenProjectileFrames >
            VR_RASEN_SHURIKEN_MAX_FLIGHT_FRAMES) {
        vr_special_moves_clear_rasen_shuriken_projectile();
        return;
    }
    if (sVrRasenShurikenExplosionFrames != 0) {
        projectile->oFaceAnglePitch = 0;
        projectile->oFaceAngleYaw = 0;
        projectile->oFaceAngleRoll = 0;
        obj_update_gfx_pos_and_angle(projectile);
        const u16 explosionFrame =
            ++sVrRasenShurikenExplosionFrames;
        if (explosionFrame >
            VR_RASEN_SHURIKEN_EXPLOSION_FRAMES) {
            vr_special_moves_clear_rasen_shuriken_projectile();
        } else if (explosionFrame >
            VR_RASEN_SHURIKEN_EXPLOSION_HOLD_FRAMES) {
            const u16 fadeFrame = explosionFrame -
                VR_RASEN_SHURIKEN_EXPLOSION_HOLD_FRAMES;
            projectile->oOpacity = (u8)(
                (u32)VR_RASENGAN_MAX_OPACITY *
                (VR_RASEN_SHURIKEN_EXPLOSION_FADE_FRAMES -
                 fadeFrame) /
                VR_RASEN_SHURIKEN_EXPLOSION_FADE_FRAMES
            );
        } else {
            projectile->oOpacity = VR_RASENGAN_MAX_OPACITY;
        }
        if (explosionFrame <= VR_RASEN_SHURIKEN_EXPLOSION_FRAMES) {
            // Keep the rendered bubble's entire volume live. Targets entering
            // later receive one hit; a target continuously inside from the
            // first frame through the fade receives one final second hit.
            vr_special_moves_rasen_shuriken_damage_area(mario, projectile);
            vr_special_moves_try_player_hit(
                mario,
                &projectile->oPosX,
                VR_RASEN_SHURIKEN_EXPLOSION_SCALE *
                    VR_RASENGAN_MODEL_RADIUS,
                VR_PLAYER_ATTACK_RASEN_SHURIKEN
            );
        }
        return;
    }

    Vec3f nextPosition = {
        projectile->oPosX + sVrRasenShurikenVelocity[0],
        projectile->oPosY + sVrRasenShurikenVelocity[1],
        projectile->oPosZ + sVrRasenShurikenVelocity[2]
    };
    bool hitGeometry = false;
    struct WallCollisionData wallData = { 0 };
    Vec3f wallPosition;
    vec3f_copy(wallPosition, nextPosition);
    resolve_and_return_wall_collisions_data(
        wallPosition,
        0.0f,
        18.0f,
        &wallData
    );
    if (wallData.numWalls > 0) {
        hitGeometry = true;
        vec3f_copy(nextPosition, wallPosition);
    }
    struct Surface* floor = NULL;
    const f32 floorHeight = find_floor(
        nextPosition[0],
        nextPosition[1] + 30.0f,
        nextPosition[2],
        &floor
    );
    if (floor != NULL && nextPosition[1] <= floorHeight + 18.0f) {
        nextPosition[1] = floorHeight + 18.0f;
        hitGeometry = true;
    }
    struct Surface* ceiling = NULL;
    const f32 ceilingHeight = find_ceil(
        nextPosition[0],
        nextPosition[1] - 30.0f,
        nextPosition[2],
        &ceiling
    );
    if (ceiling != NULL && nextPosition[1] >= ceilingHeight - 18.0f) {
        nextPosition[1] = ceilingHeight - 18.0f;
        hitGeometry = true;
    }
    vec3f_copy(&projectile->oPosX, nextPosition);
    projectile->oFaceAnglePitch = 0;
    projectile->oFaceAngleYaw = 0;
    projectile->oFaceAngleRoll = 0;
    obj_update_gfx_pos_and_angle(projectile);
    if (hitGeometry ||
        vr_special_moves_rasen_shuriken_hits_enemy(mario, projectile)) {
        vr_special_moves_rasen_shuriken_explode(mario);
    }
}

static void vr_special_moves_launch_rasen_shuriken(
    struct MarioState* mario,
    const Vec3f position,
    const Vec3f velocity
) {
    if (mario == NULL || mario->marioObj == NULL ||
        sVrRasenShurikenProjectile != NULL) {
        return;
    }
    sVrRasenShurikenProjectile = spawn_object(
        mario->marioObj,
        MODEL_VR_RASEN_SHURIKEN,
        bhvStaticObject
    );
    if (sVrRasenShurikenProjectile == NULL) {
        return;
    }
    for (u32 axis = 0; axis < 3; axis++) {
        (&sVrRasenShurikenProjectile->oPosX)[axis] = position[axis];
    }
    sVrRasenShurikenProjectile->oOpacity = VR_RASENGAN_MAX_OPACITY;
    obj_scale(sVrRasenShurikenProjectile, VR_RASENGAN_HAND_SCALE);

    const f32 speed = sqrtf(
        velocity[0] * velocity[0] +
        velocity[1] * velocity[1] +
        velocity[2] * velocity[2]
    );
    if (speed > 0.001f) {
        for (u32 axis = 0; axis < 3; axis++) {
            sVrRasenShurikenVelocity[axis] =
                velocity[axis] / speed *
                VR_RASEN_SHURIKEN_FLIGHT_SPEED;
        }
    } else {
        const s16 yaw = vr_get_first_person_view_yaw();
        sVrRasenShurikenVelocity[0] =
            sins(yaw) * VR_RASEN_SHURIKEN_FLIGHT_SPEED;
        sVrRasenShurikenVelocity[1] = 0.0f;
        sVrRasenShurikenVelocity[2] =
            coss(yaw) * VR_RASEN_SHURIKEN_FLIGHT_SPEED;
    }
    sVrRasenShurikenProjectileFrames = 0;
    sVrRasenShurikenExplosionFrames = 0;
    play_sound(
        SOUND_OBJ_FLAME_BLOWN,
        sVrRasenShurikenProjectile->header.gfx.cameraToObject
    );
    vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.8f, 0.12f, -1.0f);
}

static bool vr_special_moves_update_rasengan_hand(
    struct MarioState* mario,
    const struct VrControllerState* state,
    const Vec3f position,
    const Vec3f velocity,
    bool handBusy,
    const struct VrControllerState* leftState,
    const Vec3f leftPosition,
    bool leftPositionValid,
    bool leftHandBusy
) {
    if (!vr_special_moves_online_allowed() ||
        !configVrSpecialRasengan || !vr_is_active() || mario == NULL ||
        vr_special_moves_fire_flower_active()) {
        vr_special_moves_clear_rasengan();
        return false;
    }
    if (sVrRasenganTarget != NULL) {
        return false;
    }

    const u16 readyFrames = vr_special_moves_rasengan_ready_frames();
    const u16 shurikenReadyFrames =
        vr_special_moves_rasen_shuriken_ready_frames();
    const bool rightTriggerPressed = state != NULL &&
        state->trigger >= VR_FIREBALL_TRIGGER_THRESHOLD;
    const bool rightGripPressed = state != NULL &&
        state->squeeze >= VR_GRIP_CLOSE_THRESHOLD;
    const bool rightHandOpen = state != NULL &&
        state->squeeze <= VR_RASENGAN_RIGHT_OPEN_THRESHOLD;
    const bool leftGripPressed = leftState != NULL &&
        leftState->squeeze >= VR_RASENGAN_LEFT_GRIP_THRESHOLD;
    const bool swirlGestureHeld = !handBusy && !leftHandBusy &&
        leftPositionValid && rightTriggerPressed &&
        rightHandOpen && leftGripPressed;
    const bool gripTriggerGestureHeld = !handBusy &&
        rightTriggerPressed && rightGripPressed;
    const bool gestureHeld = configVrSpecialRasenganGripTrigger
        ? gripTriggerGestureHeld
        : swirlGestureHeld;
    const bool wasReady =
        sVrRasenganChargeFrames >= readyFrames;
    const bool retainChargedRasengan = wasReady &&
        !handBusy && rightTriggerPressed;

    if (wasReady && !rightTriggerPressed &&
        sVrRasenShurikenReady && sVrRasenganObject != NULL) {
        const f32 throwSpeedSq =
            sVrRasenganRememberedVelocity[0] *
                sVrRasenganRememberedVelocity[0] +
            sVrRasenganRememberedVelocity[1] *
                sVrRasenganRememberedVelocity[1] +
            sVrRasenganRememberedVelocity[2] *
                sVrRasenganRememberedVelocity[2];
        if (throwSpeedSq >=
            VR_RASEN_SHURIKEN_MIN_THROW_SPEED *
                VR_RASEN_SHURIKEN_MIN_THROW_SPEED) {
            vr_special_moves_launch_rasen_shuriken(
                mario,
                &sVrRasenganObject->oPosX,
                sVrRasenganRememberedVelocity
            );
        }
        vr_special_moves_clear_rasengan();
        return false;
    }

    if (gestureHeld && !wasReady) {
        if (sVrRasenganGestureFrames < readyFrames) {
            sVrRasenganGestureFrames++;
        }
        if (configVrSpecialRasenganGripTrigger) {
            sVrRasenganSwirlRadians = fminf(
                sVrRasenganSwirlRadians +
                    VR_RASENGAN_SWIRL_TARGET_RADIANS /
                        (f32)readyFrames,
                VR_RASENGAN_SWIRL_TARGET_RADIANS
            );
        } else {
            Vec3f orbitVector;
            for (u32 axis = 0; axis < 3; axis++) {
                orbitVector[axis] =
                    leftPosition[axis] - position[axis];
            }
            const f32 orbitRadius = sqrtf(
                orbitVector[0] * orbitVector[0] +
                orbitVector[1] * orbitVector[1] +
                orbitVector[2] * orbitVector[2]
            );
            if (orbitRadius >= VR_RASENGAN_SWIRL_MIN_RADIUS &&
                orbitRadius <= VR_RASENGAN_SWIRL_MAX_RADIUS) {
                if (sVrRasenganPreviousOrbitValid) {
                    const f32 previousRadius = sqrtf(
                        sVrRasenganPreviousOrbitVector[0] *
                            sVrRasenganPreviousOrbitVector[0] +
                        sVrRasenganPreviousOrbitVector[1] *
                            sVrRasenganPreviousOrbitVector[1] +
                        sVrRasenganPreviousOrbitVector[2] *
                            sVrRasenganPreviousOrbitVector[2]
                    );
                    if (previousRadius > 0.001f) {
                        Vec3f previousUnit;
                        Vec3f currentUnit;
                        for (u32 axis = 0; axis < 3; axis++) {
                            previousUnit[axis] =
                                sVrRasenganPreviousOrbitVector[axis] /
                                previousRadius;
                            currentUnit[axis] =
                                orbitVector[axis] / orbitRadius;
                        }
                        const f32 dot = clamp(
                            previousUnit[0] * currentUnit[0] +
                            previousUnit[1] * currentUnit[1] +
                            previousUnit[2] * currentUnit[2],
                            -1.0f,
                            1.0f
                        );
                        const f32 angularStep = acosf(dot);
                        if (angularStep >=
                                VR_RASENGAN_SWIRL_MIN_STEP &&
                            angularStep <=
                                VR_RASENGAN_SWIRL_MAX_STEP) {
                            sVrRasenganSwirlRadians = fminf(
                                sVrRasenganSwirlRadians + angularStep,
                                VR_RASENGAN_SWIRL_TARGET_RADIANS
                            );
                        }
                    }
                }
                vec3f_copy(
                    sVrRasenganPreviousOrbitVector,
                    orbitVector
                );
                sVrRasenganPreviousOrbitValid = true;
            } else {
                sVrRasenganPreviousOrbitValid = false;
            }
        }

        const f32 motionProgress = clamp(
            sVrRasenganSwirlRadians /
                VR_RASENGAN_SWIRL_TARGET_RADIANS,
            0.0f,
            1.0f
        );
        const f32 timeProgress = clamp(
            (f32)sVrRasenganGestureFrames / (f32)readyFrames,
            0.0f,
            1.0f
        );
        const f32 chargeProgress = fminf(
            motionProgress,
            timeProgress
        );
        sVrRasenganChargeFrames = (u16)min(
            (u32)(chargeProgress *
                (f32)readyFrames + 0.5f),
            readyFrames
        );
        if (sVrRasenganObject == NULL) {
            sVrRasenganObject = spawn_object(
                mario->marioObj,
                MODEL_VR_RASENGAN,
                bhvStaticObject
            );
            if (sVrRasenganObject != NULL) {
                sVrRasenganObject->oInteractType = 0;
                vr_apply_haptic(
                    VR_CONTROLLER_RIGHT,
                    0.22f,
                    0.05f,
                    -1.0f
                );
            }
        }
    } else if (!retainChargedRasengan &&
               sVrRasenganTarget == NULL &&
               (sVrRasenganObject != NULL ||
                sVrRasenganSwirlRadians > 0.0f)) {
        vr_special_moves_clear_rasengan();
        return false;
    }

    if (sVrRasenganChargeFrames >= readyFrames) {
        const bool specialHeld = mario->controller != NULL &&
            (mario->controller->buttonDown & Y_BUTTON) != 0;
        bool overheadAllowed = true;
        if (configVrRasenShurikenOverheadCharge &&
            !sVrRasenShurikenReady) {
            Vec3f headPosition;
            overheadAllowed =
                vr_get_stabilized_headset_world_position(
                    headPosition,
                    false
                ) && position[1] >= headPosition[1] + 12.0f;
        }
        if (specialHeld && rightTriggerPressed && overheadAllowed &&
            !sVrRasenShurikenReady) {
            if (sVrRasenShurikenChargeFrames == 0 &&
                sVrRasenganObject != NULL) {
                obj_set_model(
                    sVrRasenganObject,
                    MODEL_VR_RASEN_SHURIKEN
                );
            }
            if (sVrRasenShurikenChargeFrames < shurikenReadyFrames) {
                sVrRasenShurikenChargeFrames++;
            }
            if (sVrRasenShurikenChargeFrames >= shurikenReadyFrames) {
                sVrRasenShurikenReady = true;
                sVrRasenganReadyLifetimeFrames = 0;
                vr_apply_haptic(
                    VR_CONTROLLER_RIGHT,
                    0.7f,
                    0.12f,
                    -1.0f
                );
            }
        } else if (!specialHeld && !sVrRasenShurikenReady &&
            sVrRasenShurikenChargeFrames > 0) {
            sVrRasenShurikenChargeFrames = 0;
            if (sVrRasenganObject != NULL) {
                obj_set_model(sVrRasenganObject, MODEL_VR_RASENGAN);
            }
        }

        const u16 lifetimeFrames = sVrRasenShurikenReady
            ? VR_RASEN_SHURIKEN_READY_LIFETIME_FRAMES
            : VR_RASENGAN_READY_LIFETIME_FRAMES;
        // Starting the Rasen-Shuriken charge freezes the remaining Rasengan
        // hold time. Lowering the hand may pause the overhead charge too, but
        // the original timer stays frozen until Special is released; an
        // incomplete charge then resumes from exactly where it stopped.
        const bool shurikenChargeInProgress =
            !sVrRasenShurikenReady && specialHeld &&
            sVrRasenShurikenChargeFrames > 0;
        if (!shurikenChargeInProgress &&
            sVrRasenganReadyLifetimeFrames <
            lifetimeFrames) {
            sVrRasenganReadyLifetimeFrames++;
        }
        if (sVrRasenganReadyLifetimeFrames >=
            lifetimeFrames) {
            vr_special_moves_clear_rasengan();
            return false;
        }
    }

    if (sVrRasenganObject != NULL) {
        Vec3f palmPosition;
        if (!vr_get_controller_world_palm_from_state(
                VR_CONTROLLER_RIGHT,
                state,
                palmPosition
            )) {
            for (u32 axis = 0; axis < 3; axis++) {
                palmPosition[axis] = position[axis];
            }
        }
        vec3f_copy(&sVrRasenganObject->oPosX, palmPosition);
        const f32 progress = clamp(
            (f32)sVrRasenganChargeFrames / (f32)readyFrames,
            0.0f,
            1.0f
        );
        const u16 lifetimeFrames = sVrRasenShurikenReady
            ? VR_RASEN_SHURIKEN_READY_LIFETIME_FRAMES
            : VR_RASENGAN_READY_LIFETIME_FRAMES;
        f32 lifetimeFade = 1.0f;
        if (sVrRasenganReadyLifetimeFrames >
            lifetimeFrames -
                VR_RASENGAN_READY_FADE_FRAMES) {
            lifetimeFade =
                (f32)(lifetimeFrames -
                    sVrRasenganReadyLifetimeFrames) /
                (f32)VR_RASENGAN_READY_FADE_FRAMES;
        }
        sVrRasenganObject->oOpacity = (s32)(
            (18.0f + progress *
                (VR_RASENGAN_MAX_OPACITY - 18.0f)) *
            lifetimeFade
        );
        obj_scale(
            sVrRasenganObject,
            VR_RASENGAN_HAND_SCALE *
                (0.08f + progress * 0.92f)
        );
        sVrRasenganObject->oFaceAnglePitch = 0;
        sVrRasenganObject->oFaceAngleYaw = 0;
        sVrRasenganObject->oFaceAngleRoll = 0;
        obj_update_gfx_pos_and_angle(sVrRasenganObject);
        sVrRasenganObject->header.gfx.skipInterpolationTimestamp =
            gGlobalTimer;

        if (wasReady || sVrRasenganChargeFrames >= readyFrames) {
            const f32 speedSq = velocity[0] * velocity[0] +
                velocity[1] * velocity[1] +
                velocity[2] * velocity[2];
            const f32 rememberedSq =
                sVrRasenganRememberedVelocity[0] *
                    sVrRasenganRememberedVelocity[0] +
                sVrRasenganRememberedVelocity[1] *
                    sVrRasenganRememberedVelocity[1] +
                sVrRasenganRememberedVelocity[2] *
                    sVrRasenganRememberedVelocity[2];
            if (speedSq >= rememberedSq *
                VR_THROW_VELOCITY_MEMORY *
                VR_THROW_VELOCITY_MEMORY) {
                for (u32 axis = 0; axis < 3; axis++) {
                    sVrRasenganRememberedVelocity[axis] = velocity[axis];
                }
            } else {
                vec3f_mul(
                    sVrRasenganRememberedVelocity,
                    VR_THROW_VELOCITY_MEMORY
                );
            }
        }
    }
    return gestureHeld || retainChargedRasengan ||
        sVrRasenganObject != NULL;
}

static bool vr_special_moves_try_quick_fireball(
    struct MarioState* mario
) {
    if (mario == NULL || mario->marioObj == NULL ||
        (mario->input & INPUT_B_PRESSED) == 0 ||
        (vr_is_active() &&
         configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON) ||
        !vr_special_moves_fire_flower_active() ||
        mario->heldObj != NULL || sVrTrackedHeldObject != NULL ||
        sVrFireballChargeObject != NULL ||
        sVrPhysicalClimbType != VR_PHYSICAL_CLIMB_NONE ||
        vr_is_controller_holding_cap(VR_CONTROLLER_LEFT) ||
        vr_is_controller_holding_cap(VR_CONTROLLER_RIGHT)) {
        return false;
    }

    Vec3f origin;
    if (!vr_get_stabilized_headset_world_position(origin, false)) {
        vec3f_set(
            origin,
            mario->pos[0],
            mario->pos[1] + 90.0f,
            mario->pos[2]
        );
    }
    const s16 yaw = vr_get_first_person_view_yaw();
    const f32 forwardX = sins(yaw);
    const f32 forwardZ = coss(yaw);

    const u32 slot = vr_special_moves_allocate_fireball_projectile();
    struct Object* projectile = spawn_object(
        mario->marioObj,
        MODEL_VR_FIREBALL,
        bhvStaticObject
    );
    if (projectile == NULL) {
        return false;
    }
    sVrFireballProjectiles[slot] = projectile;

    projectile->oPosX = origin[0] + forwardX * 42.0f;
    projectile->oPosY = origin[1] - 18.0f;
    projectile->oPosZ = origin[2] + forwardZ * 42.0f;
    projectile->oOpacity = 255;
    // This projectile uses the explicit world/enemy tests below. Leaving its
    // native interaction type empty guarantees the firing Mario's own body
    // collider cannot consume or deflect it at the launch point.
    projectile->oInteractType = 0;
    projectile->oAnimState =
        (s32)((gGlobalTimer >> 1) & 7U);
    obj_scale(projectile, 0.9f);
    obj_update_gfx_pos_and_angle(projectile);

    vec3f_set(
        sVrFireballProjectileVelocity[slot],
        forwardX * 22.0f,
        4.0f,
        forwardZ * 22.0f
    );
    sVrFireballProjectileLifetime[slot] = 0;
    mario->input &= ~INPUT_B_PRESSED;
    play_sound(
        SOUND_OBJ_FLAME_BLOWN,
        projectile->header.gfx.cameraToObject
    );
    vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.25f, 0.05f, -1.0f);
    return true;
}

static void vr_hand_interaction_apply_carry_speed(
    struct MarioState* mario
) {
    if (!configVrImmersiveCarrySpeed || mario == NULL ||
        mario->heldObj == NULL ||
        sVrTrackedHeldObject != mario->heldObj) {
        return;
    }

    // Native holding locomotion applies this factor itself. Physical holds
    // deliberately bypass those actions, so mirror the same carrying pace
    // only when the native action will not do it for us.
    switch (mario->action) {
        case ACT_HOLD_WALKING:
        case ACT_HOLD_HEAVY_WALKING:
        case ACT_HOLD_DECELERATING:
            return;
    }
    mario->intendedMag *= 0.4f;
}

static bool vr_special_moves_update_fireball_hand(
    struct MarioState* mario,
    const struct VrControllerState* state,
    const Vec3f position,
    const Vec3f velocity,
    bool handBusy
) {
    const u16 readyFrames = vr_special_moves_fireball_ready_frames();
    const u16 formFrames = max(
        1U,
        (u32)readyFrames - VR_FIREBALL_FORM_DELAY_FRAMES
    );
    const bool triggerPressed = state != NULL &&
        state->trigger >= VR_FIREBALL_TRIGGER_THRESHOLD;
    const bool gripPressed = state != NULL &&
        state->squeeze >= VR_GRIP_CLOSE_THRESHOLD &&
        sVrGripPressed[VR_CONTROLLER_RIGHT];
    const bool canCharge = vr_special_moves_fire_flower_active() &&
        !handBusy && gripPressed &&
        mario != NULL;
    const bool chargingInput = triggerPressed && canCharge;
    const bool wasCharging = sVrFireballChargeFrames > 0 ||
        sVrFireballChargeObject != NULL;

    if (chargingInput) {
        const u16 previousFrames = sVrFireballChargeFrames;
        sVrFireballChargeFrames = (u16)min(
            sVrFireballChargeFrames + 1U,
            readyFrames
        );

        // Grip may be held before or after the trigger. The charge is driven
        // by the combined held state, so no single input-edge frame can be
        // missed. Wait 0.2 seconds, then visibly form for 1.5 seconds.
        if (sVrFireballChargeObject == NULL &&
            sVrFireballChargeFrames >= VR_FIREBALL_FORM_DELAY_FRAMES) {
            sVrFireballChargeObject = spawn_object(
                mario->marioObj,
                MODEL_VR_FIREBALL,
                bhvStaticObject
            );
            vec3f_set(
                sVrFireballRememberedVelocity,
                0.0f,
                0.0f,
                0.0f
            );
            if (sVrFireballChargeObject != NULL) {
                vr_apply_haptic(
                    VR_CONTROLLER_RIGHT,
                    0.18f,
                    0.04f,
                    -1.0f
                );
            }
        }
        if (sVrFireballChargeObject != NULL) {
            Vec3f fireballPosition;
            if (!vr_get_controller_world_palm_from_state(
                    VR_CONTROLLER_RIGHT,
                    state,
                    fireballPosition
                )) {
                for (u32 axis = 0; axis < 3; axis++) {
                    fireballPosition[axis] = position[axis];
                }
            }
            const f32 progress = clamp(
                (f32)(sVrFireballChargeFrames -
                    VR_FIREBALL_FORM_DELAY_FRAMES) /
                    (f32)formFrames,
                0.0f,
                1.0f
            );
            for (u32 axis = 0; axis < 3; axis++) {
                (&sVrFireballChargeObject->oPosX)[axis] =
                    fireballPosition[axis];
            }
            sVrFireballChargeObject->oAnimState =
                (s32)((gGlobalTimer >> 1) & 7U);
            sVrFireballChargeObject->oOpacity =
                (s32)(51.0f + progress * 204.0f);
            obj_scale(
                sVrFireballChargeObject,
                0.1f + progress * 1.1f
            );
            obj_update_gfx_pos_and_angle(sVrFireballChargeObject);
            // A forming fireball is a controller attachment, not a simulated
            // moving object. Do not blend its root toward the prior 30 Hz
            // sample; the render-time late latch supplies the newest pose.
            sVrFireballChargeObject->header.gfx.skipInterpolationTimestamp =
                gGlobalTimer;

            const f32 speedSq = velocity[0] * velocity[0] +
                velocity[1] * velocity[1] + velocity[2] * velocity[2];
            const f32 rememberedSq =
                sVrFireballRememberedVelocity[0] *
                    sVrFireballRememberedVelocity[0] +
                sVrFireballRememberedVelocity[1] *
                    sVrFireballRememberedVelocity[1] +
                sVrFireballRememberedVelocity[2] *
                    sVrFireballRememberedVelocity[2];
            if (speedSq >= rememberedSq * VR_THROW_VELOCITY_MEMORY *
                VR_THROW_VELOCITY_MEMORY) {
                for (u32 axis = 0; axis < 3; axis++) {
                    sVrFireballRememberedVelocity[axis] = velocity[axis];
                }
            } else {
                vec3f_mul(
                    sVrFireballRememberedVelocity,
                    VR_THROW_VELOCITY_MEMORY
                );
            }
        }
        if (previousFrames < readyFrames &&
            sVrFireballChargeFrames >= readyFrames) {
            vr_apply_haptic(
                VR_CONTROLLER_RIGHT,
                0.42f,
                0.08f,
                -1.0f
            );
        }
    }

    if (!chargingInput && wasCharging) {
        const f32 speedSq =
            sVrFireballRememberedVelocity[0] * sVrFireballRememberedVelocity[0] +
            sVrFireballRememberedVelocity[1] * sVrFireballRememberedVelocity[1] +
            sVrFireballRememberedVelocity[2] * sVrFireballRememberedVelocity[2];
        if (sVrFireballChargeObject != NULL &&
            sVrFireballChargeFrames >= readyFrames &&
            speedSq >= VR_FIREBALL_MIN_THROW_SPEED * VR_FIREBALL_MIN_THROW_SPEED) {
            const u32 slot =
                vr_special_moves_allocate_fireball_projectile();
            for (u32 axis = 0; axis < 3; axis++) {
                sVrFireballProjectileVelocity[slot][axis] =
                    sVrFireballRememberedVelocity[axis] *
                    VR_FIREBALL_LAUNCH_VELOCITY_SCALE;
            }
            sVrFireballProjectileLaunchBoost[slot] = true;
            sVrFireballChargeObject->oOpacity = 255;
            sVrFireballChargeObject->oInteractType = 0;
            obj_scale(sVrFireballChargeObject, 1.2f);
            sVrFireballProjectiles[slot] = sVrFireballChargeObject;
            sVrFireballProjectileLifetime[slot] = 0;
            play_sound(
                SOUND_OBJ_FLAME_BLOWN,
                sVrFireballChargeObject->header.gfx.cameraToObject
            );
            sVrFireballChargeObject = NULL;
            sVrFireballChargeFrames = 0;
            vec3f_set(
                sVrFireballRememberedVelocity,
                0.0f,
                0.0f,
                0.0f
            );
        } else {
            vr_special_moves_clear_fireball_charge();
        }
    }
    if ((!canCharge || !configVrSpecialFireFlower) &&
        sVrFireballChargeObject != NULL) {
        vr_special_moves_clear_fireball_charge();
    }
    return sVrFireballChargeObject != NULL;
}

static bool vr_special_moves_update_hammer_hand(
    struct MarioState* mario,
    const struct VrControllerState* state,
    const Vec3f position,
    const Vec3f velocity,
    bool handBusy
) {
    const bool triggerPressed = state != NULL &&
        state->trigger >= VR_FIREBALL_TRIGGER_THRESHOLD;
    const bool gripPressed = state != NULL &&
        state->squeeze >= VR_GRIP_CLOSE_THRESHOLD &&
        sVrGripPressed[VR_CONTROLLER_RIGHT];
    const bool canCharge = vr_special_moves_hammer_suit_active() &&
        mario != NULL && !handBusy && gripPressed;
    const bool chargingInput = canCharge && triggerPressed;
    const bool wasCharging = sVrHammerChargeFrames > 0 ||
        sVrHammerChargeObject != NULL;

    if (chargingInput) {
        const u16 previousFrames = sVrHammerChargeFrames;
        sVrHammerChargeFrames = (u16)min(
            sVrHammerChargeFrames + 1U,
            VR_HAMMER_CHARGE_FRAMES
        );
        if (sVrHammerChargeObject == NULL) {
            sVrHammerChargeObject = spawn_object(
                mario->marioObj,
                MODEL_VR_HAMMER,
                bhvStaticObject
            );
            if (sVrHammerChargeObject != NULL) {
                sVrHammerChargeObject->oInteractType = 0;
            }
        }
        if (sVrHammerChargeObject != NULL) {
            const f32 gloveScale =
                (f32)clamp(configVrGloveSize, 25U, 250U) / 70.0f;
            const f32 hammerGloveScale = clamp(
                0.75f + gloveScale * 0.25f,
                0.85f,
                1.50f
            );
            // The hammer model's origin sits inside its handle. Center that
            // origin in the closed fist so the fingers wrap around the shaft,
            // then grow the hammer outward from that exact grip point.
            sVrHammerChargeObject->oPosX = position[0];
            sVrHammerChargeObject->oPosY = position[1] +
                2.0f * gloveScale;
            sVrHammerChargeObject->oPosZ = position[2];
            const f32 progress = clamp(
                (f32)sVrHammerChargeFrames /
                    (f32)VR_HAMMER_CHARGE_FRAMES,
                0.0f,
                1.0f
            );
            sVrHammerChargeObject->oFaceAnglePitch = 0;
            sVrHammerChargeObject->oFaceAngleYaw =
                vr_get_first_person_view_yaw();
            sVrHammerChargeObject->oFaceAngleRoll = 0;
            obj_scale(
                sVrHammerChargeObject,
                hammerGloveScale * (0.02f + progress * 0.22f)
            );
            obj_update_gfx_pos_and_angle(sVrHammerChargeObject);
            sVrHammerChargeObject->header.gfx.skipInterpolationTimestamp =
                gGlobalTimer;

            const f32 speedSq = velocity[0] * velocity[0] +
                velocity[1] * velocity[1] +
                velocity[2] * velocity[2];
            const f32 rememberedSq =
                sVrHammerRememberedVelocity[0] *
                    sVrHammerRememberedVelocity[0] +
                sVrHammerRememberedVelocity[1] *
                    sVrHammerRememberedVelocity[1] +
                sVrHammerRememberedVelocity[2] *
                    sVrHammerRememberedVelocity[2];
            if (speedSq >= rememberedSq * VR_THROW_VELOCITY_MEMORY *
                VR_THROW_VELOCITY_MEMORY) {
                for (u32 axis = 0; axis < 3; axis++) {
                    sVrHammerRememberedVelocity[axis] = velocity[axis];
                }
            } else {
                vec3f_mul(
                    sVrHammerRememberedVelocity,
                    VR_THROW_VELOCITY_MEMORY
                );
            }
        }
        if (previousFrames < VR_HAMMER_CHARGE_FRAMES &&
            sVrHammerChargeFrames >= VR_HAMMER_CHARGE_FRAMES) {
            vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.48f, 0.09f, -1.0f);
        }
        if (sVrHammerChargeFrames >= VR_HAMMER_CHARGE_FRAMES) {
            Vec3f hammerHead;
            if (vr_get_controller_world_hammer_head_from_state(
                    VR_CONTROLLER_RIGHT,
                    state,
                    hammerHead
                )) {
                vr_special_moves_hammer_melee_contact(mario, hammerHead);
            } else {
                sVrHammerMeleeContact = NULL;
            }
        } else {
            sVrHammerMeleeContact = NULL;
        }
    }

    if (!chargingInput && wasCharging) {
        if (sVrHammerChargeObject != NULL &&
            sVrHammerChargeFrames >= VR_HAMMER_CHARGE_FRAMES) {
            Vec3f launchPosition;
            Vec3f launchVelocity;
            vec3f_copy(
                launchPosition,
                &sVrHammerChargeObject->oPosX
            );
            const f32 releaseSpeedSq =
                velocity[0] * velocity[0] +
                velocity[1] * velocity[1] +
                velocity[2] * velocity[2];
            if (releaseSpeedSq >=
                VR_FIREBALL_MIN_THROW_SPEED *
                    VR_FIREBALL_MIN_THROW_SPEED) {
                for (u32 axis = 0; axis < 3; axis++) {
                    launchVelocity[axis] = velocity[axis];
                }
            } else {
                vec3f_copy(
                    launchVelocity,
                    sVrHammerRememberedVelocity
                );
            }
            vr_special_moves_launch_hammer_volley(
                mario,
                launchPosition,
                launchVelocity
            );
        }
        vr_special_moves_clear_hammer_charge();
    }
    if ((!canCharge || !configVrSpecialHammerSuit) &&
        sVrHammerChargeObject != NULL) {
        vr_special_moves_clear_hammer_charge();
    }
    return sVrHammerChargeObject != NULL;
}

void vr_hand_interaction_update(struct MarioState* mario) {
    // Remote Mario states run through the same interaction function. They
    // must not reset the local player's tracked fist state.
    if (mario == NULL || mario->playerIndex != 0) {
        return;
    }

    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        if (sVrPhysicalPlayerHitCooldown[hand] > 0) {
            sVrPhysicalPlayerHitCooldown[hand]--;
        }
    }
    for (u32 playerIndex = 1; playerIndex < MAX_PLAYERS; playerIndex++) {
        if (sVrSpecialPlayerHitCooldown[playerIndex] > 0) {
            sVrSpecialPlayerHitCooldown[playerIndex]--;
        }
    }

    if (!vr_special_moves_online_allowed()) {
        vr_special_moves_reset_power();
        vr_special_moves_reset_hammer_suit();
        vr_special_moves_clear_rasengan();
        vr_special_moves_clear_rasen_shuriken_projectile();
        for (u32 i = 0; i < VR_FIRE_FLOWER_PICKUP_COUNT; i++) {
            vr_special_moves_delete_object(&sVrFireFlowerPickups[i]);
            sVrFireFlowerPickupVelocityY[i] = 0.0f;
            sVrFireFlowerPickupLanded[i] = false;
            sVrFireFlowerPickupAge[i] = 0;
        }
        vr_special_moves_delete_object(&sVrHammerSuitPickupObject);
        sVrHammerSuitPickupVelocityY = 0.0f;
        sVrHammerSuitPickupLanded = false;
        sVrHammerSuitPickupAge = 0;
    }

    if (!configVrSpecialRasengan || !vr_is_active() ||
        !vr_special_moves_online_allowed()) {
        vr_special_moves_clear_rasen_shuriken_projectile();
    }

    if (!configVrSpecialFireFlower || !vr_is_active() ||
        !vr_special_moves_online_allowed() ||
        (sVrFireFlowerPowered &&
         (sVrFireFlowerLevel != gCurrLevelNum ||
          sVrFireFlowerArea != gCurrAreaIndex))) {
        vr_special_moves_reset_power();
    }
    if (!configVrSpecialHammerSuit || !vr_is_active() ||
        !vr_special_moves_online_allowed() ||
        (sVrHammerSuitPowered &&
         (sVrHammerSuitLevel != gCurrLevelNum ||
          sVrHammerSuitArea != gCurrAreaIndex))) {
        vr_special_moves_reset_hammer_suit();
    }
    vr_special_moves_update_fire_flower_music(mario);
    vr_special_moves_update_hammer_suit_music(mario);
    if (sVrFireFlowerPowered &&
        (!ns_coopnet_vr_gameplay_allowed() ||
         !configVrCheatNoFireFlowerTimer) &&
        sVrFireFlowerTimer > 0 &&
        --sVrFireFlowerTimer == 0) {
        vr_special_moves_reset_power();
    }
    vr_special_moves_update_pickups(mario);
    vr_special_moves_update_hammer_suit_pickup(mario);
    vr_special_moves_update_hammer_suit_shell(mario);
    vr_special_moves_update_projectiles(mario);
    vr_special_moves_update_hammer_projectiles(mario);
    vr_special_moves_update_rasen_shuriken_projectile(mario);
    if ((!configVrSpecialRasengan ||
         !vr_special_moves_online_allowed() ||
         vr_special_moves_fire_flower_active() ||
         vr_special_moves_hammer_suit_active()) &&
        sVrRasenganObject != NULL) {
        vr_special_moves_clear_rasengan();
    }
    if (sVrHammerSuitPowered && sVrHammerSuitTimer > 0 &&
        --sVrHammerSuitTimer == 0) {
        vr_special_moves_reset_hammer_suit();
    }
    vr_special_moves_update_rasengan_impact(mario);
    vr_special_moves_try_quick_fireball(mario);
    vr_hand_interaction_apply_carry_speed(mario);

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
        if (sVrFireballChargeObject != NULL) {
            vr_special_moves_clear_fireball_charge();
        }
        if (sVrHammerChargeObject != NULL) {
            vr_special_moves_clear_hammer_charge();
        }
        if (sVrRasenganObject != NULL) {
            vr_special_moves_clear_rasengan();
        }
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

    struct VrControllerState rasenganLeftState = { 0 };
    Vec3f rasenganLeftPosition = { 0.0f, 0.0f, 0.0f };
    bool rasenganLeftPositionValid = false;
    bool rasenganLeftHandBusy = true;
    struct VrControllerState rasenganRightPreview = { 0 };
    const bool rasenganRightPreviewValid =
        vr_get_controller_state(
            VR_CONTROLLER_RIGHT,
            &rasenganRightPreview
        );
    const bool rasenganRightGestureReady =
        vr_special_moves_online_allowed() &&
        configVrSpecialRasengan &&
        !vr_special_moves_fire_flower_active() &&
        !vr_special_moves_hammer_suit_active() &&
        rasenganRightPreviewValid &&
        rasenganRightPreview.trigger >=
            VR_FIREBALL_TRIGGER_THRESHOLD &&
        (configVrSpecialRasenganGripTrigger
            ? rasenganRightPreview.squeeze >=
                VR_GRIP_CLOSE_THRESHOLD
            : rasenganRightPreview.squeeze <=
                VR_RASENGAN_RIGHT_OPEN_THRESHOLD);

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
            sVrHandCollision[hand].constraintObject = NULL;
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
        const bool handIsHoldingVrItem = handIsHoldingCap;
        const bool fireballHandBusy =
            handIsHoldingVrItem ||
            (sVrTrackedHeldGripMask & (u8)(1U << hand)) != 0 ||
            sVrPhysicalClimbHands[hand] ||
            sVrTrackedHootHand == hand ||
            sVrTrackedAnchorHand == hand ||
            (sVrBowserGripMask & (u8)(1U << hand)) != 0;
        if (hand == VR_CONTROLLER_LEFT) {
            rasenganLeftState = state;
            vec3f_copy(rasenganLeftPosition, position);
            rasenganLeftPositionValid = positionValid;
            rasenganLeftHandBusy = fireballHandBusy;
        }
        bool handIsChargingRasengan = false;
        if (hand == VR_CONTROLLER_LEFT) {
            handIsChargingRasengan =
                !configVrSpecialRasenganGripTrigger &&
                rasenganRightGestureReady &&
                positionValid && !fireballHandBusy &&
                state.squeeze >= VR_RASENGAN_LEFT_GRIP_THRESHOLD;
        } else if (hand == VR_CONTROLLER_RIGHT && positionValid) {
            handIsChargingRasengan =
                vr_special_moves_update_rasengan_hand(
                    mario,
                    &state,
                    position,
                    velocity,
                    fireballHandBusy,
                &rasenganLeftState,
                rasenganLeftPosition,
                rasenganLeftPositionValid,
                rasenganLeftHandBusy
            );
            if (handIsChargingRasengan) {
                vr_special_moves_check_rasengan_contact(
                    mario,
                    velocity
                );
            }
        }
        if (hand == VR_CONTROLLER_RIGHT && !positionValid &&
            sVrRasenganObject != NULL &&
            sVrRasenganTarget == NULL) {
            vr_special_moves_clear_rasengan();
        }
        if (hand == VR_CONTROLLER_RIGHT && !positionValid &&
            sVrHammerChargeObject != NULL) {
            vr_special_moves_clear_hammer_charge();
        }
        const bool handIsChargingHammer =
            hand == VR_CONTROLLER_RIGHT && positionValid &&
            vr_special_moves_update_hammer_hand(
                mario,
                &state,
                position,
                velocity,
                fireballHandBusy || handIsChargingRasengan
            );
        const bool handIsChargingFireball =
            hand == VR_CONTROLLER_RIGHT && positionValid &&
            vr_special_moves_update_fireball_hand(
                mario,
                &state,
                position,
                velocity,
                fireballHandBusy || handIsChargingRasengan ||
                    handIsChargingHammer
            );
        const bool handIsChargingSpecialMove =
            handIsChargingRasengan || handIsChargingFireball ||
            handIsChargingHammer;
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
            // Let the second hand join an existing physical tail hold. Bowser
            // remains on his native grounded spin axis; this only changes the
            // physical ownership and averaged swing/release direction.
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
                    vr_hand_interaction_release_grab(
                        mario,
                        positionValid
                    );
                } else if (sVrTrackedHeldHand == hand) {
                    sVrTrackedHeldHand = hand == VR_CONTROLLER_LEFT
                        ? VR_CONTROLLER_RIGHT
                        : VR_CONTROLLER_LEFT;
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
                   !handIsChargingSpecialMove &&
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
            handIsChargingSpecialMove ||
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

    vr_hand_interaction_update_physical_climb_handoff(mario);

    if (canStartInteraction) {
        vr_hand_interaction_process_star_contacts(
            mario,
            collectibleHandPositions,
            collectibleHandPositionValid
        );
    }
}
