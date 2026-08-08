#include <math.h>
#include <stdio.h>

#include "controller_vr.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"

#define VR_STICK_DEADZONE 0.18f
#define VR_TRIGGER_THRESHOLD 0.55f
#define VR_CAMERA_BUTTON_THRESHOLD 0.55f
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
    const float magnitude = sqrtf(
        input[0] * input[0] +
        input[1] * input[1]
    );

    *outputX = 0;
    *outputY = 0;
    if (magnitude <= VR_STICK_DEADZONE) {
        return;
    }

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

    const float speed = sqrtf(
        state->gripLinearVelocity[0] *
            state->gripLinearVelocity[0] +
        state->gripLinearVelocity[1] *
            state->gripLinearVelocity[1] +
        state->gripLinearVelocity[2] *
            state->gripLinearVelocity[2]
    );

    const float gripAmount = fmaxf(
        state->squeeze,
        state->trigger
    );
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
    const float frameTravel = sqrtf(
        deltaX * deltaX +
        deltaY * deltaY +
        deltaZ * deltaZ
    );
    for (uint32_t axis = 0; axis < 3; axis++) {
        sVrPunchLastPosition[hand][axis] =
            state->gripPosition[axis];
    }

    if (speed <= resetSpeed) {
        sVrPunchArmed[hand] = true;
        sVrPunchTravel[hand] = 0.0f;
        return false;
    }

    if (speed >= travelSpeed) {
        sVrPunchTravel[hand] += frameTravel;
    } else {
        sVrPunchTravel[hand] = 0.0f;
    }

    if (!sVrPunchArmed[hand] ||
        speed < requiredSpeed ||
        sVrPunchTravel[hand] < requiredDistance) {
        return false;
    }

    sVrPunchArmed[hand] = false;
    printf(
        "[VR] %s physical punch detected "
        "(%.2f m/s, %.2f m travel).\n",
        hand == VR_CONTROLLER_LEFT ? "Left" : "Right",
        speed,
        sVrPunchTravel[hand]
    );
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

static void controller_vr_init(void) {
}

static void controller_vr_read(OSContPad* pad) {
    if (!vr_is_active() ||
        !configVrMotionControllerInput ||
        pad == NULL) {
        controller_vr_reset_physical_punches();
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

    if (configVrPhysicalPunching) {
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

    if (leftAvailable) {
        controller_vr_merge_stick(
            &pad->stick_x,
            &pad->stick_y,
            left.thumbstick
        );

        if (left.primaryButton ||
            left.trigger >= VR_TRIGGER_THRESHOLD) {
            pad->button |= Z_TRIG;
        }
        if (left.secondaryButton) {
            pad->button |= R_TRIG;
        }
        if (left.thumbstickButton) {
            pad->button |= L_TRIG;
        }
        if (left.menuButton) {
            pad->button |= START_BUTTON;
        }
    }

    if (rightAvailable) {
        controller_vr_merge_stick(
            &pad->ext_stick_x,
            &pad->ext_stick_y,
            right.thumbstick
        );

        if (right.primaryButton) {
            pad->button |= A_BUTTON;
        }
        if (right.secondaryButton ||
            (configVrPunchButton &&
             right.trigger >= VR_TRIGGER_THRESHOLD)) {
            pad->button |= B_BUTTON;
        }
        if (right.thumbstickButton) {
            pad->button |= R_TRIG;
        }
        if (right.menuButton) {
            pad->button |= START_BUTTON;
        }

        if (right.thumbstick[0] <=
            -VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= L_CBUTTONS;
        } else if (right.thumbstick[0] >=
                   VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= R_CBUTTONS;
        }
        if (right.thumbstick[1] >=
            VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= U_CBUTTONS;
        } else if (right.thumbstick[1] <=
                   -VR_CAMERA_BUTTON_THRESHOLD) {
            pad->button |= D_CBUTTONS;
        }
    }
}

static u32 controller_vr_rawkey(void) {
    return VK_INVALID;
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
