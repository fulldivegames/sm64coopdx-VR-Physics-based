#include <stdbool.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"

#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "data/dynos.c.h"
#include "game/rendering_graph_node.h"
#include "game/vr_hand_interaction.h"
#include "level_table.h"

static bool sVrMode = false;

#define VR_LEVEL_SELECT_ENTRIES(X) \
    X(castle_grounds, "Castle Grounds", LEVEL_CASTLE_GROUNDS) \
    X(castle, "Peach's Castle", LEVEL_CASTLE) \
    X(castle_courtyard, "Castle Courtyard", LEVEL_CASTLE_COURTYARD) \
    X(bob, "1 - Bob-omb Battlefield", LEVEL_BOB) \
    X(wf, "2 - Whomp's Fortress", LEVEL_WF) \
    X(jrb, "3 - Jolly Roger Bay", LEVEL_JRB) \
    X(ccm, "4 - Cool, Cool Mountain", LEVEL_CCM) \
    X(bbh, "5 - Big Boo's Haunt", LEVEL_BBH) \
    X(hmc, "6 - Hazy Maze Cave", LEVEL_HMC) \
    X(lll, "7 - Lethal Lava Land", LEVEL_LLL) \
    X(ssl, "8 - Shifting Sand Land", LEVEL_SSL) \
    X(ddd, "9 - Dire, Dire Docks", LEVEL_DDD) \
    X(sl, "10 - Snowman's Land", LEVEL_SL) \
    X(wdw, "11 - Wet-Dry World", LEVEL_WDW) \
    X(ttm, "12 - Tall, Tall Mountain", LEVEL_TTM) \
    X(thi, "13 - Tiny-Huge Island", LEVEL_THI) \
    X(ttc, "14 - Tick Tock Clock", LEVEL_TTC) \
    X(rr, "15 - Rainbow Ride", LEVEL_RR) \
    X(bitdw, "Bowser in the Dark World", LEVEL_BITDW) \
    X(bowser_1, "Bowser 1 Arena", LEVEL_BOWSER_1) \
    X(bitfs, "Bowser in the Fire Sea", LEVEL_BITFS) \
    X(bowser_2, "Bowser 2 Arena", LEVEL_BOWSER_2) \
    X(bits, "Bowser in the Sky", LEVEL_BITS) \
    X(bowser_3, "Bowser 3 Arena", LEVEL_BOWSER_3) \
    X(pss, "The Princess's Secret Slide", LEVEL_PSS) \
    X(sa, "The Secret Aquarium", LEVEL_SA) \
    X(cotmc, "Cavern of the Metal Cap", LEVEL_COTMC) \
    X(totwc, "Tower of the Wing Cap", LEVEL_TOTWC) \
    X(vcutm, "Vanish Cap Under the Moat", LEVEL_VCUTM) \
    X(wmotr, "Wing Mario Over the Rainbow", LEVEL_WMOTR) \
    X(ending, "Ending / Credits", LEVEL_ENDING)

static void djui_panel_vr_warp_to_level(s32 level) {
    djui_panel_shutdown();
    dynos_warp_to_level(level, 1, 1);
}

#define VR_DEFINE_LEVEL_CALLBACK(id, label, level) \
    static void djui_panel_vr_warp_##id(UNUSED struct DjuiBase* caller) { \
        djui_panel_vr_warp_to_level(level); \
    }
VR_LEVEL_SELECT_ENTRIES(VR_DEFINE_LEVEL_CALLBACK)
#undef VR_DEFINE_LEVEL_CALLBACK

static void djui_panel_vr_level_select_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Level Select", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

#define VR_CREATE_LEVEL_BUTTON(id, label, level) \
    djui_button_create( \
        body, label, DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_warp_##id \
    );
    VR_LEVEL_SELECT_ENTRIES(VR_CREATE_LEVEL_BUTTON)
#undef VR_CREATE_LEVEL_BUTTON

    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
}

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

static void djui_panel_vr_spawn_fire_flower(struct DjuiBase* caller) {
    (void)caller;
    vr_special_moves_spawn_cheat_fire_flower();
}

static void djui_panel_vr_spawn_hammer_suit(struct DjuiBase* caller) {
    (void)caller;
    vr_special_moves_spawn_cheat_hammer_suit();
}

static void djui_panel_vr_spawn_wing_cap(UNUSED struct DjuiBase* caller) {
    vr_special_moves_spawn_cheat_cap(VR_CHEAT_SPAWN_WING_CAP);
}

static void djui_panel_vr_spawn_vanish_cap(UNUSED struct DjuiBase* caller) {
    vr_special_moves_spawn_cheat_cap(VR_CHEAT_SPAWN_VANISH_CAP);
}

static void djui_panel_vr_spawn_metal_cap(UNUSED struct DjuiBase* caller) {
    vr_special_moves_spawn_cheat_cap(VR_CHEAT_SPAWN_METAL_CAP);
}

static void djui_panel_vr_spawn_menu_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Spawn Menu", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "Wing Cap", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_wing_cap);
    djui_button_create(body, "Vanish Cap", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_vanish_cap);
    djui_button_create(body, "Metal Cap", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_metal_cap);
    djui_button_create(body, "Fire Flower", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_fire_flower);
    djui_button_create(body, "Hammer (Hammer Suit)", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_hammer_suit);
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
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
    configVrPreviousBodyHeight = false;
    configVrFacingSource = VR_FACING_SOURCE_HEADSET;
    configVrMovementCalibration = 50;
    configVrFov = 100;
    configVrBrightness = 100;

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

    configVrRenderScale = 100;
    configVrShowFps = false;
    configVrFlameOptimizations = true;
    configVrUltraPerformanceMode = false;
    configVrDisableFog = true;
    configVrDesktopMirror = true;
    configVrDesktopMirrorFps = 60;
}

static void djui_panel_vr_ultra_performance_changed(struct DjuiBase* caller) {
    (void)caller;
    if (configVrUltraPerformanceMode) {
        configVrTwirlTornadoEffect = false;
    }
}

static void djui_panel_vr_experimental_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrExperimentalFlatFirstPerson = false;
    configVrExperimentalTrueFirstPerson = false;
    configVrExperimentalTrueDiving = false;
    configVrExperimentalArmsMode = false;
    configVrExperimentalClimbableColliders = false;
    configVrOriginalMarioMovement = false;
    configVrBackpedalSpeed = VR_BACKPEDAL_SPEED_DEFAULT;
}

static void djui_panel_vr_controller_defaults(
    struct DjuiBase* caller
) {
    (void)caller;

    configVrMotionControllerInput = true;
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
    configVrSpecialBinding =
        VR_CONTROLLER_BINDING_LEFT_SECONDARY;
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
    configVrPunchColliderLength = 275;
    configVrBowserSpinAcceleration = 100;
    configVrBowserMaxSpinSpeed = 100;
}

static void djui_panel_vr_model_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrFirstPersonBody = true;
    configVrHideTorsoWhileCrawling = true;
    configVrFeetOnlyBody = false;
    configVrBodyOpacity = 100;
    configVrGhostPunchArmOpacity = 25;
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
    configVrMenuAnchor = VR_UI_ANCHOR_HEADSET;
    configVrHudAnchor = VR_UI_ANCHOR_HEADSET;
}

static void djui_panel_vr_cheat_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrCheatSurfaceClimbing = false;
    configVrCheatShakingHatWingCap = false;
    configVrCheatUnderwaterBoxPunching = false;
    configVrCheatFreeFly = false;
    configVrCheatNoFireFlowerTimer = false;
    configVrFlyingSpeed = VR_FLYING_SPEED_DEFAULT;
    configVrSwimmingSpeed = VR_SWIMMING_SPEED_DEFAULT;
    configVrRunningSpeed = VR_RUNNING_SPEED_DEFAULT;
    configVrFireballChargeTime = 15;
    configVrRasenganChargeTime = 30;
    configVrRasenShurikenChargeTime = 20;
}

static void djui_panel_vr_special_moves_defaults(
    struct DjuiBase* caller
) {
    (void)caller;
    configVrSpecialFireFlower = true;
    configVrSpecialFireFlowerMusic = true;
    configVrSpecialHammerSuit = true;
    configVrSpecialRasengan = true;
    configVrSpecialRasenganGripTrigger = false;
    configVrRasenShurikenOverheadCharge = true;
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
    configVrImmersiveCarrySpeed = false;
    configVrImmersiveStarSpawnFocus = false;
    configVrImmersiveGhostPunchArm = true;
    configVrImmersiveWallJumpCameraRelative = false;
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

        djui_checkbox_create(
            body,
            "Use Previous Mario Body Height",
            &configVrPreviousBodyHeight,
            NULL
        );

        char* facingSources[VR_FACING_SOURCE_COUNT] = {
            "Headset",
            "Left Controller",
            "Right Controller"
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
            "Facing Calibration (50 = Center)",
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

        djui_slider_create(
            body,
            "Brightness (%)",
            &configVrBrightness,
            10,
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

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Performance", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_slider_create(
            body,
            "Render Scale (%)",
            &configVrRenderScale,
            VR_RENDER_SCALE_MIN,
            VR_RENDER_SCALE_MAX,
            NULL
        );

        djui_checkbox_create(
            body,
            "FPS Counter",
            &configVrShowFps,
            NULL
        );

        djui_checkbox_create(
            body,
            "Ultra Performance Mode (Degrades Visuals)",
            &configVrUltraPerformanceMode,
            djui_panel_vr_ultra_performance_changed
        );

        djui_checkbox_create(
            body,
            "Disable Fog",
            &configVrDisableFog,
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
            "Colliders for Poles, Trees, and Hangables",
            &configVrExperimentalClimbableColliders,
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

        djui_selectionbox_create(
            body,
            "Special Button",
            sVrControllerBindingChoices,
            VR_CONTROLLER_BINDING_COUNT,
            &configVrSpecialBinding,
            NULL
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

static void djui_panel_vr_model_body_settings_create(struct DjuiBase* caller) {
    configVrBodyOpacity = djui_panel_vr_clamp_uint(
        configVrBodyOpacity,
        0U,
        100U
    );
    configVrGhostPunchArmOpacity = djui_panel_vr_clamp_uint(
        configVrGhostPunchArmOpacity,
        0U,
        100U
    );
    configVrLookDownTransparencyAngle = djui_panel_vr_clamp_uint(
        configVrLookDownTransparencyAngle,
        5U,
        60U
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
        djui_panel_menu_create("Body Settings", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Show Torso and Legs in First Person",
            &configVrFirstPersonBody,
            NULL
        );
        djui_checkbox_create(
            body,
            "Feet Only (Hide Torso and Legs)",
            &configVrFeetOnlyBody,
            NULL
        );
        djui_checkbox_create(
            body,
            "Hide Torso While Crawling",
            &configVrHideTorsoWhileCrawling,
            NULL
        );
        djui_checkbox_create(
            body,
            "Body During Flying, Swimming, and Shell Riding",
            &configVrExperimentalMountedBody,
            NULL
        );
        djui_checkbox_create(
            body,
            "Body During Top-of-Pole Flip",
            &configVrTopPoleFlipBody,
            NULL
        );
        djui_checkbox_create(
            body,
            "Hide Body While on Ledges",
            &configVrHideBodyOnLedge,
            NULL
        );
        djui_slider_create(
            body,
            "Body Opacity (%)",
            &configVrBodyOpacity,
            0,
            100,
            NULL
        );
        djui_slider_create(
            body,
            "Button Punch Arm Opacity (%)",
            &configVrGhostPunchArmOpacity,
            0,
            100,
            NULL
        );
        djui_slider_create(
            body,
            "Look-Down Fade Angle",
            &configVrLookDownTransparencyAngle,
            5,
            60,
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

static void djui_panel_vr_model_settings_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Model Settings", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        djui_button_create(
            body,
            "Body Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_model_body_settings_create
        );
        djui_button_create(
            body,
            "Hand Settings",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_model_hand_settings_create
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
    if (configVrMenuAnchor >= VR_UI_ANCHOR_COUNT) {
        configVrMenuAnchor = VR_UI_ANCHOR_HEADSET;
    }
    if (configVrHudAnchor >= VR_HUD_ANCHOR_COUNT) {
        configVrHudAnchor = VR_HUD_ANCHOR_HEADSET;
    }

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("HUD Settings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        char* menuAnchorChoices[VR_UI_ANCHOR_COUNT] = {
            "Headset",
            "Left Hand",
            "Right Hand"
        };
        char* hudAnchorChoices[VR_HUD_ANCHOR_COUNT] = {
            "Headset",
            "Hand"
        };

        djui_selectionbox_create(
            body,
            "Menu Placement",
            menuAnchorChoices,
            VR_UI_ANCHOR_COUNT,
            &configVrMenuAnchor,
            NULL
        );

        djui_selectionbox_create(
            body,
            "HUD Placement",
            hudAnchorChoices,
            VR_HUD_ANCHOR_COUNT,
            &configVrHudAnchor,
            NULL
        );

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

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_button_create(
            body,
            "Level Select",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_level_select_create
        );
        djui_button_create(
            body,
            "Spawn Menu",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_spawn_menu_create
        );
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
        djui_checkbox_create(
            body,
            "Free Fly",
            &configVrCheatFreeFly,
            NULL
        );
        djui_checkbox_create(
            body,
            "No Fire Flower Timer",
            &configVrCheatNoFireFlowerTimer,
            NULL
        );

        configVrFireballChargeTime = djui_panel_vr_clamp_uint(
            configVrFireballChargeTime, 5U, 50U
        );
        configVrRasenganChargeTime = djui_panel_vr_clamp_uint(
            configVrRasenganChargeTime, 10U, 80U
        );
        configVrRasenShurikenChargeTime = djui_panel_vr_clamp_uint(
            configVrRasenShurikenChargeTime, 5U, 50U
        );
        djui_slider_create(
            body,
            "Fireball Charge (0.1 sec)",
            &configVrFireballChargeTime,
            5,
            50,
            NULL
        );
        djui_slider_create(
            body,
            "Rasengan Charge (0.1 sec)",
            &configVrRasenganChargeTime,
            10,
            80,
            NULL
        );
        djui_slider_create(
            body,
            "Rasen-Shuriken Charge (0.1 sec)",
            &configVrRasenShurikenChargeTime,
            5,
            50,
            NULL
        );

        if (configVrFlyingSpeed < VR_FLYING_SPEED_MIN) {
            configVrFlyingSpeed = VR_FLYING_SPEED_MIN;
        } else if (configVrFlyingSpeed > VR_FLYING_SPEED_MAX) {
            configVrFlyingSpeed = VR_FLYING_SPEED_MAX;
        }

        djui_slider_create(
            body,
            "Flying Speed (%)",
            &configVrFlyingSpeed,
            VR_FLYING_SPEED_MIN,
            VR_FLYING_SPEED_MAX,
            NULL
        );

        if (configVrSwimmingSpeed < VR_SWIMMING_SPEED_MIN) {
            configVrSwimmingSpeed = VR_SWIMMING_SPEED_MIN;
        } else if (configVrSwimmingSpeed > VR_SWIMMING_SPEED_MAX) {
            configVrSwimmingSpeed = VR_SWIMMING_SPEED_MAX;
        }
        djui_slider_create(
            body,
            "Swimming Speed (%)",
            &configVrSwimmingSpeed,
            VR_SWIMMING_SPEED_MIN,
            VR_SWIMMING_SPEED_MAX,
            NULL
        );

        if (configVrRunningSpeed < VR_RUNNING_SPEED_MIN) {
            configVrRunningSpeed = VR_RUNNING_SPEED_MIN;
        } else if (configVrRunningSpeed > VR_RUNNING_SPEED_MAX) {
            configVrRunningSpeed = VR_RUNNING_SPEED_MAX;
        }
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
    }

    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_special_moves_create(
    struct DjuiBase* caller
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Special Moves", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Fire Flower (50% Item Boxes / 30% Cork Boxes)",
            &configVrSpecialFireFlower,
            NULL
        );
        djui_checkbox_create(
            body,
            "Fire Flower Music",
            &configVrSpecialFireFlowerMusic,
            NULL
        );
        djui_checkbox_create(
            body,
            "Hammer Suit",
            &configVrSpecialHammerSuit,
            NULL
        );
        djui_checkbox_create(
            body,
            "Rasengan / Rasen-Shuriken",
            &configVrSpecialRasengan,
            NULL
        );
        djui_checkbox_create(
            body,
            "Rasengan Grip + Trigger",
            &configVrSpecialRasenganGripTrigger,
            NULL
        );
        djui_checkbox_create(
            body,
            "Rasen-Shuriken Overhead Charge",
            &configVrRasenShurikenOverheadCharge,
            NULL
        );
        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_special_moves_defaults
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

static void djui_panel_vr_immersion_camera_create(
    struct DjuiBase* caller
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Camera & Comfort", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Crouch / Sand Camera",
            &configVrImmersiveCameraMotion,
            NULL
        );

        djui_checkbox_create(
            body,
            "Face-Stuck Blackout",
            &configVrImmersiveFaceStuck,
            NULL
        );

        djui_checkbox_create(
            body,
            "Cannon Aim Direction Cone",
            &configVrImmersiveCannonCone,
            NULL
        );

        djui_checkbox_create(
            body,
            "Camera on Body During Climb Up",
            &configVrImmersiveLedgeCamera,
            NULL
        );

        djui_checkbox_create(
            body,
            "Underwater Filter",
            &configVrImmersiveUnderwaterFilter,
            NULL
        );

        djui_checkbox_create(
            body,
            "Look Toward Spawned Stars",
            &configVrImmersiveStarSpawnFocus,
            NULL
        );

        djui_checkbox_create(
            body,
            "Side-Flip Camera Follow",
            &configVrExperimentalSideFlipFollow,
            NULL
        );

        djui_checkbox_create(
            body,
            "180 Degree Wall-Jump Camera Turn",
            &configVrExperimentalWallJumpTurn,
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

static void djui_panel_vr_immersion_movement_create(
    struct DjuiBase* caller
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Movement & Body", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_checkbox_create(
        body,
        "Camera-Relative Wall-Jump Steering",
        &configVrImmersiveWallJumpCameraRelative,
        NULL
    );
    djui_checkbox_create(
        body,
        "Physical Crouching / Ground Pounds",
        &configVrPhysicalCrouching,
        NULL
    );
    djui_checkbox_create(
        body,
        "Carrying-Speed Movement While Holding",
        &configVrImmersiveCarrySpeed,
        NULL
    );
    djui_checkbox_create(
        body,
        "Mario Transparency While Looking Down",
        &configVrImmersiveLookDownTransparency,
        NULL
    );
    djui_checkbox_create(
        body,
        "Ghost Arm for Button Punches",
        &configVrImmersiveGhostPunchArm,
        NULL
    );
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_immersion_interaction_create(
    struct DjuiBase* caller
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Interaction & Audio", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_checkbox_create(
        body,
        "Head-Tracked 3D Sound",
        &configVrImmersive3dSound,
        NULL
    );
    djui_checkbox_create(
        body,
        "Grab Cap at Any Time",
        &configVrImmersiveRemovableCap,
        NULL
    );
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_immersion_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Immersion", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(
        body,
        "Camera & Comfort",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_immersion_camera_create
    );
    djui_button_create(
        body,
        "Movement & Body",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_immersion_movement_create
    );
    djui_button_create(
        body,
        "Interaction & Audio",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_immersion_interaction_create
    );
    djui_button_create(
        body,
        "Set All Immersion Defaults",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_immersion_defaults
    );
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_effects_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Effects", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        djui_checkbox_create(
            body,
            "Twirl Tornado Effect",
            &configVrTwirlTornadoEffect,
            NULL
        );
        djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_effects_defaults
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

static void djui_panel_vr_tutorial_page(
    struct DjuiBase* caller,
    char* title,
    const char* message
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create(title, false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    struct DjuiText* text = djui_text_create(body, message);
    djui_base_set_location(&text->base, 0, 0);
    djui_base_set_size(
        &text->base,
        (DJUI_DEFAULT_PANEL_WIDTH *
            (configDjuiThemeCenter ? DJUI_THEME_CENTERED_WIDTH : 1)) - 64,
        300
    );
    djui_base_set_color(&text->base, 235, 235, 235, 255);
    djui_text_set_drop_shadow(text, 32, 32, 32, 180);
    djui_text_set_alignment(text, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_tutorial_start(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Getting Started",
        "Move with the selected movement stick. Your movement direction can follow the headset or either controller in Camera Settings. Jump, crouch, punch, and pause use the bindings shown in Controller Settings. Turn physically or use your configured camera controls. Recenter from Camera Settings whenever your forward direction or seated height needs correction."
    );
}

static void djui_panel_vr_tutorial_physical(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Hands & Physical Actions",
        "Hold a Grip button to make a fist, then swing that fist to punch enemies and break valid blocks. You must make a fist for a physical punch. Close a hand with Grip to grab supported objects; release to drop or throw using your real hand velocity. Head and hand colliders can collect stars, coins, caps, and 1-Ups where supported. Physical hand collision stops gloves at solid geometry unless an exception is enabled."
    );
}

static void djui_panel_vr_tutorial_troubleshooting(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Common Bugs & Fixes",
        "If performance is lower than expected after entering a course or loading content, fully restart the game; this commonly clears temporary performance or visual issues. Recenter if height or forward direction looks wrong. If menu text appears blurry or a mod leaves visual artifacts, restarting can also restore the normal display. Disable recently enabled gameplay or HUD mods when an issue only appears with those mods active."
    );
}

static void djui_panel_vr_tutorial_climbing(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Climbing & Movement",
        "Hold Grip as a hand reaches a pole, tree, or hangable ceiling. Pull your body by moving that hand, then alternate hands for monkey-bar movement. Let go with both hands to fall; swing and release for a momentum jump when enabled. To climb a ledge, move the headset over its top and release. Standard Climbing and Physical Climbing can be enabled separately; Climb Any Wall or Ceiling is a cheat."
    );
}

static void djui_panel_vr_tutorial_water(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Swimming, Flying & Caps",
        "Swim and fly in the headset's look direction, including straight up or down. Wing Cap flight retains normal momentum unless Free Fly is enabled. Grab removable caps with Trigger near the cap, throw them like physics objects, or release one over Mario's head to put it back on. Shaking Hat Gives Wing Cap requires Grab Cap at Any Time. Underwater head and hand colliders collect supported items."
    );
}

static void djui_panel_vr_tutorial_bowser(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Objects, Bosses & Bowser",
        "Grabbable NPCs and enemies use Grip; large enemies may require their valid rear grab area. King Bob-omb and other scripted pickups keep their original rules. Hold Bowser's tail with one or both hands, build the normal spin momentum, then release the grabbing hand or both hands to throw in the real swing direction. Taking damage drops held objects when the original game requires it."
    );
}

static void djui_panel_vr_tutorial_ui(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Menus, HUD & Multiplayer",
        "Pause opens the in-game menu; B backs out of supported menus. HUD Settings controls opacity, spread, and whether menus and the HUD attach independently to the headset or either hand. Select a text field to open the VR keyboard; Enter confirms. Chat and player lists are available from the online menus. Public standalone lobbies target compatible Android/Quest clients; direct connections can work with matching PC builds."
    );
}

static void djui_panel_vr_tutorial_fire_flower(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Fire Flower",
        "Make a right fist, hold Grip and Trigger to charge a fireball, then swing and release to throw it. The Fire Flower palette and timer end when the power expires or you leave the area. Spawn and timer cheats are available from the VR Cheats menu."
    );
}

static void djui_panel_vr_tutorial_hammer_suit(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Hammer Suit",
        "Make a right fist, then hold Grip and Trigger to grow a hammer in the glove. The hammer head can strike valid targets while held. After it finishes charging, swing and release to throw a three-hammer volley along your real throw direction."
    );
}

static void djui_panel_vr_tutorial_rasengan(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Rasengan",
        "Hold right Trigger with an open right hand. Grip with the empty left hand and circle it around the right hand until the Rasengan finishes charging. Keep right Trigger held, then touch a valid enemy with the sphere."
    );
}

static void djui_panel_vr_tutorial_rasen_shuriken(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Rasen-Shuriken",
        "Begin with a charged Rasengan, hold the Special button, and keep it above the headset while it charges. Once ready, keep holding right Trigger, swing the right hand, and release to throw it. It explodes when it reaches an enemy or solid surface."
    );
}

static void djui_panel_vr_tutorial_moves(
    struct DjuiBase* caller
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Special Moves", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "Fire Flower", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_fire_flower);
    djui_button_create(body, "Hammer Suit", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_hammer_suit);
    djui_button_create(body, "Rasengan", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_rasengan);
    djui_button_create(body, "Rasen-Shuriken", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_rasen_shuriken);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_tutorial_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("VR Tutorial", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "Getting Started", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_start);
    djui_button_create(body, "Hands & Physical Actions", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_physical);
    djui_button_create(body, "Climbing & Movement", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_climbing);
    djui_button_create(body, "Swimming, Flying & Caps", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_water);
    djui_button_create(body, "Objects, Bosses & Bowser", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_bowser);
    djui_button_create(body, "Menus, HUD & Multiplayer", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_ui);
    djui_button_create(body, "Special Moves", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_moves);
    djui_button_create(body, "Common Bugs & Fixes", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_troubleshooting);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_setup_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("VR Setup", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "Camera Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_camera_settings_create);
    djui_button_create(body, "Controller Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_controller_settings_create);
    djui_button_create(body, "Motion Control Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_motion_control_settings_create);
    djui_button_create(body, "Model Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_model_settings_create);
    djui_button_create(body, "Immersion", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_immersion_create);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_display_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Display", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "Performance", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_performance_create);
    djui_button_create(body, "HUD Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_hud_settings_create);
    djui_button_create(body, "Effects", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_effects_create);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

static void djui_panel_vr_special_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Special", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_button_create(body, "Experimental", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_experimental_create);
    djui_button_create(body, "Special Moves", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_special_moves_create);
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
            "Tutorial",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_tutorial_create
        );

        djui_button_create(
            body,
            "VR Setup",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_setup_create
        );

        djui_button_create(
            body,
            "Display",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_display_create
        );

        djui_button_create(
            body,
            "Special",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_special_create
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
