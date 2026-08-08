#include <math.h>

#include "controller_vr.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"

#define VR_STICK_DEADZONE 0.18f
#define VR_TRIGGER_THRESHOLD 0.55f
#define VR_CAMERA_BUTTON_THRESHOLD 0.55f

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

static void controller_vr_init(void) {
}

static void controller_vr_read(OSContPad* pad) {
    if (!vr_is_active() ||
        !configVrMotionControllerInput ||
        pad == NULL) {
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
        return;
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
            right.trigger >= VR_TRIGGER_THRESHOLD) {
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
