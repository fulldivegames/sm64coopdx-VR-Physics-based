#include <stdbool.h>
#include <stdio.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_flow_layout.h"
#include "djui_palette_colors.h"
#include "djui_tutorial_pages.h"

#include "pc/configfile.h"
#include "pc/controller/controller_api.h"
#include "pc/controller/controller_vr.h"
#include "pc/vr/vr.h"
#include "pc/network/coopnet/coopnet.h"
#include "data/dynos.c.h"
#include "game/rendering_graph_node.h"
#include "game/vr_hand_interaction.h"
#include "game/vr_speedrun.h"
#include "pc/fs/fs.h"
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
    if (!vr_gameplay_modifiers_allowed()) {
        return;
    }
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

static unsigned int* sVrBindingTargets[] = {
    &configVrJumpBinding,
    &configVrAttackBinding,
    &configVrCrouchBinding,
    &configVrLBinding,
    &configVrRBinding,
    &configVrPauseBinding,
    &configVrSpecialBinding,
    &configVrSplitBinding
};

static const char* sVrBindingLabels[] = {
    "Jump",
    "Attack / Interact",
    "Crouch",
    "L Button",
    "R Button",
    "Pause",
    "Special Button",
    "Timer Start / Split"
};

static struct DjuiButton* sVrBindingCaptureButton = NULL;
static unsigned int* sVrBindingCaptureTarget = NULL;
static struct DjuiText* sVrBindingValueText[
    sizeof(sVrBindingTargets) / sizeof(sVrBindingTargets[0])
] = { NULL };

static const char* djui_panel_vr_binding_choice(unsigned int value) {
    return value < VR_CONTROLLER_BINDING_COUNT
        ? sVrControllerBindingChoices[value]
        : sVrControllerBindingChoices[VR_CONTROLLER_BINDING_DISABLED];
}

static void djui_panel_vr_binding_set_value_text(
    struct DjuiText* text,
    unsigned int action,
    unsigned int value
) {
    char message[128];
    snprintf(
        message,
        sizeof(message),
        "%s: %s",
        sVrBindingLabels[action],
        djui_panel_vr_binding_choice(value)
    );
    djui_text_set_text(text, message);
}

static void djui_panel_vr_binding_on_click(struct DjuiBase* caller) {
    struct DjuiButton* button = (struct DjuiButton*)caller;
    const unsigned int action = (unsigned int)button->base.tag;
    if (action >= sizeof(sVrBindingTargets) / sizeof(sVrBindingTargets[0])) {
        return;
    }

    sVrBindingCaptureButton = button;
    sVrBindingCaptureTarget = sVrBindingTargets[action];
    djui_text_set_text(button->text, "Press a controller button...");
    // Consume the click that opened the row. The next edge is the binding.
    controller_get_raw_key();
    djui_interactable_set_binding(caller);
}

static void djui_panel_vr_binding_on_bind(struct DjuiBase* caller) {
    (void)caller;
    if (sVrBindingCaptureButton == NULL ||
        sVrBindingCaptureTarget == NULL) {
        djui_interactable_set_binding(NULL);
        return;
    }

    const u32 key = controller_get_raw_key();
    if (key == VK_INVALID ||
        key < VK_BASE_VR ||
        key >= VK_BASE_VR + VR_CONTROLLER_BINDING_COUNT) {
        return;
    }

    const unsigned int action = (unsigned int)sVrBindingCaptureButton->base.tag;
    const unsigned int binding = key - VK_BASE_VR;
    *sVrBindingCaptureTarget = binding;
    if (sVrBindingValueText[action] != NULL) {
        djui_panel_vr_binding_set_value_text(
            sVrBindingValueText[action], action, binding
        );
    }
    djui_text_set_text(sVrBindingCaptureButton->text, "Bind");
    sVrBindingCaptureButton = NULL;
    sVrBindingCaptureTarget = NULL;
    djui_interactable_set_binding(NULL);
    controller_reconfigure();
}

static struct DjuiButton* djui_panel_vr_binding_create(
    struct DjuiBase* parent,
    unsigned int action
) {
    struct DjuiFlowLayout* row = djui_flow_layout_create(parent);
    djui_base_set_size_type(&row->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&row->base, 1.0f, 52.0f);
    djui_base_set_color(&row->base, 0, 0, 0, 0);
    djui_flow_layout_set_flow_direction(row, DJUI_FLOW_DIR_RIGHT);
    djui_flow_layout_set_margin(row, 4.0f);

    struct DjuiText* valueText = djui_text_create(&row->base, "");
    djui_base_set_size_type(&valueText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&valueText->base, 0.65f, 52.0f);
    djui_base_set_color(&valueText->base, 220, 220, 220, 255);
    djui_base_set_alignment(&valueText->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_CENTER);
    sVrBindingValueText[action] = valueText;
    djui_panel_vr_binding_set_value_text(
        valueText, action, *sVrBindingTargets[action]
    );

    struct DjuiButton* button = djui_button_create(
        &row->base,
        "Bind",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_binding_on_click
    );
    djui_base_set_size_type(&button->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&button->base, 0.30f, 52.0f);
    button->base.tag = (s64)action;
    djui_interactable_hook_bind(
        &button->base,
        djui_panel_vr_binding_on_bind
    );
    djui_interactable_set_navigation_axis_free(&button->base, true);
    return button;
}
static void djui_panel_vr_spawn_fire_flower(struct DjuiBase* caller) {
    (void)caller;
    vr_special_moves_spawn_cheat_fire_flower();
}

static void djui_panel_vr_spawn_hammer_suit(struct DjuiBase* caller) {
    (void)caller;
    vr_special_moves_spawn_cheat_hammer_suit();
}

static void djui_panel_vr_spawn_sonic_shoes(struct DjuiBase* caller) {
    (void)caller;
    vr_special_moves_spawn_cheat_sonic_shoes();
}

static void djui_panel_vr_spawn_big_hands(struct DjuiBase* caller) {
    (void)caller;
    vr_special_moves_spawn_cheat_big_hands();
}

static void djui_panel_vr_spawn_propeller(UNUSED struct DjuiBase* caller) {
    vr_special_moves_spawn_cheat_propeller();
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

static void djui_panel_vr_spawn_power_star(UNUSED struct DjuiBase* caller) {
    vr_special_moves_spawn_cheat_power_star();
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
    djui_button_create(body, "Hammer Suit", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_hammer_suit);
    djui_button_create(body, "Propeller Mushroom", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_propeller);
    djui_button_create(body, "Power Star", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_power_star);
    djui_button_create(body, "Sonic Shoes", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_sonic_shoes);
    djui_button_create(body, "Big Hands", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_big_hands);
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
    configVrBrightness = VR_BRIGHTNESS_DEFAULT;

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

static void djui_panel_vr_experimental_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrExperimentalFlatFirstPerson = false;
    configVrExperimentalTrueFirstPerson = false;
    configVrExperimentalTrueDiving = false;
    configVrExperimentalArmsMode = false;
    configVrExperimentalClimbableColliders = false;
    configVrOriginalMarioMovement = false;
    configVrDisablePunchSound = false;
    configVrSpeedRunningMode = false;
    configVrVanillaMovement = false;
    configVrBackpedalSpeed = VR_BACKPEDAL_SPEED_DEFAULT;
    configVrImmersiveFlipBillboards = false;
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
        VR_CONTROLLER_BINDING_LEFT_STICK_CLICK;
    configVrSpecialBinding =
        VR_CONTROLLER_BINDING_LEFT_SECONDARY;
    configVrSplitBinding = VR_CONTROLLER_BINDING_DISABLED;
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
    configVrHudSpread = 100;
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
    configVrSpecialMovesEnabled = true;
    configVrSpecialFireFlower = true;
    configVrSpecialFireFlowerMusic = true;
    configVrAlternatePowerUpMusic = false;
    configVrSpecialHammerSuit = true;
    configVrSpecialSonicShoes = true;
    configVrBigHandsLongTimer = false;
    configVrPowerStarLongTimer = false;
    configVrSonicShoesSpeed = VR_SONIC_SHOES_SPEED_DEFAULT;
    configVrBigHandsReach = VR_BIG_HANDS_REACH_DEFAULT;
    configVrSpecialRasengan = true;
}
static unsigned int sSpawnWeightDraft[6];
static void djui_panel_vr_spawn_pool_apply(UNUSED struct DjuiBase* caller) {
    configVrSpawnWeightFireFlower = sSpawnWeightDraft[0];
    configVrSpawnWeightHammerSuit = sSpawnWeightDraft[1];
    configVrSpawnWeightSonicShoes = sSpawnWeightDraft[2];
    configVrSpawnWeightBigHands = sSpawnWeightDraft[3];
    configVrSpawnWeightPropeller = sSpawnWeightDraft[4];
    configVrSpawnWeightPowerStar = sSpawnWeightDraft[5];
    configfile_save(configfile_name());
}
static void djui_panel_vr_spawn_pool_defaults(UNUSED struct DjuiBase* caller) {
    for (unsigned i = 0; i < 6; ++i) sSpawnWeightDraft[i] = 50;
    configVrSpawnPoolFireFlower = true;
    configVrSpawnPoolHammerSuit = true;
    configVrSpawnPoolSonicShoes = true;
    configVrSpawnPoolBigHands = true;
    configVrSpawnPoolPropeller = true;
    configVrSpawnPoolPowerStar = true;
    configVrSpecialRasengan = true;
}

static void djui_panel_vr_immersion_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrImmersiveCameraMotion = true;
    configVrImmersiveFaceStuck = true;
    configVrImmersiveCrushedScreen = true;
    configVrCrossedTreeBillboards = true;
    configVrImmersiveCannonCone = true;
    configVrImmersive3dSound = true;
    configVrImmersiveLedgeCamera = true;
    configVrImmersiveUnderwaterFilter = true;
    configVrImmersiveRemovableCap = false;
    configVrImmersiveLookDownTransparency = true;
    configVrImmersiveCarrySpeed = false;
    configVrImmersiveStarSpawnFocus = false;
    configVrImmersiveGhostPunchArm = true;
    configVrImmersiveMatchMarioHeight = false;
    configVrExperimentalSideFlipFollow = true;
    configVrExperimentalWallJumpTurn = true;
    configVrPhysicalCrouching = true;
    configVrPhysicalCrouchDepth = 10;
    configVrPhysicalJumping = true;
    configVrJumpUseTriggers = false;
    configVrPhysicalSwimming = true;
    configVrMovementOverhaul = false;
}

static void djui_panel_vr_effects_defaults(struct DjuiBase* caller) {
    (void)caller;

    configVrTwirlTornadoEffect = true;
    configVrNormalMaps = false;
    configVrNormalMapStrength = 500;
    configVrNormalMapGloss = 170;
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
            NULL
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
            "Flip Billboards",
            &configVrImmersiveFlipBillboards,
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
        &configVrPauseBinding,
        &configVrSpecialBinding,
        &configVrSplitBinding
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
        djui_panel_menu_create("Controller Bindings", false);

    struct DjuiBase* body =
        djui_three_panel_get_body(panel);

    {
        struct DjuiSelectionbox* movementStick = djui_selectionbox_create(
            body,
            "Movement",
            sVrControllerStickChoices,
            VR_CONTROLLER_STICK_COUNT,
            &configVrMoveStick,
            NULL
        );
        djui_interactable_set_navigation_axis_free(&movementStick->base, true);

        struct DjuiSelectionbox* cameraStick = djui_selectionbox_create(
            body,
            "Camera",
            sVrControllerStickChoices,
            VR_CONTROLLER_STICK_COUNT,
            &configVrCameraStick,
            NULL
        );
        djui_interactable_set_navigation_axis_free(&cameraStick->base, true);

        djui_panel_vr_binding_create(body, 0);

        djui_panel_vr_binding_create(body, 1);

        djui_panel_vr_binding_create(body, 2);

        djui_panel_vr_binding_create(body, 3);

        djui_panel_vr_binding_create(body, 4);

        djui_panel_vr_binding_create(body, 5);

        djui_panel_vr_binding_create(body, 6);
        djui_panel_vr_binding_create(body, 7);

        struct DjuiButton* defaultsButton = djui_button_create(
            body,
            "Set to Defaults",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_controller_defaults
        );
        djui_interactable_set_navigation_axis_free(&defaultsButton->base, true);

        struct DjuiButton* backButton = djui_button_create(
            body,
            DLANG(MENU, BACK),
            DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back
        );
        djui_interactable_set_navigation_axis_free(&backButton->base, true);
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

static void djui_vr_timer_action(struct DjuiBase* caller) {
    bool setupChanged = false;
    switch (caller->tag) {
        case 0: vr_speedrun_split(vr_speedrun_now()); break;
        case 1: vr_speedrun_pause(vr_speedrun_now()); break;
        case 2: vr_speedrun_reset(); break;
        case 3: vr_speedrun_configure(configVrSpeedrunSegments); setupChanged = true; break;
        case 4: setupChanged = vr_speedrun_load(fs_get_write_path("speedrun/setup.json")); break;
        case 5: setupChanged = vr_speedrun_import_lss(fs_get_write_path("speedrun/run.lss")); break;
        case 6: vr_speedrun_save(fs_get_write_path("speedrun/export.json")); break;
    }
    if (setupChanged) {
        configVrSpeedrunSegments = vr_speedrun_total();
        vr_speedrun_save_local();
    }
    djui_console_message_create(vr_speedrun_status(), CONSOLE_MESSAGE_INFO);
}

static void djui_vr_timer_help(struct DjuiBase* body, const char* message, f32 height) {
    struct DjuiText* text = djui_text_create(body, message);
    djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&text->base, 1, height);
    djui_text_set_font_scale(text, 22);
}

static void djui_vr_timer_name_changed(struct DjuiBase* caller) {
    struct DjuiInputbox* input = (struct DjuiInputbox*)caller;
    vr_speedrun_set_name(caller->tag, input->buffer);
}
static void (*sVrNamesDestroy)(struct DjuiBase*);
static void djui_vr_timer_names_destroy(struct DjuiBase* caller) {
    if (!vr_speedrun_save_local())
        djui_console_message_create(vr_speedrun_status(), CONSOLE_MESSAGE_INFO);
    sVrNamesDestroy(caller);
}
static void djui_vr_timer_names_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Name Your Splits", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    ((struct DjuiFlowLayout*)body)->orderedNavigation = true;
    sVrNamesDestroy = panel->base.destroy;
    panel->base.destroy = djui_vr_timer_names_destroy;
    for (unsigned int i = 0; i < vr_speedrun_total(); ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Split %u", i + 1);
        struct DjuiRect* row = djui_rect_container_create(body, 32);
        struct DjuiText* text = djui_text_create(&row->base, label);
        djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text->base, .22f, 32);
        struct DjuiInputbox* input = djui_inputbox_create(&row->base, 49);
        djui_base_set_size_type(&input->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&input->base, .75f, 32);
        djui_base_set_alignment(&input->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_TOP);
        input->base.tag = i;
        djui_inputbox_set_text(input, (char*)vr_speedrun_segment_name(i));
        djui_interactable_hook_value_change(&input->base, djui_vr_timer_name_changed);
    }
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}

static void djui_vr_timer_color_selected(struct DjuiBase* caller) {
    if (caller->tag < 0 || (u64)caller->tag >= sizeof(sPaletteQuickColors) / sizeof(sPaletteQuickColors[0])) return;
    const struct DjuiPaletteQuickColor* color = &sPaletteQuickColors[caller->tag];
    configVrSpeedrunColor = (color->r << 16) | (color->g << 8) | color->b;
    djui_panel_menu_back(caller);
}
static void djui_vr_timer_colors_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Timer Text Color", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    for (unsigned int i = 0; i < sizeof(sPaletteQuickColors) / sizeof(sPaletteQuickColors[0]); ++i) {
        const struct DjuiPaletteQuickColor* color = &sPaletteQuickColors[i];
        struct DjuiButton* button = djui_button_create(body, color->name, DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_color_selected);
        button->base.tag = i;
        struct DjuiRect* swatch = djui_rect_create(&button->rect->base);
        djui_base_set_size(&swatch->base, 24, 24);
        djui_base_set_alignment(&swatch->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_CENTER);
        djui_base_set_location(&swatch->base, -8, 0);
        djui_base_set_color(&swatch->base, color->r, color->g, color->b, 255);
        djui_base_set_border_width(&swatch->base, 1);
        djui_base_set_border_color(&swatch->base, 230, 230, 230, 255);
    }
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}
static void djui_vr_timer_files_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Import / Export Setups", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    djui_vr_timer_help(body, "Optional: copy files into the speedrun folder beside mods and saves.", 60);
    djui_vr_timer_help(body, "Load: use a copied setup.json. Import: use LiveSplit run.lss names and PB times. Both reset this run.", 80);
    djui_vr_timer_help(body, "Export: save names and results to export.json for another device. Rename an old export before saving again.", 80);
    const char* labels[] = {"Load Copied Setup (setup.json)", "Import LiveSplit File (run.lss)", "Export Setup and Results"};
    for (unsigned int i = 0; i < 3; ++i) {
        struct DjuiButton* button = djui_button_create(body, labels[i], DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_action);
        button->base.tag = i + 4;
    }
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}
static void djui_vr_timer_history_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("All Split Results", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    for (unsigned int i = 0; i < vr_speedrun_total(); ++i) {
        char line[100];
        double actual = vr_speedrun_segment_time(i);
        if (actual < 0) snprintf(line, sizeof(line), "%u. %s - not completed", i + 1, vr_speedrun_segment_name(i));
        else snprintf(line, sizeof(line), "%u. %s - %.2f seconds", i + 1, vr_speedrun_segment_name(i), actual);
        // A focusable read-only row lets controller users scroll every result.
        struct DjuiButton* result = djui_button_create(body, line, DJUI_BUTTON_STYLE_NORMAL, NULL);
        djui_base_set_size(&result->base, 1, 72);
        djui_text_set_font_scale(result->text, 22);
    }
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}
static void djui_vr_timer_reset_viewer(UNUSED struct DjuiBase* caller) {
    configVrSpeedrunX = 180;
    configVrSpeedrunY = 20;
    configVrSpeedrunScale = 100;
    configVrSpeedrunHud = true;
}
static void djui_panel_vr_timer_create(struct DjuiBase* caller) {
    fs_sys_mkdir(fs_get_write_path("speedrun"));
    vr_speedrun_initialize(fs_get_write_path("speedrun/setup.json"), configVrSpeedrunSegments);
    configVrSpeedrunSegments = vr_speedrun_total();
    struct DjuiThreePanel* panel = djui_panel_menu_create("Speedrunning", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    djui_checkbox_create(body, "Speed Running Mode (Vanilla Game)", &configVrSpeedRunningMode, NULL);
    djui_checkbox_create(body, "Vanilla Movement (Restrictive Jumps)", &configVrVanillaMovement, NULL);
    djui_vr_timer_help(body, "Vanilla Game overrides VR cheats, extra power-ups and speed multipliers without changing saved options. VR controls and normal caps remain. Restrictive Jumps disables instant landing turns independently. Other mods are not disabled.", 120);
    configVrSpeedrunScale = djui_panel_vr_clamp_uint(configVrSpeedrunScale, 50, 200);
    configVrSpeedrunX = djui_panel_vr_clamp_uint(configVrSpeedrunX, 0, 600);
    configVrSpeedrunY = djui_panel_vr_clamp_uint(configVrSpeedrunY, 0, 210);
    djui_vr_timer_help(body, "Choose your split count, then Confirm to apply it and reset the timer. Existing split names are kept.", 80);
    djui_slider_create(body, "Split Count", &configVrSpeedrunSegments, 1, 120, NULL);
    struct DjuiButton* confirm = djui_button_create(body, "Confirm", DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_action);
    confirm->base.tag = 3;
    djui_button_create(body, "Name Your Splits", DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_names_create);
    djui_checkbox_create(body, "Show Timer and Splits", &configVrSpeedrunHud, NULL);
    djui_checkbox_create(body, "Reveal Splits as You Go", &configVrSpeedrunProgressive, NULL);
    djui_slider_create(body, "Viewer Size (%)", &configVrSpeedrunScale, 50, 200, NULL);
    djui_slider_create(body, "Horizontal Position", &configVrSpeedrunX, 0, 600, NULL);
    djui_slider_create(body, "Vertical Position", &configVrSpeedrunY, 0, 210, NULL);
    djui_button_create(body, "Reset Viewer Position", DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_reset_viewer);
    djui_button_create(body, "Text Color (Palette Colors)", DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_colors_create);
    const char* actions[] = {"Start / Split", "Pause / Resume", "Reset Timer"};
    for (unsigned int i = 0; i < 3; ++i) {
        struct DjuiButton* button = djui_button_create(body, actions[i], DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_action);
        button->base.tag = i;
    }
    djui_button_create(body, "All Split Results", DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_history_create);
    djui_button_create(body, "Import / Export Setups", DJUI_BUTTON_STYLE_NORMAL, djui_vr_timer_files_create);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
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
            "Left Hand",
            "Right Hand"
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
        djui_checkbox_create(
            body,
            "Enable Special Moves",
            &configVrSpecialMovesEnabled,
            NULL
        );
        if (!vr_gameplay_modifiers_allowed()) {
            struct DjuiText* warning = djui_text_create(
                body,
                configVrSpeedRunningMode
                    ? "Speed Running Mode overrides cheats and extra power-ups. Your saved options are unchanged."
                    : "Cheats and power-ups are disabled in Standard Public Lobbies for compatibility and fair play."
            );
            djui_base_set_size_type(
                &warning->base,
                DJUI_SVT_RELATIVE,
                DJUI_SVT_ABSOLUTE
            );
            djui_base_set_size(&warning->base, 1.0f, 120.0f);
            djui_base_set_color(&warning->base, 255, 210, 120, 255);
            djui_text_set_alignment(
                warning,
                DJUI_HALIGN_CENTER,
                DJUI_VALIGN_CENTER
            );
            djui_button_create(
                body,
                DLANG(MENU, BACK),
                DJUI_BUTTON_STYLE_BACK,
                djui_panel_menu_back
            );
            djui_panel_add(caller, panel, NULL);
            return;
        }

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

static void djui_panel_vr_spawn_heading(struct DjuiBase* body, const char* message) {
    struct DjuiText* heading = djui_text_create(body, message);
    djui_base_set_size_type(&heading->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&heading->base, 1.0f, 40.0f);
    djui_base_set_color(&heading->base, 255, 220, 120, 255);
    djui_text_set_alignment(heading, DJUI_HALIGN_LEFT, DJUI_VALIGN_CENTER);
}

static void djui_panel_vr_spawn_pool_create(struct DjuiBase* caller) {
    sSpawnWeightDraft[0] = configVrSpawnWeightFireFlower;
    sSpawnWeightDraft[1] = configVrSpawnWeightHammerSuit;
    sSpawnWeightDraft[2] = configVrSpawnWeightSonicShoes;
    sSpawnWeightDraft[3] = configVrSpawnWeightBigHands;
    sSpawnWeightDraft[4] = configVrSpawnWeightPropeller;
    sSpawnWeightDraft[5] = configVrSpawnWeightPowerStar;
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Power-Up Spawn/Enable", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    djui_panel_vr_spawn_heading(body, "Power-Ups");
    djui_checkbox_create(
        body,
        "Fire Flower",
        &configVrSpawnPoolFireFlower,
        NULL
    );
    djui_slider_create(body, "Spawn Weight", &sSpawnWeightDraft[0], 1, 100, NULL);
    djui_checkbox_create(
        body,
        "Hammer Suit",
        &configVrSpawnPoolHammerSuit,
        NULL
    );
    djui_slider_create(body, "Spawn Weight", &sSpawnWeightDraft[1], 1, 100, NULL);
    djui_checkbox_create(body, "Propeller Mushroom", &configVrSpawnPoolPropeller, NULL);
    djui_slider_create(body, "Spawn Weight", &sSpawnWeightDraft[4], 1, 100, NULL);
    djui_checkbox_create(
        body,
        "Sonic Shoes",
        &configVrSpawnPoolSonicShoes,
        NULL
    );
    djui_slider_create(body, "Spawn Weight", &sSpawnWeightDraft[2], 1, 100, NULL);
    djui_panel_vr_spawn_heading(body, "Specials - Half Spawn Weight");
    djui_checkbox_create(
        body,
        "Big Hands",
        &configVrSpawnPoolBigHands,
        NULL
    );
    djui_slider_create(body, "Spawn Weight (Special)", &sSpawnWeightDraft[3], 1, 100, NULL);
    djui_checkbox_create(body, "Power Star", &configVrSpawnPoolPowerStar, NULL);
    djui_slider_create(body, "Spawn Weight (Special)", &sSpawnWeightDraft[5], 1, 100, NULL);
    djui_panel_vr_spawn_heading(body, "Gestures");
    djui_checkbox_create(
        body,
        "Rasengan / Rasen-Shuriken",
        &configVrSpecialRasengan,
        NULL
    );
    djui_button_create(
        body,
        "Set to Defaults",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_spawn_pool_defaults
    );
    djui_button_create(
        body,
        DLANG(MENU, BACK),
        DJUI_BUTTON_STYLE_BACK,
        djui_panel_menu_back
    );
    djui_panel_add(caller, panel, NULL)->on_panel_destroy = djui_panel_vr_spawn_pool_apply;
}

static void djui_panel_vr_special_moves_create(
    struct DjuiBase* caller
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Special Moves", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);

    {
        if (!vr_gameplay_modifiers_allowed()) {
            struct DjuiText* warning = djui_text_create(
                body,
                configVrSpeedRunningMode
                    ? "Speed Running Mode overrides Special Moves. Your saved options are unchanged."
                    : "Special Moves are disabled in Standard Public Lobbies for compatibility and fair play."
            );
            djui_base_set_size_type(
                &warning->base,
                DJUI_SVT_RELATIVE,
                DJUI_SVT_ABSOLUTE
            );
            djui_base_set_size(&warning->base, 1.0f, 120.0f);
            djui_base_set_color(&warning->base, 255, 210, 120, 255);
            djui_text_set_alignment(
                warning,
                DJUI_HALIGN_CENTER,
                DJUI_VALIGN_CENTER
            );
            djui_button_create(
                body,
                DLANG(MENU, BACK),
                DJUI_BUTTON_STYLE_BACK,
                djui_panel_menu_back
            );
            djui_panel_add(caller, panel, NULL);
            return;
        }

        djui_checkbox_create(
            body,
            "Power" "-" "Up Music",
            &configVrSpecialFireFlowerMusic,
            NULL
        );

        djui_checkbox_create(
            body,
            "Alternate Power-Up Music",
            &configVrAlternatePowerUpMusic,
            NULL
        );
        configVrSonicShoesSpeed = djui_panel_vr_clamp_uint(
            configVrSonicShoesSpeed,
            VR_SONIC_SHOES_SPEED_MIN,
            VR_SONIC_SHOES_SPEED_MAX
        );
        djui_slider_create(
            body,
            "Sonic Shoes Speed Multiplier",
            &configVrSonicShoesSpeed,
            VR_SONIC_SHOES_SPEED_MIN,
            VR_SONIC_SHOES_SPEED_MAX,
            NULL
        );
        djui_checkbox_create(
            body,
            "Big Hands 60-Second Timer",
            &configVrBigHandsLongTimer,
            NULL
        );
        djui_checkbox_create(body, "Power Star 60-Second Timer", &configVrPowerStarLongTimer, NULL);
        djui_button_create(
            body,
            "Power-Up Spawn/Enable",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_spawn_pool_create
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
            "Mario Crushed Screen",
            &configVrImmersiveCrushedScreen,
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
        "Physical Crouching / Ground Pounds",
        &configVrPhysicalCrouching,
        NULL
    );
    configVrPhysicalCrouchDepth = djui_panel_vr_clamp_uint(configVrPhysicalCrouchDepth, 10U, 50U);
    djui_slider_create(body, "Physical Crouch Depth", &configVrPhysicalCrouchDepth, 10, 50, NULL);
    djui_checkbox_create(body, "Physical Jumping", &configVrPhysicalJumping, NULL);
    djui_checkbox_create(body, "Disable Punch Sound", &configVrDisablePunchSound, NULL);
    djui_checkbox_create(body, "Use Triggers Instead of Grips", &configVrJumpUseTriggers, NULL);
    djui_vr_timer_help(body, "Must change crouch binding when enabled.", 32);
    djui_checkbox_create(body, "Physical Swimming", &configVrPhysicalSwimming, NULL);
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
    djui_checkbox_create(
        body,
        "Match Multiplayer Mario Height",
        &configVrImmersiveMatchMarioHeight,
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

    djui_checkbox_create(body, "Crossed Tree Billboards",
                         &configVrCrossedTreeBillboards, NULL);

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

    if (configVrNormalMapStrength > 800) {
        configVrNormalMapStrength = 800;
    }
    if (configVrNormalMapGloss > 400) {
        configVrNormalMapGloss = 400;
    }

    {
        djui_checkbox_create(
            body,
            "Twirl Tornado Effect",
            &configVrTwirlTornadoEffect,
            NULL
        );
        djui_checkbox_create(
            body,
            "Add Normal Maps",
            &configVrNormalMaps,
            NULL
        );
        djui_slider_create(body, "Normal Map Strength (%)", &configVrNormalMapStrength, 0, 800, NULL);
        djui_slider_create(body, "Normal Map Gloss (%)", &configVrNormalMapGloss, 0, 400, NULL);
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

static const char* sVrTutorialMessage;
static size_t sVrTutorialOffset;
static struct DjuiText* sVrTutorialText;
static struct DjuiText* sVrTutorialPageLabel;

static void djui_panel_vr_tutorial_refresh(void) {
    size_t end = djui_tutorial_next(sVrTutorialMessage, sVrTutorialOffset);
    char page[512], label[64];
    djui_tutorial_format(page, sizeof(page), sVrTutorialMessage, sVrTutorialOffset, end);
    djui_text_set_text(sVrTutorialText, page);
    unsigned current = 1, total = 1;
    size_t offset = 0, length = strlen(sVrTutorialMessage);
    while ((offset = djui_tutorial_next(sVrTutorialMessage, offset)) < length) {
        while (sVrTutorialMessage[offset] == ' ') ++offset;
        ++total;
        if (offset <= sVrTutorialOffset) ++current;
    }
    snprintf(label, sizeof(label), "Page %u of %u", current, total);
    djui_text_set_text(sVrTutorialPageLabel, label);
}
static void djui_panel_vr_tutorial_next(UNUSED struct DjuiBase* caller) {
    size_t next = djui_tutorial_next(sVrTutorialMessage, sVrTutorialOffset);
    while (sVrTutorialMessage[next] == ' ') ++next;
    if (sVrTutorialMessage[next]) sVrTutorialOffset = next;
    djui_panel_vr_tutorial_refresh();
}
static void djui_panel_vr_tutorial_previous(UNUSED struct DjuiBase* caller) {
    size_t previous = 0, offset = 0;
    while (offset < sVrTutorialOffset) {
        previous = offset;
        offset = djui_tutorial_next(sVrTutorialMessage, offset);
        while (sVrTutorialMessage[offset] == ' ') ++offset;
    }
    sVrTutorialOffset = previous;
    djui_panel_vr_tutorial_refresh();
}

static void djui_panel_vr_tutorial_page(
    struct DjuiBase* caller,
    char* title,
    const char* message
) {
    struct DjuiThreePanel* panel =
        djui_panel_menu_create(title, false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    sVrTutorialMessage = message;
    sVrTutorialOffset = 0;
    sVrTutorialPageLabel = djui_text_create(body, "");
    djui_base_set_size(&sVrTutorialPageLabel->base, DJUI_DEFAULT_PANEL_WIDTH - 64, 35);
    struct DjuiText* text = djui_text_create(body, "");
    sVrTutorialText = text;
    djui_base_set_location(&text->base, 12, 0);
    djui_base_set_size(
        &text->base,
        (DJUI_DEFAULT_PANEL_WIDTH *
            (configDjuiThemeCenter ? DJUI_THEME_CENTERED_WIDTH : 1)) - 88,
        300
    );
    djui_base_set_color(&text->base, 235, 235, 235, 255);
    djui_text_set_drop_shadow(text, 32, 32, 32, 180);
    djui_text_set_alignment(text, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
    djui_panel_vr_tutorial_refresh();
    djui_button_create(body, "Previous Page", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_previous);
    djui_button_create(body, "Next Page", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_next);
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
        "Move with the selected movement stick. Your movement direction can follow the headset or either controller in Camera Settings. Jump, crouch, punch, and pause use the bindings shown in Controller Bindings. Turn physically or use your configured camera controls. Recenter from Camera Settings whenever your forward direction or seated height needs correction."
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
        "Hold Grip as a hand reaches a pole, tree, or hangable ceiling. Pull your body by moving that hand, then alternate hands. Let go to fall; swing and release for a momentum jump when enabled. A hard swing gives full jump-off ascent; a soft swing gives a shorter release. Each hand attaches independently, including surface-to-tree handoffs with Big Hands or Free Climb. Move the headset over a ledge and release to climb it. Standard and Physical Climbing have separate switches."
    );
}

static void djui_panel_vr_tutorial_water(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Swimming, Flying & Caps",
        "Button swimming and Wing Cap flight retain their normal VR steering. Optional Physical Swimming uses strokes with headset aiming and an overhead upward-swim gesture; see its tutorial page. Wing Cap flight retains momentum unless Free Fly is enabled. With Grab Cap at Any Time enabled, hold Grip and Trigger near your head to take your hat; either button can be pressed first. Release to throw, or return it over your head. Shaking Hat Gives Wing Cap requires this option. A red X on the life icon marks a missing hat."
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
        "Pause defaults to left stick click; B backs out of supported menus. Controller Bindings waits for your chosen input. Right-primary still confirms menus when Jump is remapped. HUD Settings controls opacity, spread and attachment. Select a text field for the VR keyboard; Enter confirms. Mic starts listening; press it again to finish and transcribe. Closing the keyboard cancels. Chat and player lists are in the online menus. Use matching builds for multiplayer; special powers are disabled in Regular Public Lobbies."
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

static void djui_panel_vr_tutorial_sonic_shoes(
    struct DjuiBase* caller
) {
    djui_panel_vr_tutorial_page(
        caller,
        "Sonic Shoes",
        "Collect the Sonic Shoes from eligible item boxes or the Spawn Menu. For 60 seconds Mario wears the powered shoes, Green Hill Zone plays, and stars trail closely around his boots and legs."
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

static void djui_panel_vr_tutorial_big_hands(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Big Hands",
        "Big Hands extends grabbing and punching to the visible enlarged hands. Hold Grip at a surface and pull to climb; alternate hands between surfaces, trees and hangables. Release Grip to detach. Lasts 30 seconds, or 60 with Big Hands 60-Second Timer enabled before pickup. This stronger power has half the box-spawn weight of ordinary powers at equal slider values, even with the longer timer. Its enable checkbox and Spawn Menu still work normally.");
}

static void djui_panel_vr_tutorial_power_star(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Power Star",
        "Collect the small bouncing star with your body, headset or hand. It grants enemy-contact invincibility, 1.5x running speed and 1.5x jump height, with rainbow shimmer and a torso sparkle trail. Its tint and music fade during the last four seconds; abilities last until the timer ends. Touching enemies deals an attack; one-hit enemies are defeated and Chain Chomps explode. Pits, scripted deaths and course boundaries still apply. Lasts 30 seconds; enable Power Star 60-Second Timer before pickup for 60. Find Power Star and Big Hands together under Specials in Power-Up Spawn/Enable: both have half the ordinary spawn weight at equal settings. Another power-up replaces it. Power Star and Sonic Shoes keep their dedicated songs regardless of Alternate Power-Up Music. Native caps keep their original cap themes.");
}

static void djui_panel_vr_tutorial_swimming(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Physical Swimming",
        "On by default for new settings; toggle in VR > Immersion > Movement & Body. No buttons needed: reach away from your upper torso, then pull water toward you. Reach/recovery adds no thrust. Normal strokes follow headset aim; look down to dive. For upward swimming while looking forward, raise your hands overhead and pull down within 20 degrees of vertical. Either hand, alternating hands and both hands work, including surface strokes. Button swimming remains available. Existing saved choices are preserved.");
}

static void djui_panel_vr_tutorial_jump_crouch(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Physical Jumping & Crouching",
        "The first 0.9.2 launch enables physical actions once, except crouching. You can disable them again; later launches retain your choice. Hold Grip and punch within 15 degrees of straight up. Either/both hands work; keep one raised to hold Jump, lower/release to shorten it. Lower and punch again for double/triple jumps; a 120 ms landing buffer helps timing. The gesture also wall-kicks during the normal wall-kick window: keep the triggering fist raised for full height, or lower/release for a shorter kick. Use Triggers Instead of Grips is optional: change your crouch binding first. See Physical Crouching & Diving for the other gestures.");
}

static void djui_panel_vr_tutorial_crouch_dive(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Physical Crouching & Diving",
        "Immersion > Movement & Body contains Physical Crouching / Ground Pounds, on by default. Crouch Depth defaults to 10% of calibrated height, adjustable from 10 to 50. Head-tilt compensation reduces accidental crouching when looking down. Recenter while standing normally. Airborne crouching requests a ground pound (Propeller uses its held drill instead). For motion diving, punch both fists forward: air dives need 20 cm at 1.65 m/s, ground dives need 13 cm at 1.20 m/s. Headset yaw sets forward, not pitch. Jump gestures take priority. No extra settle timer. Ground and air motion dives have separate switches.");
}

static void djui_panel_vr_tutorial_speedrun(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Speedrunning",
        "Open VR > Speedrunning. Choose Split Count and Confirm, then Name Your Splits. Enable Show Timer and Splits and adjust position, size, and text color. Reveal Splits as You Go adds each next row after a split. Bind Timer Start / Split in Controller Bindings: first press starts, later presses split. Import / Export Setups explains copying LiveSplit run.lss files and portable setups between PC and Quest.");
}

static void djui_panel_vr_tutorial_visuals(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Visuals & Character Select",
        "Special > Filters includes Cell Shaded, Super Mario Land, Game Boy, and Virtual Boy. Display > Effects offers optional normal maps with separate strength and gloss controls. Immersion's cross-tree option keeps tree billboards fixed in a cross shape. Character Select opens its preview and controls together on a floating theater panel. Close it to return to the regular menus.");
}

static void djui_panel_vr_tutorial_propeller(struct DjuiBase* caller) {
    djui_panel_vr_tutorial_page(caller, "Propeller Mushroom",
        "Lasts 60 seconds. Press Jump again or make another Physical Jump gesture during any normal airtime for one burst, including tiny hops, water exits, slope jumps and lava rebounds. Ground/slope contact, water entry and lava bounces recharge it without extending the timer. No minimum jump height is required. A top stomp on a normally stompable enemy relaunches you at full strength; side hits and Bullies do not. Hold Crouch (or physically crouch) for a fast descent with an opaque tornado spinning three times faster. Release to glide. Ordinary twirling has no crouch drill. Edit/export the Propeller palette: Cap colors the helmet, Emblem colors its rotor. The helmet is hidden in first person. Another power-up replaces this one.");
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
    djui_button_create(body, "Sonic Shoes", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_sonic_shoes);
    djui_button_create(body, "Big Hands", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_big_hands);
    djui_button_create(body, "Propeller Mushroom", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_propeller);
    djui_button_create(body, "Power Star", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_tutorial_power_star);
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
    djui_button_create(body, "Physical Swimming", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_swimming);
    djui_button_create(body, "Physical Jumping & Crouching", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_jump_crouch);
    djui_button_create(body, "Physical Crouching & Diving", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_crouch_dive);
    djui_button_create(body, "Objects, Bosses & Bowser", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_bowser);
    djui_button_create(body, "Menus, HUD & Multiplayer", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_ui);
    djui_button_create(body, "Special Moves", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_moves);
    djui_button_create(body, "Speedrunning", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_speedrun);
    djui_button_create(body, "Visuals & Character Select", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_tutorial_visuals);
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
    djui_button_create(body, "Controller Bindings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_controller_settings_create);
    djui_button_create(body, "Motion Control Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_motion_control_settings_create);
    djui_button_create(body, "Model Settings", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_model_settings_create);
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

static void djui_panel_vr_filters_create(struct DjuiBase* caller) {
    if (configVrColorFilter >= VR_COLOR_FILTER_COUNT) {
        configVrColorFilter = VR_COLOR_FILTER_NONE;
    }

    struct DjuiThreePanel* panel =
        djui_panel_menu_create("Filters", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    char* filterChoices[VR_COLOR_FILTER_COUNT] = {
        "Off",
        "Virtual Boy",
        "Game Boy",
        "Super Mario Land",
        "Cell Shaded"
    };

    djui_selectionbox_create(
        body,
        "Color Filter",
        filterChoices,
        VR_COLOR_FILTER_COUNT,
        &configVrColorFilter,
        NULL
    );
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
    djui_button_create(body, "Filters", DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_vr_filters_create);
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
            "Speedrunning",
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_timer_create
        );

        djui_button_create(body, "Immersion", DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_vr_immersion_create);

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
