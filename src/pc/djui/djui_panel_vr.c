#include <stdbool.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"

static bool sVrMode = false;

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

    configVrCameraMode = VR_CAMERA_MODE_THIRD_PERSON;
    configVrCameraDistance = 100;
    configVrCameraDepth = VR_CAMERA_DEPTH_CENTER;
    configVrMovementCalibration = 50;
    configVrFov = 100;

    for (unsigned int character = 0;
         character < CT_MAX;
         character++) {
        *config_vr_camera_height_for_character(character) =
            VR_CAMERA_HEIGHT_CENTER;
    }
}

static void djui_panel_vr_performance_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrRenderScale = 100;
    configVrDesktopMirror = true;
    configVrDesktopMirrorFps = 60;
}

static void djui_panel_vr_experimental_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrExperimentalFlipTurn = true;
    configVrExperimentalFlatFirstPerson = false;
    configVrExperimentalTrueFirstPerson = false;
    configVrExperimentalArmsMode = false;
}

static void djui_panel_vr_controls_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrMotionControllerInput = true;
    configVrPunchButton = false;
}

static void djui_panel_vr_motion_control_defaults(
    struct DjuiBase* caller
) {
    (void)caller;

    configVrPhysicalPunching = true;
    configVrPhysicalGrabbing = true;
    configVrMarioPunchSound = true;
    configVrMotionControlledDive = true;
    configVrMotionControlledGroundDive = true;
    configVrPunchSpeed = 150;
    configVrPunchDistance = 20;
    configVrPunchGripThreshold = 35;
    configVrPunchColliderLength = 150;
    configVrBowserSpinAcceleration = 100;
    configVrBowserMaxSpinSpeed = 100;
}

static void djui_panel_vr_model_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrFirstPersonBody = true;
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
}

static void djui_panel_vr_camera_settings_create(struct DjuiBase* caller) {
    unsigned int* cameraHeight =
        config_vr_camera_height_for_character(configPlayerModel);

    if (configVrCameraMode >= VR_CAMERA_MODE_COUNT) {
        configVrCameraMode = VR_CAMERA_MODE_THIRD_PERSON;
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
    if (configVrFov < 70) {
        configVrFov = 70;
    } else if (configVrFov > 120) {
        configVrFov = 120;
    }
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Camera Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        char* cameraModes[VR_CAMERA_MODE_COUNT] = {
            "Third Person Mode",
            "First Person Mode (Experimental)"
        };

        djui_selectionbox_create(
            body,
            "Camera Mode",
            cameraModes,
            VR_CAMERA_MODE_COUNT,
            &configVrCameraMode,
            NULL
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
            "First Person Height (500 = Center)",
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
    if (configVrRenderScale < 50) {
        configVrRenderScale = 50;
    } else if (configVrRenderScale > 100) {
        configVrRenderScale = 100;
    }
    if (configVrDesktopMirrorFps < 15) {
        configVrDesktopMirrorFps = 15;
    } else if (configVrDesktopMirrorFps > 60) {
        configVrDesktopMirrorFps = 60;
    }

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Performance", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_slider_create(
            body,
            "Render Scale (%)",
            &configVrRenderScale,
            50,
            100,
            NULL
        );

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
            "180 Degree Flip Camera Turn",
            &configVrExperimentalFlipTurn,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable First Person in Flat Mode",
            &configVrExperimentalFlatFirstPerson,
            NULL
        );

        djui_checkbox_create(
            body,
            "True First Person (Might Cause Sickness)",
            &configVrExperimentalTrueFirstPerson,
            NULL
        );

        djui_checkbox_create(
            body,
            "Arms Mode",
            &configVrExperimentalArmsMode,
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

static void djui_panel_vr_controls_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("VR Controls", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Motion Controller Input",
            &configVrMotionControllerInput,
            NULL
        );

        djui_checkbox_create(
            body,
            "Enable Punch Button (Right Trigger)",
            &configVrPunchButton,
            NULL
        );

        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_controls_defaults
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

static void djui_panel_vr_model_settings_create(struct DjuiBase* caller) {
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
    configVrTorsoHeight = djui_panel_vr_clamp_uint(
        configVrTorsoHeight,
        0U,
        200U
    );
    configVrLegHeight = djui_panel_vr_clamp_uint(
        configVrLegHeight,
        0U,
        200U
    );

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Model Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Show Torso and Legs in First Person",
            &configVrFirstPersonBody,
            NULL
        );

        djui_slider_create(
            body,
            "Torso Height (100 = Center)",
            &configVrTorsoHeight,
            0,
            200,
            NULL
        );

        djui_slider_create(
            body,
            "Leg Height (100 = Center)",
            &configVrLegHeight,
            0,
            200,
            NULL
        );

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

void djui_panel_vr_create(struct DjuiBase* caller) {
    // Make the checkbox match the actual VR state whenever
    // the panel is opened.
    sVrMode = vr_is_active();

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("VR", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
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

        djui_button_create(
            body,
            "Camera Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_camera_settings_create
        );

        djui_button_create(
            body,
            "VR Controls",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_controls_create
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
            "Experimental",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_experimental_create
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
