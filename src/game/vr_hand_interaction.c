#include <math.h>
#include <stdio.h>

#include "audio/external.h"
#include "characters.h"
#include "engine/math_util.h"
#include "interaction.h"
#include "mario.h"
#include "object_constants.h"
#include "object_fields.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "sm64.h"
#include "vr_hand_interaction.h"

#include "pc/configfile.h"
#include "pc/lua/smlua_hooks.h"
#include "pc/vr/vr.h"

#define VR_FIST_ACTIVE_FRAMES 33
#define VR_FIST_SWEEP_SAMPLES 4
#define VR_FIST_BASE_RADIUS 12.0f
#define VR_FIST_MAX_SWEEP_DISTANCE 150.0f
#define VR_PUNCH_SOUND_COMBO_RESET_FRAMES 18
#define VR_MOTION_DIVE_PAIR_WINDOW_FRAMES 5

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

static void vr_hand_interaction_reset(void) {
    for (u32 hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        sVrFistActiveFrames[hand] = 0;
        sVrFistPreviousPositionValid[hand] = false;
        sVrMotionDivePairFrames[hand] = 0;
    }
    sVrPunchSoundComboStep = 0;
    sVrPunchSoundComboResetFrames = 0;
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
        return set_mario_action(mario, ACT_DIVE, 0);
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
    if (!configVrMotionControlledDive ||
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

    if (!vr_is_active() ||
        !configVrMotionControllerInput ||
        !configVrPhysicalPunching ||
        mario->marioObj == NULL ||
        (mario->action & ACT_FLAG_INTANGIBLE) != 0) {
        vr_hand_interaction_reset();
        return;
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
        if (!configVrMotionControlledDive) {
            sVrMotionDivePairFrames[hand] = 0;
        } else if (sVrMotionDivePairFrames[hand] > 0) {
            sVrMotionDivePairFrames[hand]--;
        }
    }

    for (u32 hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        Vec3f position;
        Vec3f velocity;
        const bool positionValid = vr_get_controller_world_fist(
            hand,
            position,
            velocity
        );
        const bool punchStarted =
            vr_consume_physical_punch(hand);

        if (punchStarted) {
            vr_hand_interaction_update_punch_sound(mario);
            vr_hand_interaction_register_motion_dive_punch(
                mario,
                hand
            );
        }

        if (!positionValid) {
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
