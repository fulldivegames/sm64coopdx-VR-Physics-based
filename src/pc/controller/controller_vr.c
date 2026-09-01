#include <math.h>
#include <stdio.h>

#include "controller_vr.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"
// The DJUI menu reads the same N64 pad passed to this backend while an
// interactable panel is active. Keep this menu-only flag separate from the
// gameplay Jump binding so rebinding Jump cannot remove the A action.
extern bool gInteractableOverridePad;

#define VR_STICK_DEADZONE 0.18f
#define VR_TRIGGER_THRESHOLD 0.55f
#define VR_CAMERA_BUTTON_THRESHOLD 0.55f
#define VR_PHYSICAL_CROUCH_HEIGHT_RATIO (2.0f / 3.0f)
#define VR_PHYSICAL_CROUCH_RELEASE_HYSTERESIS 0.08f
#define VR_PUNCH_MIN_RESET_SPEED 0.20f
#define VR_PUNCH_RESET_SPEED_SCALE 0.35f
#define VR_PUNCH_TRAVEL_SPEED_SCALE 0.60f

static bool sVrPunchArmed[VR_CONTROLLER_COUNT] = {
    true,
    true
};
static bool sVrPunchLastPositionValid[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static float
    sVrPunchLastPosition[VR_CONTROLLER_COUNT][3] = { 0 };
static float sVrPunchTravel[VR_CONTROLLER_COUNT] = {
    0.0f,
    0.0f
};
static bool sVrPhysicalCrouchActive = false;
static uint32_t sVrPhysicalCrouchTrackingGeneration = 0;

// VR action bindings are exposed to the DJUI binding screen through the
// controller API raw-key channel. Keep this edge-triggered: a held button
// must not immediately rebind another action when the user opens a row.
static bool sVrPreviousBindingDown[VR_CONTROLLER_BINDING_COUNT] = { false };
static u32 sVrPendingRawKey = VK_INVALID;

static void controller_vr_reset_rawkey_state(void) {
    for (unsigned int binding = 0;
         binding < VR_CONTROLLER_BINDING_COUNT;
         binding++) {
        sVrPreviousBindingDown[binding] = false;
    }
    sVrPendingRawKey = VK_INVALID;
}
static bool sVrPhysicalCrouchReferenceValid = false;
static float sVrPhysicalCrouchReferenceY = 0.0f;

static float controller_vr_clampf(
    float value,
    float minimum,
    float maximum
) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void controller_vr_convert_stick(
    const float input[2],
    s8* outputX,
    s8* outputY
) {
    const float magnitudeSquared =
        input[0] * input[0] +
        input[1] * input[1];

    *outputX = 0;
    *outputY = 0;
    if (magnitudeSquared <=
        VR_STICK_DEADZONE * VR_STICK_DEADZONE) {
        return;
    }
    const float magnitude = sqrtf(magnitudeSquared);

    const float normalizedMagnitude = controller_vr_clampf(
        (magnitude - VR_STICK_DEADZONE) /
            (1.0f - VR_STICK_DEADZONE),
        0.0f,
        1.0f
    );
    const float scale = normalizedMagnitude * 127.0f / magnitude;
    const float scaledX = controller_vr_clampf(
        input[0] * scale,
        -127.0f,
        127.0f
    );
    const float scaledY = controller_vr_clampf(
        input[1] * scale,
        -127.0f,
        127.0f
    );

    *outputX = (s8)roundf(scaledX);
    *outputY = (s8)roundf(scaledY);
}

static void controller_vr_merge_stick(
    s8* destinationX,
    s8* destinationY,
    const float source[2]
) {
    s8 sourceX = 0;
    s8 sourceY = 0;
    controller_vr_convert_stick(source, &sourceX, &sourceY);

    const s32 destinationMagnitudeSquared =
        (s32)(*destinationX) * (s32)(*destinationX) +
        (s32)(*destinationY) * (s32)(*destinationY);
    const s32 sourceMagnitudeSquared =
        (s32)sourceX * (s32)sourceX +
        (s32)sourceY * (s32)sourceY;

    // Keep whichever physical device has the stronger input. This lets a
    // connected gamepad remain usable without neutral Touch sticks erasing it.
    if (sourceMagnitudeSquared > destinationMagnitudeSquared) {
        *destinationX = sourceX;
        *destinationY = sourceY;
    }
}

static bool controller_vr_binding_down(
    unsigned int binding,
    bool leftAvailable,
    const struct VrControllerState* left,
    bool rightAvailable,
    const struct VrControllerState* right
) {
    const struct VrControllerState* state = NULL;

    switch (binding) {
        case VR_CONTROLLER_BINDING_LEFT_PRIMARY:
        case VR_CONTROLLER_BINDING_LEFT_SECONDARY:
        case VR_CONTROLLER_BINDING_LEFT_TRIGGER:
        case VR_CONTROLLER_BINDING_LEFT_GRIP:
        case VR_CONTROLLER_BINDING_LEFT_STICK_CLICK:
        case VR_CONTROLLER_BINDING_LEFT_MENU:
            if (leftAvailable) {
                state = left;
            }
            break;
        case VR_CONTROLLER_BINDING_RIGHT_PRIMARY:
        case VR_CONTROLLER_BINDING_RIGHT_SECONDARY:
        case VR_CONTROLLER_BINDING_RIGHT_TRIGGER:
        case VR_CONTROLLER_BINDING_RIGHT_GRIP:
        case VR_CONTROLLER_BINDING_RIGHT_STICK_CLICK:
        case VR_CONTROLLER_BINDING_RIGHT_MENU:
            if (rightAvailable) {
                state = right;
            }
            break;
        default:
            return false;
    }

    if (state == NULL) {
        return false;
    }

    switch (binding) {
        case VR_CONTROLLER_BINDING_LEFT_PRIMARY:
        case VR_CONTROLLER_BINDING_RIGHT_PRIMARY:
            return state->primaryButton;
        case VR_CONTROLLER_BINDING_LEFT_SECONDARY:
        case VR_CONTROLLER_BINDING_RIGHT_SECONDARY:
            return state->secondaryButton;
        case VR_CONTROLLER_BINDING_LEFT_TRIGGER:
        case VR_CONTROLLER_BINDING_RIGHT_TRIGGER:
            return state->trigger >= VR_TRIGGER_THRESHOLD;
        case VR_CONTROLLER_BINDING_LEFT_GRIP:
        case VR_CONTROLLER_BINDING_RIGHT_GRIP:
            return state->squeeze >= VR_TRIGGER_THRESHOLD;
        case VR_CONTROLLER_BINDING_LEFT_STICK_CLICK:
        case VR_CONTROLLER_BINDING_RIGHT_STICK_CLICK:
            return state->thumbstickButton;
        case VR_CONTROLLER_BINDING_LEFT_MENU:
        case VR_CONTROLLER_BINDING_RIGHT_MENU:
            return state->menuButton;
        default:
            return false;
    }
}

static const float* controller_vr_select_stick(
    unsigned int stick,
    bool leftAvailable,
    const struct VrControllerState* left,
    bool rightAvailable,
    const struct VrControllerState* right
) {
    if (stick == VR_CONTROLLER_STICK_LEFT && leftAvailable) {
        return left->thumbstick;
    }
    if (stick == VR_CONTROLLER_STICK_RIGHT && rightAvailable) {
        return right->thumbstick;
    }
    return NULL;
}

static bool controller_vr_update_physical_crouch(void) {
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !configVrPhysicalCrouching) {
        sVrPhysicalCrouchActive = false;
        sVrPhysicalCrouchReferenceValid = false;
        return false;
    }

    const uint32_t trackingGeneration =
        vr_get_tracking_origin_generation();
    if (trackingGeneration !=
        sVrPhysicalCrouchTrackingGeneration) {
        sVrPhysicalCrouchTrackingGeneration = trackingGeneration;
        sVrPhysicalCrouchActive = false;
        sVrPhysicalCrouchReferenceValid = false;
        return false;
    }

    float translation[3] = { 0 };
    float calibratedHeight = 0.0f;
    if (!vr_get_head_translation(translation) ||
        !vr_get_calibrated_head_height(&calibratedHeight)) {
        sVrPhysicalCrouchActive = false;
        sVrPhysicalCrouchReferenceValid = false;
        return false;
    }

    const float crouchDescent = calibratedHeight *
        (1.0f - VR_PHYSICAL_CROUCH_HEIGHT_RATIO);
    if (!sVrPhysicalCrouchReferenceValid) {
        sVrPhysicalCrouchReferenceY = translation[1];
        sVrPhysicalCrouchReferenceValid = true;
        sVrPhysicalCrouchActive = false;
        return false;
    }
    const float releaseDescent = fmaxf(
        0.0f,
        crouchDescent -
            VR_PHYSICAL_CROUCH_RELEASE_HYSTERESIS
    );
    const float downwardTravel =
        -(translation[1] - sVrPhysicalCrouchReferenceY);

    if (sVrPhysicalCrouchActive) {
        if (downwardTravel <= releaseDescent) {
            sVrPhysicalCrouchActive = false;
        }
    } else if (downwardTravel >= crouchDescent) {
        sVrPhysicalCrouchActive = true;
    }

    return sVrPhysicalCrouchActive;
}

static bool controller_vr_update_physical_punch(
    uint32_t hand,
    const struct VrControllerState* state
) {
    if (hand >= VR_CONTROLLER_COUNT ||
        state == NULL ||
        !state->gripPoseValid ||
        !state->gripLinearVelocityValid) {
        if (hand < VR_CONTROLLER_COUNT) {
            sVrPunchArmed[hand] = false;
            sVrPunchLastPositionValid[hand] = false;
            sVrPunchTravel[hand] = 0.0f;
        }
        return false;
    }

    const float gripAmount = state->squeeze;
    const float gripThreshold = controller_vr_clampf(
        (float)configVrPunchGripThreshold,
        10.0f,
        100.0f
    ) / 100.0f;
    if (gripAmount < gripThreshold) {
        sVrPunchArmed[hand] = true;
        sVrPunchLastPositionValid[hand] = false;
        sVrPunchTravel[hand] = 0.0f;
        return false;
    }

    // Most frames have an open hand. Avoid the velocity square root and all
    // gesture-threshold work until the grip has armed a physical punch.
    const float speedSquared =
        state->gripLinearVelocity[0] *
            state->gripLinearVelocity[0] +
        state->gripLinearVelocity[1] *
            state->gripLinearVelocity[1] +
        state->gripLinearVelocity[2] *
            state->gripLinearVelocity[2];

    const float requiredSpeed = controller_vr_clampf(
        (float)configVrPunchSpeed,
        75.0f,
        300.0f
    ) / 100.0f;
    const float requiredDistance = controller_vr_clampf(
        (float)configVrPunchDistance,
        5.0f,
        50.0f
    ) / 100.0f;
    const float resetSpeed = fmaxf(
        VR_PUNCH_MIN_RESET_SPEED,
        requiredSpeed * VR_PUNCH_RESET_SPEED_SCALE
    );
    const float travelSpeed =
        requiredSpeed * VR_PUNCH_TRAVEL_SPEED_SCALE;

    if (!sVrPunchLastPositionValid[hand]) {
        for (uint32_t axis = 0; axis < 3; axis++) {
            sVrPunchLastPosition[hand][axis] =
                state->gripPosition[axis];
        }
        sVrPunchLastPositionValid[hand] = true;
        sVrPunchTravel[hand] = 0.0f;
        return false;
    }

    const float deltaX = state->gripPosition[0] -
        sVrPunchLastPosition[hand][0];
    const float deltaY = state->gripPosition[1] -
        sVrPunchLastPosition[hand][1];
    const float deltaZ = state->gripPosition[2] -
        sVrPunchLastPosition[hand][2];
    for (uint32_t axis = 0; axis < 3; axis++) {
        sVrPunchLastPosition[hand][axis] =
            state->gripPosition[axis];
    }

    if (speedSquared <= resetSpeed * resetSpeed) {
        sVrPunchArmed[hand] = true;
        sVrPunchTravel[hand] = 0.0f;
        return false;
    }

    if (speedSquared >= travelSpeed * travelSpeed) {
        const float frameTravel = sqrtf(
            deltaX * deltaX +
            deltaY * deltaY +
            deltaZ * deltaZ
        );
        sVrPunchTravel[hand] += frameTravel;
    } else {
        sVrPunchTravel[hand] = 0.0f;
    }

    if (!sVrPunchArmed[hand] ||
        speedSquared < requiredSpeed * requiredSpeed ||
        sVrPunchTravel[hand] < requiredDistance) {
        return false;
    }

    sVrPunchArmed[hand] = false;
#ifdef DEBUG
    const float speed = sqrtf(speedSquared);
    printf(
        "[VR] %s physical punch detected "
        "(%.2f m/s, %.2f m travel).\n",
        hand == VR_CONTROLLER_LEFT ? "Left" : "Right",
        speed,
        sVrPunchTravel[hand]
    );
#endif
    sVrPunchTravel[hand] = 0.0f;
    vr_apply_haptic(hand, 0.25f, 0.04f, -1.0f);
    return true;
}

static void controller_vr_reset_physical_punch(uint32_t hand) {
    if (hand < VR_CONTROLLER_COUNT) {
        sVrPunchArmed[hand] = true;
        sVrPunchLastPositionValid[hand] = false;
        sVrPunchTravel[hand] = 0.0f;
    }
}

static void controller_vr_reset_physical_punches(void) {
    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        controller_vr_reset_physical_punch(hand);
    }
}

static void controller_vr_update_rawkey_state(
    bool leftAvailable,
    const struct VrControllerState* left,
    bool rightAvailable,
    const struct VrControllerState* right
) {
    for (unsigned int binding = VR_CONTROLLER_BINDING_LEFT_PRIMARY;
         binding < VR_CONTROLLER_BINDING_COUNT;
         binding++) {
        const bool down = controller_vr_binding_down(
            binding,
            leftAvailable,
            left,
            rightAvailable,
            right
        );
        if (down && !sVrPreviousBindingDown[binding] &&
            sVrPendingRawKey == VK_INVALID) {
            // controller_get_raw_key() adds VK_BASE_VR after this callback;
            // return the logical enum value so the UI maps it to the same
            // choices used by gameplay.
            sVrPendingRawKey = binding;
        }
        sVrPreviousBindingDown[binding] = down;
    }
}

static void controller_vr_init(void) {
}

static void controller_vr_read(OSContPad* pad) {
    if (!vr_is_active() || pad == NULL) {
        controller_vr_reset_physical_punches();
        sVrPhysicalCrouchActive = false;
        sVrPhysicalCrouchReferenceValid = false;
        controller_vr_reset_rawkey_state();
        return;
    }

    if (controller_vr_update_physical_crouch()) {
        pad->button |= Z_TRIG;
    }

    if (!configVrMotionControllerInput) {
        controller_vr_reset_physical_punches();
        controller_vr_reset_rawkey_state();
        return;
    }

    struct VrControllerState left = { 0 };
    struct VrControllerState right = { 0 };
    const bool leftAvailable = vr_get_controller_state(
        VR_CONTROLLER_LEFT,
        &left
    );
    const bool rightAvailable = vr_get_controller_state(
        VR_CONTROLLER_RIGHT,
        &right
    );

    controller_vr_update_rawkey_state(
        leftAvailable,
        &left,
        rightAvailable,
        &right
    );

    if (!leftAvailable && !rightAvailable) {
        controller_vr_reset_physical_punches();
        return;
    }

    if (!leftAvailable) {
        controller_vr_reset_physical_punch(VR_CONTROLLER_LEFT);
    }
    if (!rightAvailable) {
        controller_vr_reset_physical_punch(VR_CONTROLLER_RIGHT);
    }

    if (configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        configVrPhysicalPunching) {
        if (leftAvailable) {
            if (controller_vr_update_physical_punch(
                    VR_CONTROLLER_LEFT,
                    &left
                )) {
                vr_queue_physical_punch(VR_CONTROLLER_LEFT);
            }
        }
        if (rightAvailable) {
            if (controller_vr_update_physical_punch(
                    VR_CONTROLLER_RIGHT,
                    &right
                )) {
                vr_queue_physical_punch(VR_CONTROLLER_RIGHT);
            }
        }
    } else {
        controller_vr_reset_physical_punches();
    }

    const float* movementStick = controller_vr_select_stick(
        configVrMoveStick,
        leftAvailable,
        &left,
        rightAvailable,
        &right
    );
    if (movementStick != NULL) {
        controller_vr_merge_stick(
            &pad->stick_x,
            &pad->stick_y,
            movementStick
        );
    }

    if (controller_vr_binding_down(
            configVrJumpBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= A_BUTTON;
    }

    if (gInteractableOverridePad &&
        rightAvailable &&
        right.primaryButton) {
        pad->button |= A_BUTTON;
    }
    if (controller_vr_binding_down(
            configVrAttackBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= B_BUTTON;
    }
    if (controller_vr_binding_down(
            configVrCrouchBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= Z_TRIG;
    }
    if (controller_vr_binding_down(
            configVrLBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= L_TRIG;
    }
    if (controller_vr_binding_down(
            configVrRBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= R_TRIG;
    }
    if (controller_vr_binding_down(
            configVrPauseBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= START_BUTTON;
    }
    if (controller_vr_binding_down(
            configVrSpecialBinding,
            leftAvailable,
            &left,
            rightAvailable,
            &right
        )) {
        pad->button |= Y_BUTTON;
    }

    const float* cameraStick = controller_vr_select_stick(
        configVrCameraStick,
        leftAvailable,
        &left,
        rightAvailable,
        &right
    );
    if (cameraStick != NULL) {
#ifdef __ANDROID__
        // OpenXR Touch X and the port's camera yaw use opposite signs on
        // Quest. Invert only the selected camera stick, leaving locomotion
        // and user stick remapping unchanged.
        const float questCameraStick[2] = {
            -cameraStick[0],
            cameraStick[1]
        };
        cameraStick = questCameraStick;
#endif
        controller_vr_merge_stick(
            &pad->ext_stick_x,
            &pad->ext_stick_y,
            cameraStick
        );

        if (cameraStick[0] <=
            -VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= L_CBUTTONS;
        } else if (cameraStick[0] >=
                   VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= R_CBUTTONS;
        }
        if (cameraStick[1] >=
            VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= U_CBUTTONS;
        } else if (cameraStick[1] <=
                   -VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= D_CBUTTONS;
        }
    }

}

static u32 controller_vr_rawkey(void) {
    const u32 key = sVrPendingRawKey;
    sVrPendingRawKey = VK_INVALID;
    return key;
}

static void controller_vr_rumble_play(
    float strength,
    float durationSeconds
) {
    if (!vr_is_active() || !configVrMotionControllerInput) {
        return;
    }

    vr_apply_haptic(
        VR_CONTROLLER_LEFT,
        strength,
        durationSeconds,
        -1.0f
    );
    vr_apply_haptic(
        VR_CONTROLLER_RIGHT,
        strength,
        durationSeconds,
        -1.0f
    );
}

static void controller_vr_rumble_stop(void) {
    vr_apply_haptic(VR_CONTROLLER_LEFT, 0.0f, 0.0f, -1.0f);
    vr_apply_haptic(VR_CONTROLLER_RIGHT, 0.0f, 0.0f, -1.0f);
}

static void controller_vr_shutdown(void) {
}

struct ControllerAPI controller_vr = {
    VK_BASE_VR,
    controller_vr_init,
    controller_vr_read,
    controller_vr_rawkey,
    controller_vr_rumble_play,
    controller_vr_rumble_stop,
    NULL,
    controller_vr_shutdown
};
