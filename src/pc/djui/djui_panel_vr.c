#include <stdbool.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "game/rendering_graph_node.h"

static bool sVrMode = false;

static char* sVrControllerBindingChoices[
    VR_CONTROLLER_BINDING_COUNT
] = {
    "Disabled",
    "Left Primary",
    "Left Secondary",
    "Left Trigger",
    "Left Grip",
    "Left Stick Click",
    "Left Menu",
    "Right Primary",
    "Right Secondary",
    "Right Trigger",
    "Right Grip",
    "Right Stick Click",
    "Right Menu"
};

static char* sVrControllerStickChoices[
    VR_CONTROLLER_STICK_COUNT
] = {
    "Left Stick",
    "Right Stick",
    "Disabled"
};

static unsigned int djui_panel_vr_clamp_uint(
    unsigned int value,
    unsigned int minimum,
    unsigned int maximum
) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void djui_panel_vr_mode_changed(struct DjuiBase* caller) {
    (void)caller;

    if (!vr_set_active(sVrMode)) {
        sVrMode = false;
    }
}

static void djui_panel_vr_camera_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrCameraMode = VR_CAMERA_MODE_FIRST_PERSON;
    configVrCameraDistance = 100;
    configVrCameraDepth = VR_CAMERA_DEPTH_CENTER;
    configVrFacingSource = VR_FACING_SOURCE_HEADSET;
    configVrMovementCalibration = 50;
    configVrFov = 100;
    configVrBrightness = 80;
    configVrSaturation = 112;
    configVrContrast = 115;

    for (unsigned int character = 0;
         character < CT_MAX;
         character++) {
        *config_vr_camera_height_for_character(character) =
            config_vr_camera_default_height_for_character(character);
    }

    vr_handle_camera_mode_change();
}

static void djui_panel_vr_camera_mode_changed(struct DjuiBase* caller) {
    (void)caller;
    vr_handle_camera_mode_change();
}

static void djui_panel_vr_performance_defaults(struct DjuiBase* caller) {
    (void)caller;

#ifdef __ANDROID__
    configVrRenderScale = 80;
#else
    configVrRenderScale = 100;
#endif
    configVrShowFps = false;
    configVrFlameOptimizations = true;
    configVrUltraPerformanceMode = false;
    configVrQuestRefreshRate = 2;
    configVrDisableFog = true;
    configVrDesktopMirror = true;
    configVrDesktopMirrorFps = 60;
#ifdef __ANDROID__
    configfile_save(configfile_name());
#endif
}

static void djui_panel_vr_ultra_performance_changed(struct DjuiBase* caller) {
    (void)caller;
    if (configVrUltraPerformanceMode) {
        configVrTwirlTornadoEffect = false;
    }
}

static void djui_panel_vr_render_scale_changed(struct DjuiBase* caller) {
    (void)caller;
#ifdef __ANDROID__
    // Quest can be suspended or force-closed without destroying the DJUI
    // panel stack, so persist this performance setting as soon as it changes.
    configfile_save(configfile_name());
#endif
}

static void djui_panel_vr_experimental_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrExperimentalFlatFirstPerson = false;
    configVrExperimentalTrueFirstPerson = false;
    configVrExperimentalTrueDiving = false;
    configVrExperimentalArmsMode = false;
    configVrOriginalMarioMovement = false;
    configVrBackpedalSpeed = VR_BACKPEDAL_SPEED_DEFAULT;
}

static void djui_panel_vr_controller_defaults(
    struct DjuiBase* caller
) {
    (void)caller;

    configVrMotionControllerInput = true;
    configVrPunchButton = false;
    configVrRightTriggerJump = false;
    configVrMoveStick = VR_CONTROLLER_STICK_LEFT;
    configVrCameraStick = VR_CONTROLLER_STICK_RIGHT;
    configVrJumpBinding =
        VR_CONTROLLER_BINDING_RIGHT_PRIMARY;
    configVrAttackBinding =
        VR_CONTROLLER_BINDING_RIGHT_SECONDARY;
    configVrCrouchBinding =
        VR_CONTROLLER_BINDING_LEFT_TRIGGER;
    configVrLBinding =
        VR_CONTROLLER_BINDING_LEFT_STICK_CLICK;
    configVrRBinding =
        VR_CONTROLLER_BINDING_RIGHT_STICK_CLICK;
    configVrPauseBinding =
        VR_CONTROLLER_BINDING_LEFT_MENU;
}

static void djui_panel_vr_quest_refresh_changed(struct DjuiBase* caller) {
    (void)caller;
#ifdef __ANDROID__
    configfile_save(configfile_name());
#endif
}

static void djui_panel_vr_right_trigger_jump_changed(
    UNUSED struct DjuiBase* caller
) {
    if (configVrRightTriggerJump) {
        configVrPunchButton = false;
    }
}

static void djui_panel_vr_punch_button_changed(
    UNUSED struct DjuiBase* caller
) {
    if (configVrPunchButton) {
        configVrRightTriggerJump = false;
    }
}

static void djui_panel_vr_motion_control_defaults(
    struct DjuiBase* caller
) {
    (void)caller;

    configVrPhysicalPunching = true;
    configVrPhysicalGrabbing = true;
    configVrPhysicalClimbing = true;
    configVrStandardGrabbing = true;
    configVrStandardClimbing = false;
    configVrSwingClimbRelease = true;
    configVrMarioPunchSound = true;
    configVrMotionControlledDive = true;
    configVrMotionControlledGroundDive = true;
    configVrTurnDuringJumps = true;
    configVrPunchSpeed = 150;
    configVrPunchDistance = 20;
    configVrPunchGripThreshold = 35;
    configVrPunchColliderLength = 250;
    configVrBowserSpinAcceleration = 100;
    configVrBowserMaxSpinSpeed = 100;
}

static void djui_panel_vr_model_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrFirstPersonBody = true;
    configVrHideTorsoWhileCrawling = true;
    configVrFeetOnlyBody = false;
    configVrBodyOpacity = 100;
    configVrLookDownTransparencyAngle = 25;
    configVrExperimentalMountedBody = false;
    configVrTopPoleFlipBody = false;
    configVrHideBodyOnLedge = true;
    configVrTorsoHeight = 100;
    configVrLegHeight = 100;
    configVrGloveSize = 70;
    configVrLeftGloveRotationX = 180;
    configVrLeftGlovePositionX = 100;
    configVrLeftGlovePositionY = 100;
    configVrLeftGlovePositionZ = 100;
    configVrRightGloveRotationX = 180;
    configVrRightGlovePositionX = 100;
    configVrRightGlovePositionY = 100;
    configVrRightGlovePositionZ = 100;
}

static void djui_panel_vr_hud_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrHudOpacity = 100;
    configVrHudSpread = 120;
}

static void djui_panel_vr_cheat_defaults(struct DjuiBase* caller) {
    (void)caller;
    configVrCheatSurfaceClimbing = false;
    configVrCheatShakingHatWingCap = false;
    configVrCheatUnderwaterBoxPunching = false;
    configVrFlyingSpeed = VR_FLYING_SPEED_DEFAULT;
    configVrSwimmingSpeed = VR_SWIMMING_SPEED_DEFAULT;
    configVrRunningSpeed = VR_RUNNING_SPEED_DEFAULT;
}

static void djui_panel_vr_immersion_defaults(struct DjuiBase* caller) {
    (void)caller;
    configVrImmersiveCameraMotion = true;
    configVrImmersiveFaceStuck = true;
    configVrImmersiveCannonCone = true;
    configVrImmersive3dSound = true;
    configVrImmersiveLedgeCamera = true;
    configVrImmersiveUnderwaterFilter = true;
    configVrImmersiveRemovableCap = false;
    configVrImmersiveLookDownTransparency = true;
    configVrExperimentalSideFlipFollow = true;
    configVrExperimentalWallJumpTurn = true;
    configVrPhysicalCrouching = true;
    configVrMovementOverhaul = false;
}

static void djui_panel_vr_effects_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrTwirlTornadoEffect = true;
}

static void djui_panel_vr_camera_settings_create(struct DjuiBase* caller) {
    unsigned int* cameraHeight =
        config_vr_camera_height_for_character(configPlayerModel);

    if (configVrCameraMode >= VR_CAMERA_MODE_COUNT) {
        configVrCameraMode = VR_CAMERA_MODE_FIRST_PERSON;
    }
    if (configVrCameraDistance < 50) {
        configVrCameraDistance = 50;
    } else if (configVrCameraDistance > 250) {
        configVrCameraDistance = 250;
    }
    if (*cameraHeight > VR_CAMERA_HEIGHT_MAX) {
        *cameraHeight = VR_CAMERA_HEIGHT_MAX;
    }
    if (configVrCameraDepth > VR_CAMERA_DEPTH_MAX) {
        configVrCameraDepth = VR_CAMERA_DEPTH_MAX;
    }
    if (configVrMovementCalibration > 100) {
        configVrMovementCalibration = 100;
    }
    if (configVrFacingSource >= VR_FACING_SOURCE_COUNT) {
        configVrFacingSource = VR_FACING_SOURCE_HEADSET;
    }
    if (configVrFov < 70) {
        configVrFov = 70;
    } else if (configVrFov > 120) {
        configVrFov = 120;
    }
    if (configVrBrightness < 10) {
        configVrBrightness = 10;
    } else if (configVrBrightness > 120) {
        configVrBrightness = 120;
    }
    configVrSaturation = djui_panel_vr_clamp_uint(
        configVrSaturation, 50, 150);
    configVrContrast = djui_panel_vr_clamp_uint(
        configVrContrast, 50, 150);
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Camera Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        char* cameraModes[VR_CAMERA_MODE_COUNT] = {
            "Third Person Mode",
            "First Person Mode"
        };

        djui_selectionbox_create(
            body,
            "Camera Mode",
            cameraModes,
            VR_CAMERA_MODE_COUNT,
            &configVrCameraMode,
            djui_panel_vr_camera_mode_changed
        );

        djui_slider_create(
            body,
            "Camera Distance (%)",
            &configVrCameraDistance,
            50,
            250,
            NULL
        );

        djui_slider_create(
            body,
            "First Person Height",
            cameraHeight,
            0,
            VR_CAMERA_HEIGHT_MAX,
            NULL
        );

        djui_slider_create(
            body,
            "First Person Forward / Back (200 = Center)",
            &configVrCameraDepth,
            0,
            VR_CAMERA_DEPTH_MAX,
            NULL
        );

        djui_slider_create(
            body,
            "Movement Calibration (50 = Center)",
            &configVrMovementCalibration,
            0,
            100,
            NULL
        );

        djui_slider_create(
            body,
            "Field of View (%)",
            &configVrFov,
            70,
            120,
            NULL
        );

        char* facingSources[VR_FACING_SOURCE_COUNT] = {
            "Headset", "Left Controller", "Right Controller"
        };
        djui_selectionbox_create(
            body,
            "Facing Direction",
            facingSources,
            VR_FACING_SOURCE_COUNT,
            &configVrFacingSource,
            NULL
        );

        djui_slider_create(
            body,
            "Brightness (%)",
            &configVrBrightness,
            10,
            120,
            NULL
        );

        djui_slider_create(
            body,
            "Color Saturation (%)",
            &configVrSaturation,
            50,
            150,
            NULL
        );

        djui_slider_create(
            body,
            "Contrast (%)",
            &configVrContrast,
            50,
            150,
            NULL
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_camera_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_performance_create(struct DjuiBase* caller) {
    if (configVrRenderScale < VR_RENDER_SCALE_MIN) {
        configVrRenderScale = VR_RENDER_SCALE_MIN;
    } else if (configVrRenderScale > VR_RENDER_SCALE_MAX) {
        configVrRenderScale = VR_RENDER_SCALE_MAX;
    }
    if (configVrDesktopMirrorFps < 15) {
        configVrDesktopMirrorFps = 15;
    } else if (configVrDesktopMirrorFps > 60) {
        configVrDesktopMirrorFps = 60;
    }
#ifdef __ANDROID__
    if (configVrQuestRefreshRate > 2) {
        configVrQuestRefreshRate = 2;
    }
#endif

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Performance", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_slider_create(
            body,
#ifdef __ANDROID__
            "Render Scale (%)",
#else
            "Render Scale (%)",
#endif
            &configVrRenderScale,
            VR_RENDER_SCALE_MIN,
            VR_RENDER_SCALE_MAX,
            djui_panel_vr_render_scale_changed
        );

        djui_checkbox_create(
            body,
            "FPS Counter",
            &configVrShowFps,
            NULL
        );

        djui_checkbox_create(
            body,
            "Flame & Lava Optimizations",
            &configVrFlameOptimizations,
            NULL
        );

        djui_checkbox_create(
            body,
            "Ultra Performance Mode (Degrades Visuals)",
            &configVrUltraPerformanceMode,
            djui_panel_vr_ultra_performance_changed
        );

#ifdef __ANDROID__
        char* refreshRateChoices[] = {
            "72 Hz",
            "90 Hz",
            "120 Hz"
        };
        djui_selectionbox_create(
            body,
            "Refresh Rate (May Not Reach Target)",
            refreshRateChoices,
            ARRAY_COUNT(refreshRateChoices),
            &configVrQuestRefreshRate,
            djui_panel_vr_quest_refresh_changed
        );
#endif

        djui_checkbox_create(
            body,
            "Disable Fog",
            &configVrDisableFog,
            NULL
        );

#ifndef __ANDROID__
        djui_checkbox_create(
            body,
            "Desktop View",
            &configVrDesktopMirror,
            NULL
        );

        djui_slider_create(
            body,
            "Desktop Mirror FPS",
            &configVrDesktopMirrorFps,
            15,
            60,
            NULL
        );
#endif

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_performance_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_experimental_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Experimental", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "True First Person (Might Cause Sickness)",
            &configVrExperimentalTrueFirstPerson,
            NULL
        );

        djui_checkbox_create(
            body,
            "True Diving (Camera Effect)",
            &configVrExperimentalTrueDiving,
            NULL
        );

        djui_checkbox_create(
            body,
            "Arms Mode",
            &configVrExperimentalArmsMode,
            NULL
        );

        djui_checkbox_create(
            body,
            "Original Mario Movement",
            &configVrOriginalMarioMovement,
            NULL
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_experimental_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_controller_settings_create(
    struct DjuiBase* caller
) {
    configVrMoveStick = djui_panel_vr_clamp_uint(
        configVrMoveStick,
        0,
        VR_CONTROLLER_STICK_COUNT - 1
    );
    configVrCameraStick = djui_panel_vr_clamp_uint(
        configVrCameraStick,
        0,
        VR_CONTROLLER_STICK_COUNT - 1
    );
    unsigned int* bindings[] = {
        &configVrJumpBinding,
        &configVrAttackBinding,
        &configVrCrouchBinding,
        &configVrLBinding,
        &configVrRBinding,
        &configVrPauseBinding
    };
    for (unsigned int i = 0;
         i < sizeof(bindings) / sizeof(bindings[0]);
         i++) {
        *bindings[i] = djui_panel_vr_clamp_uint(
            *bindings[i],
            0,
            VR_CONTROLLER_BINDING_COUNT - 1
        );
    }

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Controller Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Motion Controller Input",
            &configVrMotionControllerInput,
            NULL
        );

        djui_selectionbox_create(
            body,
            "Movement",
            sVrControllerStickChoices,
            VR_CONTROLLER_STICK_COUNT,
            &configVrMoveStick,
            NULL
        );

        djui_selectionbox_create(
            body,
            "Camera",
            sVrControllerStickChoices,
            VR_CONTROLLER_STICK_COUNT,
            &configVrCameraStick,
            NULL
        );

        djui_selectionbox_create(
            body,
            "Jump",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrJumpBinding,
            NULL
        );

        djui_selectionbox_create(
            body,
            "Attack / Interact",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrAttackBinding,
            NULL
        );

        djui_selectionbox_create(
            body,
            "Crouch",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrCrouchBinding,
            NULL
        );

        djui_selectionbox_create(
            body,
            "L Button",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrLBinding,
            NULL
        );

        djui_selectionbox_create(
            body,
            "R Button",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrRBinding,
            NULL
        );

        djui_selectionbox_create(
            body,
            "Pause",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrPauseBinding,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Punch Button (Right Trigger)",
            &configVrPunchButton,
            djui_panel_vr_punch_button_changed
        );

        djui_checkbox_create(
            body,
            "Right Trigger Jump",
            &configVrRightTriggerJump,
            djui_panel_vr_right_trigger_jump_changed
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_controller_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_motion_control_settings_create(
    struct DjuiBase* caller
) {
    configVrPunchSpeed = djui_panel_vr_clamp_uint(
        configVrPunchSpeed,
        75U,
        300U
    );
    configVrPunchDistance = djui_panel_vr_clamp_uint(
        configVrPunchDistance,
        5U,
        50U
    );
    configVrPunchGripThreshold = djui_panel_vr_clamp_uint(
        configVrPunchGripThreshold,
        10U,
        100U
    );
    configVrPunchColliderLength = djui_panel_vr_clamp_uint(
        configVrPunchColliderLength,
        50U,
        300U
    );
    configVrBowserSpinAcceleration =
        djui_panel_vr_clamp_uint(
            configVrBowserSpinAcceleration,
            25U,
            200U
        );
    configVrBowserMaxSpinSpeed =
        djui_panel_vr_clamp_uint(
            configVrBowserMaxSpinSpeed,
            50U,
            150U
        );

    struct DjuiThreePanel* panel =
        djui_panel_menu_create(
            "Motion Control Settings",
            false
        );

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Enable Physical Punches",
            &configVrPhysicalPunching,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Physical Grabbing",
            &configVrPhysicalGrabbing,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Physical Climbing",
            &configVrPhysicalClimbing,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Standard Grabbing",
            &configVrStandardGrabbing,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Standard Climbing",
            &configVrStandardClimbing,
            NULL
        );

        djui_checkbox_create(
            body,
            "Swing Off While Releasing",
            &configVrSwingClimbRelease,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Mario Punch Sound Effect",
            &configVrMarioPunchSound,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Motion Jump Dive",
            &configVrMotionControlledDive,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Motion Ground Dive",
            &configVrMotionControlledGroundDive,
            NULL
        );

        djui_checkbox_create(
            body,
            "Turn During Jumps",
            &configVrTurnDuringJumps,
            NULL
        );

        djui_slider_create(
            body,
            "Punch Speed Required (cm/s)",
            &configVrPunchSpeed,
            75,
            300,
            NULL
        );

        djui_slider_create(
            body,
            "Punch Range of Motion (cm)",
            &configVrPunchDistance,
            5,
            50,
            NULL
        );

        djui_slider_create(
            body,
            "Grip Strength Required (%)",
            &configVrPunchGripThreshold,
            10,
            100,
            NULL
        );

        djui_slider_create(
            body,
            "Punch Collider Length (%)",
            &configVrPunchColliderLength,
            50,
            300,
            NULL
        );

        djui_slider_create(
            body,
            "Bowser Spin Acceleration (%)",
            &configVrBowserSpinAcceleration,
            25,
            200,
            NULL
        );

        djui_slider_create(
            body,
            "Bowser Maximum Spin Speed (%)",
            &configVrBowserMaxSpinSpeed,
            50,
            150,
            NULL
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_motion_control_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_model_hand_settings_create(struct DjuiBase* caller) {
    configVrGloveSize = djui_panel_vr_clamp_uint(
        configVrGloveSize,
        25U,
        250U
    );
    configVrLeftGloveRotationX %= 360U;
    configVrLeftGloveRotationY %= 360U;
    configVrLeftGloveRotationZ %= 360U;
    configVrRightGloveRotationX %= 360U;
    configVrRightGloveRotationY %= 360U;
    configVrRightGloveRotationZ %= 360U;
    configVrLeftGlovePositionX =
        djui_panel_vr_clamp_uint(
            configVrLeftGlovePositionX,
            0U,
            200U
        );
    configVrLeftGlovePositionY =
        djui_panel_vr_clamp_uint(
            configVrLeftGlovePositionY,
            0U,
            200U
        );
    configVrLeftGlovePositionZ =
        djui_panel_vr_clamp_uint(
            configVrLeftGlovePositionZ,
            0U,
            200U
        );
    configVrRightGlovePositionX =
        djui_panel_vr_clamp_uint(
            configVrRightGlovePositionX,
            0U,
            200U
        );
    configVrRightGlovePositionY =
        djui_panel_vr_clamp_uint(
            configVrRightGlovePositionY,
            0U,
            200U
        );
    configVrRightGlovePositionZ =
        djui_panel_vr_clamp_uint(
            configVrRightGlovePositionZ,
            0U,
            200U
        );
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Hand Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_slider_create(
            body,
            "Glove Size (%)",
            &configVrGloveSize,
            25,
            250,
            NULL
        );

        djui_slider_create(
            body,
            "Left Rotation X (Degrees)",
            &configVrLeftGloveRotationX,
            0,
            359,
            NULL
        );
        djui_slider_create(
            body,
            "Left Position X (100 = Center)",
            &configVrLeftGlovePositionX,
            0,
            200,
            NULL
        );
        djui_slider_create(
            body,
            "Left Position Y (100 = Center)",
            &configVrLeftGlovePositionY,
            0,
            200,
            NULL
        );
        djui_slider_create(
            body,
            "Left Position Z (100 = Center)",
            &configVrLeftGlovePositionZ,
            0,
            200,
            NULL
        );

        djui_slider_create(
            body,
            "Right Rotation X (Degrees)",
            &configVrRightGloveRotationX,
            0,
            359,
            NULL
        );
        djui_slider_create(
            body,
            "Right Position X (100 = Center)",
            &configVrRightGlovePositionX,
            0,
            200,
            NULL
        );
        djui_slider_create(
            body,
            "Right Position Y (100 = Center)",
            &configVrRightGlovePositionY,
            0,
            200,
            NULL
        );
        djui_slider_create(
            body,
            "Right Position Z (100 = Center)",
            &configVrRightGlovePositionZ,
            0,
            200,
            NULL
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_model_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_hud_settings_create(struct DjuiBase* caller) {
    if (configVrHudOpacity > 100) {
        configVrHudOpacity = 100;
    }
    if (configVrHudSpread < 80) {
        configVrHudSpread = 80;
    } else if (configVrHudSpread > 200) {
        configVrHudSpread = 200;
    }

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("HUD Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_slider_create(
            body,
            "HUD Opacity (%)",
            &configVrHudOpacity,
            0,
            100,
            NULL
        );

        djui_slider_create(
            body,
            "HUD Corner Spread (%)",
            &configVrHudSpread,
            80,
            200,
            NULL
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_hud_defaults
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_cheats_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Cheats", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    djui_checkbox_create(
        body,
        "Climb Any Wall or Ceiling",
        &configVrCheatSurfaceClimbing,
        NULL
    );
    djui_checkbox_create(
        body,
        "Shaking Hat Gives Wing Cap (Grab Cap at Any Time Required)",
        &configVrCheatShakingHatWingCap,
        NULL
    );
    djui_checkbox_create(
        body,
        "Punch Boxes While Underwater",
        &configVrCheatUnderwaterBoxPunching,
        NULL
    );

    configVrFlyingSpeed = djui_panel_vr_clamp_uint(
        configVrFlyingSpeed,
        VR_FLYING_SPEED_MIN,
        VR_FLYING_SPEED_MAX
    );
    djui_slider_create(
        body,
        "Flying Speed (%)",
        &configVrFlyingSpeed,
        VR_FLYING_SPEED_MIN,
        VR_FLYING_SPEED_MAX,
        NULL
    );
    configVrSwimmingSpeed = djui_panel_vr_clamp_uint(
        configVrSwimmingSpeed,
        VR_SWIMMING_SPEED_MIN,
        VR_SWIMMING_SPEED_MAX
    );
    djui_slider_create(
        body,
        "Swimming Speed (%)",
        &configVrSwimmingSpeed,
        VR_SWIMMING_SPEED_MIN,
        VR_SWIMMING_SPEED_MAX,
        NULL
    );
    configVrRunningSpeed = djui_panel_vr_clamp_uint(
        configVrRunningSpeed,
        VR_RUNNING_SPEED_MIN,
        VR_RUNNING_SPEED_MAX
    );
    djui_slider_create(
        body,
        "Running Speed (%)",
        &configVrRunningSpeed,
        VR_RUNNING_SPEED_MIN,
        VR_RUNNING_SPEED_MAX,
        NULL
    );
    djui_button_create(
        body,
        "Set to Defaults",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_cheat_defaults
    );
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_model_body_settings_create(struct DjuiBase* caller) {
    configVrBodyOpacity = djui_panel_vr_clamp_uint(configVrBodyOpacity, 0U, 100U);
    configVrLookDownTransparencyAngle = djui_panel_vr_clamp_uint(configVrLookDownTransparencyAngle, 5U, 60U);
    configVrTorsoHeight = djui_panel_vr_clamp_uint(configVrTorsoHeight, 0U, 200U);
    configVrLegHeight = djui_panel_vr_clamp_uint(configVrLegHeight, 0U, 200U);

    struct DjuiThreePanel* panel = djui_panel_menu_create("Body Settings", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(body, "Show Torso and Legs in First Person", &configVrFirstPersonBody, NULL);
        djui_checkbox_create(body, "Feet Only (Hide Torso and Legs)", &configVrFeetOnlyBody, NULL);
        djui_checkbox_create(body, "Hide Torso While Crawling", &configVrHideTorsoWhileCrawling, NULL);
        djui_checkbox_create(body, "Body During Flying, Swimming, and Shell Riding", &configVrExperimentalMountedBody, NULL);
        djui_checkbox_create(body, "Body During Top-of-Pole Flip", &configVrTopPoleFlipBody, NULL);
        djui_checkbox_create(body, "Hide Body While on Ledges", &configVrHideBodyOnLedge, NULL);
        djui_slider_create(body, "Body Opacity (%)", &configVrBodyOpacity, 0, 100, NULL);
        djui_slider_create(body, "Look-Down Fade Angle", &configVrLookDownTransparencyAngle, 5, 60, NULL);
        djui_slider_create(body, "Torso Height (100 = Center)", &configVrTorsoHeight, 0, 200, NULL);
        djui_slider_create(body, "Leg Height (100 = Center)", &configVrLegHeight, 0, 200, NULL);
        djui_button_create(body, "Set to Defaults", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_model_defaults);
        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_model_settings_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Model Settings", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        djui_button_create(body, "Body Settings", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_model_body_settings_create);
        djui_button_create(body, "Hand Settings", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_model_hand_settings_create);
        djui_button_create(body, "Set to Defaults", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_model_defaults);
        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_immersion_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Immersion", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    djui_checkbox_create(body, "Crouch / Sand Camera",
        &configVrImmersiveCameraMotion, NULL);
    djui_checkbox_create(body, "Face-Stuck Blackout",
        &configVrImmersiveFaceStuck, NULL);
    djui_checkbox_create(body, "Cannon Aim Direction Cone",
        &configVrImmersiveCannonCone, NULL);
    djui_checkbox_create(body, "Head-Tracked 3D Sound",
        &configVrImmersive3dSound, NULL);
    djui_checkbox_create(body, "Camera on Body During Climb Up",
        &configVrImmersiveLedgeCamera, NULL);
    djui_checkbox_create(body, "Underwater Filter",
        &configVrImmersiveUnderwaterFilter, NULL);
    djui_checkbox_create(body, "Grab Cap at Any Time",
        &configVrImmersiveRemovableCap, NULL);
    djui_checkbox_create(body, "Mario Transparency While Looking Down",
        &configVrImmersiveLookDownTransparency, NULL);
    djui_checkbox_create(body, "Side-Flip Camera Follow",
        &configVrExperimentalSideFlipFollow, NULL);
    djui_checkbox_create(body, "180 Degree Wall-Jump Camera Turn",
        &configVrExperimentalWallJumpTurn, NULL);
    djui_checkbox_create(body, "Physical Crouching / Ground Pounds",
        &configVrPhysicalCrouching, NULL);
    djui_button_create(body, "Set to Defaults", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_immersion_defaults);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_effects_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Effects", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    djui_checkbox_create(body, "Twirl Tornado Effect",
        &configVrTwirlTornadoEffect, NULL);
    djui_button_create(body, "Set to Defaults", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_effects_defaults);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

void djui_panel_vr_create(struct DjuiBase* caller) {
    // Make the checkbox match the actual VR state whenever
    // the panel is opened.
    sVrMode = vr_is_active();

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("VR", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
#ifndef __ANDROID__
        djui_checkbox_create(
            body,
            "VR Mode",
            &sVrMode,
            djui_panel_vr_mode_changed
        );

        djui_checkbox_create(
            body,
            "Launch in VR",
            &configVrAutoStart,
            NULL
        );

#endif

        djui_button_create(
            body,
            "Camera Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_camera_settings_create
        );

        djui_button_create(
            body,
            "Controller Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_controller_settings_create
        );

        djui_button_create(
            body,
            "Motion Control Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_motion_control_settings_create
        );

        djui_button_create(
            body,
            "Model Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_model_settings_create
        );

        djui_button_create(
            body,
            "Performance",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_performance_create
        );

        djui_button_create(
            body,
            "HUD Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_hud_settings_create
        );

        djui_button_create(
            body,
            "Immersion",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_immersion_create
        );

        djui_button_create(
            body,
            "Effects",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_effects_create
        );

        djui_button_create(
            body,
            "Experimental",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_experimental_create
        );

        djui_button_create(
            body,
            "Cheats",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_cheats_create
        );

        djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
    }

    djui_panel_add(caller, panel, NULL);
}
