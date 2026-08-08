#include <stdbool.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"

static bool sVrMode = false;

static void djui_panel_vr_mode_changed(struct DjuiBase* caller) {
    (void)caller;

    if (!vr_set_active(sVrMode)) {
        sVrMode = false;
    }
}

static void djui_panel_vr_camera_settings_create(struct DjuiBase* caller) {
    if (configVrCameraMode >= VR_CAMERA_MODE_COUNT) {
        configVrCameraMode = VR_CAMERA_MODE_THIRD_PERSON;
    }
    if (configVrCameraDistance < 50) {
        configVrCameraDistance = 50;
    } else if (configVrCameraDistance > 250) {
        configVrCameraDistance = 250;
    }
    if (configVrCameraHeight < 50) {
        configVrCameraHeight = 50;
    } else if (configVrCameraHeight > 140) {
        configVrCameraHeight = 140;
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
            "First Person Height",
            &configVrCameraHeight,
            50,
            140,
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
