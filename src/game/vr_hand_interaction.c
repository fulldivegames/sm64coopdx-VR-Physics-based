#include <math.h>
#include <stdio.h>

#include "audio/external.h"
#include "behavior_data.h"
#include "characters.h"
#include "engine/math_util.h"
#include "interaction.h"
#include "mario.h"
#include "object_constants.h"
#include "object_fields.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "sm64.h"
#include "vr_hand_interaction.h"

#include "pc/configfile.h"
#include "pc/lua/smlua_hooks.h"
#include "pc/network/packets/packet.h"
#include "pc/vr/vr.h"

#define VR_FIST_ACTIVE_FRAMES 33
#define VR_FIST_SWEEP_SAMPLES 4
#define VR_FIST_BASE_RADIUS 12.0f
#define VR_FIST_MAX_SWEEP_DISTANCE 150.0f
#define VR_PUNCH_SOUND_COMBO_RESET_FRAMES 18
#define VR_MOTION_DIVE_PAIR_WINDOW_FRAMES 5
#define VR_GRIP_CLOSE_THRESHOLD 0.55f
#define VR_GRIP_OPEN_THRESHOLD 0.35f
#define VR_GRAB_EXTRA_REACH 10.0f
#define VR_THROW_MIN_SPEED 60.0f
#define VR_THROW_VELOCITY_SCALE 0.125f
#define VR_THROW_VELOCITY_MEMORY 0.78f
#define VR_BOWSER_TURN_DEADZONE 0.18f

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
static Vec3f sVrTrackedHeldPosition = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrTrackedHeldVelocity = { 0.0f, 0.0f, 0.0f };
static bool sVrTrackedHeldPositionValid = false;
static bool sVrTrackedReleaseInProgress = false;
static struct Object* sVrTrackedHootObject = NULL;
static u32 sVrTrackedHootHand = VR_CONTROLLER_COUNT;
static struct Object* sVrTrackedAnchorObject = NULL;
static u32 sVrTrackedAnchorHand = VR_CONTROLLER_COUNT;
static Vec3f sVrTrackedAnchorOffset = { 0.0f, 0.0f, 0.0f };
static u32 sVrBowserGripHand = VR_CONTROLLER_COUNT;

static void vr_hand_interaction_clear_tracked_hold(void) {
    sVrTrackedHeldObject = NULL;
    sVrTrackedHeldHand = VR_CONTROLLER_COUNT;
    sVrTrackedHeldPositionValid = false;
    sVrTrackedReleaseInProgress = false;
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

static void vr_hand_interaction_reset(void) {
    for (u32 hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        sVrFistActiveFrames[hand] = 0;
        sVrFistPreviousPositionValid[hand] = false;
        sVrMotionDivePairFrames[hand] = 0;
        sVrGripPressed[hand] = false;
    }
    sVrPunchSoundComboStep = 0;
    sVrPunchSoundComboResetFrames = 0;
    sVrBowserGripHand = VR_CONTROLLER_COUNT;
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
        printf("[VR] Two-hand motion dive triggered.\n");
    }
}

static f32 vr_hand_interaction_fist_radius(void) {
    const f32 gloveSize = (f32)clamp(
        configVrGloveSize,
        25U,
        250U
    );
    return VR_FIST_BASE_RADIUS * gloveSize / 70.0f;
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
        sVrTrackedHeldHand < VR_CONTROLLER_COUNT;
}

bool vr_hand_interaction_blocks_native_held_object_release(
    struct MarioState* mario
) {
    return !sVrTrackedReleaseInProgress &&
        mario != NULL &&
        mario->playerIndex == 0 &&
        mario->heldObj != NULL &&
        mario->heldObj == sVrTrackedHeldObject &&
        sVrTrackedHeldHand < VR_CONTROLLER_COUNT &&
        sVrGripPressed[sVrTrackedHeldHand];
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

bool vr_hand_interaction_apply_held_object_transform(
    struct Object* object
) {
    if (!sVrTrackedHeldPositionValid ||
        !vr_hand_interaction_is_tracked_held_object(object) ||
        (object->activeFlags & ACTIVE_FLAG_ACTIVE) == 0) {
        return false;
    }

    vec3f_copy(&object->oPosX, sVrTrackedHeldPosition);
    vec3f_copy(object->header.gfx.pos, sVrTrackedHeldPosition);
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
        object == sVrTrackedAnchorObject &&
        sVrTrackedAnchorHand < VR_CONTROLLER_COUNT;
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

bool vr_hand_interaction_get_bowser_controls(
    struct MarioState* mario,
    f32* turnInput,
    bool* gripReleased
) {
    if (turnInput != NULL) {
        *turnInput = 0.0f;
    }
    if (gripReleased != NULL) {
        *gripReleased = false;
    }

    if (!vr_is_active() ||
        !configVrMotionControllerInput ||
        !configVrPhysicalGrabbing ||
        !vr_hand_interaction_is_bowser_hold(mario)) {
        sVrBowserGripHand = VR_CONTROLLER_COUNT;
        return false;
    }

    if (sVrBowserGripHand >= VR_CONTROLLER_COUNT) {
        // Bowser remains a native tail grab. Once either physical grip is
        // closed, that hand owns the hold until it is released.
        if (sVrGripPressed[VR_CONTROLLER_RIGHT]) {
            sVrBowserGripHand = VR_CONTROLLER_RIGHT;
        } else if (sVrGripPressed[VR_CONTROLLER_LEFT]) {
            sVrBowserGripHand = VR_CONTROLLER_LEFT;
        } else {
            return false;
        }
    }

    if (!sVrGripPressed[sVrBowserGripHand]) {
        sVrBowserGripHand = VR_CONTROLLER_COUNT;
        if (gripReleased != NULL) {
            *gripReleased = true;
        }
        return true;
    }

    if (turnInput != NULL && mario->controller != NULL) {
        f32 input = (f32)mario->controller->extStickX / 127.0f;
        const f32 magnitude = fabsf(input);
        if (magnitude <= VR_BOWSER_TURN_DEADZONE) {
            input = 0.0f;
        } else {
            input = copysignf(
                (magnitude - VR_BOWSER_TURN_DEADZONE) /
                    (1.0f - VR_BOWSER_TURN_DEADZONE),
                input
            );
        }
        *turnInput = clamp(input, -1.0f, 1.0f);
    }

    return true;
}

bool vr_hand_interaction_bowser_spin_active(void) {
    return sVrBowserGripHand < VR_CONTROLLER_COUNT &&
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

    sVrTrackedHeldPosition[0] = handPosition[0];
    sVrTrackedHeldPosition[1] = handPosition[1] - centerOffset;
    sVrTrackedHeldPosition[2] = handPosition[2];

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
    if (objectRadius <= 0.0f) {
        objectRadius = 30.0f;
    }
    if (objectHeight <= 0.0f) {
        objectHeight = 60.0f;
    }

    const f32 objectBottom =
        object->oPosY - object->hitboxDownOffset;
    const f32 objectTop = objectBottom + objectHeight;
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
    printf("[VR] Physical hold attached to Hoot.\n");
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
    printf("[VR] Physical hold anchored to a moving actor.\n");
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
    printf("[VR] Physical moving-actor hold released.\n");
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

    // The native pickup animation is deliberately bypassed. The object still
    // uses its normal HELD_HELD behavior, preserving fuses, NPC state, Lua
    // hooks, and network ownership while its local transform follows the hand.
    mario->input &= ~INPUT_INTERACT_OBJ_GRABBABLE;
    sVrTrackedHeldObject = object;
    sVrTrackedHeldHand = hand;
    vr_hand_interaction_update_held_position(
        object,
        handPosition,
        handVelocity
    );

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
    printf(
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
    printf(
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
    const Vec3f start,
    const Vec3f end,
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

    for (u32 sample = 0;
         sample <= VR_FIST_SWEEP_SAMPLES;
         sample++) {
        const f32 amount =
            (f32)sample / (f32)VR_FIST_SWEEP_SAMPLES;
        const f32 x = start[0] + (end[0] - start[0]) * amount;
        const f32 y = start[1] + (end[1] - start[1]) * amount;
        const f32 z = start[2] + (end[2] - start[2]) * amount;
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
    const Vec3f start,
    const Vec3f end,
    const Vec3f velocity,
    struct Object* object
) {
    const f32 fistRadius =
        vr_hand_interaction_fist_radius();
    if (!vr_hand_interaction_object_is_attackable(mario, object) ||
        !vr_hand_interaction_sweep_overlaps_object(
            start,
            end,
            fistRadius,
            vr_hand_interaction_fist_length(fistRadius),
            object
        )) {
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

    printf(
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
                    start,
                    end,
                    velocity,
                    object
                )) {
                return true;
            }
        }
    }

    return false;
}

void vr_hand_interaction_update(struct MarioState* mario) {
    // Remote Mario states run through the same interaction function. They
    // must not reset the local player's tracked fist state.
    if (mario == NULL || mario->playerIndex != 0) {
        return;
    }

    const bool interactionAvailable =
        vr_is_active() &&
        configVrMotionControllerInput &&
        mario->marioObj != NULL &&
        (mario->action & ACT_FLAG_INTANGIBLE) == 0;
    if (!interactionAvailable) {
        if (sVrTrackedHeldObject != NULL) {
            vr_hand_interaction_release_grab(mario, false);
        }
        if (sVrTrackedAnchorObject != NULL) {
            vr_hand_interaction_release_player_anchor(mario);
        }
        vr_hand_interaction_force_release_hoot(mario);
        vr_hand_interaction_reset();
        return;
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

    if (!vr_hand_interaction_is_bowser_hold(mario)) {
        sVrBowserGripHand = VR_CONTROLLER_COUNT;
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

    if (!configVrPhysicalGrabbing) {
        if (sVrTrackedHeldObject != NULL) {
            vr_hand_interaction_release_grab(mario, false);
        }
        if (sVrTrackedAnchorObject != NULL) {
            vr_hand_interaction_release_player_anchor(mario);
        }
        vr_hand_interaction_clear_hoot_hold();
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
            vr_get_controller_world_fist(
                hand,
                position,
                velocity
            );

        if (sVrTrackedHeldHand == hand) {
            if (!positionValid ||
                !sVrGripPressed[hand]) {
                vr_hand_interaction_release_grab(
                    mario,
                    positionValid
                );
            } else {
                vr_hand_interaction_update_held_position(
                    sVrTrackedHeldObject,
                    position,
                    velocity
                );
            }
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
        } else if (configVrPhysicalGrabbing &&
                   !gripWasPressed &&
                   sVrGripPressed[hand] &&
                   positionValid) {
            const bool grabbedObject =
                vr_hand_interaction_try_grab(
                    mario,
                    hand,
                    position,
                    velocity
                );
            if (!grabbedObject) {
                vr_hand_interaction_try_actor_hold(
                    mario,
                    hand,
                    position
                );
            }
        }

        const bool punchStarted =
            vr_consume_physical_punch(hand);
        const bool handIsHoldingObject =
            sVrTrackedHeldHand == hand ||
            sVrTrackedHootHand == hand ||
            sVrTrackedAnchorHand == hand;

        if (punchStarted &&
            configVrPhysicalPunching &&
            !handIsHoldingObject) {
            vr_hand_interaction_update_punch_sound(mario);
            vr_hand_interaction_register_motion_dive_punch(
                mario,
                hand
            );
        }

        if (!positionValid ||
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

        Vec3f sweepStart;
        vec3f_copy(sweepStart, position);
        if (sVrFistPreviousPositionValid[hand]) {
            Vec3f displacement;
            vec3f_dif(
                displacement,
                position,
                sVrFistPreviousPosition[hand]
            );
            if (vec3f_length(displacement) <=
                VR_FIST_MAX_SWEEP_DISTANCE) {
                vec3f_copy(
                    sweepStart,
                    sVrFistPreviousPosition[hand]
                );
            }
        }

        if (sVrFistActiveFrames[hand] > 0) {
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
}
