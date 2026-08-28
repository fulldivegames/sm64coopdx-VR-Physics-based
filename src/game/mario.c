#include <PR/ultratypes.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sm64.h"
#include "area.h"
#include "audio/external.h"
#include "behavior_actions.h"
#include "behavior_data.h"
#include "camera.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "interaction.h"
#include "level_table.h"
#include "level_update.h"
#include "main.h"
#include "mario.h"
#include "mario_actions_airborne.h"
#include "mario_actions_automatic.h"
#include "mario_actions_cutscene.h"
#include "mario_actions_moving.h"
#include "mario_actions_object.h"
#include "mario_actions_stationary.h"
#include "mario_actions_submerged.h"
#include "mario_misc.h"
#include "mario_step.h"
#include "memory.h"
#include "object_fields.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "print.h"
#include "rendering_graph_node.h"
#include "vr_hand_interaction.h"
#include "save_file.h"
#include "sound_init.h"
#include "rumble_init.h"
#include "obj_behaviors.h"
#include "hardcoded.h"
#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "pc/network/network.h"
#include "pc/lua/smlua.h"
#include "pc/network/socket/socket.h"
#include "bettercamera.h"
#include "first_person_cam.h"

#define MAX_HANG_PREVENTION 64
#define VR_FIRST_PERSON_FORWARD_STEER_LIMIT 0x4000
#define VR_FIRST_PERSON_BACKPEDAL_SIDE_LIMIT 0x4000
#define VR_FIRST_PERSON_FORWARD_TO_BACK_BUFFER_FRAMES 18U
#define VR_FIRST_PERSON_SIDE_FLIP_PRIME_RATIO 0.40f
#define VR_FIRST_PERSON_FORWARD_REVERSAL_LATERAL_LIMIT 0.35f
#define VR_FIRST_PERSON_LATERAL_REVERSAL_FRAMES 16U
#define VR_FIRST_PERSON_LATERAL_AXIS_RATIO 0.45f
#define VR_FIRST_PERSON_LATERAL_FORWARD_BAND 0.55f
#define VR_FIRST_PERSON_LATERAL_REQUEST_FRAMES 16U
#define VR_FIRST_PERSON_BACKPEDAL_ENTER_RATIO -0.08f
#define VR_FIRST_PERSON_BACKPEDAL_EXIT_RATIO   0.08f
#define VR_FIRST_PERSON_SIDE_FLIP_MIN_SPEED 6.0f
#define VR_FIRST_PERSON_GROUND_SPEED_LIMIT 48.0f

static bool sVrBackpedalRequested = false;
static bool sVrBackpedalGraceActive = false;
static bool sVrAssistedSkidActive = false;
static bool sVrForwardSideFlipPrimed = false;
static bool sVrForwardRecoveryActive = false;
static bool sVrLateralSkidActive = false;
static s8 sVrLateralSideFlipPrimeSign = 0;
static u8 sVrBackpedalGraceTicks = 0;
static u8 sVrForwardSideFlipPrimeTicks = 0;
static u8 sVrLateralSideFlipPrimeTicks = 0;
static u8 sVrLateralSideFlipRequestTicks = 0;
static struct Object *sVrTwirlTornado = NULL;

static void vr_update_twirl_tornado_effect(struct MarioState *m) {
    if (m == NULL || m->playerIndex != 0) {
        return;
    }

    const bool twirling =
        vr_is_active() &&
        configVrTwirlTornadoEffect &&
        (m->action == ACT_TWIRLING ||
         m->action == ACT_TORNADO_TWIRLING);

    if (!twirling) {
        if (sVrTwirlTornado != NULL &&
            sVrTwirlTornado->activeFlags != ACTIVE_FLAG_DEACTIVATED &&
            sVrTwirlTornado->behavior ==
                segmented_to_virtual(bhvStaticObject)) {
            obj_mark_for_deletion(sVrTwirlTornado);
        }
        sVrTwirlTornado = NULL;
        return;
    }

    if (sVrTwirlTornado == NULL ||
        sVrTwirlTornado->activeFlags == ACTIVE_FLAG_DEACTIVATED ||
        sVrTwirlTornado->behavior !=
            segmented_to_virtual(bhvStaticObject) ||
        sVrTwirlTornado->parentObj != m->marioObj) {
        sVrTwirlTornado = spawn_object(
            m->marioObj,
            MODEL_VR_TWIRL_TORNADO,
            bhvStaticObject
        );
        if (sVrTwirlTornado == NULL) {
            return;
        }
        sVrTwirlTornado->oFaceAngleYaw = 0;
        sVrTwirlTornado->oInteractType = 0;
        sVrTwirlTornado->oIntangibleTimer = -1;
    }

    const unsigned int character =
        (m->character != NULL && m->character->type < CT_MAX)
            ? m->character->type
            : CT_MARIO;
    const f32 characterScale =
        (f32)config_vr_head_attachment_height_for_character(character) /
        (f32)VR_HEAD_ATTACHMENT_HEIGHT_MARIO;

    Vec3f headsetPosition;
    const bool headsetPositionValid =
        vr_get_stabilized_headset_world_position(
            headsetPosition,
            false
        );

    // Keep the visual shell directly beneath the player's tracked head in
    // X/Z, while its base remains planted at Mario's actual foot height.
    // Falling back to Mario's root preserves the effect during brief tracking
    // loss and in camera modes without a stabilized first-person head sample.
    sVrTwirlTornado->oPosX = headsetPositionValid
        ? headsetPosition[0]
        : m->pos[0];
    sVrTwirlTornado->oPosY = m->pos[1];
    sVrTwirlTornado->oPosZ = headsetPositionValid
        ? headsetPosition[2]
        : m->pos[2];
    sVrTwirlTornado->oFaceAngleYaw += 0x1800;
    obj_scale(sVrTwirlTornado, 0.0315f * characterScale);
    obj_update_gfx_pos_and_angle(sVrTwirlTornado);
}

static bool vr_get_first_person_raw_head_yaw_from_rotation(
    const float rotation[4],
    s16* yaw
) {
    if (rotation == NULL || yaw == NULL) {
        return false;
    }

    // Rotate OpenXR's forward vector (0, 0, -1) by the current HMD
    // quaternion, then convert its horizontal direction to an SM64 yaw.
    const float forwardX = -2.0f *
        (rotation[3] * rotation[1] + rotation[0] * rotation[2]);
    const float forwardZ = -1.0f + 2.0f *
        (rotation[0] * rotation[0] + rotation[1] * rotation[1]);

    if (fabsf(forwardX) < 0.0001f && fabsf(forwardZ) < 0.0001f) {
        return false;
    }

    // SM64's yaw direction is opposite OpenXR's pose-yaw direction. Measure
    // X against OpenXR's -Z forward axis, then invert it so looking right
    // produces rightward movement and looking left produces leftward movement.
    *yaw = (s16)-atan2s(-forwardZ, forwardX);
    return true;
}

static bool vr_get_first_person_raw_head_yaw(s16* yaw) {
    float rotation[4] = { 0 };

    return vr_get_head_rotation(rotation) &&
        vr_get_first_person_raw_head_yaw_from_rotation(
            rotation,
            yaw
        );
}

static bool vr_get_first_person_facing_rotation(
    float rotation[4]
) {
    if (rotation == NULL) {
        return false;
    }

    unsigned int source = configVrFacingSource;
    if (source == VR_FACING_SOURCE_LEFT_CONTROLLER ||
        source == VR_FACING_SOURCE_RIGHT_CONTROLLER) {
        const u32 hand = source == VR_FACING_SOURCE_LEFT_CONTROLLER
            ? VR_CONTROLLER_LEFT
            : VR_CONTROLLER_RIGHT;
        struct VrControllerState state = { 0 };
        if (vr_get_controller_state(hand, &state)) {
            // Aim poses are standardized by OpenXR for pointing. Fall back
            // to the grip pose only on runtimes that do not expose aim.
            const float* sourceRotation = state.aimPoseValid
                ? state.aimRotation
                : (state.gripPoseValid ? state.gripRotation : NULL);
            if (sourceRotation != NULL) {
                memcpy(rotation, sourceRotation, sizeof(state.aimRotation));
                return true;
            }
        }
    }

    // Headset is both the default and a safe fallback if the selected hand
    // temporarily loses tracking or motion-controller input is disabled.
    return vr_get_head_rotation(rotation);
}

static bool vr_get_first_person_raw_facing_yaw(s16* yaw) {
    float rotation[4] = { 0 };

    return vr_get_first_person_facing_rotation(rotation) &&
        vr_get_first_person_raw_head_yaw_from_rotation(
            rotation,
            yaw
        );
}

static s16 vr_first_person_facing_yaw_offset(void) {
    s16 rawYaw = 0;
    if (!vr_get_first_person_raw_facing_yaw(&rawYaw)) {
        return 0;
    }
    // Headset and controller poses are both reported relative to the same
    // recentered OpenXR origin. Do not zero the selected source again on a
    // level transition; doing so would redefine forward during loading.
    return rawYaw;
}

static s16 vr_first_person_compress_stick_yaw(s16 stickYaw) {
    // Express the stick direction relative to forward, halve its sideways
    // angle, then convert it back. Forward and backward remain available,
    // while full left/right become 45-degree steering directions.
    s32 relativeYaw = (s16)(stickYaw + 0x8000);

    if (relativeYaw > 0x4000) {
        relativeYaw = 0x8000 - (0x8000 - relativeYaw) / 2;
    } else if (relativeYaw < -0x4000) {
        relativeYaw = -0x8000 + (0x8000 + relativeYaw) / 2;
    } else {
        relativeYaw /= 2;
    }

    return (s16)(relativeYaw + 0x8000);
}

static bool vr_first_person_locomotion_available(
    struct MarioState* m
) {
    return m != NULL &&
        m->playerIndex == 0 &&
        m->controller != NULL &&
        vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        !configVrOriginalMarioMovement;
}

bool vr_first_person_movement_overhaul_active(
    struct MarioState* m
) {
    (void)m;
    return false;
}

static bool vr_first_person_backpedal_requested(
    struct MarioState* m
) {
    return vr_first_person_locomotion_available(m) &&
        sVrBackpedalRequested;
}

static bool vr_first_person_skid_transition_action(u32 action) {
    switch (action) {
        case ACT_WALKING:
        case ACT_TURNING_AROUND:
        case ACT_FINISH_TURNING_AROUND:
        case ACT_BRAKING:
        case ACT_DECELERATING:
        case ACT_SIDE_FLIP_LAND:
        case ACT_SIDE_FLIP_LAND_STOP:
            return true;
        default:
            return false;
    }
}

static bool vr_first_person_skid_landing_action(u32 action) {
    return action == ACT_SIDE_FLIP_LAND ||
        action == ACT_SIDE_FLIP_LAND_STOP;
}

static void vr_reset_first_person_lateral_reversal(void) {
    sVrLateralSideFlipPrimeSign = 0;
    sVrLateralSideFlipPrimeTicks = 0;
    sVrLateralSideFlipRequestTicks = 0;
}

static void vr_update_first_person_lateral_reversal(
    struct MarioState* m
) {
    if (sVrLateralSideFlipPrimeTicks > 0) {
        sVrLateralSideFlipPrimeTicks--;
    }
    if (sVrLateralSideFlipPrimeTicks == 0) {
        sVrLateralSideFlipPrimeSign = 0;
    }
    if (sVrLateralSideFlipRequestTicks > 0) {
        sVrLateralSideFlipRequestTicks--;
    }

    if (!vr_first_person_movement_overhaul_active(m) ||
        !vr_first_person_skid_transition_action(m->action)) {
        sVrLateralSkidActive = false;
        vr_reset_first_person_lateral_reversal();
        return;
    }

    if (m->controller->stickMag <= 0.0f) {
        // Preserve the prime briefly across the neutral samples produced by
        // a real stick during a quick left/right reversal.
        return;
    }

    const f32 forwardAxis =
        m->controller->stickY / m->controller->stickMag;
    const f32 lateralAxis =
        m->controller->stickX / m->controller->stickMag;

    // A circular stick motion necessarily passes through a strong forward or
    // rear sector. Clear the lateral prime there so circling the stick cannot
    // accidentally enter ACT_TURNING_AROUND and stop Mario at east or west.
    if (fabsf(forwardAxis) >
        VR_FIRST_PERSON_LATERAL_FORWARD_BAND) {
        vr_reset_first_person_lateral_reversal();
        return;
    }

    if (fabsf(lateralAxis) <
        VR_FIRST_PERSON_LATERAL_AXIS_RATIO) {
        return;
    }

    const s8 lateralSign = lateralAxis > 0.0f ? 1 : -1;
    if (sVrLateralSideFlipPrimeSign != 0 &&
        lateralSign != sVrLateralSideFlipPrimeSign &&
        sVrLateralSideFlipPrimeTicks > 0 &&
        fabsf(m->forwardVel) >=
            VR_FIRST_PERSON_SIDE_FLIP_MIN_SPEED) {
        sVrLateralSideFlipRequestTicks =
            VR_FIRST_PERSON_LATERAL_REQUEST_FRAMES;
        sVrLateralSkidActive = true;
        sVrLateralSideFlipPrimeSign = 0;
        sVrLateralSideFlipPrimeTicks = 0;
        return;
    }

    if (sVrLateralSideFlipRequestTicks == 0) {
        sVrLateralSideFlipPrimeSign = lateralSign;
        sVrLateralSideFlipPrimeTicks =
            VR_FIRST_PERSON_LATERAL_REVERSAL_FRAMES;
    }
}

static void vr_clamp_first_person_horizontal_vector(
    f32* velocityX,
    f32* velocityZ
) {
    if (velocityX == NULL || velocityZ == NULL) {
        return;
    }

    if (!isfinite(*velocityX) || !isfinite(*velocityZ)) {
        *velocityX = 0.0f;
        *velocityZ = 0.0f;
        return;
    }

    const f32 speedSquared =
        *velocityX * *velocityX +
        *velocityZ * *velocityZ;
    const f32 speedLimit = VR_FIRST_PERSON_GROUND_SPEED_LIMIT *
        vr_special_moves_sonic_speed_scale();
    const f32 speedLimitSquared = speedLimit * speedLimit;
    if (speedSquared > speedLimitSquared) {
        const f32 scale =
            speedLimit /
            sqrtf(speedSquared);
        *velocityX *= scale;
        *velocityZ *= scale;
    }
}

static void vr_stabilize_first_person_skid_speed(
    struct MarioState* m
) {
    if (!vr_first_person_locomotion_available(m) ||
        !vr_first_person_skid_transition_action(m->action)) {
        return;
    }

    const bool landingAction =
        vr_first_person_skid_landing_action(m->action);
    const bool invalidHorizontalVector =
        !isfinite(m->vel[0]) ||
        !isfinite(m->vel[2]) ||
        !isfinite(m->slideVelX) ||
        !isfinite(m->slideVelZ);

    const f32 speedLimit = VR_FIRST_PERSON_GROUND_SPEED_LIMIT *
        vr_special_moves_sonic_speed_scale();
    if (vr_first_person_movement_overhaul_active(m)) {
        // Movement Overhaul deliberately separates Mario's body yaw from his
        // world-space travel vector. Never repair that valid separation by
        // calling mario_set_forward_vel(), because that function rebuilds the
        // vector from faceAngle and was reintroducing the circular-stick stop.
        if (invalidHorizontalVector) {
            m->vel[0] = 0.0f;
            m->vel[2] = 0.0f;
            m->slideVelX = 0.0f;
            m->slideVelZ = 0.0f;
        } else {
            vr_clamp_first_person_horizontal_vector(&m->vel[0], &m->vel[2]);
            vr_clamp_first_person_horizontal_vector(
                &m->slideVelX,
                &m->slideVelZ
            );
        }
        if (!isfinite(m->forwardVel)) {
            m->forwardVel = 0.0f;
        } else {
            m->forwardVel = clamp(
                m->forwardVel,
                -speedLimit,
                speedLimit
            );
        }
        return;
    }

    const f32 previousSpeed = m->forwardVel;
    const bool invalidForwardSpeed = !isfinite(previousSpeed);
    if (invalidForwardSpeed) {
        m->forwardVel = 0.0f;
    } else {
        m->forwardVel = clamp(
            previousSpeed,
            -speedLimit,
            speedLimit
        );
    }

    const bool assistedTransitionActive =
        sVrBackpedalRequested ||
        sVrBackpedalGraceActive ||
        sVrForwardSideFlipPrimed ||
        sVrForwardRecoveryActive;
    const f32 forwardDot =
        m->vel[0] * sins(m->faceAngle[1]) +
        m->vel[2] * coss(m->faceAngle[1]);
    const bool vectorHasOppositeSign =
        isfinite(forwardDot) &&
        fabsf(m->forwardVel) > 0.01f &&
        m->forwardVel * forwardDot < -0.01f;

    if (invalidForwardSpeed ||
        invalidHorizontalVector ||
        (!landingAction &&
         (m->forwardVel != previousSpeed ||
          assistedTransitionActive ||
          vectorHasOppositeSign))) {
        // The custom skid changes both Mario's facing direction and the sign
        // of forwardVel. Rebuild every authoritative assisted-ground vector,
        // not just an out-of-range scalar, so stale slide/velocity components
        // cannot survive an action handoff and launch Mario across the floor.
        mario_set_forward_vel(m, m->forwardVel);
        if (m->forwardVel != previousSpeed) {
            sVrForwardRecoveryActive = false;
        }
        return;
    }

    // Landing may intentionally rotate Mario without rotating his existing
    // momentum. Preserve that direction, but still reject corrupt or runaway
    // horizontal vector magnitudes in both movement representations.
    vr_clamp_first_person_horizontal_vector(
        &m->vel[0],
        &m->vel[2]
    );
    vr_clamp_first_person_horizontal_vector(
        &m->slideVelX,
        &m->slideVelZ
    );
}

static void vr_update_first_person_backpedal_state(
    struct MarioState* m
) {
    // These are gameplay-tick windows, not rendered-frame windows. Using
    // gGlobalTimer made the same nominal window several times shorter on a
    // 90/120 Hz headset than at the engine's 30 Hz simulation rate.
    if (sVrBackpedalGraceTicks > 0) {
        sVrBackpedalGraceTicks--;
    }
    if (!vr_first_person_locomotion_available(m) ||
        m->controller->stickMag <= 0.0f) {
        sVrBackpedalRequested = false;
        sVrBackpedalGraceActive = false;
        sVrAssistedSkidActive = false;
        sVrForwardSideFlipPrimed = false;
        sVrForwardRecoveryActive = false;
        sVrLateralSkidActive = false;
        sVrBackpedalGraceTicks = 0;
        sVrForwardSideFlipPrimeTicks = 0;
        vr_reset_first_person_lateral_reversal();
        return;
    }

    const bool wasRequested = sVrBackpedalRequested;
    const f32 forwardAxis =
        m->controller->stickY /
        m->controller->stickMag;
    // Classify the stick by normalized direction rather than a fixed raw-Y
    // value. Partial diagonal deflections now register as reliably as a fully
    // tilted stick, while separate enter/exit angles still reject axis noise.
    if (sVrBackpedalRequested) {
        if (forwardAxis >
            VR_FIRST_PERSON_BACKPEDAL_EXIT_RATIO) {
            sVrBackpedalRequested = false;
        }
    } else if (forwardAxis <
               VR_FIRST_PERSON_BACKPEDAL_ENTER_RATIO) {
        sVrBackpedalRequested = true;
    }

    if (!wasRequested &&
        sVrBackpedalRequested &&
        m->action == ACT_WALKING &&
        m->forwardVel >= VR_FIRST_PERSON_SIDE_FLIP_MIN_SPEED) {
        sVrBackpedalGraceActive = true;
        sVrAssistedSkidActive = false;
        sVrBackpedalGraceTicks =
            VR_FIRST_PERSON_FORWARD_TO_BACK_BUFFER_FRAMES;
    }

    if (wasRequested && !sVrBackpedalRequested) {
        sVrBackpedalGraceActive = false;
        sVrAssistedSkidActive = false;
        sVrBackpedalGraceTicks = 0;
        sVrForwardRecoveryActive =
            m->action == ACT_WALKING &&
            m->forwardVel < 0.0f;
    } else if (sVrBackpedalRequested) {
        sVrForwardRecoveryActive = false;
    }

    if (m->action == ACT_SIDE_FLIP ||
        sVrBackpedalGraceTicks == 0) {
        sVrBackpedalGraceActive = false;
        sVrAssistedSkidActive = false;
        sVrBackpedalGraceTicks = 0;
    }
}

bool vr_first_person_lateral_side_flip_requested(
    struct MarioState* m
) {
    return vr_first_person_movement_overhaul_active(m) &&
        sVrLateralSideFlipRequestTicks > 0;
}

void vr_consume_first_person_lateral_side_flip(void) {
    sVrLateralSkidActive = false;
    vr_reset_first_person_lateral_reversal();
}

bool vr_first_person_lateral_skid_active(struct MarioState* m) {
    return vr_first_person_movement_overhaul_active(m) &&
        sVrLateralSkidActive;
}

void vr_finish_first_person_lateral_skid(void) {
    sVrLateralSkidActive = false;
    vr_reset_first_person_lateral_reversal();
}

bool vr_first_person_backpedal_grace_active(
    struct MarioState* m
) {
    return vr_first_person_backpedal_requested(m) &&
        sVrBackpedalGraceActive;
}

bool vr_first_person_assisted_skid_active(struct MarioState* m) {
    (void)m;
    return false;
}

void vr_finish_first_person_assisted_skid(void) {
    // This is a one-way phase latch. The held backward stick cannot re-arm it;
    // a new skid is only armed after the stick first leaves the back region.
    sVrAssistedSkidActive = false;
    if (sVrBackpedalGraceActive) {
        // Count the slightly tighter post-skid jump opportunity from the
        // beginning of actual reverse motion, not from the visible skid.
        sVrBackpedalGraceTicks =
            VR_FIRST_PERSON_FORWARD_TO_BACK_BUFFER_FRAMES;
    }
}

bool vr_first_person_side_flip_ready(struct MarioState* m) {
    if (!vr_first_person_locomotion_available(m) ||
        !sVrBackpedalRequested ||
        m->intendedMag <= 0.0f ||
        m->forwardVel < VR_FIRST_PERSON_SIDE_FLIP_MIN_SPEED) {
        return false;
    }

    // The assist is intentionally one-way: positive forward momentum followed
    // by entry into the backward stick region. Leaving backpedal for forward
    // movement must use forward recovery and produce a normal jump.
    return true;
}

bool vr_first_person_backpedal_active(struct MarioState* m) {
    if (!vr_first_person_backpedal_requested(m)) {
        return false;
    }

    // Side-flip eligibility and the visible skid are separate states. Once the
    // bounded skid explicitly finishes, reverse movement starts immediately
    // even though the jump window can remain open.
    return !(sVrBackpedalGraceActive &&
        ((m->action == ACT_WALKING &&
          m->forwardVel >= VR_FIRST_PERSON_SIDE_FLIP_MIN_SPEED) ||
         m->action == ACT_TURNING_AROUND));
}

bool vr_first_person_forward_recovery_active(
    struct MarioState* m
) {
    return vr_first_person_locomotion_available(m) &&
        !sVrBackpedalRequested &&
        sVrForwardRecoveryActive;
}

void vr_finish_first_person_forward_recovery(void) {
    sVrForwardRecoveryActive = false;
}

static s16 vr_get_first_person_view_yaw_from_head_yaw(
    s16 headYaw
) {
    const s32 calibration =
        (s32)MIN(configVrMovementCalibration, 100U) - 50;

    // This is the same world-space direction produced by holding the stick
    // straight forward in First Person Mode: Free Camera yaw establishes the
    // body-independent base, then the HMD, action turn, and calibration are
    // applied on top of it.
    return (s16)(
        -gNewCamera.yaw - 0x4000 +
        headYaw +
        vr_get_first_person_action_turn_yaw() +
        calibration * 0x10000 / 720
    );
}

static s16 vr_get_first_person_aim_yaw_from_head_yaw(
    s16 headYaw
) {
    // Cannon aiming must match the rendered center of view exactly. Movement
    // calibration deliberately rotates locomotion, but applying it here would
    // make the cannon fire beside the headset reticle.
    return (s16)(
        -gNewCamera.yaw - 0x4000 +
        headYaw +
        vr_get_first_person_action_turn_yaw()
    );
}

s16 vr_get_first_person_view_yaw(void) {
    return vr_get_first_person_view_yaw_from_head_yaw(
        vr_first_person_facing_yaw_offset()
    );
}

s16 vr_get_first_person_headset_yaw(void) {
    return vr_get_first_person_aim_yaw_from_head_yaw(
        vr_first_person_facing_yaw_offset()
    );
}

static bool vr_get_first_person_direction(
    struct MarioState* m,
    Vec3f direction,
    bool applyMovementCalibration
) {
    float rotation[4] = { 0 };

    if (m == NULL ||
        direction == NULL ||
        m->playerIndex != 0 ||
        !vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !(applyMovementCalibration
            ? vr_get_first_person_facing_rotation(rotation)
            : vr_get_head_rotation(rotation))) {
        return false;
    }

    // Rotate OpenXR's local forward vector (0, 0, -1). Its vertical
    // component supplies pitch; the matching world-space yaw uses the stable
    // free-camera base and action turns, with locomotion calibration included
    // only for callers that explicitly request it.
    const f32 localForwardY = 2.0f *
        (rotation[3] * rotation[0] -
         rotation[1] * rotation[2]);
    if (!isfinite(localForwardY)) {
        return false;
    }

    // OpenXR normalizes the cached quaternion once per frame, so its rotated
    // forward vector is already unit length. Reusing that invariant avoids a
    // second normalization in this gameplay hot path.
    const f32 normalizedY = clamp(
        localForwardY,
        -1.0f,
        1.0f
    );
    const f32 horizontalLength = sqrtf(MAX(
        0.0f,
        1.0f - normalizedY * normalizedY
    ));
    s16 headYaw = 0;
    vr_get_first_person_raw_head_yaw_from_rotation(
        rotation,
        &headYaw
    );
    const s16 viewYaw = applyMovementCalibration
        ? vr_get_first_person_view_yaw_from_head_yaw(headYaw)
        : vr_get_first_person_aim_yaw_from_head_yaw(headYaw);

    direction[0] = horizontalLength * sins(viewYaw);
    direction[1] = normalizedY;
    direction[2] = horizontalLength * coss(viewYaw);
    return true;
}

bool vr_get_first_person_view_direction(
    struct MarioState* m,
    Vec3f direction
) {
    return vr_get_first_person_direction(m, direction, true);
}

bool vr_get_first_person_aim_direction(
    struct MarioState* m,
    Vec3f direction
) {
    return vr_get_first_person_direction(m, direction, false);
}

static bool vr_is_normal_jump_landing_action(u32 action) {
    switch (action) {
        case ACT_JUMP_LAND:
        case ACT_JUMP_LAND_STOP:
        case ACT_DOUBLE_JUMP_LAND:
        case ACT_DOUBLE_JUMP_LAND_STOP:
        case ACT_TRIPLE_JUMP_LAND:
        case ACT_TRIPLE_JUMP_LAND_STOP:
        case ACT_BACKFLIP_LAND:
        case ACT_BACKFLIP_LAND_STOP:
        case ACT_SIDE_FLIP_LAND:
        case ACT_SIDE_FLIP_LAND_STOP:
        case ACT_LONG_JUMP_LAND:
        case ACT_LONG_JUMP_LAND_STOP:
        case ACT_FREEFALL_LAND:
        case ACT_FREEFALL_LAND_STOP:
        case ACT_HOLD_JUMP_LAND:
        case ACT_HOLD_JUMP_LAND_STOP:
        case ACT_HOLD_FREEFALL_LAND:
        case ACT_HOLD_FREEFALL_LAND_STOP:
        case ACT_QUICKSAND_JUMP_LAND:
        case ACT_HOLD_QUICKSAND_JUMP_LAND:
            return true;
        default:
            return false;
    }
}

static void vr_turn_toward_headset_on_jump_landing(
    struct MarioState* m,
    u32 landingAction
) {
    Vec3f headsetDirection;

    if (m == NULL ||
        !configVrTurnDuringJumps ||
        !(m->action & ACT_FLAG_AIR) ||
        !vr_is_normal_jump_landing_action(landingAction) ||
        !vr_get_first_person_aim_direction(m, headsetDirection)) {
        return;
    }

    const f32 horizontalLengthSquared =
        headsetDirection[0] * headsetDirection[0] +
        headsetDirection[2] * headsetDirection[2];
    if (horizontalLengthSquared <= 0.0001f) {
        return;
    }

    const s16 headsetYaw = atan2s(
        headsetDirection[2],
        headsetDirection[0]
    );

    // Only change the direction Mario faces. In particular, do not call
    // mario_set_forward_vel here: the horizontal velocity vector, scalar
    // speed, landing timers, and jump-combo state must survive unchanged.
    // The next chained jump will begin from this new heading while the
    // just-completed jump retains its exact landing momentum.
    m->faceAngle[1] = headsetYaw;
    m->intendedYaw = headsetYaw;
}

u32 unused80339F10;
s8 filler80339F1C[20];
u16 gLocalBubbleCounter = 0;


/**************************************************
 *                    ANIMATIONS                  *
 **************************************************/

/**
 * Checks if Mario's animation has reached its end point.
 */
s32 is_anim_at_end(struct MarioState *m) {
    if (!m) { return FALSE; }
    struct Object *o = m->marioObj;

    if (o->header.gfx.animInfo.curAnim == NULL) { return TRUE; }
    return (o->header.gfx.animInfo.animFrame + 1) == o->header.gfx.animInfo.curAnim->loopEnd;
}

/**
 * Checks if Mario's animation has surpassed 2 frames before its end point.
 */
s32 is_anim_past_end(struct MarioState *m) {
    if (m == NULL || m->marioObj == NULL) { return 0; }
    struct Object *o = m->marioObj;

    if (o->header.gfx.animInfo.curAnim == NULL) { return 0; }
    return o->header.gfx.animInfo.animFrame >= (o->header.gfx.animInfo.curAnim->loopEnd - 2);
}

static s16 mario_set_animation_internal(struct MarioState *m, s32 targetAnimID, s32 accel) {
    if (!m) { return 0; }
    struct Object *o = m->marioObj;
    if (!o || !m->animation) { return 0; }

    load_patchable_table(m->animation, targetAnimID, true);
    if (!m->animation->targetAnim) { return 0; }

    if (o->header.gfx.animInfo.animID != targetAnimID) {
        struct Animation *targetAnim = m->animation->targetAnim;
        o->header.gfx.animInfo.animID = targetAnimID;
        o->header.gfx.animInfo.curAnim = targetAnim;
        o->header.gfx.animInfo.animYTrans = m->unkB0;

        if (targetAnim->flags & ANIM_FLAG_2) {
            o->header.gfx.animInfo.animFrameAccelAssist = (targetAnim->startFrame << 0x10);
        } else {
            if (targetAnim->flags & ANIM_FLAG_BACKWARD) {
                o->header.gfx.animInfo.animFrameAccelAssist = (targetAnim->startFrame << 0x10) + accel;
            } else {
                o->header.gfx.animInfo.animFrameAccelAssist = (targetAnim->startFrame << 0x10) - accel;
            }
        }

        o->header.gfx.animInfo.animFrame = (o->header.gfx.animInfo.animFrameAccelAssist >> 0x10);
    }

    o->header.gfx.animInfo.animAccel = accel;

    return o->header.gfx.animInfo.animFrame;
}

/**
 * Sets Mario's animation without any acceleration, running at its default rate.
 */
s16 set_mario_animation(struct MarioState *m, s32 targetAnimID) {
    return mario_set_animation_internal(m, targetAnimID, 0x10000);
}

/**
 * Sets Mario's animation where the animation is sped up or
 * slowed down via acceleration.
 */
s16 set_mario_anim_with_accel(struct MarioState *m, s32 targetAnimID, s32 accel) {
    return mario_set_animation_internal(m, targetAnimID, accel);
}

/**
 * Sets the character specific animation without any acceleration, running at its default rate.
 */
s16 set_character_animation(struct MarioState *m, enum CharacterAnimID targetAnimID) {
    return mario_set_animation_internal(m, get_character_anim(m, targetAnimID), 0x10000);
}

/**
 * Sets character specific animation where the animation is sped up or
 * slowed down via acceleration.
 */
s16 set_character_anim_with_accel(struct MarioState *m, enum CharacterAnimID targetAnimID, s32 accel) {
    return mario_set_animation_internal(m, get_character_anim(m, targetAnimID), accel);
}

/**
 * Sets the animation to a specific "next" frame from the frame given.
 */
void set_anim_to_frame(struct MarioState *m, s16 animFrame) {
    if (m == NULL || m->marioObj == NULL) { return; }

    struct AnimInfo *animInfo = &m->marioObj->header.gfx.animInfo;
    struct Animation *curAnim = animInfo->curAnim;
    if (animInfo == NULL) { return; }

    if (animInfo->animAccel) {
        if (curAnim != NULL && curAnim->flags & ANIM_FLAG_BACKWARD) {
            animInfo->animFrameAccelAssist = (animFrame << 0x10) + animInfo->animAccel;
        } else {
            animInfo->animFrameAccelAssist = (animFrame << 0x10) - animInfo->animAccel;
        }
    } else {
        if (curAnim != NULL && curAnim->flags & ANIM_FLAG_BACKWARD) {
            animInfo->animFrame = animFrame + 1;
        } else {
            animInfo->animFrame = animFrame - 1;
        }
    }
}

s32 is_anim_past_frame(struct MarioState *m, s16 animFrame) {
    s32 isPastFrame;
    s32 acceleratedFrame = animFrame << 0x10;

    if (!m || !m->marioObj) { return TRUE; }
    struct AnimInfo *animInfo = &m->marioObj->header.gfx.animInfo;

    if (!animInfo->curAnim) { return TRUE; }
    struct Animation *curAnim = animInfo->curAnim;

    if (animInfo->animAccel) {
        if (curAnim->flags & ANIM_FLAG_BACKWARD) {
            isPastFrame =
                (animInfo->animFrameAccelAssist > acceleratedFrame)
                && (acceleratedFrame >= (animInfo->animFrameAccelAssist - animInfo->animAccel));
        } else {
            isPastFrame =
                (animInfo->animFrameAccelAssist < acceleratedFrame)
                && (acceleratedFrame <= (animInfo->animFrameAccelAssist + animInfo->animAccel));
        }
    } else {
        if (curAnim->flags & ANIM_FLAG_BACKWARD) {
            isPastFrame = (animInfo->animFrame == (animFrame + 1));
        } else {
            isPastFrame = ((animInfo->animFrame + 1) == animFrame);
        }
    }

    return isPastFrame;
}

/**
 * Rotates the animation's translation into the global coordinate system
 * and returns the animation's flags.
 */
s16 find_mario_anim_flags_and_translation(struct Object *obj, s32 yaw, VEC_OUT Vec3s translation) {
    if (!obj) { return 0; }
    f32 dx;
    f32 dz;

    struct Animation *curAnim = (void *) obj->header.gfx.animInfo.curAnim;
    if (curAnim == NULL) { return 0; }
    s16 animFrame = geo_update_animation_frame(&obj->header.gfx.animInfo, NULL);
    u16 *animIndex = segmented_to_virtual((void *) curAnim->index);

    f32 s = (f32) sins(yaw);
    f32 c = (f32) coss(yaw);

    dx = retrieve_animation_value(curAnim, animFrame, &animIndex) / 4.0f;
    translation[1] = retrieve_animation_value(curAnim, animFrame, &animIndex) / 4.0f;
    dz = retrieve_animation_value(curAnim, animFrame, &animIndex) / 4.0f;

    translation[0] = (dx * c) + (dz * s);
    translation[2] = (-dx * s) + (dz * c);

    return curAnim->flags;
}

/**
 * Updates Mario's position from his animation's translation.
 */
void update_mario_pos_for_anim(struct MarioState *m) {
    if (!m) { return; }
    Vec3s translation;
    s16 flags;

    flags = find_mario_anim_flags_and_translation(m->marioObj, m->faceAngle[1], translation);

    if (flags & (ANIM_FLAG_HOR_TRANS | ANIM_FLAG_6)) {
        m->pos[0] += (f32) translation[0];
        m->pos[2] += (f32) translation[2];
    }

    if (flags & (ANIM_FLAG_VERT_TRANS | ANIM_FLAG_6)) {
        m->pos[1] += (f32) translation[1];
    }
}

/**
 * Finds the vertical translation from Mario's animation.
 */
s16 return_mario_anim_y_translation(struct MarioState *m) {
    if (!m) { return 0; }
    Vec3s translation = { 0 };
    find_mario_anim_flags_and_translation(m->marioObj, 0, translation);

    return translation[1];
}

/**************************************************
 *                      AUDIO                     *
 **************************************************/

/**
 * Plays a sound if if Mario doesn't have the flag being checked.
 */
void play_sound_if_no_flag(struct MarioState *m, u32 soundBits, u32 flags) {
    if (!m) { return; }
    if (!(m->flags & flags)) {
        play_sound(soundBits, m->marioObj->header.gfx.cameraToObject);
        m->flags |= flags;
    }
}

/**
 * Plays a jump sound if one has not been played since the last action change.
 */
void play_mario_jump_sound(struct MarioState *m) {
    if (!m) { return; }
    if (!(m->flags & MARIO_MARIO_SOUND_PLAYED)) {
#ifndef VERSION_JP
        if (m->action == ACT_TRIPLE_JUMP) {
            play_character_sound_offset(m, CHAR_SOUND_YAHOO_WAHA_YIPPEE, ((gAudioRandom % 5) << 16));
        } else {
#endif
            play_character_sound_offset(m, CHAR_SOUND_YAH_WAH_HOO, ((gAudioRandom % 3) << 16));
#ifndef VERSION_JP
        }
#endif
        m->flags |= MARIO_MARIO_SOUND_PLAYED;
    }
}

/**
 * Adjusts the volume/pitch of sounds from Mario's speed.
 */
void adjust_sound_for_speed(struct MarioState *m) {
    if (!m) { return; }
    s32 absForwardVel = (m->forwardVel > 0.0f) ? m->forwardVel : -m->forwardVel;
    set_sound_moving_speed(SOUND_BANK_MOVING, (absForwardVel > 100) ? 100 : absForwardVel);
}

/**
 * Spawns particles if the step sound says to, then either plays a step sound or relevant other sound.
 */
void play_sound_and_spawn_particles(struct MarioState *m, u32 soundBits, u32 waveParticleType) {
    if (!m) { return; }
    if (m->terrainSoundAddend == (SOUND_TERRAIN_WATER << 16)) {
        if (waveParticleType != 0) {
            set_mario_particle_flags(m, PARTICLE_SHALLOW_WATER_SPLASH, FALSE);
        } else {
            set_mario_particle_flags(m, PARTICLE_SHALLOW_WATER_WAVE, FALSE);
        }
    } else {
        if (m->terrainSoundAddend == (SOUND_TERRAIN_SAND << 16)) {
            set_mario_particle_flags(m, PARTICLE_DIRT, FALSE);
        } else if (m->terrainSoundAddend == (SOUND_TERRAIN_SNOW << 16)) {
            set_mario_particle_flags(m, PARTICLE_SNOW, FALSE);
        }
    }

    if (soundBits == CHAR_SOUND_PUNCH_HOO) {
        play_character_sound(m, CHAR_SOUND_PUNCH_HOO);
        return;
    }

    if ((m->flags & MARIO_METAL_CAP) || soundBits == SOUND_ACTION_UNSTUCK_FROM_GROUND) {
        play_sound(soundBits, m->marioObj->header.gfx.cameraToObject);
    } else {
        play_sound(m->terrainSoundAddend + soundBits, m->marioObj->header.gfx.cameraToObject);
    }
}

/**
 * Plays an environmental sound if one has not been played since the last action change.
 */
void play_mario_action_sound(struct MarioState *m, u32 soundBits, u32 waveParticleType) {
    if (!m) { return; }
    if (!(m->flags & MARIO_ACTION_SOUND_PLAYED)) {
        play_sound_and_spawn_particles(m, soundBits, waveParticleType);
        m->flags |= MARIO_ACTION_SOUND_PLAYED;
    }
}

/**
 * Plays a landing sound, accounting for metal cap.
 */
void play_mario_landing_sound(struct MarioState *m, u32 soundBits) {
    if (!m) { return; }
    play_sound_and_spawn_particles(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_LANDING : soundBits, 1);
}

/**
 * Plays a landing sound, accounting for metal cap. Unlike play_mario_landing_sound,
 * this function uses play_mario_action_sound, making sure the sound is only
 * played once per action.
 */
void play_mario_landing_sound_once(struct MarioState *m, u32 soundBits) {
    if (!m) { return; }
    play_mario_action_sound(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_LANDING : soundBits, 1);
}

/**
 * Plays a heavy landing (ground pound, etc.) sound, accounting for metal cap.
 */
void play_mario_heavy_landing_sound(struct MarioState *m, u32 soundBits) {
    if (!m) { return; }
    play_sound_and_spawn_particles(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_HEAVY_LANDING : soundBits, 1);
}

/**
 * Plays a heavy landing (ground pound, etc.) sound, accounting for metal cap.
 * Unlike play_mario_heavy_landing_sound, this function uses play_mario_action_sound,
 * making sure the sound is only played once per action.
 */
void play_mario_heavy_landing_sound_once(struct MarioState *m, u32 soundBits) {
    if (!m) { return; }
    play_mario_action_sound(
        m, (m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_HEAVY_LANDING : soundBits, 1);
}

/**
 * Plays action and Mario sounds relevant to what was passed into the function.
 */
void play_mario_sound(struct MarioState *m, s32 actionSound, s32 marioSound) {
    if (!m) { return; }
    if (actionSound == SOUND_ACTION_TERRAIN_JUMP) {
        play_mario_action_sound(m, (m->flags & MARIO_METAL_CAP) ? (s32) SOUND_ACTION_METAL_JUMP
                                                                : (s32) SOUND_ACTION_TERRAIN_JUMP, 1);
    } else {
        play_sound_if_no_flag(m, actionSound, MARIO_ACTION_SOUND_PLAYED);
    }

    if (marioSound == 0) {
        play_mario_jump_sound(m);
    }

    if (marioSound != -1) {
        play_character_sound_if_no_flag(m, marioSound, MARIO_MARIO_SOUND_PLAYED);
    }
}

/**************************************************
 *                     ACTIONS                    *
 **************************************************/

bool mario_is_crouching(struct MarioState *m) {
    if (!m) { return false; }

    return m->action == ACT_START_CROUCHING || m->action == ACT_CROUCHING || m->action == ACT_STOP_CROUCHING ||
        m->action == ACT_START_CRAWLING || m->action == ACT_CRAWLING || m->action == ACT_STOP_CRAWLING ||
        m->action == ACT_CROUCH_SLIDE;
}

bool mario_is_ground_pound_landing(struct MarioState *m) {
    if (!m) { return false; }

    return m->action == ACT_GROUND_POUND_LAND ||
        (!(m->action & ACT_FLAG_AIR) && (determine_interaction(m, m->marioObj) & INT_GROUND_POUND));
}

bool mario_can_bubble(struct MarioState* m) {
    if (!m) { return false; }
    if (!gServerSettings.bubbleDeath) { return false; }
    if (m->playerIndex != 0) { return false; }
    if (m->action == ACT_BUBBLED) { return false; }
    if (!m->visibleToObjects) { return false; }

    u8 allInBubble = TRUE;
    for (s32 i = 1; i < MAX_PLAYERS; i++) {
        if (!is_player_active(&gMarioStates[i])) { continue; }
        if (!gMarioStates[i].visibleToObjects) { continue; }
        if (gMarioStates[i].action != ACT_BUBBLED && gMarioStates[i].health >= 0x100) {
            allInBubble = FALSE;
            break;
        }
    }
    if (allInBubble) { return false; }
    return true;
}

void mario_set_bubbled(struct MarioState* m) {
    if (!m) { return; }
    if (m->playerIndex != 0) { return; }
    if (m->action == ACT_BUBBLED) { return; }

    gLocalBubbleCounter = 20;

    drop_and_set_mario_action(m, ACT_BUBBLED, 0);
    if (m->numLives > 0) {
        m->numLives--;
    }
    m->healCounter = 0;
    m->hurtCounter = 31;
    gCamera->cutscene = 0;
    m->statusForCamera->action = m->action;
    m->statusForCamera->cameraEvent = 0;
    m->marioObj->activeFlags |= ACTIVE_FLAG_MOVE_THROUGH_GRATE;

    extern s16 gCutsceneTimer;
    gCutsceneTimer = 0;
    soft_reset_camera(m->area->camera);
}

/**
 * Sets Mario's other velocities from his forward speed.
 */
void mario_set_forward_vel(struct MarioState *m, f32 forwardVel) {
    if (!m) { return; }
    m->forwardVel = forwardVel;

    m->slideVelX = sins(m->faceAngle[1]) * m->forwardVel;
    m->slideVelZ = coss(m->faceAngle[1]) * m->forwardVel;

    m->vel[0] = (f32) m->slideVelX;
    m->vel[2] = (f32) m->slideVelZ;
}

/**
 * Returns the slipperiness class of Mario's floor.
 */
s32 mario_get_floor_class(struct MarioState *m) {
    if (!m) { return SURFACE_CLASS_NOT_SLIPPERY; }
    s32 floorClass;

    // The slide terrain type defaults to slide slipperiness.
    // This doesn't matter too much since normally the slide terrain
    // is checked for anyways.
    if ((m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE) {
        floorClass = SURFACE_CLASS_VERY_SLIPPERY;
    } else {
        floorClass = SURFACE_CLASS_DEFAULT;
    }

    if (m->floor != NULL) {
        switch (m->floor->type) {
            case SURFACE_NOT_SLIPPERY:
            case SURFACE_HARD_NOT_SLIPPERY:
            case SURFACE_SWITCH:
                floorClass = SURFACE_CLASS_NOT_SLIPPERY;
                break;

            case SURFACE_SLIPPERY:
            case SURFACE_NOISE_SLIPPERY:
            case SURFACE_HARD_SLIPPERY:
            case SURFACE_NO_CAM_COL_SLIPPERY:
                floorClass = SURFACE_CLASS_SLIPPERY;
                break;

            case SURFACE_VERY_SLIPPERY:
            case SURFACE_ICE:
            case SURFACE_HARD_VERY_SLIPPERY:
            case SURFACE_NOISE_VERY_SLIPPERY_73:
            case SURFACE_NOISE_VERY_SLIPPERY_74:
            case SURFACE_NOISE_VERY_SLIPPERY:
            case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
                floorClass = SURFACE_CLASS_VERY_SLIPPERY;
                break;
        }
    }

    // Crawling allows Mario to not slide on certain steeper surfaces.
    if (m->action == ACT_CRAWLING && m->floor && m->floor->normal.y > 0.5f && floorClass == SURFACE_CLASS_DEFAULT) {
        floorClass = SURFACE_CLASS_NOT_SLIPPERY;
    }

    s32 floorClassOverride = 0;
    if (smlua_call_event_hooks(HOOK_MARIO_OVERRIDE_FLOOR_CLASS, m, floorClass, &floorClassOverride)) {
        return floorClassOverride;
    }

    return floorClass;
}

// clang-format off
s8 sTerrainSounds[7][6] = {
    // default,              hard,                 slippery,
    // very slippery,        noisy default,        noisy slippery
    { SOUND_TERRAIN_DEFAULT, SOUND_TERRAIN_STONE,  SOUND_TERRAIN_GRASS,
      SOUND_TERRAIN_GRASS,   SOUND_TERRAIN_GRASS,  SOUND_TERRAIN_DEFAULT }, // TERRAIN_GRASS
    { SOUND_TERRAIN_STONE,   SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE,
      SOUND_TERRAIN_STONE,   SOUND_TERRAIN_GRASS,  SOUND_TERRAIN_GRASS }, // TERRAIN_STONE
    { SOUND_TERRAIN_SNOW,    SOUND_TERRAIN_ICE,    SOUND_TERRAIN_SNOW,
      SOUND_TERRAIN_ICE,     SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE }, // TERRAIN_SNOW
    { SOUND_TERRAIN_SAND,    SOUND_TERRAIN_STONE,  SOUND_TERRAIN_SAND,
      SOUND_TERRAIN_SAND,    SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE }, // TERRAIN_SAND
    { SOUND_TERRAIN_SPOOKY,  SOUND_TERRAIN_SPOOKY, SOUND_TERRAIN_SPOOKY,
      SOUND_TERRAIN_SPOOKY,  SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE }, // TERRAIN_SPOOKY
    { SOUND_TERRAIN_DEFAULT, SOUND_TERRAIN_STONE,  SOUND_TERRAIN_GRASS,
      SOUND_TERRAIN_ICE,     SOUND_TERRAIN_STONE,  SOUND_TERRAIN_ICE }, // TERRAIN_WATER
    { SOUND_TERRAIN_STONE,   SOUND_TERRAIN_STONE,  SOUND_TERRAIN_STONE,
      SOUND_TERRAIN_STONE,   SOUND_TERRAIN_ICE,    SOUND_TERRAIN_ICE }, // TERRAIN_SLIDE
};
// clang-format on

/**
 * Computes a value that should be added to terrain sounds before playing them.
 * This depends on surfaces and terrain.
 */
u32 mario_get_terrain_sound_addend(struct MarioState *m) {
    if (!m) { return SURFACE_CLASS_NOT_SLIPPERY; }
    s16 floorSoundType;
    s16 terrainType = m->area->terrainType & TERRAIN_MASK;
    s32 ret = SOUND_TERRAIN_DEFAULT << 16;
    s32 floorType;

    if (m->floor != NULL) {
        floorType = m->floor->type;

        if ((gCurrLevelNum != LEVEL_LLL) && (m->floorHeight < (m->waterLevel - 10))) {
            // Water terrain sound, excluding LLL since it uses water in the volcano.
            ret = SOUND_TERRAIN_WATER << 16;
        } else if (SURFACE_IS_QUICKSAND(floorType)) {
            ret = SOUND_TERRAIN_SAND << 16;
        } else {
            switch (floorType) {
                default:
                    floorSoundType = 0;
                    break;

                case SURFACE_NOT_SLIPPERY:
                case SURFACE_HARD:
                case SURFACE_HARD_NOT_SLIPPERY:
                case SURFACE_SWITCH:
                    floorSoundType = 1;
                    break;

                case SURFACE_SLIPPERY:
                case SURFACE_HARD_SLIPPERY:
                case SURFACE_NO_CAM_COL_SLIPPERY:
                    floorSoundType = 2;
                    break;

                case SURFACE_VERY_SLIPPERY:
                case SURFACE_ICE:
                case SURFACE_HARD_VERY_SLIPPERY:
                case SURFACE_NOISE_VERY_SLIPPERY_73:
                case SURFACE_NOISE_VERY_SLIPPERY_74:
                case SURFACE_NOISE_VERY_SLIPPERY:
                case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
                    floorSoundType = 3;
                    break;

                case SURFACE_NOISE_DEFAULT:
                    floorSoundType = 4;
                    break;

                case SURFACE_NOISE_SLIPPERY:
                    floorSoundType = 5;
                    break;
            }

            ret = sTerrainSounds[terrainType][floorSoundType] << 16;
        }
    }

    return ret;
}

/**
 * Collides with walls and returns the most recent wall.
 */
struct Surface *resolve_and_return_wall_collisions(VEC_OUT Vec3f pos, f32 offset, f32 radius) {
    struct WallCollisionData collisionData;
    struct Surface *wall = NULL;

    collisionData.x = pos[0];
    collisionData.y = pos[1];
    collisionData.z = pos[2];
    collisionData.radius = radius;
    collisionData.offsetY = offset;

    if (find_wall_collisions(&collisionData)) {
        wall = collisionData.walls[collisionData.numWalls - 1];
    }

    // I'm not sure if this code is actually ever used or not.
    pos[0] = collisionData.x;
    pos[1] = collisionData.y;
    pos[2] = collisionData.z;

    return wall;
}

/**
 * Collides with walls and returns the wall collision data.
 */
void resolve_and_return_wall_collisions_data(VEC_OUT Vec3f pos, f32 offset, f32 radius, struct WallCollisionData* collisionData) {
    if (!collisionData || !pos) { return; }

    collisionData->x = pos[0];
    collisionData->y = pos[1];
    collisionData->z = pos[2];
    collisionData->radius = radius;
    collisionData->offsetY = offset;

    find_wall_collisions(collisionData);

    pos[0] = collisionData->x;
    pos[1] = collisionData->y;
    pos[2] = collisionData->z;
}

/**
 * Finds the ceiling from a vec3f horizontally and a height (with 80 vertical buffer).
 */
f32 vec3f_find_ceil(Vec3f pos, f32 height, RET struct Surface **ceil) {
    if (!ceil) { return 0; }
    UNUSED f32 unused;

    return find_ceil(pos[0], height + 80.0f, pos[2], ceil);
}

/**
 * Finds the ceiling from a vec3f horizontally and a height (with 80 vertical buffer).
 * Prevents exposed ceiling bug
 */
// Prevent exposed ceilings
f32 vec3f_mario_ceil(Vec3f pos, f32 height, RET struct Surface **ceil) {
    if (!ceil) { return 0; }
    if (gLevelValues.fixCollisionBugs) {
        height = MAX(height + 80.0f, pos[1] - 2);
        return find_ceil(pos[0], height, pos[2], ceil);
    } else {
        return vec3f_find_ceil(pos, height, ceil);
    }
}

/**
 * Determines if Mario is facing "downhill."
 */
s32 mario_facing_downhill(struct MarioState *m, s32 turnYaw) {
    if (!m) { return 0; }
    s16 faceAngleYaw = m->faceAngle[1];

    // This is never used in practice, as turnYaw is
    // always passed as zero.
    if (turnYaw && m->forwardVel < 0.0f) {
        faceAngleYaw += 0x8000;
    }

    faceAngleYaw = m->floorAngle - faceAngleYaw;

    return (-0x4000 < faceAngleYaw) && (faceAngleYaw < 0x4000);
}

/**
 * Determines if a surface is slippery based on the surface class.
 */
u32 mario_floor_is_slippery(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (!m->floor) { return FALSE; }

    f32 normY;

    if ((m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE
        && m->floor->normal.y < 0.9998477f //~cos(1 deg)
    ) {
        return TRUE;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_VERY_SLIPPERY:
            normY = 0.9848077f; //~cos(10 deg)
            break;

        case SURFACE_SLIPPERY:
            normY = 0.9396926f; //~cos(20 deg)
            break;

        default:
            normY = 0.7880108f; //~cos(38 deg)
            break;

        case SURFACE_NOT_SLIPPERY:
            normY = 0.0f;
            break;
    }

    return m->floor->normal.y <= normY;
}

/**
 * Determines if a surface is a slope based on the surface class.
 */
s32 mario_floor_is_slope(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (!m->floor) { return FALSE; }
    f32 normY;

    if ((m->area->terrainType & TERRAIN_MASK) == TERRAIN_SLIDE
        && m->floor->normal.y < 0.9998477f) { // ~cos(1 deg)
        return TRUE;
    }

    switch (mario_get_floor_class(m)) {
        case SURFACE_VERY_SLIPPERY:
            normY = 0.9961947f; // ~cos(5 deg)
            break;

        case SURFACE_SLIPPERY:
            normY = 0.9848077f; // ~cos(10 deg)
            break;

        default:
            normY = 0.9659258f; // ~cos(15 deg)
            break;

        case SURFACE_NOT_SLIPPERY:
            normY = 0.9396926f; // ~cos(20 deg)
            break;
    }

    return m->floor->normal.y <= normY;
}

/**
 * Determines if a surface is steep based on the surface class.
 */
s32 mario_floor_is_steep(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (!m->floor) { return FALSE; }
    f32 normY;
    s32 result = FALSE;

    // Interestingly, this function does not check for the
    // slide terrain type. This means that steep behavior persists for
    // non-slippery and slippery surfaces.
    // This does not matter in vanilla game practice.
    if (!mario_facing_downhill(m, FALSE)) {
        switch (mario_get_floor_class(m)) {
            case SURFACE_VERY_SLIPPERY:
                normY = 0.9659258f; // ~cos(15 deg)
                break;

            case SURFACE_SLIPPERY:
                normY = 0.9396926f; // ~cos(20 deg)
                break;

            default:
                normY = 0.8660254f; // ~cos(30 deg)
                break;

            case SURFACE_NOT_SLIPPERY:
                normY = 0.8660254f; // ~cos(30 deg)
                break;
        }

        result = m->floor->normal.y <= normY;
    }

    return result;
}

/**
 * Finds the floor height relative from Mario given polar displacement.
 */
f32 find_floor_height_relative_polar(struct MarioState *m, s16 angleFromMario, f32 distFromMario) {
    if (!m) { return 0; }
    struct Surface *floor;
    f32 floorY;

    f32 y = sins(m->faceAngle[1] + angleFromMario) * distFromMario;
    f32 x = coss(m->faceAngle[1] + angleFromMario) * distFromMario;

    floorY = find_floor(m->pos[0] + y, m->pos[1] + 100.0f, m->pos[2] + x, &floor);

    return floorY;
}

/**
 * Returns the slope of the floor based off points around Mario.
 */
s16 find_floor_slope(struct MarioState *m, s16 yawOffset) {
    if (!m) { return 0; }
    struct Surface *floor;
    f32 forwardFloorY, backwardFloorY;
    f32 forwardYDelta, backwardYDelta;
    s16 result;

    f32 x = sins(m->faceAngle[1] + yawOffset) * 5.0f;
    f32 z = coss(m->faceAngle[1] + yawOffset) * 5.0f;

    forwardFloorY = find_floor(m->pos[0] + x, m->pos[1] + 100.0f, m->pos[2] + z, &floor);
    backwardFloorY = find_floor(m->pos[0] - x, m->pos[1] + 100.0f, m->pos[2] - z, &floor);

    //! If Mario is near OOB, these floorY's can sometimes be -11000.
    //  This will cause these to be off and give improper slopes.
    forwardYDelta = forwardFloorY - m->pos[1];
    backwardYDelta = m->pos[1] - backwardFloorY;

    if (forwardYDelta * forwardYDelta < backwardYDelta * backwardYDelta) {
        result = atan2s(5.0f, forwardYDelta);
    } else {
        result = atan2s(5.0f, backwardYDelta);
    }

    return result;
}

/**
 * Adjusts Mario's camera and sound based on his action status.
 */
void update_mario_sound_and_camera(struct MarioState *m) {
    if (!m) { return; }

    // only update for local player
    if (m != &gMarioStates[0]) { return; }
    if (!m->area || !m->area->camera) { return; }

    u32 action = m->action;
    s32 camPreset = m->area->camera->mode;

    if (action == ACT_FIRST_PERSON) {
        if (m->playerIndex == 0) {
            raise_background_noise(2);
            gCameraMovementFlags &= ~CAM_MOVE_C_UP_MODE;
            // Go back to the last camera mode
            set_camera_mode(m->area->camera, -1, 1);
        }
    } else if (action == ACT_SLEEPING) {
        if (m->playerIndex == 0) {
            raise_background_noise(2);
        }
    }

    if (m->playerIndex == 0) {
        if (!(action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER))) {
            if (camPreset == CAMERA_MODE_BEHIND_MARIO || camPreset == CAMERA_MODE_WATER_SURFACE) {
                set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
            }
        }
    }
}

/**
 * Transitions Mario to a steep jump action.
 */
void set_steep_jump_action(struct MarioState *m) {
    if (!m) { return; }
    m->marioObj->oMarioSteepJumpYaw = m->faceAngle[1];

    if (m->forwardVel > 0.0f) {
        //! ((s16)0x8000) has undefined behavior. Therefore, this downcast has
        // undefined behavior if m->floorAngle >= 0.
        s16 angleTemp = m->floorAngle + 0x8000;
        s16 faceAngleTemp = m->faceAngle[1] - angleTemp;

        f32 y = sins(faceAngleTemp) * m->forwardVel;
        f32 x = coss(faceAngleTemp) * m->forwardVel * 0.75f;

        m->forwardVel = sqrtf(y * y + x * x);
        m->faceAngle[1] = atan2s(x, y) + angleTemp;
    }

    drop_and_set_mario_action(m, ACT_STEEP_JUMP, 0);
}

/**
 * Sets Mario's vertical speed from his forward speed.
 */
void set_mario_y_vel_based_on_fspeed(struct MarioState *m, f32 initialVelY, f32 multiplier) {
    if (!m) { return; }
    // get_additive_y_vel_for_jumps is always 0 and a stubbed function.
    // It was likely trampoline related based on code location.
    m->vel[1] = initialVelY + get_additive_y_vel_for_jumps() + m->forwardVel * multiplier;

    if (m->squishTimer != 0 || m->quicksandDepth > 1.0f) {
        m->vel[1] *= 0.5f;
    }
}

/**
 * Transitions for a variety of airborne actions.
 */
static u32 set_mario_action_airborne(struct MarioState *m, u32 action, u32 actionArg) {
    if (!m) { return FALSE; }
    f32 fowardVel;

    if ((m->squishTimer != 0 || m->quicksandDepth >= 1.0f)
        && (action == ACT_DOUBLE_JUMP || action == ACT_TWIRLING)) {
        action = ACT_JUMP;
    }

    switch (action) {
        case ACT_DOUBLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 52.0f, 0.25f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_BACKFLIP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            m->forwardVel = -16.0f;
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            break;

        case ACT_TRIPLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 69.0f, 0.0f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_FLYING_TRIPLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 82.0f, 0.0f);
            break;

        case ACT_WATER_JUMP:
        case ACT_HOLD_WATER_JUMP:
            if (actionArg == 0) {
                set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.0f);
            }
            break;

        case ACT_BURNING_JUMP:
            m->vel[1] = 31.5f;
            m->forwardVel = 8.0f;
            break;

        case ACT_RIDING_SHELL_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            break;

        case ACT_JUMP:
        case ACT_HOLD_JUMP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            m->forwardVel *= 0.8f;
            break;

        case ACT_WALL_KICK_AIR:
        case ACT_TOP_OF_POLE_JUMP:
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            if (m->forwardVel < 24.0f) {
                m->forwardVel = 24.0f;
            }
            m->wallKickTimer = 0;
            break;

        case ACT_SIDE_FLIP:
            set_mario_y_vel_based_on_fspeed(m, 62.0f, 0.0f);
            if (vr_first_person_movement_overhaul_active(m)) {
                const s16 travelYaw = m->intendedYaw;
                m->faceAngle[1] = vr_get_first_person_view_yaw();
                m->forwardVel = 8.0f;
                m->slideYaw = travelYaw;
                m->vel[0] = m->slideVelX =
                    m->forwardVel * sins(travelYaw);
                m->vel[2] = m->slideVelZ =
                    m->forwardVel * coss(travelYaw);
            } else {
                m->faceAngle[1] = m->intendedYaw;
                // Changing the side-flip facing angle without rebuilding the
                // horizontal vectors can carry a stale skid vector into the jump.
                mario_set_forward_vel(m, 8.0f);
            }
            break;

        case ACT_STEEP_JUMP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 42.0f, 0.25f);
            m->faceAngle[0] = -0x2000;
            break;

        case ACT_LAVA_BOOST:
            m->vel[1] = 84.0f;
            if (actionArg == 0) {
                m->forwardVel = 0.0f;
            }
            break;

        case ACT_DIVE:
            {
                const f32 sonicScale = vr_special_moves_sonic_speed_scale();
                const f32 maximumDiveSpeed = 48.0f * sonicScale;
                if ((fowardVel = m->forwardVel + 15.0f * sonicScale) > maximumDiveSpeed) {
                    fowardVel = maximumDiveSpeed;
                }
            }
            mario_set_forward_vel(m, fowardVel);
            break;

        case ACT_LONG_JUMP:
            m->marioObj->header.gfx.animInfo.animID = -1;
            set_mario_y_vel_based_on_fspeed(m, 30.0f, 0.0f);
            m->marioObj->oMarioLongJumpIsSlow = m->forwardVel > 16.0f ? FALSE : TRUE;

            //! (BLJ's) This properly handles long jumps from getting forward speed with
            //  too much velocity, but misses backwards longs allowing high negative speeds.
            if ((m->forwardVel *= 1.5f) > 48.0f) {
                m->forwardVel = 48.0f;
            }
            break;

        case ACT_SLIDE_KICK:
            m->vel[1] = 12.0f;
            if (m->forwardVel < 32.0f) {
                m->forwardVel = 32.0f;
            }
            break;

        case ACT_JUMP_KICK:
            m->vel[1] = 20.0f;
            break;

        // Set forward vel to a predefined value for non-player knockbacks
        case ACT_BACKWARD_AIR_KB:
        case ACT_HARD_BACKWARD_AIR_KB:
            if (!(actionArg & PVP_ATTACK_KNOCKBACK_ACTION_ARG)) {
                mario_set_forward_vel(m, -16.0f);
            }
            break;

        case ACT_FORWARD_AIR_KB:
        case ACT_HARD_FORWARD_AIR_KB:
            if (!(actionArg & PVP_ATTACK_KNOCKBACK_ACTION_ARG)) {
                mario_set_forward_vel(m, 16.0f);
            }
            break;

        case ACT_THROWN_BACKWARD:
        case ACT_THROWN_FORWARD:
        case ACT_SOFT_BONK:
            if (!(actionArg & PVP_ATTACK_KNOCKBACK_ACTION_ARG)) {
                mario_set_forward_vel(m, m->forwardVel); // needed to update velocities
            }
            break;
    }

    m->peakHeight = m->pos[1];
    m->flags |= MARIO_UNKNOWN_08;

    return action;
}

/**
 * Transitions for a variety of moving actions.
 */
static u32 set_mario_action_moving(struct MarioState *m, u32 action, UNUSED u32 actionArg) {
    if (!m) { return FALSE; }
    s16 floorClass = mario_get_floor_class(m);
    f32 forwardVel = m->forwardVel;
    f32 mag = min(m->intendedMag, 8.0f);

    switch (action) {
        case ACT_WALKING:
            if (floorClass != SURFACE_CLASS_VERY_SLIPPERY) {
                if (0.0f <= forwardVel && forwardVel < mag) {
                    m->forwardVel = mag;
                }
            }

            m->marioObj->oMarioWalkingPitch = 0;
            break;

        case ACT_HOLD_WALKING:
            if (0.0f <= forwardVel && forwardVel < mag / 2.0f) {
                m->forwardVel = mag / 2.0f;
            }
            break;

        case ACT_BEGIN_SLIDING:
            if (mario_facing_downhill(m, FALSE)) {
                action = ACT_BUTT_SLIDE;
            } else {
                action = ACT_STOMACH_SLIDE;
            }
            break;

        case ACT_HOLD_BEGIN_SLIDING:
            if (mario_facing_downhill(m, FALSE)) {
                action = ACT_HOLD_BUTT_SLIDE;
            } else {
                action = ACT_HOLD_STOMACH_SLIDE;
            }
            break;
    }

    return action;
}

/**
 * Transition for certain submerged actions, which is actually just the metal jump actions.
 */
static u32 set_mario_action_submerged(struct MarioState *m, u32 action, UNUSED u32 actionArg) {
    if (!m) { return FALSE; }
    if (action == ACT_METAL_WATER_JUMP || action == ACT_HOLD_METAL_WATER_JUMP) {
        m->vel[1] = 32.0f;
    }

    return action;
}

/**
 * Transitions for a variety of cutscene actions.
 */
static u32 set_mario_action_cutscene(struct MarioState *m, u32 action, UNUSED u32 actionArg) {
    if (!m) { return FALSE; }
    switch (action) {
        case ACT_EMERGE_FROM_PIPE:
            m->vel[1] = 52.0f;
            break;

        case ACT_FALL_AFTER_STAR_GRAB:
            mario_set_forward_vel(m, 0.0f);
            break;

        case ACT_SPAWN_SPIN_AIRBORNE:
            mario_set_forward_vel(m, 2.0f);
            break;

        case ACT_SPECIAL_EXIT_AIRBORNE:
        case ACT_SPECIAL_DEATH_EXIT:
            m->vel[1] = 64.0f;
            break;
    }

    return action;
}

/**
 * Puts Mario into a given action, putting Mario through the appropriate
 * specific function if needed.
 */
u32 set_mario_action(struct MarioState *m, u32 action, u32 actionArg) {
    if (!m) { return FALSE; }
    u32 actionOverride = 0;
    smlua_call_event_hooks(HOOK_BEFORE_SET_MARIO_ACTION, m, action, actionArg, &actionOverride);
    if (actionOverride == 1) { return TRUE; }
    if (actionOverride != 0) { action = actionOverride; }

    switch (action & ACT_GROUP_MASK) {
        case ACT_GROUP_MOVING:
            action = set_mario_action_moving(m, action, actionArg);
            break;

        case ACT_GROUP_AIRBORNE:
            action = set_mario_action_airborne(m, action, actionArg);
            break;

        case ACT_GROUP_SUBMERGED:
            action = set_mario_action_submerged(m, action, actionArg);
            break;

        case ACT_GROUP_CUTSCENE:
            action = set_mario_action_cutscene(m, action, actionArg);
            break;
    }

    vr_turn_toward_headset_on_jump_landing(m, action);

    // Resets the sound played flags, meaning Mario can play those sound types again.
    m->flags &= ~(MARIO_ACTION_SOUND_PLAYED | MARIO_MARIO_SOUND_PLAYED);

    if (!(m->action & ACT_FLAG_AIR)) {
        m->flags &= ~MARIO_UNKNOWN_18;
    }

    // Initialize the action information.
    m->prevAction = m->action;
    m->action = action;
    m->actionArg = actionArg;
    m->actionState = 0;
    m->actionTimer = 0;

    smlua_call_event_hooks(HOOK_ON_SET_MARIO_ACTION, m);

    return TRUE;
}

/**
 * Puts Mario into a specific jumping action from a landing action.
 */
s32 set_jump_from_landing(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (m->quicksandDepth >= 11.0f) {
        if (m->heldObj == NULL) {
            return set_mario_action(m, ACT_QUICKSAND_JUMP_LAND, 0);
        } else {
            return set_mario_action(m, ACT_HOLD_QUICKSAND_JUMP_LAND, 0);
        }
    }

    if (mario_floor_is_steep(m)) {
        set_steep_jump_action(m);
    } else {
        if ((m->doubleJumpTimer == 0) || (m->squishTimer != 0)) {
            set_mario_action(m, ACT_JUMP, 0);
        } else {
            switch (m->prevAction) {
                case ACT_JUMP_LAND:
                    set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                    break;

                case ACT_FREEFALL_LAND:
                    set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                    break;

                case ACT_SIDE_FLIP_LAND_STOP:
                    set_mario_action(m, ACT_DOUBLE_JUMP, 0);
                    break;

                case ACT_DOUBLE_JUMP_LAND:
                    // If Mario has a wing cap, he ignores the typical speed
                    // requirement for a triple jump.
                    if (m->flags & MARIO_WING_CAP) {
                        set_mario_action(m, ACT_FLYING_TRIPLE_JUMP, 0);
                    } else if (m->forwardVel > 20.0f) {
                        set_mario_action(m, ACT_TRIPLE_JUMP, 0);
                    } else {
                        set_mario_action(m, ACT_JUMP, 0);
                    }
                    break;

                default:
                    set_mario_action(m, ACT_JUMP, 0);
                    break;
            }
        }
    }

    m->doubleJumpTimer = 0;

    return TRUE;
}

/**
 * Puts Mario in a given action, as long as it is not overruled by
 * either a quicksand or steep jump.
 */
s32 set_jumping_action(struct MarioState *m, u32 action, u32 actionArg) {
    if (!m) { return FALSE; }
    UNUSED u32 currAction = m->action;

    if (m->quicksandDepth >= 11.0f) {
        // Checks whether Mario is holding an object or not.
        if (m->heldObj == NULL) {
            return set_mario_action(m, ACT_QUICKSAND_JUMP_LAND, 0);
        } else {
            return set_mario_action(m, ACT_HOLD_QUICKSAND_JUMP_LAND, 0);
        }
    }

    if (mario_floor_is_steep(m)) {
        set_steep_jump_action(m);
    } else {
        set_mario_action(m, action, actionArg);
    }

    return TRUE;
}

/**
 * Drop anything Mario is holding and set a new action.
 */
s32 drop_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg) {
    if (!m) { return FALSE; }
    mario_stop_riding_and_holding(m);

    return set_mario_action(m, action, actionArg);
}

/**
 * Increment Mario's hurt counter and set a new action.
 */
s32 hurt_and_set_mario_action(struct MarioState *m, u32 action, u32 actionArg, s16 hurtCounter) {
    if (!m) { return FALSE; }
    m->hurtCounter = hurtCounter;

    return set_mario_action(m, action, actionArg);
}

/**
 * Checks a variety of inputs for common transitions between many different
 * actions. A common variant of the below function.
 */
s32 check_common_action_exits(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }

    return FALSE;
}

/**
 * Checks a variety of inputs for common transitions between many different
 * object holding actions. A holding variant of the above function.
 */
s32 check_common_hold_action_exits(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (m->input & INPUT_A_PRESSED) {
        return set_mario_action(m, ACT_HOLD_JUMP, 0);
    }
    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_HOLD_FREEFALL, 0);
    }
    if (m->input & INPUT_NONZERO_ANALOG) {
        return set_mario_action(m, ACT_HOLD_WALKING, 0);
    }
    if (m->input & INPUT_ABOVE_SLIDE) {
        return set_mario_action(m, ACT_HOLD_BEGIN_SLIDING, 0);
    }

    return FALSE;
}

/**
 * Transitions Mario from a submerged action to a walking action.
 */
s32 transition_submerged_to_walking(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (m->playerIndex == 0 && m->area && m->area->camera) {
        set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
    }

    vec3s_set(m->angleVel, 0, 0, 0);

    if (m->heldObj == NULL) {
        return set_mario_action(m, ACT_WALKING, 0);
    } else {
        return set_mario_action(m, ACT_HOLD_WALKING, 0);
    }
}

/**
 * This is the transition function typically for entering a submerged action for a
 * non-submerged action. This also applies the water surface camera preset.
 */
s32 set_water_plunge_action(struct MarioState *m) {
    if (!m) { return FALSE; }
    if (m->action == ACT_BUBBLED) { return FALSE; }
    if (m->action == ACT_IN_CANNON) { return FALSE; }

    m->forwardVel = m->forwardVel / 4.0f;
    m->vel[1] = m->vel[1] / 2.0f;

    m->pos[1] = m->waterLevel - 100;

    m->faceAngle[2] = 0;

    vec3s_set(m->angleVel, 0, 0, 0);

    if (!(m->action & ACT_FLAG_DIVING)) {
        m->faceAngle[0] = 0;
    }

    if (m->playerIndex == 0 && m->area->camera->mode != CAMERA_MODE_WATER_SURFACE) {
        set_camera_mode(m->area->camera, CAMERA_MODE_WATER_SURFACE, 1);
    }

    return set_mario_action(m, ACT_WATER_PLUNGE, 0);
}

/**
 * These are the scaling values for the x and z axis for Mario
 * when he is close to unsquishing.
 */
u8 sSquishScaleOverTime[16] = { 0x46, 0x32, 0x32, 0x3C, 0x46, 0x50, 0x50, 0x3C,
                                0x28, 0x14, 0x14, 0x1E, 0x32, 0x3C, 0x3C, 0x28 };

/**
 * Applies the squish to Mario's model via scaling.
 */
void squish_mario_model(struct MarioState *m) {
    if (!m) { return; }
    if (m->squishTimer == 0xFF && m->bounceSquishTimer == 0) { return; }

    // If no longer squished, scale back to default.
    // Also handles the Tiny Mario and Huge Mario cheats.
    u8 squishTimer = (m->squishTimer > m->bounceSquishTimer) ? m->squishTimer : m->bounceSquishTimer;
    if (squishTimer == 0) {
        vec3f_set(m->marioObj->header.gfx.scale, 1.0f, 1.0f, 1.0f);
        return;
    }

    // If timer is less than 16, rubber-band Mario's size scale up and down.
    if (squishTimer <= 16) {
        squishTimer--;

        m->marioObj->header.gfx.scale[1] = 1.0f - ((sSquishScaleOverTime[15 - squishTimer] * 0.6f) / 100.0f);
        m->marioObj->header.gfx.scale[0] = ((sSquishScaleOverTime[15 - squishTimer] * 0.4f) / 100.0f) + 1.0f;
        m->marioObj->header.gfx.scale[2] = m->marioObj->header.gfx.scale[0];
    } else {
        vec3f_set(m->marioObj->header.gfx.scale, 1.4f, 0.4f, 1.4f);
    }

    if (m->squishTimer > 0) { m->squishTimer--; }
    if (m->bounceSquishTimer > 0) { m->bounceSquishTimer--; }
}

/**
 * Debug function that prints floor normal, velocity, and action information.
 */
void debug_print_speed_action_normal(struct MarioState *m) {
    if (!m) { return; }
    f32 steepness;
    f32 floor_nY;

    if (gShowDebugText) {
        steepness = sqrtf(
            ((m->floor->normal.x * m->floor->normal.x) + (m->floor->normal.z * m->floor->normal.z)));
        floor_nY = m->floor->normal.y;

        print_text_fmt_int(210, 88, "ANG %d", (atan2s(floor_nY, steepness) * 180.0f) / 32768.0f);

        print_text_fmt_int(210, 72, "SPD %d", m->forwardVel);

        // STA short for "status," the official action name via SMS map.
        print_text_fmt_int(210, 56, "STA %x", (m->action & ACT_ID_MASK));
    }
}

/**
 * Update the button inputs for Mario.
 */
void update_mario_button_inputs(struct MarioState *m) {
    if (!m) { return; }

    // don't update remote inputs
    if (m->playerIndex != 0) { return; }

    if (m->controller->buttonPressed & A_BUTTON) {
        m->input |= INPUT_A_PRESSED;
    }

    if (m->controller->buttonDown & A_BUTTON) {
        m->input |= INPUT_A_DOWN;
    }

    // Don't update for these buttons if squished.
    if (m->squishTimer == 0) {
        if (m->controller->buttonPressed & B_BUTTON) {
            m->input |= INPUT_B_PRESSED;
        }

        if (m->controller->buttonDown & Z_TRIG) {
            m->input |= INPUT_Z_DOWN;
        }

        if (m->controller->buttonPressed & Z_TRIG) {
            m->input |= INPUT_Z_PRESSED;
        }
    }

    if (m->input & INPUT_A_PRESSED) {
        m->framesSinceA = 0;
    } else if (m->framesSinceA < 0xFF) {
        m->framesSinceA += 1;
    }

    if (m->input & INPUT_B_PRESSED) {
        m->framesSinceB = 0;
    } else if (m->framesSinceB < 0xFF) {
        m->framesSinceB += 1;
    }
}

/**
 * Updates the joystick intended magnitude.
 */
void update_mario_joystick_inputs(struct MarioState *m) {
    if (!m) { return; }
    struct Controller *controller = m->controller;
    f32 mag = ((controller->stickMag / 64.0f) * (controller->stickMag / 64.0f)) * 64.0f;

    if (m->squishTimer == 0) {
        m->intendedMag = mag / 2.0f;
    } else {
        m->intendedMag = mag / 8.0f;
    }

    // don't update remote inputs past this point
    if ((sCurrPlayMode == PLAY_MODE_PAUSED) || m->playerIndex != 0) { return; }

    vr_update_first_person_backpedal_state(m);

    if (m->intendedMag > 0.0f) {
        const s16 stickYaw = atan2s(
            -controller->stickY,
            controller->stickX
        );

        if (gLakituState.mode != CAMERA_MODE_NEWCAM) {
            m->intendedYaw = stickYaw + m->area->camera->yaw;
        } else if (get_first_person_enabled()) {
            m->intendedYaw = stickYaw + gLakituState.yaw;
        } else {
            m->intendedYaw = stickYaw - gNewCamera.yaw + 0x4000;
        }

        if (vr_is_active() &&
            configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON) {
            if (!configVrOriginalMarioMovement) {
                const s16 viewYaw =
                    vr_get_first_person_view_yaw();

                if (vr_first_person_backpedal_requested(m)) {
                    const f32 lateralInput = clamp(
                        controller->stickX /
                            controller->stickMag,
                        -1.0f,
                        1.0f
                    );
                    const f32 lateralMagnitude =
                        fabsf(lateralInput);
                    const f32 curvedLateralInput =
                        lateralInput *
                        lateralMagnitude *
                        lateralMagnitude;
                    const s16 backpedalSteer = (s16)roundf(
                        curvedLateralInput *
                            VR_FIRST_PERSON_BACKPEDAL_SIDE_LIMIT
                    );

                    // Meet the full 90-degree side direction continuously at
                    // the front/rear boundary, then use a cubic curve to
                    // settle quickly toward mostly straight backpedaling.
                    // A normal down-left/down-right diagonal reaches roughly
                    // 32 degrees off backward. This slightly wider reverse
                    // steering range removes the weak lateral/dead feeling
                    // without turning diagonal backpedaling into a full strafe.
                    m->intendedYaw =
                        viewYaw + 0x8000 + backpedalSteer;
                } else {
                    // SM64's gameplay yaw and the physical stick's lateral
                    // axis run in opposite directions in this view basis.
                    // Negating the relative angle keeps stick-left moving
                    // left and stick-right moving right.
                    const s16 relativeStickYaw = (s16)-atan2s(
                        controller->stickY,
                        controller->stickX
                    );
                    const s16 limitedStickYaw = (s16)clamp(
                        (s32)relativeStickYaw,
                        -VR_FIRST_PERSON_FORWARD_STEER_LIMIT,
                        VR_FIRST_PERSON_FORWARD_STEER_LIMIT
                    );

                    // Preserve normal Mario locomotion throughout the full
                    // symmetrical 90-degree forward steering half-circle.
                    m->intendedYaw = viewYaw + limitedStickYaw;
                }
            } else {
                const s16 compressedStickYaw =
                    vr_first_person_compress_stick_yaw(stickYaw);

                // Preserve the original First Person Mario movement path
                // when the player explicitly selects it.
                m->intendedYaw += compressedStickYaw - stickYaw;
                m->intendedYaw += vr_first_person_facing_yaw_offset();
                m->intendedYaw +=
                    vr_get_first_person_action_turn_yaw();

                const s32 calibration =
                    (s32)MIN(configVrMovementCalibration, 100U) - 50;
                m->intendedYaw +=
                    (s16)(calibration * 0x10000 / 720);
            }
        }
        m->input |= INPUT_NONZERO_ANALOG;
    } else {
        const f32 horizontalSpeedSquared =
            m->vel[0] * m->vel[0] +
            m->vel[2] * m->vel[2];
        const bool stableVrStand =
            m->playerIndex == 0 &&
            vr_is_active() &&
            configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
            (m->action & ACT_GROUP_MASK) == ACT_GROUP_STATIONARY &&
            (m->action & (ACT_FLAG_INTANGIBLE |
                          ACT_FLAG_SWIMMING |
                          ACT_FLAG_ON_POLE |
                          ACT_FLAG_HANGING)) == 0 &&
            fabsf(m->forwardVel) <= 0.5f &&
            horizontalSpeedSquared <= 0.25f;
        if (stableVrStand) {
            const s16 viewYaw = vr_get_first_person_view_yaw();
            m->faceAngle[1] = viewYaw;
            m->intendedYaw = viewYaw;
        } else {
            m->intendedYaw = m->faceAngle[1];
        }
    }
}

/**
 * Resolves wall collisions, and updates a variety of inputs.
 */
void update_mario_geometry_inputs(struct MarioState *m) {
    if (!m) { return; }

    f32 gasLevel;
    f32 ceilToFloorDist;

    bool allowUpdateGeometryInputs = true;
    smlua_call_event_hooks(HOOK_MARIO_OVERRIDE_GEOMETRY_INPUTS, m, &allowUpdateGeometryInputs);
    if (!allowUpdateGeometryInputs) { return; }

    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 60.0f, 50.0f);
    f32_find_wall_collision(&m->pos[0], &m->pos[1], &m->pos[2], 30.0f, 24.0f);

    m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);

    // If Mario is OOB, move his position to his graphical position (which was not updated)
    // and check for the floor there.
    // This can cause errant behavior when combined with astral projection,
    // since the graphical position was not Mario's previous location.
    if (m->floor == NULL) {
        vec3f_copy(m->pos, m->marioObj->header.gfx.pos);
        m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);
    }

    m->ceilHeight = vec3f_mario_ceil(&m->pos[0], m->floorHeight, &m->ceil);
    gasLevel = find_poison_gas_level(m->pos[0], m->pos[2]);
    m->waterLevel = find_water_level(m->pos[0], m->pos[2]);

    struct Surface *sonicWaterFloor = mario_get_sonic_water_run_floor(
        m,
        m->waterLevel,
        m->floorHeight
    );
    if (sonicWaterFloor != NULL && (m->action & ACT_FLAG_MOVING)) {
        m->floor = sonicWaterFloor;
        m->floorHeight = m->waterLevel;
        m->pos[1] = MAX(m->pos[1], (f32)m->waterLevel);
        m->vel[1] = 0.0f;
        m->faceAngle[0] = 0;
        m->faceAngle[2] = 0;
        m->angleVel[0] = 0;
        m->angleVel[2] = 0;
        if (m->marioObj != NULL) {
            m->marioObj->header.gfx.angle[0] = 0;
            m->marioObj->header.gfx.angle[2] = 0;
            m->marioObj->oMoveAnglePitch = 0;
            m->marioObj->oMoveAngleRoll = 0;
        }
    }

    if (m->action == ACT_DEBUG_FREE_MOVE) { return; }

    if (m->floor != NULL) {
        m->floorAngle = atan2s(m->floor->normal.z, m->floor->normal.x);
        m->terrainSoundAddend = mario_get_terrain_sound_addend(m);

        if ((m->pos[1] > m->waterLevel - 40) && mario_floor_is_slippery(m)) {
            m->input |= INPUT_ABOVE_SLIDE;
        }

        if ((m->floor->flags & SURFACE_FLAG_DYNAMIC)
            || (m->ceil && m->ceil->flags & SURFACE_FLAG_DYNAMIC)) {
            ceilToFloorDist = m->ceilHeight - m->floorHeight;

            if ((0.0f <= ceilToFloorDist) && (ceilToFloorDist <= 150.0f)) {
                m->input |= INPUT_SQUISHED;
            }
        }

        if (m->pos[1] > m->floorHeight + 100.0f) {
            m->input |= INPUT_OFF_FLOOR;
        }

        if (m->pos[1] < (m->waterLevel - 10)) {
            m->input |= INPUT_IN_WATER;
        }

        if (m->pos[1] < (gasLevel - 100.0f)) {
            m->input |= INPUT_IN_POISON_GAS;
        }

    } else if (!is_other_player_active()) {
        level_trigger_warp(m, WARP_OP_DEATH);
    } else {
        vec3s_to_vec3f(m->pos, m->spawnInfo->startPos);
        m->faceAngle[1] = m->spawnInfo->startAngle[1];
        struct Surface* floor = NULL;
        find_floor(m->pos[0], m->pos[1], m->pos[2], &floor);
        if (floor == NULL) {
            level_trigger_warp(m, WARP_OP_DEATH);
        } else {
            update_mario_geometry_inputs(m);
            return;
        }
    }
}

/**
 * Handles Mario's input flags as well as a couple timers.
 */
void update_mario_inputs(struct MarioState *m) {
    if (!m) { return; }
    if (m->playerIndex == 0) { m->input = 0; }

    u8 localIsPaused = (m->playerIndex == 0) && (sCurrPlayMode == PLAY_MODE_PAUSED || m->freeze > 0);

    m->collidedObjInteractTypes = m->marioObj->collidedObjInteractTypes;
    m->flags &= 0xFFFFFF;

    update_mario_button_inputs(m);
    update_mario_joystick_inputs(m);

    // prevent any inputs when paused
    if ((m->playerIndex == 0) && (sCurrPlayMode == PLAY_MODE_PAUSED || m->freeze > 0)) {
        m->input = 0;
        m->intendedMag = 0;
    }

    update_mario_geometry_inputs(m);

    debug_print_speed_action_normal(m);

    /* Developer stuff */
#ifdef DEVELOPMENT
    if (gNetworkSystem == &gNetworkSystemSocket) {
        if (m->playerIndex == 0) {
            if (m->action != ACT_DEBUG_FREE_MOVE && m->controller->buttonPressed & L_TRIG && m->controller->buttonDown & Z_TRIG) {
                set_mario_action(m, ACT_DEBUG_FREE_MOVE, 0);
                m->marioObj->oTimer = 0;
            }
        }
    }
#endif
    /* End of developer stuff */

    if (m->playerIndex == 0) {
        if (!localIsPaused && (gCameraMovementFlags & CAM_MOVE_C_UP_MODE)) {
            if (m->action & ACT_FLAG_ALLOW_FIRST_PERSON) {
                m->input |= INPUT_FIRST_PERSON;
            } else {
                gCameraMovementFlags &= ~CAM_MOVE_C_UP_MODE;
            }
        }

        if (!(m->input & (INPUT_NONZERO_ANALOG | INPUT_A_PRESSED))) {
            m->input |= INPUT_ZERO_MOVEMENT;
        }

        if (m->marioObj->oInteractStatus
            & (INT_STATUS_HOOT_GRABBED_BY_MARIO | INT_STATUS_MARIO_UNK1 | INT_STATUS_HIT_BY_SHOCKWAVE)) {
            m->input |= INPUT_UNKNOWN_10;
        }
        if (m->heldObj != NULL) {
            m->heldObj->heldByPlayerIndex = 0;
        }
    }

    // This function is located near other unused trampoline functions,
    // perhaps logically grouped here with the timers.
    stub_mario_step_1(m);

    if (m->wallKickTimer > 0) {
        m->wallKickTimer--;
    }

    if (m->doubleJumpTimer > 0) {
        m->doubleJumpTimer--;
    }
}

/**
 * Set's the camera preset for submerged action behaviors.
 */
void set_submerged_cam_preset_and_spawn_bubbles(struct MarioState *m) {
    if (!m) { return; }
    f32 heightBelowWater;
    s16 camPreset;

    if ((m->action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED) {
        heightBelowWater = (f32)(m->waterLevel - 80) - m->pos[1];
        camPreset = m->area->camera->mode;

        if (m->action & ACT_FLAG_METAL_WATER) {
            if (m->playerIndex == 0 && camPreset != CAMERA_MODE_CLOSE) {
                set_camera_mode(m->area->camera, CAMERA_MODE_CLOSE, 1);
            }
        } else {
            if (m->playerIndex == 0 && (heightBelowWater > 800.0f) && (camPreset != CAMERA_MODE_BEHIND_MARIO)) {
                set_camera_mode(m->area->camera, CAMERA_MODE_BEHIND_MARIO, 1);
            }

            if (m->playerIndex == 0 && (heightBelowWater < 400.0f) && (camPreset != CAMERA_MODE_WATER_SURFACE)) {
                set_camera_mode(m->area->camera, CAMERA_MODE_WATER_SURFACE, 1);
            }

            // As long as Mario isn't drowning or at the top
            // of the water with his head out, spawn bubbles.
            if (!(m->action & ACT_FLAG_INTANGIBLE)) {
                if ((m->pos[1] < (f32)(m->waterLevel - 160)) || (m->faceAngle[0] < -0x800)) {
                    set_mario_particle_flags(m, PARTICLE_BUBBLE, FALSE);
                }
            }
        }
    }
}

/**
 * Both increments and decrements Mario's HP.
 */
void update_mario_health(struct MarioState *m) {
    if (!m) { return; }
    s32 terrainIsSnow;

    if (m->health >= 0x100) {
        // When already healing or hurting Mario, Mario's HP is not changed any more here.
        if (((u32) m->healCounter | (u32) m->hurtCounter) == 0) {
            if ((m->input & INPUT_IN_POISON_GAS) && !(m->action & ACT_FLAG_INTANGIBLE)) {
                if (!(m->flags & MARIO_METAL_CAP) && !gDebugLevelSelect) {
                    m->health -= 4;
                }
            } else {
                if ((m->action & ACT_FLAG_SWIMMING) && !(m->action & ACT_FLAG_INTANGIBLE)) {
                    terrainIsSnow = (m->area->terrainType & TERRAIN_MASK) == TERRAIN_SNOW;

                    // When Mario is near the water surface, recover health (unless in snow),
                    // when in snow terrains lose 3 health.
                    // If using the debug level select, do not lose any HP to water.
                    if ((m->pos[1] >= (m->waterLevel - 140)) && !terrainIsSnow) {
                        m->health += 0x1A;
                    } else if (!gDebugLevelSelect) {
                        m->health -= (terrainIsSnow ? 3 : 1);
                    }
                }
            }
        }

        if (m->healCounter > 0) {
            m->health += 0x40;
            m->healCounter--;
        }
        if (m->hurtCounter > 0) {
            m->health -= 0x40;
            m->hurtCounter--;
        }

        if (m->health > 0x880) {
            m->health = 0x880;
        }
        if (m->health < 0x100) {
            if (m != &gMarioStates[0]) {
                // never kill remote marios
                m->health = 0x100;
            } else {
                m->health = 0xFF;
            }
        }

        if (m->playerIndex == 0) {
            // Play a noise to alert the player when Mario is close to drowning.
            if (((m->action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED) && (m->health < 0x300)) {
                play_sound(SOUND_MOVING_ALMOST_DROWNING, gGlobalSoundSource);
                if (!gRumblePakTimer) {
                    gRumblePakTimer = 36;
                    if (is_rumble_finished_and_queue_empty()) {
                        queue_rumble_data_mario(m, 3, 30);
                    }
                }
            }
            else {
                gRumblePakTimer = 0;
            }
        }
    }
}

/**
 * Updates some basic info for camera usage.
 */
void update_mario_info_for_cam(struct MarioState *m) {
    if (!m) { return; }
    m->marioBodyState->action = m->action;
    m->statusForCamera->action = m->action;

    vec3s_copy(m->statusForCamera->faceAngle, m->faceAngle);

    if (!(m->flags & MARIO_UNKNOWN_25)) {
        vec3f_copy(m->statusForCamera->pos, m->pos);
    }
}

/**
 * Resets Mario's model, done every time an action is executed.
 */
void mario_reset_bodystate(struct MarioState *m) {
    if (!m) { return; }
    struct MarioBodyState *bodyState = m->marioBodyState;

    bodyState->capState = MARIO_HAS_DEFAULT_CAP_OFF;
    bodyState->eyeState = MARIO_EYES_BLINK;
    bodyState->handState = MARIO_HAND_FISTS;
    bodyState->modelState = 0;
    bodyState->wingFlutter = FALSE;

    m->flags &= ~MARIO_METAL_SHOCK;
}

/**
 * Adjusts Mario's graphical height for quicksand.
 */
void sink_mario_in_quicksand(struct MarioState *m) {
    if (!m) { return; }
    struct Object *o = m->marioObj;

    if (o->header.gfx.throwMatrix) {
        (*o->header.gfx.throwMatrix)[3][1] -= m->quicksandDepth;
    }

    o->header.gfx.pos[1] -= m->quicksandDepth;
}

/**
 * Is a binary representation of the frames to flicker Mario's cap when the timer
 * is running out.
 *
 * Equals [1000]^5 . [100]^8 . [10]^9 . [1] in binary, which is
 * 100010001000100010001001001001001001001001001010101010101010101.
 */
u64 sCapFlickerFrames = 0x4444449249255555;

/**
 * Updates the cap flags mainly based on the cap timer.
 */
u32 update_and_return_cap_flags(struct MarioState *m) {
    if (!m) { return 0; }
    u32 flags = m->flags;
    u32 action;

    if (m->capTimer > 0) {
        action = m->action;

        if ((m->capTimer <= 60)
            || ((action != ACT_READING_AUTOMATIC_DIALOG) && (action != ACT_READING_NPC_DIALOG)
                && (action != ACT_READING_SIGN) && (action != ACT_IN_CANNON))) {
            m->capTimer -= 1;
        }

        if (m->capTimer == 0) {
            stop_cap_music();

            m->flags &= ~MARIO_SPECIAL_CAPS;
            if (!(m->flags & MARIO_CAPS)) {
                m->flags &= ~MARIO_CAP_ON_HEAD;
            }
        }

        if (m->capTimer == 60) {
            fadeout_cap_music();
        }

        // This code flickers the cap through a long binary string, increasing in how
        // common it flickers near the end.
        if ((m->capTimer < 64) && ((1ULL << m->capTimer) & sCapFlickerFrames)) {
            flags &= ~MARIO_SPECIAL_CAPS;
            if (!(flags & MARIO_CAPS)) {
                flags &= ~MARIO_CAP_ON_HEAD;
            }
        }
    }

    return flags;
}

/**
 * Updates the Mario's cap, rendering, and hitbox.
 */
void mario_update_hitbox_and_cap_model(struct MarioState *m) {
    if (!m) { return; }
    struct MarioBodyState *bodyState = m->marioBodyState;
    s32 flags = update_and_return_cap_flags(m);

    if (flags & MARIO_VANISH_CAP) {
        bodyState->modelState = MODEL_STATE_NOISE_ALPHA;
    }

    if (flags & MARIO_METAL_CAP) {
        bodyState->modelState |= MODEL_STATE_METAL;
    }

    if (flags & MARIO_METAL_SHOCK) {
        bodyState->modelState |= MODEL_STATE_METAL;
    }

    //! (Pause buffered hitstun) Since the global timer increments while paused,
    //  this can be paused through to give continual invisibility. This leads to
    //  no interaction with objects.
    if ((m->invincTimer >= 3) && (gGlobalTimer & 1)) {
        m->marioObj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
    }

    if (flags & MARIO_CAP_IN_HAND) {
        if (flags & MARIO_WING_CAP) {
            bodyState->handState = MARIO_HAND_HOLDING_WING_CAP;
        } else {
            bodyState->handState = MARIO_HAND_HOLDING_CAP;
        }
    }

    if (flags & MARIO_CAP_ON_HEAD) {
        if (flags & MARIO_WING_CAP) {
            bodyState->capState = MARIO_HAS_WING_CAP_ON;
        } else {
            bodyState->capState = MARIO_HAS_DEFAULT_CAP_ON;
        }
    }

    // Short hitbox for crouching/crawling/etc.
    if (m->action & ACT_FLAG_SHORT_HITBOX) {
        m->marioObj->hitboxHeight = 100.0f;
    } else {
        m->marioObj->hitboxHeight = 160.0f;
    }

    struct NetworkPlayer* np = &gNetworkPlayers[gMarioState->playerIndex];
    u8 teleportFade = (m->flags & MARIO_TELEPORTING) || (gMarioState->playerIndex != 0 && np->fadeOpacity < 32);
    if (teleportFade && (m->fadeWarpOpacity != 0xFF)) {
        bodyState->modelState &= ~0xFF;
        bodyState->modelState |= (0x100 | m->fadeWarpOpacity);
    }
}

/**
 * An unused and possibly a debug function. Z + another button input
 * sets Mario with a different cap.
 */
UNUSED static void debug_update_mario_cap(u16 button, s32 flags, u16 capTimer, u16 capMusic) {
    // This checks for Z_TRIG instead of Z_DOWN flag
    // (which is also what other debug functions do),
    // so likely debug behavior rather than unused behavior.
    if ((gPlayer1Controller->buttonDown & Z_TRIG) && (gPlayer1Controller->buttonPressed & button)
        && !(gMarioState->flags & flags)) {
        gMarioState->flags |= (flags + MARIO_CAP_ON_HEAD);

        if (capTimer > gMarioState->capTimer) {
            gMarioState->capTimer = capTimer;
        }

        play_cap_music(capMusic);
    }
}

void queue_particle_rumble(void) {
    if (gMarioState->particleFlags & PARTICLE_HORIZONTAL_STAR) {
        queue_rumble_data_mario(gMarioState, 5, 80);
    } else if (gMarioState->particleFlags & PARTICLE_VERTICAL_STAR) {
        queue_rumble_data_mario(gMarioState, 5, 80);
    } else if (gMarioState->particleFlags & PARTICLE_TRIANGLE) {
        queue_rumble_data_mario(gMarioState, 5, 80);
    }
    if(gMarioState->heldObj && gMarioState->heldObj->behavior == segmented_to_virtual(smlua_override_behavior(bhvBobomb))) {
        reset_rumble_timers(gMarioState);
    }
}

static u8 prevent_hang(u32 hangPreventionActions[], u8* hangPreventionIndex) {
    if (!hangPreventionActions) { return TRUE; }
    // save the action sequence
    hangPreventionActions[*hangPreventionIndex] = gMarioState->action;
    *hangPreventionIndex = *hangPreventionIndex + 1;
    if (*hangPreventionIndex < MAX_HANG_PREVENTION) { return FALSE; }

    // complain to console
    LOG_ERROR("Action loop hang prevented");

    return TRUE;
}

/**
 * Main function for executing Mario's behavior.
 */
s32 execute_mario_action(UNUSED struct Object *o) {
    s32 inLoop = TRUE;
    if (!gMarioState) { return 0; }
    if (!gMarioState->marioObj) { return 0; }
    if (gMarioState->playerIndex >= MAX_PLAYERS) { return 0; }

    if (gMarioState->knockbackTimer > 0) {
        gMarioState->knockbackTimer--;
    } else if (gMarioState->knockbackTimer < 0) {
        gMarioState->knockbackTimer++;
    }

    // hide inactive players
    struct NetworkPlayer *np = &gNetworkPlayers[gMarioState->playerIndex];
    if (gMarioState->playerIndex != 0) {
        bool levelAreaMismatch = ((gNetworkPlayerLocal == NULL)
            || np->currCourseNum != gNetworkPlayerLocal->currCourseNum
            || np->currActNum    != gNetworkPlayerLocal->currActNum
            || np->currLevelNum  != gNetworkPlayerLocal->currLevelNum
            || np->currAreaIndex != gNetworkPlayerLocal->currAreaIndex);

        bool fadedOut = gNetworkAreaLoaded && (levelAreaMismatch && gMarioState->wasNetworkVisible && np->fadeOpacity == 0);
        bool wasNeverVisible = gNetworkAreaLoaded && !gMarioState->wasNetworkVisible;

        if (!gNetworkAreaLoaded || fadedOut || wasNeverVisible) {
            gMarioState->marioObj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
            gMarioState->marioObj->oIntangibleTimer = -1;
            mario_stop_riding_and_holding(gMarioState);

            // drop their held object
            if (gMarioState->heldObj != NULL) {
                LOG_INFO("dropping held object");
                u8 tmpPlayerIndex = gMarioState->playerIndex;
                gMarioState->playerIndex = 0;
                mario_drop_held_object(gMarioState);
                gMarioState->playerIndex = tmpPlayerIndex;
            }

            // no longer held by an object
            if (gMarioState->heldByObj != NULL) {
                LOG_INFO("dropping heldby object");
                gMarioState->heldByObj = NULL;
            }

            // no longer riding object
            if (gMarioState->riddenObj != NULL) {
                LOG_INFO("dropping ridden object");
                u8 tmpPlayerIndex = gMarioState->playerIndex;
                gMarioState->playerIndex = 0;
                mario_stop_riding_object(gMarioState);
                gMarioState->playerIndex = tmpPlayerIndex;
            }

            return 0;
        }

        if (levelAreaMismatch && gMarioState->wasNetworkVisible) {
            if (np->fadeOpacity <= 2) {
                np->fadeOpacity = 0;
            } else {
                np->fadeOpacity -= 2;
            }
            gMarioState->fadeWarpOpacity = np->fadeOpacity << 3;
        } else if (np->fadeOpacity < 32) {
            np->fadeOpacity += 2;
            gMarioState->fadeWarpOpacity = np->fadeOpacity << 3;
        }
    }

    if (gMarioState->action) {
        if (gMarioState->action != ACT_BUBBLED) {
            gMarioState->marioObj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
        }
        mario_reset_bodystate(gMarioState);
        update_mario_inputs(gMarioState);
        mario_handle_special_floors(gMarioState);
        mario_process_interactions(gMarioState);

        // HACK: mute snoring even when we skip the waking up action
        if (gMarioState->isSnoring && gMarioState->action != ACT_SLEEPING) {
                stop_sound(get_character(gMarioState)->soundSnoring1, gMarioState->marioObj->header.gfx.cameraToObject);
                stop_sound(get_character(gMarioState)->soundSnoring2, gMarioState->marioObj->header.gfx.cameraToObject);
#ifndef VERSION_JP
                stop_sound(get_character(gMarioState)->soundSnoring3, gMarioState->marioObj->header.gfx.cameraToObject);
#endif
                gMarioState->isSnoring = FALSE;
        }

        // If Mario is OOB, stop executing actions.
        if (gMarioState->floor == NULL && gMarioState->action != ACT_DEBUG_FREE_MOVE) {
            return 0;
        }

        // don't update mario when in a cutscene
        if (gMarioState->playerIndex == 0) {
            extern s32 gDialogID;
            if (gMarioState->freeze > 0) { gMarioState->freeze--; }
            if (gMarioState->freeze < 2 && gDialogID != DIALOG_NONE) { gMarioState->freeze = 2; }
            if (gMarioState->freeze < 2 && sCurrPlayMode == PLAY_MODE_PAUSED) { gMarioState->freeze = 2; }
        }

        // drop held object if someone else is holding it
        if (gMarioState->playerIndex == 0 && gMarioState->heldObj != NULL) {
            u8 inCutscene = ((gMarioState->action & ACT_GROUP_MASK) != ACT_GROUP_CUTSCENE);
            if (!inCutscene && gMarioState->heldObj->heldByPlayerIndex != 0) {
                drop_and_set_mario_action(gMarioState, ACT_IDLE, 0);
            }
        }

        u32 hangPreventionActions[MAX_HANG_PREVENTION];
        u8 hangPreventionIndex = 0;

        // The function can loop through many action shifts in one frame,
        // which can lead to unexpected sub-frame behavior. Could potentially hang
        // if a loop of actions were found, but there has not been a situation found.
        while (inLoop) {
            // don't update mario when in a cutscene
            /*if (gMarioState->freeze > 0 && (gMarioState->action & ACT_GROUP_MASK) != ACT_GROUP_CUTSCENE) {
                break;
            }*/

            // this block can get stuck in an infinite loop due to unexpected circumstances arising from networked players
            if (prevent_hang(hangPreventionActions, &hangPreventionIndex)) {
                break;
            }

            switch (gMarioState->action & ACT_GROUP_MASK) {
                case ACT_GROUP_STATIONARY:
                    inLoop = mario_execute_stationary_action(gMarioState);
                    break;

                case ACT_GROUP_MOVING:
                    inLoop = mario_execute_moving_action(gMarioState);
                    break;

                case ACT_GROUP_AIRBORNE:
                    inLoop = mario_execute_airborne_action(gMarioState);
                    break;

                case ACT_GROUP_SUBMERGED:
                    inLoop = mario_execute_submerged_action(gMarioState);
                    break;

                case ACT_GROUP_CUTSCENE:
                    inLoop = mario_execute_cutscene_action(gMarioState);
                    break;

                case ACT_GROUP_AUTOMATIC:
                    inLoop = mario_execute_automatic_action(gMarioState);
                    break;

                case ACT_GROUP_OBJECT:
                    inLoop = mario_execute_object_action(gMarioState);
                    break;
            }
        }

        sink_mario_in_quicksand(gMarioState);
        squish_mario_model(gMarioState);
        set_submerged_cam_preset_and_spawn_bubbles(gMarioState);
        update_mario_health(gMarioState);
        update_mario_info_for_cam(gMarioState);
        mario_update_hitbox_and_cap_model(gMarioState);
        vr_hand_interaction_update_headset_collider(gMarioState);
        vr_update_twirl_tornado_effect(gMarioState);

        // Both of the wind handling portions play wind audio only in
        // non-Japanese releases.
        extern bool gDjuiInMainMenu;
        if (gMarioState->floor && gMarioState->floor->type == SURFACE_HORIZONTAL_WIND && !gDjuiInMainMenu) {
            spawn_wind_particles(0, (gMarioState->floor->force << 8));
#ifndef VERSION_JP
            play_sound(SOUND_ENV_WIND2, gMarioState->marioObj->header.gfx.cameraToObject);
#endif
        }

        if (gMarioState->floor && gMarioState->floor->type == SURFACE_VERTICAL_WIND) {
            spawn_wind_particles(1, 0);
#ifndef VERSION_JP
            play_sound(SOUND_ENV_WIND2, gMarioState->marioObj->header.gfx.cameraToObject);
#endif
        }

        play_infinite_stairs_music();
        gMarioState->marioObj->oInteractStatus = 0;
        queue_particle_rumble();

        // Make remote players disappear when they enter a painting
        // should use same logic as in get_painting_warp_node
        if (gMarioState->playerIndex != 0 && gCurrentArea->paintingWarpNodes != NULL) {
            s32 paintingIndex = gMarioState->floor->type - SURFACE_PAINTING_WARP_D3;
            if (paintingIndex >= PAINTING_WARP_INDEX_START && paintingIndex < PAINTING_WARP_INDEX_END) {
                if (paintingIndex < PAINTING_WARP_INDEX_FA || gMarioState->pos[1] - gMarioState->floorHeight < 80.0f) {
                    struct WarpNode *warpNode = &gCurrentArea->paintingWarpNodes[paintingIndex];
                    if (warpNode->id != 0) {
                        set_mario_action(gMarioState, ACT_DISAPPEARED, 0);
                        gMarioState->marioObj->header.gfx.node.flags &= ~GRAPH_RENDER_ACTIVE;
                    }
                }
            }
        }

        return gMarioState->particleFlags;
    }

    return 0;
}

s32 force_idle_state(struct MarioState* m) {
    if (!m) { return 0; }
    u8 underWater = (m->pos[1] < ((f32)m->waterLevel));
    return set_mario_action(m, underWater ? ACT_WATER_IDLE : ACT_IDLE, 0);
}

/**************************************************
 *                  INITIALIZATION                *
 **************************************************/

void init_single_mario(struct MarioState* m) {
    if (!m) { return; }

    u16 playerIndex = m->playerIndex;
    struct SpawnInfo* spawnInfo = &gPlayerSpawnInfos[playerIndex];
    unused80339F10 = 0;

    m->freeze = 0;

    m->actionTimer = 0;
    m->framesSinceA = 0xFF;
    m->framesSinceB = 0xFF;

    m->invincTimer = 0;
    m->visibleToObjects = true;

    if (m->cap & (SAVE_FLAG_CAP_ON_GROUND | SAVE_FLAG_CAP_ON_KLEPTO | SAVE_FLAG_CAP_ON_UKIKI | SAVE_FLAG_CAP_ON_MR_BLIZZARD)) {
        m->flags = 0;
    } else {
        m->flags = (MARIO_CAP_ON_HEAD | MARIO_NORMAL_CAP);
    }

    m->forwardVel = 0.0f;
    m->squishTimer = 0;

    m->hurtCounter = 0;
    m->healCounter = 0;

    m->capTimer = 0;
    m->quicksandDepth = 0.0f;

    m->heldObj = NULL;
    m->heldByObj = NULL;
    m->interactObj = NULL;
    m->riddenObj = NULL;
    m->usedObj = NULL;
    m->bubbleObj = NULL;

    m->waterLevel = find_water_level(spawnInfo->startPos[0], spawnInfo->startPos[2]);

    m->area = gCurrentArea;
    m->marioObj = gMarioObjects[m->playerIndex];
    if (m->marioObj == NULL) { return; }
    m->marioObj->header.gfx.shadowInvisible = false;
    m->marioObj->header.gfx.disableAutomaticShadowPos = false;
    m->marioObj->header.gfx.animInfo.animID = -1;
    vec3s_copy(m->faceAngle, spawnInfo->startAngle);
    vec3s_set(m->angleVel, 0, 0, 0);
    vec3s_to_vec3f(m->pos, spawnInfo->startPos);
    vec3f_set(m->vel, 0, 0, 0);

    if (m->marioObj != NULL) {
        vec3f_set(m->marioObj->header.gfx.scale, 1.0f, 1.0f, 1.0f);
        m->marioObj->header.gfx.node.flags |= GRAPH_RENDER_ACTIVE;
    }

    m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);

    if (m->pos[1] < m->floorHeight) {
        m->pos[1] = m->floorHeight;
    }

    m->marioObj->header.gfx.pos[1] = m->pos[1];

    m->action = (m->pos[1] <= (m->waterLevel - 100)) ? ACT_WATER_IDLE : ACT_IDLE;

    update_mario_info_for_cam(m);
    m->marioBodyState->punchState = 0;

    m->marioBodyState->shadeR = 127;
    m->marioBodyState->shadeG = 127;
    m->marioBodyState->shadeB = 127;

    m->marioBodyState->lightR = 255;
    m->marioBodyState->lightG = 255;
    m->marioBodyState->lightB = 255;

    m->marioBodyState->lightingDirX = 0;
    m->marioBodyState->lightingDirY = 0;
    m->marioBodyState->lightingDirZ = 0;

    m->marioBodyState->allowPartRotation = FALSE;

    m->marioObj->oPosX = m->pos[0];
    m->marioObj->oPosY = m->pos[1];
    m->marioObj->oPosZ = m->pos[2];

    m->marioObj->oMoveAnglePitch = m->faceAngle[0];
    m->marioObj->oMoveAngleYaw = m->faceAngle[1];
    m->marioObj->oMoveAngleRoll = m->faceAngle[2];

    m->marioObj->oIntangibleTimer = 0;

    vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
    vec3s_set(m->marioObj->header.gfx.angle, 0, m->faceAngle[1], 0);

    // cap will never be lying on the ground in coop
    /* Vec3s capPos;
    if (save_file_get_cap_pos(capPos)) {
        struct Object *capObject = spawn_object(m->marioObj, MODEL_MARIOS_CAP, bhvNormalCap);

        capObject->oPosX = capPos[0];
        capObject->oPosY = capPos[1];
        capObject->oPosZ = capPos[2];

        capObject->oForwardVelS32 = 0;

        capObject->oMoveAngleYaw = 0;
    }*/

    // force all other players to be invisible by default
    if (playerIndex != 0) {
        m->marioObj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
        m->wasNetworkVisible = false;
        gNetworkPlayers[playerIndex].fadeOpacity = 0;
    }

    // set character model
    u8 modelIndex = gNetworkPlayers[playerIndex].overrideModelIndex;
    if (modelIndex >= CT_MAX) { modelIndex = 0; }
    m->character = &gCharacters[modelIndex];
    obj_set_model(m->marioObj, m->character->modelId);
}

void init_mario(void) {
    for (s32 i = 0; i < MAX_PLAYERS; i++) {
        gMarioStates[i].playerIndex = i;
        init_single_mario(&gMarioStates[i]);
    }
}

void init_mario_single_from_save_file(struct MarioState* m, u16 index) {
    if (!m) { return; }
    m->playerIndex = index;
    m->flags = 0;
    m->action = 0;
    m->spawnInfo = &gPlayerSpawnInfos[index];
    m->statusForCamera = &gPlayerCameraState[index];
    m->marioBodyState = &gBodyStates[index];
    m->controller = &gControllers[index];
    m->animation = &D_80339D10[index];

    m->numCoins = 0;
    m->numStars = save_file_get_total_star_count(gCurrSaveFileNum - 1, COURSE_MIN - 1, COURSE_MAX - 1);
    m->numKeys = 0;

    m->numLives = 4;
    m->health = 0x880;

    m->prevNumStarsForDialog = m->numStars;
    m->unkB0 = 0xBD;
}

void init_mario_from_save_file(void) {
    for (s32 i = 0; i < MAX_PLAYERS; i++) {
        init_mario_single_from_save_file(&gMarioStates[i], i);
    }
    gHudDisplay.coins = 0;
    gHudDisplay.wedges = 8;
}

void set_mario_particle_flags(struct MarioState* m, u32 flags, u8 clear) {
    if (!m) { return; }
    if (m->playerIndex != 0) {
        return;
    }

    if (clear) {
        m->particleFlags &= ~flags;
    } else {
        m->particleFlags |= flags;
    }
}

void mario_update_wall(struct MarioState* m, struct WallCollisionData* wcd) {
    if (!m || !wcd) { return; }

    if (gLevelValues.fixCollisionBugs && gLevelValues.fixCollisionBugsPickBestWall) {
        // turn face angle into a direction vector
        Vec3f faceAngle;
        faceAngle[0] = coss(m->faceAngle[0]) * sins(m->faceAngle[1]);
        faceAngle[1] = sins(m->faceAngle[0]);
        faceAngle[2] = coss(m->faceAngle[0]) * coss(m->faceAngle[1]);
        vec3f_normalize(faceAngle);

        // reset wall
        m->wall = NULL;
        for (int i = 0; i < wcd->numWalls; i++) {
            if (m->wall == NULL) {
                m->wall = wcd->walls[i];
                continue;
            }

            // find the wall that is most "facing away"
            Vec3f w1 = { m->wall->normal.x, m->wall->normal.y, m->wall->normal.z };
            Vec3f w2 = {wcd->walls[i]->normal.x,wcd->walls[i]->normal.y, wcd->walls[i]->normal.z };
            if (vec3f_dot(w1, faceAngle) > vec3f_dot(w2, faceAngle)) {
                m->wall = wcd->walls[i];
            }
        }
    } else {
        m->wall = (wcd->numWalls > 0)
            ? wcd->walls[wcd->numWalls - 1]
            : NULL;
    }

    if (gLevelValues.fixCollisionBugs && wcd->normalCount > 0) {
        vec3f_set(m->wallNormal,
                  wcd->normalAddition[0] / wcd->normalCount,
                  wcd->normalAddition[1] / wcd->normalCount,
                  wcd->normalAddition[2] / wcd->normalCount);
    } else if (m->wall) {
        vec3f_set(m->wallNormal,
                  m->wall->normal.x,
                  m->wall->normal.y,
                  m->wall->normal.z);
    }
}

struct MarioState *get_mario_state_from_object(struct Object *o) {
    if (!o) { return NULL; }
    if (o->behavior != bhvMario) { return NULL; }
    s32 stateIndex = o->oBehParams - 1;
    if (stateIndex >= 0 && stateIndex < MAX_PLAYERS) {
        return &gMarioStates[stateIndex];
    }
    return NULL;
}
