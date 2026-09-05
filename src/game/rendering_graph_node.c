#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stdlib.h>

#include <PR/ultratypes.h>

#include "area.h"
#include "audio/external.h"
#include "camera.h"
#include "engine/math_util.h"
#include "engine/lighting_engine.h"
#include "data/dynos_cmap.cpp.h"
#include "data/dynos.c.h"
#include "game_init.h"
#include "gfx_dimensions.h"
#include "main.h"
#include "memory.h"
#include "print.h"
#include "rendering_graph_node.h"
#include "shadow.h"
#include "sm64.h"
#include "game/level_update.h"
#include "pc/lua/smlua_hooks.h"
#include "pc/lua/smlua_utils.h"
#include "pc/configfile.h"
#include "pc/vr/vr.h"
#include "pc/utils/misc.h"
#include "pc/debuglog.h"
#include "pc/network/coopnet/coopnet.h"
#include "skybox.h"
#include "first_person_cam.h"
#include "bettercamera.h"
#include "course_table.h"
#include "mario.h"
#include "mario_misc.h"
#include "vr_hand_interaction.h"
#include "hardcoded.h"
#include "levels/menu/header.h"
#include "actors/common1.h"
#include "actors/mario/geo_header.h"
#include "behavior_data.h"
#include "object_helpers.h"
#include "interaction.h"
#include "sound_init.h"

/**
 * This file contains the code that processes the scene graph for rendering.
 * The scene graph is responsible for drawing everything except the HUD / text boxes.
 * First the root of the scene graph is processed when geo_process_root
 * is called from level_script.c. The rest of the tree is traversed recursively
 * using the function geo_process_node_and_siblings, which switches over all
 * geo node types and calls a specialized function accordingly.
 * The types are defined in engine/graph_node.h
 *
 * The scene graph typically looks like:
 * - Root (viewport)
 *  - Master list
 *   - Ortho projection
 *    - Background (skybox)
 *  - Master list
 *   - Perspective
 *    - Camera
 *     - <area-specific display lists>
 *     - Object parent
 *      - <group with 240 object nodes>
 *  - Master list
 *   - Script node (Cannon overlay)
 *
 */

#define DISPLAY_LIST_HEAP_SIZE 32000

#define MAX_FAR_PLANE_DIST 1000000.f
#define VR_LEDGE_CAMERA_DROP 80.0f
#define VR_STAR_SPAWN_FOCUS_RESPONSE 0.24f

f32 gProjectionMaxNearValue = 5;
s16 gProjectionVanillaNearValue = 100;
s16 gProjectionVanillaFarValue = 1000;

s16 gMatStackIndex;
Mat4 gMatStack[MATRIX_STACK_SIZE] = {};
Mat4 gMatStackPrev[MATRIX_STACK_SIZE] = {};
Mtx *gMatStackFixed[MATRIX_STACK_SIZE] = { 0 };
Mtx *gMatStackPrevFixed[MATRIX_STACK_SIZE] = { 0 };

s32 gCamSkipInterp = 0;
Vec3f gCamSkipInterpDisplacement = { 0 };

extern u8 gRenderingInterpolated;
extern f32 gRenderingDelta;

u8 sUsingCamSpace = FALSE;
static u8 sUsingBillboard = FALSE;
Mtx sPrevCamTranf, sCurrCamTranf = {
    .m = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    }
};

static Gfx obj_sanitize_gfx[] = {
    gsSPClearGeometryMode(G_CULL_BOTH | G_FOG | G_TEXTURE_GEN
                        | G_TEXTURE_GEN_LINEAR | G_LOD | G_PACKED_NORMALS_EXT
                        | G_LIGHT_MAP_EXT | G_LIGHTING_ENGINE_EXT | G_CULL_INVERT_EXT
                        | G_FRESNEL_COLOR_EXT | G_FRESNEL_ALPHA_EXT),
    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LIGHTING),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPSetEnvColor(0xFF, 0xFF, 0xFF, 0xFF),
    gsDPSetPrimColor(0, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    gsDPSetFogColor(0x00, 0x00, 0x00, 0x00),
    gsDPSetAlphaCompare(G_AC_NONE),
    gsDPSetCycleType(G_CYC_1CYCLE),
    gsSPNumLights(NUMLIGHTS_1),
    gsSPEndDisplayList(),
};

static Gfx obj_load_gfx_state[] = {
    gsSPLoadState(G_STATE_GEOMETRY_MODE
                | G_STATE_COMBINE_MODE
                | G_STATE_OTHER_MODE
                | G_STATE_ENV_COLOR
                | G_STATE_PRIM_COLOR
                | G_STATE_FOG_COLOR
                | G_STATE_FILL_COLOR
                | G_STATE_FRESNEL
                | G_STATE_TEXTURES
                | G_STATE_LIGHTS),
    gsSPEndDisplayList(),
};

static Gfx obj_save_gfx_state[] = {
    gsSPSaveState(G_STATE_GEOMETRY_MODE
                | G_STATE_COMBINE_MODE
                | G_STATE_OTHER_MODE
                | G_STATE_ENV_COLOR
                | G_STATE_PRIM_COLOR
                | G_STATE_FOG_COLOR
                | G_STATE_FILL_COLOR
                | G_STATE_FRESNEL
                | G_STATE_TEXTURES
                | G_STATE_LIGHTS),
    gsSPEndDisplayList(),
};

/**
 * Animation nodes have state in global variables, so this struct captures
 * the animation state so a 'context switch' can be made when rendering the
 * held object.
 */
struct GeoAnimState {
    /*0x00*/ u8 type;
    /*0x01*/ u8 enabled;
    /*0x02*/ s16 frame;
    /*0x04*/ f32 translationMultiplier;
    /*0x08*/ u16 *attribute;
    /*0x0C*/ struct Animation* anim;
    s16 prevFrame;
};

// For some reason, this is a GeoAnimState struct, but the current state consists
// of separate global variables. It won't match EU otherwise.
struct GeoAnimState gGeoTempState;

u8 gCurAnimType;
u8 gCurAnimEnabled;
s16 gCurrAnimFrame;
s16 gPrevAnimFrame;
f32 gCurAnimTranslationMultiplier;
u16 *gCurrAnimAttribute = NULL;
struct Animation *gCurAnim = NULL;

struct GrowingPool* gDisplayListHeap = NULL;

struct RenderModeContainer {
    u32 modes[8];
};

/* Rendermode settings for cycle 1 for all 8 layers. */
struct RenderModeContainer renderModeTable_1Cycle[2] = { { {
    G_RM_OPA_SURF,
    G_RM_AA_OPA_SURF,
    G_RM_AA_OPA_SURF,
    G_RM_AA_OPA_SURF,
    G_RM_AA_TEX_EDGE,
    G_RM_AA_XLU_SURF,
    G_RM_AA_XLU_SURF,
    G_RM_AA_XLU_SURF,
    } },
    { {
    /* z-buffered */
    G_RM_ZB_OPA_SURF,
    G_RM_AA_ZB_OPA_SURF,
    G_RM_AA_ZB_OPA_DECAL,
    G_RM_AA_ZB_OPA_INTER,
    G_RM_AA_ZB_TEX_EDGE,
    G_RM_AA_ZB_XLU_SURF,
    G_RM_AA_ZB_XLU_DECAL,
    G_RM_AA_ZB_XLU_INTER,
    } } };

/* Rendermode settings for cycle 2 for all 8 layers. */
struct RenderModeContainer renderModeTable_2Cycle[2] = { { {
    G_RM_OPA_SURF2,
    G_RM_AA_OPA_SURF2,
    G_RM_AA_OPA_SURF2,
    G_RM_AA_OPA_SURF2,
    G_RM_AA_TEX_EDGE2,
    G_RM_AA_XLU_SURF2,
    G_RM_AA_XLU_SURF2,
    G_RM_AA_XLU_SURF2,
    } },
    { {
    /* z-buffered */
    G_RM_ZB_OPA_SURF2,
    G_RM_AA_ZB_OPA_SURF2,
    G_RM_AA_ZB_OPA_DECAL2,
    G_RM_AA_ZB_OPA_INTER2,
    G_RM_AA_ZB_TEX_EDGE2,
    G_RM_AA_ZB_XLU_SURF2,
    G_RM_AA_ZB_XLU_DECAL2,
    G_RM_AA_ZB_XLU_INTER2,
    } } };

struct GraphNodeRoot *gCurGraphNodeRoot = NULL;
struct GraphNodeMasterList *gCurGraphNodeMasterList = NULL;
struct GraphNodePerspective *gCurGraphNodeCamFrustum = NULL;
struct GraphNodeCamera *gCurGraphNodeCamera = NULL;
struct GraphNodeObject *gCurGraphNodeObject = NULL;
struct GraphNodeHeldObject *gCurGraphNodeHeldObject = NULL;
struct MarioBodyState *gCurMarioBodyState = NULL;
static bool sVrFilteringLocalMarioBody = false;
u16 gAreaUpdateCounter = 0;

#ifdef F3DEX_GBI_2
LookAt lookAt;
#endif

static struct GraphNodePerspective *sPerspectiveNode = NULL;

static Mtx* sPerspectiveMtx   = NULL;
static f32 sPerspectiveAspect = 0;

static Mtx** sVrUiMatrices = NULL;
static u32 sVrUiMatrixCount = 0;
static u32 sVrUiMatrixCapacity = 0;
static Mtx** sVrHudMatrices = NULL;
static u32 sVrHudMatrixCount = 0;
static u32 sVrHudMatrixCapacity = 0;
struct VrWorldLabelMatrix {
    Mtx* matrix;
    Vec3f previousPosition;
    Vec3f currentPosition;
};
static struct VrWorldLabelMatrix* sVrWorldLabelMatrices = NULL;
static u32 sVrWorldLabelMatrixCount = 0;
static u32 sVrWorldLabelMatrixCapacity = 0;
static Mtx* sVrControllerHandMatrices[VR_CONTROLLER_COUNT] = { 0 };
static bool sVrControllerHandClosed[VR_CONTROLLER_COUNT] = { false };

#define VR_PAINTING_EXIT_HAT_GRAB_HEIGHT 22.0f
#define VR_PAINTING_EXIT_HAT_HORIZONTAL_REACH 24.0f
#define VR_PAINTING_EXIT_HAT_REACH_BELOW 13.0f
#define VR_PAINTING_EXIT_HAT_REACH_ABOVE 22.0f
#define VR_PAINTING_EXIT_HAT_REATTACH_HORIZONTAL_RADIUS 40.0f
#define VR_PAINTING_EXIT_HAT_REATTACH_BELOW 35.0f
#define VR_PAINTING_EXIT_HAT_REATTACH_ABOVE 50.0f
#define VR_PAINTING_EXIT_HAT_THROW_SCALE 0.125f
#define VR_PAINTING_EXIT_HAT_MAX_THROW_SPEED 75.0f
#define VR_HAT_SHAKE_MIN_SPEED 220.0f
#define VR_HAT_SHAKE_REVERSALS_REQUIRED 8
#define VR_HAT_SHAKE_WINDOW_FRAMES 45U
static u32 sVrPaintingExitHatHand = VR_CONTROLLER_COUNT;
static u32 sVrPaintingExitHatStartFrame = 0;
static Vec3f sVrPaintingExitHatLastPosition = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrPaintingExitHatVelocity = { 0.0f, 0.0f, 0.0f };
static bool sVrPaintingExitHatPickupLatched = false;
static bool sVrPaintingExitHatTriggerDown[VR_CONTROLLER_COUNT] = {
    false,
    false
};
static bool sVrPaintingExitHatWingCap = false;
static bool sVrPaintingExitHatShakeVelocityValid = false;
static Vec3f sVrPaintingExitHatShakeVelocity = { 0.0f, 0.0f, 0.0f };
static u32 sVrPaintingExitHatShakeWindowStart = 0;
static u32 sVrPaintingExitHatShakeSampleFrame = 0;
static u8 sVrPaintingExitHatShakeReversals = 0;
extern struct Object* gVrPaintingExitHatObject;

static bool vr_victory_hand_gesture_available(void) {
    switch (gMarioStates[0].action) {
        case ACT_FALL_AFTER_STAR_GRAB:
        case ACT_STAR_DANCE_EXIT:
        case ACT_STAR_DANCE_NO_EXIT:
        case ACT_STAR_DANCE_WATER:
        case ACT_JUMBO_STAR_CUTSCENE:
        case ACT_EXIT_LAND_SAVE_DIALOG:
            return true;
    }
    return false;
}

static bool vr_painting_exit_hat_action_active(void) {
    switch (gMarioStates[0].action) {
        // These are successful course/Bowser exits. Exit Course and every
        // death path use different actions and intentionally cannot activate
        // the gesture.
        case ACT_EXIT_AIRBORNE:
        case ACT_FALLING_EXIT_AIRBORNE:
        case ACT_SPECIAL_EXIT_AIRBORNE:
        case ACT_EXIT_LAND_SAVE_DIALOG:
            return true;
        default:
            return false;
    }
}

static void vr_clear_painting_exit_hat_hold(void) {
    sVrPaintingExitHatHand = VR_CONTROLLER_COUNT;
    sVrPaintingExitHatStartFrame = 0;
    sVrPaintingExitHatWingCap = false;
    sVrPaintingExitHatShakeVelocityValid = false;
    sVrPaintingExitHatShakeWindowStart = 0;
    sVrPaintingExitHatShakeSampleFrame = 0;
    sVrPaintingExitHatShakeReversals = 0;
    vec3f_set(sVrPaintingExitHatVelocity, 0.0f, 0.0f, 0.0f);
}

static void vr_grant_shaken_wing_cap(void) {
    struct MarioState* mario = &gMarioStates[0];
    if (mario->marioObj == NULL) {
        return;
    }

    mario->flags &= ~(
        MARIO_NORMAL_CAP |
        MARIO_METAL_CAP |
        MARIO_VANISH_CAP |
        MARIO_CAP_IN_HAND
    );
    mario->flags |= MARIO_WING_CAP | MARIO_CAP_ON_HEAD;
    mario->capTimer = MAX(
        mario->capTimer,
        gLevelValues.wingCapDuration
    );
    play_sound(
        SOUND_MENU_STAR_SOUND,
        mario->marioObj->header.gfx.cameraToObject
    );
    play_cap_music(SEQUENCE_ARGS(4, gLevelValues.wingCapSequence));
}

static void vr_update_painting_exit_hat_shake(
    u32 hand,
    const Vec3f velocity
) {
    if (sVrPaintingExitHatWingCap ||
        !ns_coopnet_vr_gameplay_allowed() ||
        !configVrCheatShakingHatWingCap ||
        !configVrImmersiveRemovableCap ||
        velocity == NULL ||
        gGlobalTimer == sVrPaintingExitHatShakeSampleFrame) {
        return;
    }
    sVrPaintingExitHatShakeSampleFrame = gGlobalTimer;

    if (sVrPaintingExitHatShakeWindowStart == 0 ||
        gGlobalTimer - sVrPaintingExitHatShakeWindowStart >
            VR_HAT_SHAKE_WINDOW_FRAMES) {
        sVrPaintingExitHatShakeWindowStart = gGlobalTimer;
        sVrPaintingExitHatShakeReversals = 0;
        sVrPaintingExitHatShakeVelocityValid = false;
    }

    const f32 speedSquared =
        velocity[0] * velocity[0] +
        velocity[1] * velocity[1] +
        velocity[2] * velocity[2];
    if (speedSquared < VR_HAT_SHAKE_MIN_SPEED * VR_HAT_SHAKE_MIN_SPEED) {
        return;
    }

    if (sVrPaintingExitHatShakeVelocityValid) {
        const f32 previousSpeedSquared =
            sVrPaintingExitHatShakeVelocity[0] *
                sVrPaintingExitHatShakeVelocity[0] +
            sVrPaintingExitHatShakeVelocity[1] *
                sVrPaintingExitHatShakeVelocity[1] +
            sVrPaintingExitHatShakeVelocity[2] *
                sVrPaintingExitHatShakeVelocity[2];
        const f32 dot =
            velocity[0] * sVrPaintingExitHatShakeVelocity[0] +
            velocity[1] * sVrPaintingExitHatShakeVelocity[1] +
            velocity[2] * sVrPaintingExitHatShakeVelocity[2];
        if (previousSpeedSquared >=
                VR_HAT_SHAKE_MIN_SPEED * VR_HAT_SHAKE_MIN_SPEED &&
            dot <= -0.25f * sqrtf(speedSquared * previousSpeedSquared)) {
            sVrPaintingExitHatShakeReversals++;
            if (sVrPaintingExitHatShakeReversals >=
                VR_HAT_SHAKE_REVERSALS_REQUIRED) {
                sVrPaintingExitHatWingCap = true;
                vr_apply_haptic(hand, 0.8f, 0.18f, -1.0f);
                play_sound(
                    SOUND_MENU_STAR_SOUND,
                    gMarioStates[0].marioObj != NULL
                        ? gMarioStates[0].marioObj->header.gfx.cameraToObject
                        : gGlobalSoundSource
                );
                return;
            }
        }
    }
    sVrPaintingExitHatShakeVelocity[0] = velocity[0];
    sVrPaintingExitHatShakeVelocity[1] = velocity[1];
    sVrPaintingExitHatShakeVelocity[2] = velocity[2];
    sVrPaintingExitHatShakeVelocityValid = true;
}

static bool vr_get_painting_exit_hat_head_position(Vec3f head) {
    if (!vr_get_stabilized_headset_world_position(head, false)) {
        return false;
    }
    head[1] += VR_PAINTING_EXIT_HAT_GRAB_HEIGHT;
    return true;
}

static f32 vr_painting_exit_hat_distance_squared(
    const Vec3f a,
    const Vec3f b
) {
    const f32 dx = a[0] - b[0];
    const f32 dy = a[1] - b[1];
    const f32 dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static void vr_throw_painting_exit_hat(void) {
    struct MarioState* mario = &gMarioStates[0];
    if (mario->marioObj == NULL || mario->character == NULL) {
        return;
    }

    if (gVrPaintingExitHatObject != NULL &&
        (gVrPaintingExitHatObject->activeFlags & ACTIVE_FLAG_ACTIVE)) {
        gVrPaintingExitHatObject->activeFlags =
            ACTIVE_FLAG_DEACTIVATED;
    }

    struct Object* cap = spawn_object(
        mario->marioObj,
        sVrPaintingExitHatWingCap
            ? mario->character->capWingModelId
            : mario->character->capModelId,
        sVrPaintingExitHatWingCap ? bhvWingCap : bhvNormalCap
    );
    if (cap == NULL) {
        return;
    }

    gVrPaintingExitHatObject = cap;
    cap->oBehParams2ndByte = VR_PAINTING_EXIT_HAT_BEH_PARAM;
    cap->oPosX = sVrPaintingExitHatLastPosition[0];
    cap->oPosY = sVrPaintingExitHatLastPosition[1];
    cap->oPosZ = sVrPaintingExitHatLastPosition[2];
    vec3f_copy(cap->header.gfx.pos, sVrPaintingExitHatLastPosition);
    Vec3f throwVelocity = {
        sVrPaintingExitHatVelocity[0] *
            VR_PAINTING_EXIT_HAT_THROW_SCALE,
        sVrPaintingExitHatVelocity[1] *
            VR_PAINTING_EXIT_HAT_THROW_SCALE,
        sVrPaintingExitHatVelocity[2] *
            VR_PAINTING_EXIT_HAT_THROW_SCALE
    };
    const f32 speed = sqrtf(
        throwVelocity[0] * throwVelocity[0] +
        throwVelocity[1] * throwVelocity[1] +
        throwVelocity[2] * throwVelocity[2]
    );
    if (speed > VR_PAINTING_EXIT_HAT_MAX_THROW_SPEED) {
        const f32 scale =
            VR_PAINTING_EXIT_HAT_MAX_THROW_SPEED / speed;
        vec3f_mul(throwVelocity, scale);
    }
    cap->oMoveAngleYaw = atan2s(throwVelocity[2], throwVelocity[0]);
    cap->oFaceAngleYaw = cap->oMoveAngleYaw;
    cap->oForwardVel = sqrtf(
        throwVelocity[0] * throwVelocity[0] +
        throwVelocity[2] * throwVelocity[2]
    );
    cap->oVelY = sVrPaintingExitHatWingCap
        ? 3.0f + MAX(throwVelocity[1], 0.0f) * 0.15f
        : throwVelocity[1];
    cap->oInteractType = 0;
    cap->oIntangibleTimer = -1;
}

static void vr_update_painting_exit_hat_gesture(void) {
    struct VrControllerState states[VR_CONTROLLER_COUNT] = { 0 };
    Vec3f hands[VR_CONTROLLER_COUNT];
    Vec3f velocities[VR_CONTROLLER_COUNT];
    Vec3f head;
    bool tracked[VR_CONTROLLER_COUNT] = { false, false };

    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        if (!vr_get_controller_state(hand, &states[hand])) {
            continue;
        }
        tracked[hand] = vr_get_controller_world_fist_from_state(
            hand,
            &states[hand],
            hands[hand],
            velocities[hand]
        );
    }

    const bool successfulExitActive =
        vr_painting_exit_hat_action_active();
    if (!vr_is_active()) {
        vr_clear_painting_exit_hat_hold();
        sVrPaintingExitHatPickupLatched = false;
        for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
            sVrPaintingExitHatTriggerDown[hand] = false;
        }
        return;
    }

    bool pickupInputDown = false;
    bool triggerPressedEdge[VR_CONTROLLER_COUNT] = { false, false };
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        const bool triggerDown =
            tracked[hand] && states[hand].trigger >= 0.55f;
        triggerPressedEdge[hand] =
            triggerDown && !sVrPaintingExitHatTriggerDown[hand];
        sVrPaintingExitHatTriggerDown[hand] = triggerDown;
        pickupInputDown = pickupInputDown ||
            (tracked[hand] &&
             states[hand].squeeze >= 0.55f &&
             states[hand].trigger >= 0.55f);
    }
    if (!pickupInputDown) {
        sVrPaintingExitHatPickupLatched = false;
    }

    // Once pulled free, only the chosen hand controls it. Releasing either
    // grab input throws a separate, non-interactive cap into the world.
    if (sVrPaintingExitHatHand < VR_CONTROLLER_COUNT) {
        const u32 hand = sVrPaintingExitHatHand;
        if (tracked[hand]) {
            vec3f_copy(sVrPaintingExitHatLastPosition, hands[hand]);
            vec3f_copy(sVrPaintingExitHatVelocity, velocities[hand]);
            vr_update_painting_exit_hat_shake(hand, velocities[hand]);
        }
        // A transition can briefly invalidate controller poses. Do not turn
        // that tracking gap into a release behind the player; only a sampled
        // open hand may throw the cap.
        // Reattach before processing release. This lets a player place the
        // cap on their head and open their hand naturally without spawning a
        // dropped cap at their feet on the same input sample.
        // Merely touching the head never attaches the cap. Releasing it inside
        // this head-shaped volume puts it on; releasing it elsewhere throws
        // the normal or transformed Wing Cap as a physics object.
        bool capOverHead = false;
        f32 capHeadHorizontal = -1.0f;
        f32 capHeadVertical = 0.0f;
        if (vr_get_painting_exit_hat_head_position(head)) {
            const f32 dx = hands[hand][0] - head[0];
            const f32 dy = hands[hand][1] - head[1];
            const f32 dz = hands[hand][2] - head[2];
            capHeadHorizontal = sqrtf(dx * dx + dz * dz);
            capHeadVertical = dy;
            capOverHead =
                capHeadHorizontal <=
                    VR_PAINTING_EXIT_HAT_REATTACH_HORIZONTAL_RADIUS &&
                dy >= -VR_PAINTING_EXIT_HAT_REATTACH_BELOW &&
                dy <= VR_PAINTING_EXIT_HAT_REATTACH_ABOVE;
        }

        // A transition can briefly invalidate controller poses. Do not turn
        // that tracking gap into a release behind the player; only a sampled
        // open hand may throw or replace the cap.
        const bool released = tracked[hand] &&
            (states[hand].squeeze < 0.35f ||
             states[hand].trigger < 0.35f);
        if (released) {
            printf(
                "[VR] Cap release: head horizontal %.1f, vertical %.1f -> %s.\n",
                capHeadHorizontal,
                capHeadVertical,
                capOverHead ? "reattach" : "throw"
            );
            if (capOverHead) {
                if (sVrPaintingExitHatWingCap) {
                    vr_grant_shaken_wing_cap();
                }
                vr_apply_haptic(hand, 0.25f, 0.06f, -1.0f);
                vr_clear_painting_exit_hat_hold();
                return;
            }
            vr_throw_painting_exit_hat();
            vr_clear_painting_exit_hat_hold();
            return;
        }
        return;
    }

    if (sVrPaintingExitHatPickupLatched) {
        return;
    }

    // A thrown cap remains a physics object until it is grabbed again or its
    // slow fade completes. Prefer it over pulling a fresh copy from the head.
    if (gVrPaintingExitHatObject != NULL) {
        struct Object* cap = gVrPaintingExitHatObject;
        const bool validCap =
            (cap->activeFlags & ACTIVE_FLAG_ACTIVE) != 0 &&
            (obj_has_behavior(cap, bhvNormalCap) ||
             obj_has_behavior(cap, bhvWingCap)) &&
            cap->oBehParams2ndByte ==
                VR_PAINTING_EXIT_HAT_BEH_PARAM;
        if (!validCap) {
            gVrPaintingExitHatObject = NULL;
        } else {
            Vec3f capPosition = {
                cap->oPosX,
                cap->oPosY + 10.0f,
                cap->oPosZ
            };
            const f32 pickupRadiusSquared =
                VR_PAINTING_EXIT_HAT_HORIZONTAL_REACH *
                VR_PAINTING_EXIT_HAT_HORIZONTAL_REACH;
            for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
                if (!tracked[hand] ||
                    states[hand].squeeze < 0.55f ||
                    !triggerPressedEdge[hand] ||
                    vr_painting_exit_hat_distance_squared(
                        hands[hand], capPosition) >
                            pickupRadiusSquared) {
                    continue;
                }
                sVrPaintingExitHatWingCap =
                    obj_has_behavior(cap, bhvWingCap);
                cap->activeFlags = ACTIVE_FLAG_DEACTIVATED;
                gVrPaintingExitHatObject = NULL;
                sVrPaintingExitHatHand = hand;
                sVrPaintingExitHatStartFrame = gGlobalTimer;
                sVrPaintingExitHatPickupLatched = true;
                vec3f_copy(sVrPaintingExitHatLastPosition, hands[hand]);
                vec3f_copy(sVrPaintingExitHatVelocity, velocities[hand]);
                vr_apply_haptic(hand, 0.35f, 0.08f, -1.0f);
                return;
            }
        }
    }

    if (!successfulExitActive &&
        !configVrImmersiveRemovableCap) {
        return;
    }

    if (!vr_get_painting_exit_hat_head_position(head)) {
        return;
    }

    const f32 horizontalReachSquared =
        VR_PAINTING_EXIT_HAT_HORIZONTAL_REACH *
        VR_PAINTING_EXIT_HAT_HORIZONTAL_REACH;
    f32 nearestDistanceSquared = FLT_MAX;
    u32 nearestHand = VR_CONTROLLER_COUNT;
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        if (!tracked[hand] ||
            states[hand].squeeze < 0.55f ||
            !triggerPressedEdge[hand]) {
            continue;
        }
        const f32 dx = hands[hand][0] - head[0];
        const f32 dy = hands[hand][1] - head[1];
        const f32 dz = hands[hand][2] - head[2];
        const f32 horizontalDistanceSquared = dx * dx + dz * dz;
        if (horizontalDistanceSquared > horizontalReachSquared ||
            dy < -VR_PAINTING_EXIT_HAT_REACH_BELOW ||
            dy > VR_PAINTING_EXIT_HAT_REACH_ABOVE) {
            continue;
        }
        const f32 distanceSquared = horizontalDistanceSquared + dy * dy;
        if (distanceSquared <= nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearestHand = hand;
        }
    }

    if (nearestHand < VR_CONTROLLER_COUNT) {
        sVrPaintingExitHatWingCap = false;
        sVrPaintingExitHatHand = nearestHand;
        sVrPaintingExitHatStartFrame = gGlobalTimer;
        sVrPaintingExitHatPickupLatched = true;
        vec3f_copy(sVrPaintingExitHatLastPosition, hands[nearestHand]);
        vec3f_copy(sVrPaintingExitHatVelocity, velocities[nearestHand]);
        vr_apply_haptic(nearestHand, 0.35f, 0.08f, -1.0f);
    }
}

static u8 vr_painting_exit_hat_alpha(void) {
    return sVrPaintingExitHatHand < VR_CONTROLLER_COUNT ? 255 : 0;
}

bool vr_is_controller_holding_cap(u32 handIndex) {
    return handIndex < VR_CONTROLLER_COUNT &&
        sVrPaintingExitHatHand == handIndex;
}

static Vp*  sViewport        = NULL;
static Gfx* sViewportPos     = NULL;
static Gfx* sViewportClipPos = NULL;
static Vp   sViewportPrev    = { 0 };
static Vp   sViewportInterp  = { 0 };

Gfx* gBackgroundSkyboxGfx = NULL;
Mtx* gBackgroundSkyboxMtx = NULL;

static struct GraphNodeBackground* sBackgroundNode = NULL;
static struct GraphNodeRoot* sBackgroundNodeRoot = NULL;
static struct GraphNodeCamera* sCameraNode = NULL;
static struct GraphNodeMasterList* sVrControllerHandMasterList = NULL;

static struct GrowingArray* sShadowInterp = NULL;
struct ShadowInterp* gShadowInterpCurrent = NULL;

struct MtxInterp {
    struct MtxInterp *nextVrBillboard;
    struct MtxInterp *nextVrHeld;
    Gfx *pos;
    Mtx *mtx;
    Mtx *mtxPrev;
    void *displayList;
    Mtx interp;
    Mtx vrBase;
    u8 usingCamSpace;
    u8 billboard;
    u8 vrBaseReady;
    struct Object *owner;
};

enum VrBillboardType {
    VR_BILLBOARD_NONE,
    VR_BILLBOARD_FULL,
    VR_BILLBOARD_CYLINDRICAL
};

static struct GrowingArray* sMtxTbl = NULL;
static struct MtxInterp* sVrBillboardHead = NULL;
static struct MtxInterp* sVrHeldMatrixHead = NULL;
static struct Object* sVrHeldAnchorOwner = NULL;
static void* sVrHeldAnchorDisplayList = NULL;
static bool sVrFirstPersonAnchorValid = false;
static u32 sVrFirstPersonAnchorTimestamp = 0;
static Vec3f sVrFirstPersonAnchorPrev = { 0 };
static Vec3f sVrFirstPersonAnchor = { 0 };
static bool sVrActionTurnInitialized = false;
static bool sVrActionTurnActive = false;
static u32 sVrActionTurnLastMarioAction = 0;
static f32 sVrActionTurnStartFrame = 0.0f;
static f32 sVrActionTurnStartYaw = 0.0f;
static f32 sVrActionTurnTargetYaw = 0.0f;
static f32 sVrActionTurnCurrentYaw = 0.0f;
static s16 sVrActionTurnYaw = 0;
static bool sVrCameraYawSampleValid = false;
static u32 sVrCameraYawSampleTimestamp = 0;
static s16 sVrCameraYawSamplePrev = 0;
static s16 sVrCameraYawSample = 0;
static bool sVrPendingCameraYawAlignment = false;
static s16 sVrPendingCameraYawTarget = 0;
static unsigned int sVrLastCameraMode = VR_CAMERA_MODE_COUNT;
static bool sVrHeadRotationMatrixValid = false;
static float sVrHeadRotationQuaternion[4] = { 0 };
static Mat4 sVrHeadRotationMatrix;
static bool sVrBodyAnchorSampleValid = false;
static u32 sVrBodyAnchorSampleTimestamp = 0;
static Vec3f sVrBodyAnchorSamplePrev = { 0 };
static Vec3f sVrBodyAnchorSample = { 0 };
static bool sVrBodyYawSampleValid = false;
static u32 sVrBodyYawSampleTimestamp = 0;
static s16 sVrBodyYawSamplePrev = 0;
static s16 sVrBodyYawSample = 0;
static bool sVrArmTargetSampleValid[VR_CONTROLLER_COUNT] = { false };
static u32 sVrArmTargetSampleTimestamp[VR_CONTROLLER_COUNT] = { 0 };
static Vec3f sVrArmTargetSamplePrev[VR_CONTROLLER_COUNT] = { 0 };
static Vec3f sVrArmTargetSample[VR_CONTROLLER_COUNT] = { 0 };
static bool sVrTrueFirstPersonHeightValid = false;
static u32 sVrTrueFirstPersonHeightTimestamp = 0;
static f32 sVrTrueFirstPersonHeightPrev = 0.0f;
static f32 sVrTrueFirstPersonHeight = 0.0f;
static bool sVrTrueFirstPersonBasisValid = false;
static u32 sVrTrueFirstPersonBasisTimestamp = 0;
static f32 sVrTrueFirstPersonBasisSign = 1.0f;
static Vec3f sVrTrueFirstPersonForwardPrev = { 0.0f, 0.0f, 1.0f };
static Vec3f sVrTrueFirstPersonForward = { 0.0f, 0.0f, 1.0f };
static Vec3f sVrTrueFirstPersonUpPrev = { 0.0f, 1.0f, 0.0f };
static Vec3f sVrTrueFirstPersonUp = { 0.0f, 1.0f, 0.0f };
static u32 sVrTrueFirstPersonSyntheticAction = ACT_UNINITIALIZED;
static u32 sVrTrueFirstPersonSyntheticActionStart = 0;
static bool sVrMountedHatAnchorValid = false;
static u32 sVrMountedHatAnchorTimestamp = 0;
static Vec3f sVrMountedBodyUpPrev = { 0.0f, 1.0f, 0.0f };
static Vec3f sVrMountedBodyUp = { 0.0f, 1.0f, 0.0f };
static bool sVrSinkingCameraDepthValid = false;
static u32 sVrSinkingCameraDepthTimestamp = 0;
static f32 sVrSinkingCameraDepthPrev = 0.0f;
static f32 sVrSinkingCameraDepth = 0.0f;
static bool sVrStarSpawnFocusValid = false;
static u32 sVrStarSpawnFocusTimestamp = 0;
static f32 sVrStarSpawnFocusBlendPrev = 0.0f;
static f32 sVrStarSpawnFocusBlend = 0.0f;
static Vec3f sVrStarSpawnFocusTarget = { 0.0f, 0.0f, 0.0f };
static bool sVrAudioListenerValid = false;
static bool sVrAudioListenerPreparedForFrame = false;
static Vec3f sVrAudioListenerPosition = { 0 };
static Vec3f sVrAudioListenerForward = { 0.0f, 0.0f, 1.0f };
static Vec3f sVrAudioListenerRight = { 1.0f, 0.0f, 0.0f };
static bool sVrHeadTrackedAudioEnabledForFrame = false;
static u32 sVrTrackingOriginGeneration = 0;
static bool sVrTorsoAlignmentValid = false;
static u8 sVrTorsoAlignmentCharacter = CT_MAX;
static u32 sVrTorsoAlignmentCharacterTimestamp = 0;
static f32 sVrTorsoAlignment = 0.0f;
static bool sVrControllerCameraInverseValid = false;
static u32 sVrControllerCameraInverseTimestamp = 0;
static f32 sVrControllerCameraInverseDelta = -1.0f;
static Mat4 sVrControllerCameraInverse;
static bool sVrRoomscaleBodyTrackingValid = false;
// Horizontal tracking-space translation already transferred into Mario's
// gameplay position. Keeping this in tracking space lets artificial camera
// yaw changes preserve the physical room origin without moving Mario twice.
static Vec3f sVrRoomscaleConsumedLocal = { 0.0f, 0.0f, 0.0f };
static Vec3f sVrRoomscaleConsumedLocalPrev = { 0.0f, 0.0f, 0.0f };
static u32 sVrRoomscaleConsumedTimestamp = 0;
static f32 sVrRoomscaleWorldFromLocal[2][2] = {
    { 1.0f, 0.0f },
    { 0.0f, 1.0f }
};
static bool sVrRoomscaleBasisValid = false;

void vr_reset_roomscale_body_tracking(void) {
    sVrRoomscaleBodyTrackingValid = false;
    sVrRoomscaleBasisValid = false;
    vec3f_set(sVrRoomscaleConsumedLocal, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrRoomscaleConsumedLocalPrev, 0.0f, 0.0f, 0.0f);
    sVrRoomscaleConsumedTimestamp = 0;
}

static void vr_get_roomscale_tracking_compensation(
    Vec3f compensation
) {
    if (compensation == NULL) {
        return;
    }
    if (!sVrRoomscaleBodyTrackingValid) {
        vec3f_set(compensation, 0.0f, 0.0f, 0.0f);
        return;
    }
    if (gRenderingInterpolated &&
        sVrRoomscaleConsumedTimestamp == gGlobalTimer) {
        delta_interpolate_vec3f(
            compensation,
            sVrRoomscaleConsumedLocalPrev,
            sVrRoomscaleConsumedLocal,
            clamp(gRenderingDelta, 0.0f, 1.0f)
        );
    } else {
        vec3f_copy(compensation, sVrRoomscaleConsumedLocal);
    }
}

static void vr_apply_roomscale_tracking_compensation(
    float position[3]
) {
    if (position == NULL || !sVrRoomscaleBodyTrackingValid) {
        return;
    }
    Vec3f compensation;
    vr_get_roomscale_tracking_compensation(compensation);
    position[0] -= compensation[0] / 100.0f;
    position[2] -= compensation[2] / 100.0f;
}

void vr_invalidate_first_person_tracked_world_cache(void) {
    sVrControllerCameraInverseValid = false;
    sVrControllerCameraInverseTimestamp = 0;
    sVrControllerCameraInverseDelta = -1.0f;
    // Keep the body anchor's previous/current samples intact. This cache
    // invalidation is also used for controller matrix refreshes; resetting
    // the body history here collapses interpolation and makes room-scale
    // torso motion visibly jitter at the render rate.
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        sVrArmTargetSampleValid[hand] = false;
        sVrArmTargetSampleTimestamp[hand] = 0;
    }
}

void vr_reset_first_person_calibration(void) {
    vr_reset_roomscale_body_tracking();
    sVrFirstPersonAnchorValid = false;
    sVrFirstPersonAnchorTimestamp = 0;
    vec3f_set(sVrFirstPersonAnchorPrev, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrFirstPersonAnchor, 0.0f, 0.0f, 0.0f);

    sVrActionTurnInitialized = false;
    sVrActionTurnActive = false;
    sVrActionTurnLastMarioAction = 0;
    sVrActionTurnStartFrame = 0.0f;
    sVrActionTurnStartYaw = 0.0f;
    sVrActionTurnTargetYaw = 0.0f;
    sVrActionTurnCurrentYaw = 0.0f;
    sVrActionTurnYaw = 0;
    sVrCameraYawSampleValid = false;
    sVrCameraYawSampleTimestamp = 0;
    sVrCameraYawSamplePrev = 0;
    sVrCameraYawSample = 0;
    sVrPendingCameraYawAlignment = false;
    sVrPendingCameraYawTarget = 0;
    sVrControllerCameraInverseValid = false;
    sVrControllerCameraInverseTimestamp = 0;
    sVrControllerCameraInverseDelta = -1.0f;

    sVrHeadRotationMatrixValid = false;
    sVrBodyAnchorSampleValid = false;
    sVrBodyAnchorSampleTimestamp = 0;
    vec3f_set(sVrBodyAnchorSamplePrev, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrBodyAnchorSample, 0.0f, 0.0f, 0.0f);
    sVrBodyYawSampleValid = false;
    sVrBodyYawSampleTimestamp = 0;
    sVrBodyYawSamplePrev = 0;
    sVrBodyYawSample = 0;
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        sVrArmTargetSampleValid[hand] = false;
        sVrArmTargetSampleTimestamp[hand] = 0;
        vec3f_set(
            sVrArmTargetSamplePrev[hand],
            0.0f,
            0.0f,
            0.0f
        );
        vec3f_set(
            sVrArmTargetSample[hand],
            0.0f,
            0.0f,
            0.0f
        );
    }
    sVrTrueFirstPersonHeightValid = false;
    sVrTrueFirstPersonHeightTimestamp = 0;
    sVrTrueFirstPersonHeightPrev = 0.0f;
    sVrTrueFirstPersonHeight = 0.0f;
    sVrTrueFirstPersonBasisValid = false;
    sVrTrueFirstPersonBasisTimestamp = 0;
    sVrTrueFirstPersonBasisSign = 1.0f;
    vec3f_set(sVrTrueFirstPersonForwardPrev, 0.0f, 0.0f, 1.0f);
    vec3f_set(sVrTrueFirstPersonForward, 0.0f, 0.0f, 1.0f);
    vec3f_set(sVrTrueFirstPersonUpPrev, 0.0f, 1.0f, 0.0f);
    vec3f_set(sVrTrueFirstPersonUp, 0.0f, 1.0f, 0.0f);
    sVrMountedHatAnchorValid = false;
    sVrMountedHatAnchorTimestamp = 0;
    vec3f_set(sVrMountedBodyUpPrev, 0.0f, 1.0f, 0.0f);
    vec3f_set(sVrMountedBodyUp, 0.0f, 1.0f, 0.0f);
    sVrSinkingCameraDepthValid = false;
    sVrSinkingCameraDepthTimestamp = 0;
    sVrSinkingCameraDepthPrev = 0.0f;
    sVrSinkingCameraDepth = 0.0f;
    sVrStarSpawnFocusValid = false;
    sVrStarSpawnFocusTimestamp = 0;
    sVrStarSpawnFocusBlendPrev = 0.0f;
    sVrStarSpawnFocusBlend = 0.0f;
    vec3f_set(sVrStarSpawnFocusTarget, 0.0f, 0.0f, 0.0f);
    sVrAudioListenerValid = false;
    sVrAudioListenerPreparedForFrame = false;
    vec3f_set(sVrAudioListenerPosition, 0.0f, 0.0f, 0.0f);
    vec3f_set(sVrAudioListenerForward, 0.0f, 0.0f, 1.0f);
    vec3f_set(sVrAudioListenerRight, 1.0f, 0.0f, 0.0f);
    sVrTorsoAlignmentValid = false;
    sVrTorsoAlignmentCharacter = CT_MAX;
    sVrTorsoAlignmentCharacterTimestamp = 0;
    sVrTorsoAlignment = 0.0f;
}

void vr_handle_camera_mode_change(void) {
    const unsigned int cameraMode =
        configVrCameraMode < VR_CAMERA_MODE_COUNT
            ? configVrCameraMode
            : VR_CAMERA_MODE_FIRST_PERSON;

    if (cameraMode == sVrLastCameraMode) {
        return;
    }

    sVrLastCameraMode = cameraMode;
    vr_reset_first_person_calibration();

    // Do not carry first-person smooth-turn momentum or a camera-space
    // interpolation endpoint into third person (or vice versa). Mixing those
    // two bases for even one rendered frame can rotate and shear the complete
    // stereo scene during a live mode switch.
    gNewCamera.yawAccel = 0;
    gNewCamera.tiltAccel = 0;
    gNewCamera.centering = false;
    gNewCamera.turnWait = 10;
    skip_camera_interpolation();

    if (vr_is_active() &&
        cameraMode == VR_CAMERA_MODE_THIRD_PERSON) {
        // Third person is composed around Mario by the normal game camera.
        // Rebase the headset once on entry so a first-person room-space lean
        // or yaw offset cannot leave Mario permanently to one side.
        // Recenter horizontal room position/yaw for third person without
        // silently replacing the player's calibrated standing/seated height.
        vr_request_horizontal_recenter();
    }
}

static void vr_refresh_tracking_origin(void) {
    const u32 generation = vr_get_tracking_origin_generation();
    if (generation == sVrTrackingOriginGeneration) {
        return;
    }

    sVrTrackingOriginGeneration = generation;
    // Never interpolate a body, arm, or camera sample across an OpenXR
    // reference-space recenter. The following valid frame establishes both
    // endpoints from the runtime's new tracking origin.
    vr_reset_first_person_calibration();
    // The gameplay camera observes this generation separately after the
    // native camera solver has produced its orbit for the current frame.
}

static void vr_update_first_person_action_turn(void) {
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON) {
        sVrActionTurnInitialized = false;
        sVrActionTurnActive = false;
        sVrActionTurnCurrentYaw = 0.0f;
        sVrActionTurnTargetYaw = 0.0f;
        sVrActionTurnYaw = 0;
        return;
    }

    const f32 renderFrame = (f32)gGlobalTimer +
        (gRenderingInterpolated ? gRenderingDelta : 0.0f);
    const u32 marioAction = gMarioStates[0].action;

    if (!sVrActionTurnInitialized) {
        sVrActionTurnInitialized = true;
        sVrActionTurnLastMarioAction = marioAction;
    }

    if (sVrActionTurnActive) {
        const f32 durationFrames = 12.0f;
        f32 progress = clamp(
            (renderFrame - sVrActionTurnStartFrame) /
                durationFrames,
            0.0f,
            1.0f
        );
        const f32 smoothProgress =
            progress * progress * (3.0f - 2.0f * progress);

        sVrActionTurnCurrentYaw =
            sVrActionTurnStartYaw +
            (sVrActionTurnTargetYaw - sVrActionTurnStartYaw) *
                smoothProgress;

        if (progress >= 1.0f) {
            sVrActionTurnActive = false;
            sVrActionTurnCurrentYaw =
                (f32)(s16)((s32)sVrActionTurnTargetYaw);
            sVrActionTurnTargetYaw = sVrActionTurnCurrentYaw;
        }
    }

    if (marioAction != sVrActionTurnLastMarioAction) {
        const u32 previousMarioAction =
            sVrActionTurnLastMarioAction;
        const bool wasOnPole =
            (previousMarioAction & ACT_FLAG_ON_POLE) != 0;
        const bool isOnPole =
            (marioAction & ACT_FLAG_ON_POLE) != 0;
        const bool wasHanging =
            (previousMarioAction & ACT_FLAG_HANGING) != 0;
        sVrActionTurnLastMarioAction = marioAction;

        if (!wasOnPole && isOnPole) {
            // Stop an unfinished flip turn at its current heading when Mario
            // grabs a pole. The player-controlled free-camera yaw remains
            // available, while the hidden climbing animation cannot turn it.
            sVrActionTurnActive = false;
            sVrActionTurnStartYaw = sVrActionTurnCurrentYaw;
            sVrActionTurnTargetYaw = sVrActionTurnCurrentYaw;
        }

        if (configVrExperimentalSideFlipFollow &&
            marioAction == ACT_SIDE_FLIP) {
            // Follow the side flip's actual gameplay momentum instead of
            // applying a fixed half-turn. Movement Overhaul deliberately
            // keeps faceAngle locked to the headset, so derive the jump yaw
            // from its independent horizontal velocity vector.
            const s16 viewYaw = vr_get_first_person_view_yaw();
            const f32 velocityX = gMarioStates[0].vel[0];
            const f32 velocityZ = gMarioStates[0].vel[2];
            const f32 speedSquared =
                velocityX * velocityX + velocityZ * velocityZ;
            const s16 momentumYaw = speedSquared > 0.01f
                ? atan2s(velocityZ, velocityX)
                : gMarioStates[0].intendedYaw;
            const s16 momentumDelta =
                (s16)(momentumYaw - viewYaw);

            sVrActionTurnStartFrame = renderFrame;
            sVrActionTurnStartYaw = sVrActionTurnCurrentYaw;
            sVrActionTurnTargetYaw =
                sVrActionTurnCurrentYaw + momentumDelta;
            sVrActionTurnActive = true;
        } else if (configVrExperimentalWallJumpTurn &&
                   marioAction == ACT_WALL_KICK_AIR &&
                   !wasOnPole &&
                   !wasHanging) {
            // Pole and ceiling dismounts reuse ACT_WALL_KICK_AIR for physics,
            // but neither is a wall jump. Their launch is already aligned to
            // the HMD, so the experimental half-turn must not rotate them.
            sVrActionTurnStartFrame = renderFrame;
            sVrActionTurnStartYaw = sVrActionTurnCurrentYaw;
            sVrActionTurnTargetYaw =
                sVrActionTurnCurrentYaw + 0x8000;
            sVrActionTurnActive = true;
        }

    }

    sVrActionTurnYaw = (s16)(
        (s16)((s32)roundf(sVrActionTurnCurrentYaw))
    );
}

s16 vr_get_first_person_action_turn_yaw(void) {
    return sVrActionTurnYaw;
}

static s16 vr_get_interpolated_first_person_camera_yaw(void) {
    const s16 sampledYaw = (s16)(-gNewCamera.yaw - 0x4000);

    if (!sVrCameraYawSampleValid ||
        sVrCameraYawSampleTimestamp != gGlobalTimer) {
        const bool continuous =
            sVrCameraYawSampleValid &&
            gGlobalTimer == sVrCameraYawSampleTimestamp + 1;

        sVrCameraYawSamplePrev = continuous
            ? sVrCameraYawSample
            : sampledYaw;
        sVrCameraYawSample = sampledYaw;
        sVrCameraYawSampleTimestamp = gGlobalTimer;
        sVrCameraYawSampleValid = true;
    }

    if (!gRenderingInterpolated) {
        return sVrCameraYawSample;
    }

    // Interpolate the shortest wrapped angle between game ticks. Smooth-turn
    // yaw is updated at 30 Hz, while OpenXR can render at 72/80/90/120 Hz;
    // using the raw current sample made controller turning visibly step even
    // though HMD rotation itself remained smooth.
    const s16 yawDelta = (s16)(
        sVrCameraYawSample - sVrCameraYawSamplePrev
    );
    return (s16)(
        sVrCameraYawSamplePrev +
        (s32)roundf((f32)yawDelta * gRenderingDelta)
    );
}

bool vr_align_first_person_camera_yaw(s16 worldYaw) {
    Vec3f currentDirection;
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !vr_get_first_person_aim_direction(
            &gMarioStates[0],
            currentDirection
        )) {
        return false;
    }

    const f32 horizontalLength = sqrtf(
        currentDirection[0] * currentDirection[0] +
        currentDirection[2] * currentDirection[2]
    );
    if (horizontalLength <= 0.0001f) {
        return false;
    }

    const s16 currentYaw = atan2s(
        currentDirection[2],
        currentDirection[0]
    );
    const s16 yawCorrection = (s16)(worldYaw - currentYaw);

    // Rotate the virtual first-person basis so the player's current physical
    // forward pose maps to the requested world direction. OpenXR's tracking
    // origin is untouched, so the HMD remains fully free immediately after
    // this one-time alignment.
    gNewCamera.yaw = (s16)(gNewCamera.yaw - yawCorrection);
    gNewCamera.yawTarget = gNewCamera.yaw;
    gNewCamera.yawAccel = 0.0f;
    gNewCamera.centering = false;

    // Do not interpolate across this intentional alignment. Sampling the old
    // and new bases as adjacent camera frames would briefly point the cannon
    // reticle between its former direction and the barrel.
    const s16 sampledYaw = (s16)(-gNewCamera.yaw - 0x4000);
    sVrCameraYawSampleValid = true;
    sVrCameraYawSampleTimestamp = gGlobalTimer;
    sVrCameraYawSamplePrev = sampledYaw;
    sVrCameraYawSample = sampledYaw;
    return true;
}

void vr_request_first_person_camera_yaw_alignment(s16 worldYaw) {
    sVrPendingCameraYawTarget = worldYaw;
    sVrPendingCameraYawAlignment = true;
}

void vr_adjust_first_person_camera_direction(Vec3f direction) {
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON) {
        return;
    }

    // Skybox generation still receives Lakitu's camera direction, which can
    // be changed by tunnels, dialogue, doors, and other scripted cameras.
    // Replace that direction with the same stable world-space base used by
    // the first-person level camera. HMD rotation is intentionally excluded
    // here because the per-eye projection applies it exactly once later.
    vr_update_first_person_action_turn();
    const s16 stableYaw = (s16)(
        vr_get_interpolated_first_person_camera_yaw() +
        sVrActionTurnYaw
    );
    direction[0] = sins(stableYaw);
    direction[1] = 0.0f;
    direction[2] = coss(stableYaw);
}

static bool vr_get_first_person_anchor(Vec3f anchor) {
    struct MarioState* mario = &gMarioStates[0];

    if (mario->marioObj == NULL) {
        sVrFirstPersonAnchorValid = false;
        return false;
    }

    if (!sVrFirstPersonAnchorValid ||
        sVrFirstPersonAnchorTimestamp != gGlobalTimer) {
        const bool continuous =
            sVrFirstPersonAnchorValid &&
            gGlobalTimer == sVrFirstPersonAnchorTimestamp + 1 &&
            gGlobalTimer !=
                mario->marioObj->header.gfx.skipInterpolationTimestamp;

        if (continuous) {
            vec3f_copy(
                sVrFirstPersonAnchorPrev,
                sVrFirstPersonAnchor
            );
        } else {
            vec3f_copy(sVrFirstPersonAnchorPrev, mario->pos);
        }

        vec3f_copy(sVrFirstPersonAnchor, mario->pos);
        sVrFirstPersonAnchorTimestamp = gGlobalTimer;
        sVrFirstPersonAnchorValid = true;
    }

    if (gRenderingInterpolated) {
        delta_interpolate_vec3f(
            anchor,
            sVrFirstPersonAnchorPrev,
            sVrFirstPersonAnchor,
            gRenderingDelta
        );
    } else {
        vec3f_copy(anchor, sVrFirstPersonAnchor);
    }

    return true;
}

static bool vr_move_world_sample_to_current_gameplay_anchor(
    Vec3f worldPosition
) {
    Vec3f interpolatedAnchor;
    if (worldPosition == NULL ||
        !vr_get_first_person_anchor(interpolatedAnchor)) {
        return false;
    }

    // Tracked world points are initially built against the render-time camera
    // anchor. The object's current/previous matrices will be interpolated
    // later, so storing that already-interpolated point would apply Mario's
    // movement twice and make the body trail forward motion (or lead reverse
    // motion). Convert the current endpoint back to the raw gameplay sample.
    worldPosition[0] +=
        sVrFirstPersonAnchor[0] - interpolatedAnchor[0];
    worldPosition[2] +=
        sVrFirstPersonAnchor[2] - interpolatedAnchor[2];

    // The camera pose used to construct tracked world points is one gameplay
    // sample behind the simulation endpoint used by Mario's model. Advance
    // the shared body/arm anchor by that single continuous-frame displacement
    // so it remains under the HMD while Mario translates. Discontinuities
    // already collapse previous/current anchors in vr_get_first_person_anchor.
    worldPosition[0] +=
        sVrFirstPersonAnchor[0] - sVrFirstPersonAnchorPrev[0];
    worldPosition[2] +=
        sVrFirstPersonAnchor[2] - sVrFirstPersonAnchorPrev[2];
    return true;
}

static void vr_rotate_pose_vector(
    const float rotation[4],
    Vec3f vector,
    Vec3f result
) {
    // Input and output are often the same vector, so preserve the source
    // components before writing any results.
    const float vectorX = vector[0];
    const float vectorY = vector[1];
    const float vectorZ = vector[2];
    const float twiceCrossX = 2.0f *
        (rotation[1] * vectorZ - rotation[2] * vectorY);
    const float twiceCrossY = 2.0f *
        (rotation[2] * vectorX - rotation[0] * vectorZ);
    const float twiceCrossZ = 2.0f *
        (rotation[0] * vectorY - rotation[1] * vectorX);

    result[0] = vectorX +
        rotation[3] * twiceCrossX +
        rotation[1] * twiceCrossZ -
        rotation[2] * twiceCrossY;
    result[1] = vectorY +
        rotation[3] * twiceCrossY +
        rotation[2] * twiceCrossX -
        rotation[0] * twiceCrossZ;
    result[2] = vectorZ +
        rotation[3] * twiceCrossZ +
        rotation[0] * twiceCrossY -
        rotation[1] * twiceCrossX;
}

static bool vr_build_head_rotation_matrix(Mat4 matrix) {
    float headRotation[4] = { 0 };

    if (!vr_get_head_rotation(headRotation)) {
        sVrHeadRotationMatrixValid = false;
        return false;
    }

    if (sVrHeadRotationMatrixValid &&
        headRotation[0] == sVrHeadRotationQuaternion[0] &&
        headRotation[1] == sVrHeadRotationQuaternion[1] &&
        headRotation[2] == sVrHeadRotationQuaternion[2] &&
        headRotation[3] == sVrHeadRotationQuaternion[3]) {
        mtxf_copy(matrix, sVrHeadRotationMatrix);
        return true;
    }

    Vec3f right = { 1.0f, 0.0f, 0.0f };
    Vec3f up = { 0.0f, 1.0f, 0.0f };
    Vec3f backward = { 0.0f, 0.0f, 1.0f };

    vr_rotate_pose_vector(headRotation, right, right);
    vr_rotate_pose_vector(headRotation, up, up);
    vr_rotate_pose_vector(headRotation, backward, backward);

    for (int axis = 0; axis < 3; axis++) {
        matrix[axis][0] = right[axis];
        matrix[axis][1] = up[axis];
        matrix[axis][2] = backward[axis];
        matrix[axis][3] = 0.0f;
    }
    matrix[3][0] = 0.0f;
    matrix[3][1] = 0.0f;
    matrix[3][2] = 0.0f;
    matrix[3][3] = 1.0f;

    for (u32 component = 0; component < 4; component++) {
        sVrHeadRotationQuaternion[component] = headRotation[component];
    }
    mtxf_copy(sVrHeadRotationMatrix, matrix);
    sVrHeadRotationMatrixValid = true;

    return true;
}

static void vr_hide_controller_hand_matrix(Mtx* fixedMatrix) {
    Mat4 matrix;
    mtxf_identity(matrix);

    // A degenerate mesh far behind the player is harmless if tracking drops
    // between the two eye submissions of a frame.
    matrix[0][0] = 0.0f;
    matrix[1][1] = 0.0f;
    matrix[2][2] = 0.0f;
    matrix[3][2] = 1000000.0f;
    mtxf_to_mtx(fixedMatrix, matrix);
}

static void vr_patch_controller_hand_matrices(uint32_t eyeIndex) {
    const float worldUnitsPerMeter = 100.0f;
    const float handModelScale = 0.20f *
        (float)clamp(configVrGloveSize, 25U, 250U) / 100.0f *
        vr_special_moves_big_hands_scale();
    const float wristToGripOffset = 25.0f * handModelScale;

    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        Mtx* fixedMatrix = sVrControllerHandMatrices[hand];
        if (fixedMatrix == NULL) {
            continue;
        }

        struct VrControllerState state;
        if (eyeIndex >= 2 ||
            !vr_get_controller_state(hand, &state) ||
            (!state.gripPoseValid && !state.aimPoseValid)) {
            vr_hide_controller_hand_matrix(fixedMatrix);
            continue;
        }

        const float* position = state.gripPoseValid
            ? state.gripPosition
            : state.aimPosition;
        const float* rotation = state.aimPoseValid
            ? state.aimRotation
            : state.gripRotation;
        Vec3f right = { 1.0f, 0.0f, 0.0f };
        Vec3f up = { 0.0f, 1.0f, 0.0f };
        Vec3f backward = { 0.0f, 0.0f, 1.0f };

        vr_rotate_pose_vector(rotation, right, right);
        vr_rotate_pose_vector(rotation, up, up);
        vr_rotate_pose_vector(rotation, backward, backward);

        unsigned int rotationDegrees[3];
        unsigned int positionValues[3];
        if (hand == VR_CONTROLLER_LEFT) {
            rotationDegrees[0] = configVrLeftGloveRotationX;
            positionValues[0] = configVrLeftGlovePositionX;
            positionValues[1] = configVrLeftGlovePositionY;
            positionValues[2] = configVrLeftGlovePositionZ;
        } else {
            rotationDegrees[0] = configVrRightGloveRotationX;
            positionValues[0] = configVrRightGlovePositionX;
            positionValues[1] = configVrRightGlovePositionY;
            positionValues[2] = configVrRightGlovePositionZ;
        }
        // X is the only model-specific rotation adjustment still exposed.
        // Ignore old saved Y/Z values now that the tested controller-space
        // mapping is authoritative, rather than leaving a hidden calibration
        // capable of changing the glove pose.
        rotationDegrees[1] = 0;
        rotationDegrees[2] = 0;

        Vec3s calibrationRotation;
        for (int axis = 0; axis < 3; axis++) {
            calibrationRotation[axis] = (s16)(
                (rotationDegrees[axis] % 360U) * 0x10000U / 360U
            );
        }

        Vec3f zeroTranslation = { 0.0f, 0.0f, 0.0f };
        Mat4 calibrationMatrix;
        mtxf_rotate_zxy_and_translate(
            calibrationMatrix,
            zeroTranslation,
            calibrationRotation
        );

        Mat4 matrix;
        for (int modelAxis = 0; modelAxis < 3; modelAxis++) {
            // Mario's open gloves extend from the wrist along local +X. Aim
            // space uses -Z as its pointing direction, so this calibration
            // makes the fingers follow the controller tip while retaining the
            // glove's native up and palm axes. The mirrored glove geometry
            // supplies the correct left/right handedness.
            for (int worldAxis = 0; worldAxis < 3; worldAxis++) {
                matrix[modelAxis][worldAxis] = (
                    calibrationMatrix[modelAxis][0] *
                        -backward[worldAxis] +
                    calibrationMatrix[modelAxis][1] *
                        up[worldAxis] +
                    calibrationMatrix[modelAxis][2] *
                        right[worldAxis]
                ) * handModelScale;
            }
            matrix[modelAxis][3] = 0.0f;
        }

        const float positionOffsetX =
            ((float)clamp(positionValues[0], 0U, 200U) - 100.0f) *
                0.5f;
        const float positionOffsetY =
            ((float)clamp(positionValues[1], 0U, 200U) - 100.0f) *
                0.5f;
        const float positionOffsetZ =
            ((float)clamp(positionValues[2], 0U, 200U) - 100.0f) *
                0.5f;

        // The glove model begins at the wrist, while OpenXR's grip position
        // sits inside the palm. Pull the wrist slightly behind the tracked
        // point so the palm, rather than the cuff, follows the controller.
        matrix[3][0] = position[0] * worldUnitsPerMeter +
            right[0] * positionOffsetX +
            up[0] * positionOffsetY +
            backward[0] * (wristToGripOffset + positionOffsetZ);
        matrix[3][1] = position[1] * worldUnitsPerMeter +
            right[1] * positionOffsetX +
            up[1] * positionOffsetY +
            backward[1] * (wristToGripOffset + positionOffsetZ);
        matrix[3][2] = position[2] * worldUnitsPerMeter +
            right[2] * positionOffsetX +
            up[2] * positionOffsetY +
            backward[2] * (wristToGripOffset + positionOffsetZ);
        if (sVrRoomscaleBodyTrackingValid) {
            Vec3f compensation;
            vr_get_roomscale_tracking_compensation(compensation);
            matrix[3][0] -= compensation[0];
            matrix[3][2] -= compensation[2];
        }
        matrix[3][3] = 1.0f;

        // The gameplay collision solver constrains the fist in world space.
        // Apply that same correction to the late-latched render pose so the
        // visible glove stops at the wall/floor/ceiling too. Previously only
        // the invisible interaction point was constrained, which made hand
        // collision appear to do nothing even though contacts were detected.
        Vec3f rawWorldPosition;
        if (vr_get_controller_world_fist_raw_from_state(
                hand,
                &state,
                rawWorldPosition,
                NULL
            ) && sVrControllerCameraInverseValid) {
            // Keep raw tracking separate from the Big Hands visual target.
            const bool bigHandsActive = vr_special_moves_big_hands_active();
            // Extend the late-latched visual pose just like the interaction
            // target. Apply only the existing contact-plane projection here;
            // never advance the gameplay sweep history from the render loop.
            if (bigHandsActive) {
                Vec3f reachWorldPosition;
                if (vr_get_controller_world_fist_reach_target_from_state(
                        hand,
                        &state,
                        reachWorldPosition,
                        NULL
                    )) {
                    vr_hand_interaction_apply_big_hands_collision_position(
                        hand,
                        reachWorldPosition
                    );
                    Vec3f reachCorrection = {
                        reachWorldPosition[0] - rawWorldPosition[0],
                        reachWorldPosition[1] - rawWorldPosition[1],
                        reachWorldPosition[2] - rawWorldPosition[2]
                    };
                    for (u32 localAxis = 0; localAxis < 3; localAxis++) {
                        matrix[3][localAxis] +=
                            reachCorrection[0] *
                                sVrControllerCameraInverse[localAxis][0] +
                            reachCorrection[1] *
                                sVrControllerCameraInverse[localAxis][1] +
                            reachCorrection[2] *
                                sVrControllerCameraInverse[localAxis][2];
                    }
                }
            }

            if (!bigHandsActive) {
                Vec3f constrainedWorldPosition;
                vec3f_copy(constrainedWorldPosition, rawWorldPosition);
                vr_hand_interaction_apply_hand_collision_position(
                    hand,
                    constrainedWorldPosition
                );
                Vec3f worldCorrection = {
                    constrainedWorldPosition[0] - rawWorldPosition[0],
                    constrainedWorldPosition[1] - rawWorldPosition[1],
                    constrainedWorldPosition[2] - rawWorldPosition[2]
                };
                for (u32 localAxis = 0; localAxis < 3; localAxis++) {
                    matrix[3][localAxis] +=
                        worldCorrection[0] *
                            sVrControllerCameraInverse[localAxis][0] +
                        worldCorrection[1] *
                            sVrControllerCameraInverse[localAxis][1] +
                        worldCorrection[2] *
                            sVrControllerCameraInverse[localAxis][2];
                }
            }
        }
        mtxf_to_mtx(fixedMatrix, matrix);
    }
}

static bool vr_get_controller_hand_attachment_matrix(
    u32 hand,
    Mat4 matrix
) {
    if (matrix == NULL || hand >= VR_CONTROLLER_COUNT ||
        sVrControllerHandMatrices[hand] == NULL) {
        return false;
    }
    mtxf_copy(matrix, sVrControllerHandMatrices[hand]->m);
    return true;
}

static void vr_get_controller_fist_from_hand_matrix(
    Mat4 handMatrix,
    Vec3f fistPosition
) {
    // The rendered glove begins at its wrist and extends along local +X.
    // This point is centered inside the closed fingers and therefore follows
    // the exact same predicted OpenXR pose as the visible glove.
    const f32 wristToFistModelUnits = 68.0f;
    for (u32 axis = 0; axis < 3; axis++) {
        fistPosition[axis] = handMatrix[3][axis] +
            handMatrix[0][axis] * wristToFistModelUnits;
    }
}

static bool vr_first_person_true_diving_active(void) {
    return vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        configVrExperimentalTrueDiving &&
        (gMarioStates[0].action & ACT_FLAG_DIVING) != 0;
}

static bool vr_first_person_uses_mounted_hat_anchor(void) {
    const u32 action = gMarioStates[0].action;

    // These actions move and/or pitch the complete character relative to
    // Mario's gameplay origin. A fixed world-up camera height consequently
    // leaves the view above, behind, or inside the model instead of at its
    // hidden hat. Ordinary movement deliberately stays on the stable camera
    // height path so running animations cannot bob the player's view.
    return (action & ACT_FLAG_RIDING_SHELL) != 0 ||
        (action & ACT_FLAG_SWIMMING_OR_FLYING) != 0 ||
        action == ACT_SHOT_FROM_CANNON ||
        action == ACT_RIDING_HOOT ||
        vr_first_person_true_diving_active();
}

static bool vr_get_mounted_hat_camera_position(
    Vec3f colliderAnchor,
    Vec3f position
) {
    struct MarioState* mario = &gMarioStates[0];
    struct MarioBodyState* bodyState = mario->marioBodyState;

    if (position == NULL ||
        !vr_first_person_uses_mounted_hat_anchor() ||
        mario->marioObj == NULL ||
        bodyState == NULL ||
        (bodyState->updateHeadPosTime != gGlobalTimer &&
         (gGlobalTimer == 0 ||
          bodyState->updateHeadPosTime != gGlobalTimer - 1))) {
        sVrMountedHatAnchorValid = false;
        return false;
    }

    Vec3f* sampledHead = &bodyState->animPartsPos[MARIO_ANIM_PART_HEAD];
    Vec3f* sampledTorso = &bodyState->animPartsPos[MARIO_ANIM_PART_TORSO];
    Vec3f sampledBodyUp;
    for (u32 axis = 0; axis < 3; axis++) {
        if (!isfinite((*sampledHead)[axis]) ||
            !isfinite((*sampledTorso)[axis])) {
            sVrMountedHatAnchorValid = false;
            return false;
        }
        // Subtracting these world-space joints deliberately removes Mario's
        // collider translation and all shared HMD/body translation. Only the
        // animation's orientation survives for flying and rideable poses.
        sampledBodyUp[axis] =
            (*sampledHead)[axis] - (*sampledTorso)[axis];
    }
    const f32 sampledLength = sqrtf(
        sampledBodyUp[0] * sampledBodyUp[0] +
        sampledBodyUp[1] * sampledBodyUp[1] +
        sampledBodyUp[2] * sampledBodyUp[2]
    );
    if (!isfinite(sampledLength) || sampledLength <= 0.001f) {
        sVrMountedHatAnchorValid = false;
        return false;
    }
    vec3f_mul(sampledBodyUp, 1.0f / sampledLength);

    const u32 sampledTimestamp = bodyState->updateHeadPosTime;
    if (!sVrMountedHatAnchorValid ||
        sampledTimestamp != sVrMountedHatAnchorTimestamp) {
        const bool continuous =
            sVrMountedHatAnchorValid &&
            sampledTimestamp == sVrMountedHatAnchorTimestamp + 1;

        if (continuous) {
            vec3f_copy(sVrMountedBodyUpPrev, sVrMountedBodyUp);
        } else {
            vec3f_copy(sVrMountedBodyUpPrev, sampledBodyUp);
        }
        vec3f_copy(sVrMountedBodyUp, sampledBodyUp);
        sVrMountedHatAnchorTimestamp = sampledTimestamp;
        sVrMountedHatAnchorValid = true;
    }

    const f32 delta = gRenderingInterpolated
        ? clamp(gRenderingDelta, 0.0f, 1.0f)
        : 1.0f;
    Vec3f bodyUp;
    delta_interpolate_vec3f(
        bodyUp,
        sVrMountedBodyUpPrev,
        sVrMountedBodyUp,
        delta
    );
    const f32 bodyUpLength = sqrtf(
        bodyUp[0] * bodyUp[0] +
        bodyUp[1] * bodyUp[1] +
        bodyUp[2] * bodyUp[2]
    );
    if (bodyUpLength > 0.001f && isfinite(bodyUpLength)) {
        vec3f_mul(bodyUp, 1.0f / bodyUpLength);
    } else {
        vec3f_set(bodyUp, 0.0f, 1.0f, 0.0f);
    }

    f32 swimmingSurfaceBlend = 0.0f;
    if ((mario->action & ACT_FLAG_SWIMMING) != 0) {
        // Mario's water collider is capped 80 units below the surface. A
        // fully body-oriented eye-height vector can consequently rotate the
        // camera through the torso and below the water plane during a nearly
        // horizontal surface stroke. Blend only this shallow region toward
        // collider-up; deeper swimming retains the same body-mounted anchor
        // used by Wing Cap flight.
        const f32 swimmingDepth =
            (f32)mario->waterLevel - colliderAnchor[1];
        swimmingSurfaceBlend = clamp(
            (180.0f - swimmingDepth) / 100.0f,
            0.0f,
            1.0f
        );
        if (swimmingSurfaceBlend > 0.0f) {
            bodyUp[0] *= 1.0f - swimmingSurfaceBlend;
            bodyUp[1] = bodyUp[1] *
                (1.0f - swimmingSurfaceBlend) +
                swimmingSurfaceBlend;
            bodyUp[2] *= 1.0f - swimmingSurfaceBlend;

            const f32 surfaceBodyUpLength = sqrtf(
                bodyUp[0] * bodyUp[0] +
                bodyUp[1] * bodyUp[1] +
                bodyUp[2] * bodyUp[2]
            );
            if (surfaceBodyUpLength > 0.001f &&
                isfinite(surfaceBodyUpLength)) {
                vec3f_mul(bodyUp, 1.0f / surfaceBodyUpLength);
            } else {
                vec3f_set(bodyUp, 0.0f, 1.0f, 0.0f);
            }
        }
    }

    u8 characterIndex = gNetworkPlayers[0].overrideModelIndex;
    if (characterIndex >= CT_MAX) {
        characterIndex = CT_MARIO;
    }
    const s32 configuredHeight = (s32)clamp(
        *config_vr_camera_height_for_character(characterIndex),
        0U,
        VR_CAMERA_HEIGHT_MAX
    );
    // The gameplay collider is the sole source of translation. Rotating the
    // configured eye-height vector with the body pose keeps the view mounted
    // during flight/shell animations without letting room-scale tracking or
    // animated joint offsets make it drift away from Mario.
    for (u32 axis = 0; axis < 3; axis++) {
        position[axis] = colliderAnchor[axis] +
            bodyUp[axis] * (f32)configuredHeight;
    }
    if (swimmingSurfaceBlend > 0.0f) {
        const f32 surfaceEyeClearance =
            (f32)mario->waterLevel + 10.0f;
        const f32 blendedSurfaceFloor =
            position[1] +
            (surfaceEyeClearance - position[1]) *
                swimmingSurfaceBlend;
        position[1] = MAX(position[1], blendedSurfaceFloor);
    }
    return true;
}

static bool vr_true_first_person_rotation_action_active(u32 action) {
    switch (action) {
        case ACT_TRIPLE_JUMP:
        case ACT_BACKFLIP:
        case ACT_WALL_KICK_AIR:
        case ACT_SIDE_FLIP:
        case ACT_DIVE:
        case ACT_TOP_OF_POLE_JUMP:
        case ACT_FLYING_TRIPLE_JUMP:
        case ACT_FORWARD_ROLLOUT:
        case ACT_GROUND_POUND:
        case ACT_BACKWARD_ROLLOUT:
        case ACT_SPECIAL_TRIPLE_JUMP:
            return true;
        default:
            return false;
    }
}

static bool vr_get_true_first_person_body_basis_legacy(
    Vec3f cameraForward,
    Vec3f cameraUp
) {
    struct MarioState* mario = &gMarioStates[0];
    struct MarioBodyState* bodyState = mario->marioBodyState;
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !configVrExperimentalTrueFirstPerson ||
        !vr_true_first_person_rotation_action_active(mario->action) ||
        cameraForward == NULL ||
        cameraUp == NULL ||
        mario->marioObj == NULL ||
        bodyState == NULL ||
        (bodyState->updateHeadPosTime != gGlobalTimer &&
         (gGlobalTimer == 0 ||
          bodyState->updateHeadPosTime != gGlobalTimer - 1))) {
        sVrTrueFirstPersonBasisValid = false;
        return false;
    }

    Vec3f* head = &bodyState->animPartsPos[MARIO_ANIM_PART_HEAD];
    Vec3f* torso = &bodyState->animPartsPos[MARIO_ANIM_PART_TORSO];
    Vec3f* leftShoulder =
        &bodyState->animPartsPos[MARIO_ANIM_PART_UPPER_LEFT];
    Vec3f* rightShoulder =
        &bodyState->animPartsPos[MARIO_ANIM_PART_UPPER_RIGHT];
    Vec3f sampledUp;
    Vec3f sampledRight;
    for (u32 axis = 0; axis < 3; axis++) {
        if (!isfinite((*head)[axis]) ||
            !isfinite((*torso)[axis]) ||
            !isfinite((*leftShoulder)[axis]) ||
            !isfinite((*rightShoulder)[axis])) {
            sVrTrueFirstPersonBasisValid = false;
            return false;
        }
        sampledUp[axis] = (*head)[axis] - (*torso)[axis];
        sampledRight[axis] =
            (*rightShoulder)[axis] - (*leftShoulder)[axis];
    }

    f32 upLength = sqrtf(
        sampledUp[0] * sampledUp[0] +
        sampledUp[1] * sampledUp[1] +
        sampledUp[2] * sampledUp[2]
    );
    if (!isfinite(upLength) || upLength <= 0.001f) {
        sVrTrueFirstPersonBasisValid = false;
        return false;
    }
    vec3f_mul(sampledUp, 1.0f / upLength);

    const f32 rightUpDot =
        sampledRight[0] * sampledUp[0] +
        sampledRight[1] * sampledUp[1] +
        sampledRight[2] * sampledUp[2];
    for (u32 axis = 0; axis < 3; axis++) {
        sampledRight[axis] -= sampledUp[axis] * rightUpDot;
    }
    const f32 rightLength = sqrtf(
        sampledRight[0] * sampledRight[0] +
        sampledRight[1] * sampledRight[1] +
        sampledRight[2] * sampledRight[2]
    );
    if (!isfinite(rightLength) || rightLength <= 0.001f) {
        sVrTrueFirstPersonBasisValid = false;
        return false;
    }
    vec3f_mul(sampledRight, 1.0f / rightLength);

    Vec3f sampledForward;
    vec3f_cross(sampledForward, sampledRight, sampledUp);
    const f32 forwardLength = sqrtf(
        sampledForward[0] * sampledForward[0] +
        sampledForward[1] * sampledForward[1] +
        sampledForward[2] * sampledForward[2]
    );
    if (!isfinite(forwardLength) || forwardLength <= 0.001f) {
        sVrTrueFirstPersonBasisValid = false;
        return false;
    }
    vec3f_mul(sampledForward, 1.0f / forwardLength);

    // Establish the model's shoulder-axis handedness while upright, then
    // retain it through the inverted half of a flip. Re-evaluating the sign
    // upside down would cancel exactly the rotation True First Person wants.
    if (sampledUp[1] > 0.7f) {
        const Vec3f facing = {
            sins(mario->marioObj->header.gfx.angle[1]),
            0.0f,
            coss(mario->marioObj->header.gfx.angle[1])
        };
        const f32 facingDot =
            sampledForward[0] * facing[0] +
            sampledForward[2] * facing[2];
        if (fabsf(facingDot) > 0.25f) {
            sVrTrueFirstPersonBasisSign =
                facingDot >= 0.0f ? 1.0f : -1.0f;
        }
    }
    vec3f_mul(sampledForward, sVrTrueFirstPersonBasisSign);

    const u32 sampleTimestamp = bodyState->updateHeadPosTime;
    if (!sVrTrueFirstPersonBasisValid ||
        sampleTimestamp != sVrTrueFirstPersonBasisTimestamp) {
        const bool continuous =
            sVrTrueFirstPersonBasisValid &&
            sampleTimestamp == sVrTrueFirstPersonBasisTimestamp + 1;
        vec3f_copy(
            sVrTrueFirstPersonForwardPrev,
            continuous
                ? sVrTrueFirstPersonForward
                : sampledForward
        );
        vec3f_copy(
            sVrTrueFirstPersonUpPrev,
            continuous
                ? sVrTrueFirstPersonUp
                : sampledUp
        );
        vec3f_copy(sVrTrueFirstPersonForward, sampledForward);
        vec3f_copy(sVrTrueFirstPersonUp, sampledUp);
        sVrTrueFirstPersonBasisTimestamp = sampleTimestamp;
        sVrTrueFirstPersonBasisValid = true;
    }

    const f32 delta = gRenderingInterpolated
        ? clamp(gRenderingDelta, 0.0f, 1.0f)
        : 1.0f;
    delta_interpolate_vec3f(
        cameraUp,
        sVrTrueFirstPersonUpPrev,
        sVrTrueFirstPersonUp,
        delta
    );
    upLength = sqrtf(
        cameraUp[0] * cameraUp[0] +
        cameraUp[1] * cameraUp[1] +
        cameraUp[2] * cameraUp[2]
    );
    if (!isfinite(upLength) || upLength <= 0.001f) {
        return false;
    }
    vec3f_mul(cameraUp, 1.0f / upLength);

    delta_interpolate_vec3f(
        cameraForward,
        sVrTrueFirstPersonForwardPrev,
        sVrTrueFirstPersonForward,
        delta
    );
    const f32 forwardUpDot =
        cameraForward[0] * cameraUp[0] +
        cameraForward[1] * cameraUp[1] +
        cameraForward[2] * cameraUp[2];
    for (u32 axis = 0; axis < 3; axis++) {
        cameraForward[axis] -= cameraUp[axis] * forwardUpDot;
    }
    const f32 interpolatedForwardLength = sqrtf(
        cameraForward[0] * cameraForward[0] +
        cameraForward[1] * cameraForward[1] +
        cameraForward[2] * cameraForward[2]
    );
    if (!isfinite(interpolatedForwardLength) ||
        interpolatedForwardLength <= 0.001f) {
        return false;
    }
    vec3f_mul(cameraForward, 1.0f / interpolatedForwardLength);

    // Lock the animation to the player's stable heading. The animated arm
    // line is not a reliable flip axis (arms swing independently and could
    // turn a front flip into a sideways roll). Head-to-torso supplies the
    // tilt amount; the selected facing source supplies the horizontal plane.
    // Flip around the player's current front-facing horizontal axis. The
    // stabilized camera helper intentionally excludes live HMD yaw because
    // OpenXR applies it later during ordinary view rendering; that is the
    // wrong basis for an animated body flip and can turn a backflip/triple
    // jump into a sideways roll when the headset is not aligned with the
    // camera base. Use the live headset-facing yaw without locomotion
    // calibration so pitch flips always remain forward/back relative to the
    // player's actual present view.
    const s16 headsetForwardYaw = vr_get_first_person_headset_yaw();
    Vec3f stableForward = {
        sins(headsetForwardYaw),
        0.0f,
        coss(headsetForwardYaw)
    };
    Vec3f stableRight = {
        stableForward[2],
        0.0f,
        -stableForward[0]
    };
    const f32 verticalAmount = cameraUp[1];
    if (mario->action == ACT_SIDE_FLIP) {
        const f32 rollAmount =
            cameraUp[0] * stableRight[0] +
            cameraUp[2] * stableRight[2];
        const f32 planeLength = sqrtf(
            verticalAmount * verticalAmount +
            rollAmount * rollAmount
        );
        if (!isfinite(planeLength) || planeLength <= 0.001f) {
            return false;
        }
        const f32 invPlaneLength = 1.0f / planeLength;
        cameraUp[0] = stableRight[0] * rollAmount * invPlaneLength;
        cameraUp[1] = verticalAmount * invPlaneLength;
        cameraUp[2] = stableRight[2] * rollAmount * invPlaneLength;
        vec3f_copy(cameraForward, stableForward);
    } else {
        const f32 pitchAmount =
            cameraUp[0] * stableForward[0] +
            cameraUp[2] * stableForward[2];
        const f32 planeLength = sqrtf(
            verticalAmount * verticalAmount +
            pitchAmount * pitchAmount
        );
        if (!isfinite(planeLength) || planeLength <= 0.001f) {
            return false;
        }
        const f32 invPlaneLength = 1.0f / planeLength;
        const f32 vertical = verticalAmount * invPlaneLength;
        const f32 pitch = pitchAmount * invPlaneLength;
        cameraUp[0] = stableForward[0] * pitch;
        cameraUp[1] = vertical;
        cameraUp[2] = stableForward[2] * pitch;
        cameraForward[0] = stableForward[0] * vertical;
        cameraForward[1] = -pitch;
        cameraForward[2] = stableForward[2] * vertical;
    }
    return true;
}

static bool vr_get_true_first_person_body_basis_deprecated(
    Vec3f cameraForward,
    Vec3f cameraUp
) {
    struct MarioState* mario = &gMarioStates[0];
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !configVrExperimentalTrueFirstPerson ||
        cameraForward == NULL ||
        cameraUp == NULL ||
        mario->marioObj == NULL) {
        sVrTrueFirstPersonSyntheticAction = ACT_UNINITIALIZED;
        return false;
    }

    const u32 action = mario->action;
    const bool frontFlip =
        action == ACT_TRIPLE_JUMP ||
        action == ACT_FLYING_TRIPLE_JUMP ||
        action == ACT_SPECIAL_TRIPLE_JUMP ||
        action == ACT_FORWARD_ROLLOUT;
    const bool backFlip =
        action == ACT_BACKFLIP ||
        action == ACT_BACKWARD_ROLLOUT;
    const bool sideFlip = action == ACT_SIDE_FLIP;
    const bool dive = action == ACT_DIVE;
    if (!frontFlip && !backFlip && !sideFlip && !dive) {
        sVrTrueFirstPersonSyntheticAction = ACT_UNINITIALIZED;
        return vr_get_true_first_person_body_basis_legacy(
            cameraForward,
            cameraUp
        );
    }

    /*
     * Start with the exact ordinary first-person camera heading. OpenXR adds
     * live HMD rotation later, so injecting headset yaw here would apply it in
     * the wrong coordinate space and force every action to look right.
     * Mario's skeleton and animation matrices are intentionally excluded.
     */
    Vec3f stableForward = { 0.0f, 0.0f, 1.0f };
    vr_adjust_first_person_camera_direction(stableForward);
    stableForward[1] = 0.0f;
    const f32 stableForwardLength = sqrtf(
        stableForward[0] * stableForward[0] +
        stableForward[2] * stableForward[2]
    );
    if (!isfinite(stableForwardLength) ||
        stableForwardLength <= 0.001f) {
        return false;
    }
    vec3f_mul(stableForward, 1.0f / stableForwardLength);
    Vec3f stableRight = {
        stableForward[2],
        0.0f,
        -stableForward[0]
    };

    /* A dive follows the ordinary view exactly and applies no rotation. */
    if (dive) {
        sVrTrueFirstPersonSyntheticAction = action;
        vec3f_copy(cameraForward, stableForward);
        vec3f_set(cameraUp, 0.0f, 1.0f, 0.0f);
        return true;
    }

    if (sVrTrueFirstPersonSyntheticAction != action) {
        sVrTrueFirstPersonSyntheticAction = action;
        sVrTrueFirstPersonSyntheticActionStart = gGlobalTimer;
    }

    /*
     * Drive one complete turn from an independent twenty-tick clock. This
     * keeps the result stable across character packs and animation changes.
     * Rendering interpolation keeps the rotation smooth above 30 Hz.
     */
    const f32 renderDelta = gRenderingInterpolated
        ? clamp(gRenderingDelta, 0.0f, 1.0f)
        : 1.0f;
    const f32 elapsed =
        (f32)(gGlobalTimer - sVrTrueFirstPersonSyntheticActionStart) +
        renderDelta;
    const f32 progress = clamp(elapsed / 20.0f, 0.0f, 1.0f);
    s16 turnAngle = (s16)roundf(progress * 65535.0f);
    if (backFlip) {
        turnAngle = (s16)-turnAngle;
    }

    const f32 turnSin = sins(turnAngle);
    const f32 turnCos = coss(turnAngle);
    if (sideFlip) {
        /*
         * The engine look-at basis stores this view-local operation opposite
         * to the conventional matrix label: this is the operation that is
         * visually observed as a sideways roll in-headset.
         */
        const f32 lateralVelocity =
            mario->vel[0] * stableRight[0] +
            mario->vel[2] * stableRight[2];
        if (lateralVelocity < 0.0f) {
            turnAngle = (s16)-turnAngle;
        }
        const f32 sideSin = sins(turnAngle);
        const f32 sideCos = coss(turnAngle);
        for (u32 axis = 0; axis < 3; axis++) {
            cameraForward[axis] = stableForward[axis] * sideCos;
            cameraUp[axis] = stableForward[axis] * sideSin;
        }
        cameraForward[1] = -sideSin;
        cameraUp[1] = sideCos;
        return true;
    }

    /*
     * In this engine's look-at storage, rotating the up vector across the
     * horizontal right vector is the operation visually observed as a
     * straight forward/backward somersault in-headset.
     */
    vec3f_copy(cameraForward, stableForward);
    for (u32 axis = 0; axis < 3; axis++) {
        cameraUp[axis] = -stableRight[axis] * turnSin;
    }
    cameraUp[1] += turnCos;
    return true;
}

static bool vr_true_first_person_synthetic_action(u32 action) {
    return action == ACT_TRIPLE_JUMP ||
        action == ACT_FLYING_TRIPLE_JUMP ||
        action == ACT_SPECIAL_TRIPLE_JUMP ||
        action == ACT_BACKFLIP ||
        action == ACT_SIDE_FLIP ||
        action == ACT_DIVE ||
        action == ACT_FORWARD_ROLLOUT ||
        action == ACT_BACKWARD_ROLLOUT;
}

static bool vr_get_true_first_person_body_basis(
    Vec3f cameraForward,
    Vec3f cameraUp
) {
    const u32 action = gMarioStates[0].action;
    if (vr_true_first_person_synthetic_action(action)) {
        /* Keep the ordinary game camera completely unchanged. */
        return false;
    }
    return vr_get_true_first_person_body_basis_legacy(
        cameraForward,
        cameraUp
    );
}

static bool vr_get_true_first_person_view_rotation(Vec3s rotation) {
    struct MarioState* mario = &gMarioStates[0];
    const u32 action = mario->action;
    const bool frontFlip =
        action == ACT_TRIPLE_JUMP ||
        action == ACT_FLYING_TRIPLE_JUMP ||
        action == ACT_SPECIAL_TRIPLE_JUMP ||
        action == ACT_FORWARD_ROLLOUT;
    const bool backFlip =
        action == ACT_BACKFLIP ||
        action == ACT_BACKWARD_ROLLOUT;
    const bool sideFlip = action == ACT_SIDE_FLIP;

    vec3s_copy(rotation, gVec3sZero);
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !configVrExperimentalTrueFirstPerson ||
        (!frontFlip && !backFlip && !sideFlip)) {
        if (action != ACT_DIVE) {
            sVrTrueFirstPersonSyntheticAction = ACT_UNINITIALIZED;
        }
        return false;
    }

    if (sVrTrueFirstPersonSyntheticAction != action) {
        sVrTrueFirstPersonSyntheticAction = action;
        sVrTrueFirstPersonSyntheticActionStart = gGlobalTimer;
    }
    const f32 renderDelta = gRenderingInterpolated
        ? clamp(gRenderingDelta, 0.0f, 1.0f)
        : 1.0f;
    const f32 elapsed =
        (f32)(gGlobalTimer - sVrTrueFirstPersonSyntheticActionStart) +
        renderDelta;
    s16 angle = (s16)roundf(
        clamp(elapsed / 20.0f, 0.0f, 1.0f) * 65535.0f
    );

    /* Camera-local X is pitch; camera-local Z is roll. */
    if (frontFlip || backFlip) {
        rotation[0] = backFlip ? (s16)-angle : angle;
    } else {
        Vec3f ordinaryForward = { 0.0f, 0.0f, 1.0f };
        vr_adjust_first_person_camera_direction(ordinaryForward);
        ordinaryForward[1] = 0.0f;
        const f32 length = sqrtf(
            ordinaryForward[0] * ordinaryForward[0] +
            ordinaryForward[2] * ordinaryForward[2]
        );
        if (length > 0.001f) {
            vec3f_mul(ordinaryForward, 1.0f / length);
            const Vec3f ordinaryRight = {
                ordinaryForward[2],
                0.0f,
                -ordinaryForward[0]
            };
            const f32 lateralVelocity =
                mario->vel[0] * ordinaryRight[0] +
                mario->vel[2] * ordinaryRight[2];
            if (lateralVelocity < 0.0f) {
                angle = (s16)-angle;
            }
        }
        rotation[2] = angle;
    }
    return true;
}

static f32 vr_get_smoothed_sinking_camera_depth(void) {
    struct MarioState* mario = &gMarioStates[0];
    f32 targetDepth = isfinite(mario->quicksandDepth)
        ? clamp(mario->quicksandDepth, 0.0f, 160.0f)
        : 0.0f;

    if (!configVrImmersiveCameraMotion) {
        targetDepth = 0.0f;
    } else {
        // Native quicksandDepth does not cover crouching or the three
        // actions where Mario is physically pressed into soft ground. Add a
        // small action-specific camera drop without changing Mario's hitbox,
        // collision position, animation, or gameplay state.
        switch (mario->action) {
            case ACT_START_CROUCHING:
            case ACT_CROUCHING:
            case ACT_STOP_CROUCHING:
            case ACT_CROUCH_SLIDE:
                targetDepth = fmaxf(targetDepth, 28.0f);
                break;
            case ACT_FEET_STUCK_IN_GROUND:
                targetDepth = fmaxf(targetDepth, 48.0f);
                break;
            case ACT_BUTT_STUCK_IN_GROUND:
                targetDepth = fmaxf(targetDepth, 76.0f);
                break;
            case ACT_HEAD_STUCK_IN_GROUND:
                targetDepth = fmaxf(targetDepth, 112.0f);
                break;
        }
    }

    if (!sVrSinkingCameraDepthValid ||
        sVrSinkingCameraDepthTimestamp != gGlobalTimer) {
        const bool continuous =
            sVrSinkingCameraDepthValid &&
            gGlobalTimer == sVrSinkingCameraDepthTimestamp + 1;
        if (!continuous) {
            sVrSinkingCameraDepthPrev = targetDepth;
            sVrSinkingCameraDepth = targetDepth;
        } else {
            sVrSinkingCameraDepthPrev = sVrSinkingCameraDepth;
            const f32 response = targetDepth > sVrSinkingCameraDepth
                ? 0.42f
                : 0.32f;
            sVrSinkingCameraDepth +=
                (targetDepth - sVrSinkingCameraDepth) * response;
            if (fabsf(targetDepth - sVrSinkingCameraDepth) < 0.01f) {
                sVrSinkingCameraDepth = targetDepth;
            }
        }
        sVrSinkingCameraDepthTimestamp = gGlobalTimer;
        sVrSinkingCameraDepthValid = true;
    }

    return gRenderingInterpolated
        ? delta_interpolate_f32(
            sVrSinkingCameraDepthPrev,
            sVrSinkingCameraDepth,
            clamp(gRenderingDelta, 0.0f, 1.0f)
        )
        : sVrSinkingCameraDepth;
}

static f32 vr_get_ledge_camera_drop(void) {
    if (!configVrImmersiveLedgeCamera) {
        return 0.0f;
    }

    struct MarioState* mario = &gMarioStates[0];
    switch (mario->action) {
        case ACT_LEDGE_GRAB:
            return VR_LEDGE_CAMERA_DROP;

        case ACT_LEDGE_CLIMB_SLOW_1:
        case ACT_LEDGE_CLIMB_SLOW_2:
        case ACT_LEDGE_CLIMB_FAST: {
            const struct Animation* animation =
                mario->marioObj != NULL
                    ? mario->marioObj->header.gfx.animInfo.curAnim
                    : NULL;
            const f32 endFrame = animation != NULL
                ? fmaxf((f32)animation->loopEnd - 1.0f, 1.0f)
                : 1.0f;
            const f32 frame = mario->marioObj != NULL
                ? (f32)mario->marioObj->header.gfx.animInfo.animFrame +
                    (gRenderingInterpolated
                        ? clamp(gRenderingDelta, 0.0f, 1.0f)
                        : 0.0f)
                : endFrame;
            const f32 progress = clamp(frame / endFrame, 0.0f, 1.0f);
            return VR_LEDGE_CAMERA_DROP * (1.0f - progress);
        }

        case ACT_LEDGE_CLIMB_DOWN: {
            const struct Animation* animation =
                mario->marioObj != NULL
                    ? mario->marioObj->header.gfx.animInfo.curAnim
                    : NULL;
            const f32 endFrame = animation != NULL
                ? fmaxf((f32)animation->loopEnd - 1.0f, 1.0f)
                : 1.0f;
            const f32 frame = mario->marioObj != NULL
                ? (f32)mario->marioObj->header.gfx.animInfo.animFrame +
                    (gRenderingInterpolated
                        ? clamp(gRenderingDelta, 0.0f, 1.0f)
                        : 0.0f)
                : 0.0f;
            return VR_LEDGE_CAMERA_DROP *
                clamp(frame / endFrame, 0.0f, 1.0f);
        }
    }
    return 0.0f;
}

static void vr_apply_star_spawn_camera_focus(
    Vec3f cameraPosition,
    Vec3f forward
) {
    if (!configVrImmersiveStarSpawnFocus) {
        // Do not let a blend accumulated while this option was enabled leak
        // into later star cutscenes after the player disables it.
        sVrStarSpawnFocusValid = false;
        sVrStarSpawnFocusTimestamp = 0;
        sVrStarSpawnFocusBlendPrev = 0.0f;
        sVrStarSpawnFocusBlend = 0.0f;
        return;
    }

    const bool cutsceneActive =
        gCamera != NULL &&
        (gCamera->cutscene == CUTSCENE_STAR_SPAWN ||
         gCamera->cutscene == CUTSCENE_RED_COIN_STAR_SPAWN) &&
        gCutsceneFocus != NULL &&
        (gCutsceneFocus->activeFlags & ACTIVE_FLAG_ACTIVE) != 0;

    if (!sVrStarSpawnFocusValid ||
        sVrStarSpawnFocusTimestamp != gGlobalTimer) {
        const bool continuous =
            sVrStarSpawnFocusValid &&
            gGlobalTimer == sVrStarSpawnFocusTimestamp + 1;
        sVrStarSpawnFocusBlendPrev = continuous
            ? sVrStarSpawnFocusBlend
            : 0.0f;
        if (!continuous) {
            sVrStarSpawnFocusBlend = 0.0f;
        }

        const f32 targetBlend = cutsceneActive ? 1.0f : 0.0f;
        sVrStarSpawnFocusBlend +=
            (targetBlend - sVrStarSpawnFocusBlend) *
            VR_STAR_SPAWN_FOCUS_RESPONSE;
        if (fabsf(targetBlend - sVrStarSpawnFocusBlend) < 0.001f) {
            sVrStarSpawnFocusBlend = targetBlend;
        }

        if (cutsceneActive) {
            sVrStarSpawnFocusTarget[0] = gCutsceneFocus->oPosX;
            sVrStarSpawnFocusTarget[1] =
                gCutsceneFocus->oPosY +
                fmaxf(gCutsceneFocus->hitboxHeight, 40.0f);
            sVrStarSpawnFocusTarget[2] = gCutsceneFocus->oPosZ;
        }
        sVrStarSpawnFocusTimestamp = gGlobalTimer;
        sVrStarSpawnFocusValid = true;
    }

    const f32 blend = gRenderingInterpolated
        ? delta_interpolate_f32(
            sVrStarSpawnFocusBlendPrev,
            sVrStarSpawnFocusBlend,
            clamp(gRenderingDelta, 0.0f, 1.0f)
        )
        : sVrStarSpawnFocusBlend;
    if (blend <= 0.001f) {
        if (!cutsceneActive) {
            sVrStarSpawnFocusValid = false;
        }
        return;
    }

    Vec3f baseFocus = {
        cameraPosition[0] + forward[0] * 100.0f,
        cameraPosition[1] + forward[1] * 100.0f,
        cameraPosition[2] + forward[2] * 100.0f
    };
    f32 unusedDistance;
    s16 basePitch;
    s16 baseYaw;
    s16 targetPitch;
    s16 targetYaw;
    vec3f_get_dist_and_angle(
        cameraPosition,
        baseFocus,
        &unusedDistance,
        &basePitch,
        &baseYaw
    );
    vec3f_get_dist_and_angle(
        cameraPosition,
        sVrStarSpawnFocusTarget,
        &unusedDistance,
        &targetPitch,
        &targetYaw
    );

    const f32 smoothBlend = blend * blend * (3.0f - 2.0f * blend);
    const s16 blendedPitch = (s16)(
        basePitch +
        (s32)roundf((f32)(s16)(targetPitch - basePitch) * smoothBlend)
    );
    const s16 blendedYaw = (s16)(
        baseYaw +
        (s32)roundf((f32)(s16)(targetYaw - baseYaw) * smoothBlend)
    );
    forward[0] = coss(blendedPitch) * sins(blendedYaw);
    forward[1] = sins(blendedPitch);
    forward[2] = coss(blendedPitch) * coss(blendedYaw);
}

static bool vr_get_stabilized_first_person_pose(
    Vec3f cameraPosition,
    Vec3f cameraFocus,
    Vec3f forward
) {
    vr_refresh_tracking_origin();

    Vec3f marioAnchor;
    if (!vr_get_first_person_anchor(marioAnchor)) {
        return false;
    }

    // Camera triggers, tunnels, doors, and dialogue cutscenes are still
    // allowed to advance for game logic, but the shared first-person helper
    // supplies the stable world-space direction used by both level and sky.
    vec3f_set(forward, 0.0f, 0.0f, 1.0f);
    vr_adjust_first_person_camera_direction(forward);

    Vec3f trueFirstPersonUp;
    Vec3f trueFirstPersonForward;
    if (vr_get_true_first_person_body_basis(
            trueFirstPersonForward,
            trueFirstPersonUp
        )) {
        vec3f_copy(forward, trueFirstPersonForward);
    }

    const s32 cameraDepthOffset =
        (s32)clamp(
            configVrCameraDepth,
            0U,
            VR_CAMERA_DEPTH_MAX
        ) - (s32)VR_CAMERA_DEPTH_CENTER;
    const f32 cameraDepth = 10.0f + (f32)cameraDepthOffset;
    u8 characterIndex =
        gNetworkPlayers[0].overrideModelIndex;
    if (characterIndex >= CT_MAX) {
        characterIndex = CT_MARIO;
    }
    const unsigned int cameraHeight =
        *config_vr_camera_height_for_character(characterIndex);
    const s32 cameraHeightOffset = (s32)clamp(
        cameraHeight,
        0U,
        VR_CAMERA_HEIGHT_MAX
    );
    Vec3f cameraAnchor;
    if (!vr_get_mounted_hat_camera_position(
            marioAnchor,
            cameraAnchor
        )) {
        cameraAnchor[0] = marioAnchor[0];
        cameraAnchor[1] = marioAnchor[1] +
            (f32)cameraHeightOffset;
        cameraAnchor[2] = marioAnchor[2];
    }
    // Keep the regular first-person eye point just above the animated upper
    // torso. This small constant clearance prevents running poses from
    // exposing the inside of the chest without changing saved height values.
    cameraAnchor[1] += 7.0f;
    cameraAnchor[1] -= vr_get_smoothed_sinking_camera_depth();
    cameraAnchor[1] -= vr_get_ledge_camera_drop();
    cameraPosition[0] = cameraAnchor[0] +
        forward[0] * cameraDepth;
    cameraPosition[1] = cameraAnchor[1];
    cameraPosition[2] = cameraAnchor[2] +
        forward[2] * cameraDepth;
    // Star-spawn cutscenes may temporarily rotate the view toward their
    // focus, but the calibrated camera position remains completely intact.
    vr_apply_star_spawn_camera_focus(cameraPosition, forward);
    cameraFocus[0] = cameraPosition[0] + forward[0] * 100.0f;
    cameraFocus[1] = cameraPosition[1] + forward[1] * 100.0f;
    cameraFocus[2] = cameraPosition[2] + forward[2] * 100.0f;

    Vec3f climbCameraOffset;
    if (vr_hand_interaction_get_climb_camera_offset(
            climbCameraOffset
        )) {
        for (u32 axis = 0; axis < 3; axis++) {
            cameraPosition[axis] += climbCameraOffset[axis];
            cameraFocus[axis] += climbCameraOffset[axis];
        }
    }

    // Keep the rendered HMD on the visible side of vertical world geometry.
    // This is wall-only; floor/ceiling movement remains owned by Mario physics.
    Vec3f cameraBeforeCollision;
    vec3f_copy(cameraBeforeCollision, cameraPosition);
    if (vr_hand_interaction_resolve_headset_camera_position(cameraPosition)) {
        for (u32 axis = 0; axis < 3; axis++) {
            cameraFocus[axis] += cameraPosition[axis] -
                cameraBeforeCollision[axis];
        }
    }
    return true;
}

static bool vr_get_head_tracked_audio_vector(
    const Vec3f sourceWorldPosition,
    Vec3f cameraToObject
) {
    if (!sVrHeadTrackedAudioEnabledForFrame) {
        return false;
    }

    if (!sVrAudioListenerPreparedForFrame) {
        // Object traversal can contain thousands of nodes. Resolve the
        // listener exactly once for this graph build, including on a failed
        // tracking sample, rather than repeating camera/HMD work per object.
        sVrAudioListenerPreparedForFrame = true;
        Vec3f focus;
        Vec3f forward;
        if (!vr_get_stabilized_first_person_pose(
                sVrAudioListenerPosition,
                focus,
                forward
            )) {
            sVrAudioListenerValid = false;
            return false;
        }

        // Only headset yaw controls left/right panning. Looking up, action
        // flips, and True First Person roll must never rotate the sound field
        // into a permanently tilted pair of ears.
        const f32 horizontalLength = sqrtf(
            forward[0] * forward[0] +
            forward[2] * forward[2]
        );
        if (!isfinite(horizontalLength) || horizontalLength <= 0.0001f) {
            sVrAudioListenerValid = false;
            return false;
        }
        sVrAudioListenerForward[0] = forward[0] / horizontalLength;
        sVrAudioListenerForward[1] = 0.0f;
        sVrAudioListenerForward[2] = forward[2] / horizontalLength;
        sVrAudioListenerRight[0] = sVrAudioListenerForward[2];
        sVrAudioListenerRight[1] = 0.0f;
        sVrAudioListenerRight[2] = -sVrAudioListenerForward[0];
        sVrAudioListenerValid = true;
    }

    if (!sVrAudioListenerValid) {
        return false;
    }

    const f32 deltaX =
        sourceWorldPosition[0] - sVrAudioListenerPosition[0];
    const f32 deltaY =
        sourceWorldPosition[1] - sVrAudioListenerPosition[1];
    const f32 deltaZ =
        sourceWorldPosition[2] - sVrAudioListenerPosition[2];
    cameraToObject[0] =
        deltaX * sVrAudioListenerRight[0] +
        deltaZ * sVrAudioListenerRight[2];
    cameraToObject[1] = deltaY;
    cameraToObject[2] =
        deltaX * sVrAudioListenerForward[0] +
        deltaZ * sVrAudioListenerForward[2];
    return true;
}

static bool vr_get_true_first_person_camera_height(f32* height) {
    struct MarioBodyState* bodyState =
        gMarioStates[0].marioBodyState;
    if (height == NULL ||
        bodyState == NULL ||
        (bodyState->updateHeadPosTime != gGlobalTimer &&
         (gGlobalTimer == 0 ||
          bodyState->updateHeadPosTime != gGlobalTimer - 1))) {
        return false;
    }

    // geo_mario_head_rotation receives its matrix before the animated head
    // part's translation, which made the old anchor land in Mario's chest.
    // This is the actual head/neck joint and inherits the torso-height
    // adjustment, so it remains on top of the torso at every model height.
    const f32 animatedHeadHeight =
        bodyState->animPartsPos[MARIO_ANIM_PART_HEAD][1];
    const f32 animatedTorsoHeight =
        bodyState->animPartsPos[MARIO_ANIM_PART_TORSO][1];
    // The head part origin is a neck/attachment point and can sit visibly
    // inside the upper torso on some built-in and replacement models. Derive
    // an eye-level offset from that model's own torso-to-head span, with
    // conservative bounds for unusually proportioned character packs.
    const f32 eyeOffset = clamp(
        fabsf(animatedHeadHeight - animatedTorsoHeight) * 0.35f,
        45.0f,
        70.0f
    );
    const f32 sampledHeight = animatedHeadHeight + eyeOffset;
    if (!isfinite(sampledHeight) ||
        sampledHeight < -100000.0f ||
        sampledHeight > 100000.0f) {
        return false;
    }

    const u32 sampledTimestamp = bodyState->updateHeadPosTime;
    if (!sVrTrueFirstPersonHeightValid ||
        sampledTimestamp != sVrTrueFirstPersonHeightTimestamp) {
        const bool continuous =
            sVrTrueFirstPersonHeightValid &&
            sampledTimestamp ==
                sVrTrueFirstPersonHeightTimestamp + 1;
        sVrTrueFirstPersonHeightPrev = continuous
            ? sVrTrueFirstPersonHeight
            : sampledHeight;
        sVrTrueFirstPersonHeight = sampledHeight;
        sVrTrueFirstPersonHeightTimestamp = sampledTimestamp;
        sVrTrueFirstPersonHeightValid = true;
    }

    const f32 delta = gRenderingInterpolated
        ? clamp(gRenderingDelta, 0.0f, 1.0f)
        : 1.0f;
    *height = sVrTrueFirstPersonHeightPrev +
        (sVrTrueFirstPersonHeight -
         sVrTrueFirstPersonHeightPrev) * delta;
    return true;
}

static bool vr_build_true_first_person_lookat(
    Mat4 matrix,
    Vec3f from,
    Vec3f to
) {
    Vec3f bodyForward;
    Vec3f desiredUp;
    if (!vr_get_true_first_person_body_basis(
            bodyForward,
            desiredUp
        )) {
        return false;
    }

    Vec3f backward = {
        from[0] - to[0],
        from[1] - to[1],
        from[2] - to[2]
    };
    f32 length = sqrtf(
        backward[0] * backward[0] +
        backward[1] * backward[1] +
        backward[2] * backward[2]
    );
    if (!isfinite(length) || length <= 0.001f) {
        return false;
    }
    vec3f_mul(backward, 1.0f / length);

    Vec3f right;
    vec3f_cross(right, desiredUp, backward);
    length = sqrtf(
        right[0] * right[0] +
        right[1] * right[1] +
        right[2] * right[2]
    );
    if (!isfinite(length) || length <= 0.001f) {
        return false;
    }
    vec3f_mul(right, 1.0f / length);

    Vec3f up;
    vec3f_cross(up, backward, right);
    length = sqrtf(
        up[0] * up[0] +
        up[1] * up[1] +
        up[2] * up[2]
    );
    if (!isfinite(length) || length <= 0.001f) {
        return false;
    }
    vec3f_mul(up, 1.0f / length);

    matrix[0][0] = right[0];
    matrix[1][0] = right[1];
    matrix[2][0] = right[2];
    matrix[3][0] = -(
        from[0] * right[0] +
        from[1] * right[1] +
        from[2] * right[2]
    );
    matrix[0][1] = up[0];
    matrix[1][1] = up[1];
    matrix[2][1] = up[2];
    matrix[3][1] = -(
        from[0] * up[0] +
        from[1] * up[1] +
        from[2] * up[2]
    );
    matrix[0][2] = backward[0];
    matrix[1][2] = backward[1];
    matrix[2][2] = backward[2];
    matrix[3][2] = -(
        from[0] * backward[0] +
        from[1] * backward[1] +
        from[2] * backward[2]
    );
    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
    matrix[3][3] = 1.0f;
    return true;
}

static void vr_build_mode_aware_lookat(
    Mat4 matrix,
    Vec3f from,
    Vec3f to,
    s16 roll
) {
    if (!vr_build_true_first_person_lookat(matrix, from, to)) {
        mtxf_lookat(matrix, from, to, roll);
    }
}

static void vr_build_game_camera_matrix(
    Mat4 matrix,
    Vec3f position,
    Vec3f focus,
    s16 roll
) {
    // The VR camera mode remains saved while VR is disabled so it is ready
    // the next time the headset is enabled. It must not, however, replace the
    // normal single-screen camera. Doing so renders a first-person view while
    // Mario's vanilla stick yaw is still based on the third-person camera,
    // making every flat-screen direction appear exactly 180 degrees reversed.
    if (!vr_is_active()) {
        sVrFirstPersonAnchorValid = false;
        mtxf_lookat(matrix, position, focus, roll);
        return;
    }

    Vec3f cameraPosition;
    Vec3f cameraFocus;
    vec3f_copy(cameraPosition, position);
    vec3f_copy(cameraFocus, focus);

    if (configVrCameraMode == VR_CAMERA_MODE_THIRD_PERSON) {
        sVrFirstPersonAnchorValid = false;
        const float distanceScale =
            (float)clamp(configVrCameraDistance, 50U, 250U) / 100.0f;

        for (int axis = 0; axis < 3; axis++) {
            cameraPosition[axis] = focus[axis] +
                (position[axis] - focus[axis]) * distanceScale;
        }
    } else if (configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON) {
        Vec3f forward;
        if (vr_get_stabilized_first_person_pose(
                cameraPosition,
                cameraFocus,
                forward
            )) {

            f32 trueFirstPersonHeight;
            if (!vr_first_person_true_diving_active() &&
                vr_is_active() &&
                configVrExperimentalTrueFirstPerson &&
                vr_get_true_first_person_camera_height(
                    &trueFirstPersonHeight
                )) {
                // Use only the animated head/neck-joint height. Horizontal
                // placement
                // stays on the non-recursive gameplay anchor; tracked HMD
                // translation is applied later and the rendered body follows
                // it independently.
                cameraPosition[1] = trueFirstPersonHeight;
            } else if (!vr_first_person_true_diving_active() &&
                       vr_is_active() &&
                       configVrExperimentalTrueFirstPerson) {
                // The skeleton has not produced its first head point yet.
                // Use Mario's normal neck height instead of consulting the
                // user camera-height slider during this single-frame fallback.
                cameraPosition[1] =
                    gMarioStates[0].pos[1] + 155.0f;
            }

            cameraFocus[0] = cameraPosition[0] + forward[0] * 100.0f;
            cameraFocus[1] = cameraPosition[1] + forward[1] * 100.0f;
            cameraFocus[2] = cameraPosition[2] + forward[2] * 100.0f;
        }
    }

    // Keep the game's camera transform independent from the headset. The
    // HMD pose is applied later in the projection path so world-space
    // lighting, billboards, and other camera-sensitive effects do not turn
    // with the player's head.
    const s16 cameraRoll =
        vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON
            ? 0
            : roll;
    vr_build_mode_aware_lookat(
        matrix,
        cameraPosition,
        cameraFocus,
        cameraRoll
    );
}

static bool vr_calculate_stabilized_headset_world_position(
    Vec3f worldPosition
) {
    const float worldUnitsPerMeter = 100.0f;
    float headTranslation[3] = { 0 };
    Vec3f cameraPosition;
    Vec3f cameraFocus;
    Vec3f forward;

    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        worldPosition == NULL ||
        sCameraNode == NULL ||
        !vr_get_head_translation(headTranslation) ||
        !vr_get_stabilized_first_person_pose(
            cameraPosition,
            cameraFocus,
            forward
    )) {
        return false;
    }

    vr_apply_roomscale_tracking_compensation(headTranslation);

    Mat4 cameraMatrix;
    Mat4 inverseCameraMatrix;
    vr_build_mode_aware_lookat(
        cameraMatrix,
        cameraPosition,
        cameraFocus,
        0
    );
    mtxf_inverse(inverseCameraMatrix, cameraMatrix);

    Vec3f localHeadPosition = {
        headTranslation[0] * worldUnitsPerMeter,
        headTranslation[1] * worldUnitsPerMeter,
        headTranslation[2] * worldUnitsPerMeter
    };
    for (u32 axis = 0; axis < 3; axis++) {
        worldPosition[axis] =
            localHeadPosition[0] * inverseCameraMatrix[0][axis] +
            localHeadPosition[1] * inverseCameraMatrix[1][axis] +
            localHeadPosition[2] * inverseCameraMatrix[2][axis] +
            inverseCameraMatrix[3][axis];
    }

    // Camera depth is a view-to-body calibration. Do not let it drag the
    // body root along with the camera or the relative adjustment would cancel
    // itself out visually. Floating hands and their hitboxes intentionally
    // remain camera-relative.
    const s32 cameraDepthOffset =
        (s32)clamp(
            configVrCameraDepth,
            0U,
            VR_CAMERA_DEPTH_MAX
        ) - (s32)VR_CAMERA_DEPTH_CENTER;
    worldPosition[0] -= forward[0] * (f32)cameraDepthOffset;
    worldPosition[2] -= forward[2] * (f32)cameraDepthOffset;
    // Apply the same wall guard after tracked head translation so a physical
    // lean cannot place the final first-person view inside a pillar or wall.
    vr_hand_interaction_resolve_headset_camera_position(worldPosition);
    return vr_move_world_sample_to_current_gameplay_anchor(
        worldPosition
    );
}

bool vr_get_stabilized_headset_world_position(
    Vec3f worldPosition,
    bool previousFrame
) {
    if (worldPosition == NULL) {
        return false;
    }

    if (!sVrBodyAnchorSampleValid ||
        sVrBodyAnchorSampleTimestamp != gGlobalTimer) {
        Vec3f sampledPosition;
        if (!vr_calculate_stabilized_headset_world_position(
                sampledPosition
            )) {
            sVrBodyAnchorSampleValid = false;
            return false;
        }

        const bool continuous =
            sVrBodyAnchorSampleValid &&
            gGlobalTimer == sVrBodyAnchorSampleTimestamp + 1;
        if (continuous) {
            vec3f_copy(
                sVrBodyAnchorSamplePrev,
                sVrBodyAnchorSample
            );
        } else {
            vec3f_copy(
                sVrBodyAnchorSamplePrev,
                sampledPosition
            );
        }
        vec3f_copy(sVrBodyAnchorSample, sampledPosition);
        sVrBodyAnchorSampleTimestamp = gGlobalTimer;
        sVrBodyAnchorSampleValid = true;
    }

    vec3f_copy(
        worldPosition,
        previousFrame
            ? sVrBodyAnchorSamplePrev
            : sVrBodyAnchorSample
    );
    return true;
}

bool vr_get_roomscale_body_displacement(Vec3f worldDisplacement) {
    const float worldUnitsPerMeter = 100.0f;
    float headTranslation[3] = { 0.0f, 0.0f, 0.0f };
    Vec3f cameraPosition;
    Vec3f cameraFocus;
    Vec3f forward;

    if (worldDisplacement == NULL ||
        !vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !vr_get_head_translation(headTranslation) ||
        !vr_get_stabilized_first_person_pose(
            cameraPosition,
            cameraFocus,
            forward
        )) {
        vr_reset_roomscale_body_tracking();
        return false;
    }

    Vec3f rawLocal = {
        headTranslation[0] * worldUnitsPerMeter,
        0.0f,
        headTranslation[2] * worldUnitsPerMeter
    };
    if (!sVrRoomscaleBodyTrackingValid) {
        vec3f_copy(sVrRoomscaleConsumedLocal, rawLocal);
        vec3f_copy(sVrRoomscaleConsumedLocalPrev, rawLocal);
        sVrRoomscaleConsumedTimestamp = gGlobalTimer;
        sVrRoomscaleBodyTrackingValid = true;
        vec3f_set(worldDisplacement, 0.0f, 0.0f, 0.0f);
        return true;
    }

    Vec3f remainingLocal = {
        rawLocal[0] - sVrRoomscaleConsumedLocal[0],
        0.0f,
        rawLocal[2] - sVrRoomscaleConsumedLocal[2]
    };
    const f32 remainingLength = sqrtf(
        remainingLocal[0] * remainingLocal[0] +
        remainingLocal[2] * remainingLocal[2]
    );
    // Discard sub-centimetre tracking noise instead of leaving it in the
    // unconsumed remainder. Accumulating tiny oscillations eventually emits
    // them as a visible burst that makes the body twitch while standing or
    // taking small real-world steps.
    if (remainingLength < 0.75f) {
        vec3f_copy(sVrRoomscaleConsumedLocal, rawLocal);
        vec3f_copy(sVrRoomscaleConsumedLocalPrev, rawLocal);
        sVrRoomscaleConsumedTimestamp = gGlobalTimer;
        vec3f_set(worldDisplacement, 0.0f, 0.0f, 0.0f);
        return true;
    }
    // Runtime recentering and tracking-origin replacement can jump several
    // metres in one sample. Adopt that as the new neutral point instead of
    // teleporting Mario through the level.
    if (remainingLength > 125.0f) {
        vec3f_copy(sVrRoomscaleConsumedLocal, rawLocal);
        vec3f_copy(sVrRoomscaleConsumedLocalPrev, rawLocal);
        sVrRoomscaleConsumedTimestamp = gGlobalTimer;
        sVrRoomscaleBasisValid = false;
        vec3f_set(worldDisplacement, 0.0f, 0.0f, 0.0f);
        vr_invalidate_first_person_tracked_world_cache();
        return true;
    }

    Mat4 cameraMatrix;
    Mat4 inverseCameraMatrix;
    vr_build_mode_aware_lookat(
        cameraMatrix,
        cameraPosition,
        cameraFocus,
        0
    );
    mtxf_inverse(inverseCameraMatrix, cameraMatrix);
    sVrRoomscaleWorldFromLocal[0][0] = inverseCameraMatrix[0][0];
    sVrRoomscaleWorldFromLocal[0][1] = inverseCameraMatrix[0][2];
    sVrRoomscaleWorldFromLocal[1][0] = inverseCameraMatrix[2][0];
    sVrRoomscaleWorldFromLocal[1][1] = inverseCameraMatrix[2][2];
    sVrRoomscaleBasisValid = true;

    worldDisplacement[0] =
        remainingLocal[0] * sVrRoomscaleWorldFromLocal[0][0] +
        remainingLocal[2] * sVrRoomscaleWorldFromLocal[1][0];
    worldDisplacement[1] = 0.0f;
    worldDisplacement[2] =
        remainingLocal[0] * sVrRoomscaleWorldFromLocal[0][1] +
        remainingLocal[2] * sVrRoomscaleWorldFromLocal[1][1];
    return true;
}

void vr_commit_roomscale_body_displacement(
    const Vec3f worldDisplacement
) {
    if (!sVrRoomscaleBodyTrackingValid ||
        !sVrRoomscaleBasisValid ||
        worldDisplacement == NULL) {
        return;
    }

    const f32 a = sVrRoomscaleWorldFromLocal[0][0];
    const f32 b = sVrRoomscaleWorldFromLocal[1][0];
    const f32 c = sVrRoomscaleWorldFromLocal[0][1];
    const f32 d = sVrRoomscaleWorldFromLocal[1][1];
    const f32 determinant = a * d - b * c;
    if (fabsf(determinant) < 0.0001f) {
        return;
    }
    if (sVrRoomscaleConsumedTimestamp != gGlobalTimer) {
        vec3f_copy(
            sVrRoomscaleConsumedLocalPrev,
            sVrRoomscaleConsumedLocal
        );
        sVrRoomscaleConsumedTimestamp = gGlobalTimer;
    }
    sVrRoomscaleConsumedLocal[0] +=
        (worldDisplacement[0] * d -
         b * worldDisplacement[2]) / determinant;
    sVrRoomscaleConsumedLocal[2] +=
        (a * worldDisplacement[2] -
         worldDisplacement[0] * c) / determinant;

    // Room-scale transfer moves Mario's authoritative position and the
    // tracked world anchor by the same resolved amount. If the render sample
    // was already requested this gameplay frame, advance only its current
    // endpoint; retaining the previous endpoint lets normal render-delta
    // interpolation keep the visible torso smooth at 72/90/120 Hz.
    if (sVrBodyAnchorSampleValid &&
        sVrBodyAnchorSampleTimestamp == gGlobalTimer) {
        sVrBodyAnchorSample[0] += worldDisplacement[0];
        sVrBodyAnchorSample[2] += worldDisplacement[2];
    }

    // Controller conversion depends on the current camera transform, so it
    // must be refreshed. Do not invalidate the body history here: doing so
    // collapses previous/current to one 30 Hz sample and visibly jitters the
    // otherwise correctly synchronized body beneath the HMD.
    sVrControllerCameraInverseValid = false;
    sVrControllerCameraInverseTimestamp = 0;
    sVrControllerCameraInverseDelta = -1.0f;
    for (u32 hand = 0; hand < VR_CONTROLLER_COUNT; hand++) {
        sVrArmTargetSampleValid[hand] = false;
        sVrArmTargetSampleTimestamp[hand] = 0;
    }
}

static s16 vr_get_stabilized_body_yaw(bool previousFrame) {
    if (!sVrBodyYawSampleValid ||
        sVrBodyYawSampleTimestamp != gGlobalTimer) {
        const s16 sampledYaw = vr_get_first_person_view_yaw();
        const bool continuous =
            sVrBodyYawSampleValid &&
            gGlobalTimer == sVrBodyYawSampleTimestamp + 1;
        sVrBodyYawSamplePrev = continuous
            ? sVrBodyYawSample
            : sampledYaw;
        sVrBodyYawSample = sampledYaw;
        sVrBodyYawSampleTimestamp = gGlobalTimer;
        sVrBodyYawSampleValid = true;
    }
    return previousFrame
        ? sVrBodyYawSamplePrev
        : sVrBodyYawSample;
}

#define VR_BIG_HANDS_REACH_MAX_DISTANCE 240.0f

static f32 vr_big_hands_reach_scale(void) {
    const unsigned int setting = clamp(
        configVrBigHandsReach,
        VR_BIG_HANDS_REACH_MIN,
        VR_BIG_HANDS_REACH_MAX
    );
    // Preserve the slider curve with 1.5x greater reach in every direction:
    // 150 now gives 7.5x and 300 gives 15x. The distance cap below scales too.
    if (setting <= 150U) {
        return 1.5f * (1.0f + (f32)setting * 4.0f / 150.0f);
    }
    return 1.5f * (5.0f +
        ((f32)setting - 150.0f) * 5.0f / 150.0f);
}

/*
 * Big Hands keeps the real controller close to the player while extending
 * the interaction point toward the same direction. Keeping this in world
 * space makes every caller
 * (grabs, pickups, punches, and climbing) agree on the same reach.
 */
static void vr_apply_big_hands_reach_extension(Vec3f worldPosition) {
    if (worldPosition == NULL || !vr_special_moves_big_hands_active()) {
        return;
    }

    Vec3f bodyPosition;
    // Raw controller conversion and the target extension use the same
    // camera-space basis. Prefer that same-frame origin so the offset cannot
    // collapse when the stabilized body sample is one frame out of date.
    if (sVrControllerCameraInverseValid) {
        vec3f_copy(bodyPosition, sVrControllerCameraInverse[3]);
    } else if (!vr_get_stabilized_headset_world_position(
            bodyPosition,
            false
        )) {
        return;
    }

    Vec3f offset = {
        worldPosition[0] - bodyPosition[0],
        worldPosition[1] - bodyPosition[1],
        worldPosition[2] - bodyPosition[2]
    };
    const f32 distanceSquared =
        offset[0] * offset[0] +
        offset[1] * offset[1] +
        offset[2] * offset[2];
    const f32 distance = sqrtf(distanceSquared);
    if (distance <= 0.001f) {
        return;
    }

    // Apply the configured reach multiplier to the complete hand offset.
    // There is no dead-zone: even hands held close to the headset receive
    // the requested extension, so the slider cannot appear to only resize
    // the glove model.
    const f32 reachScale = vr_big_hands_reach_scale();
    const f32 maxDistance = 240.0f * (reachScale / 4.0f);
    const f32 extendedDistance = fminf(distance * reachScale, maxDistance);
    const f32 distanceScale = extendedDistance / distance;
    for (u32 axis = 0; axis < 3; axis++) {
        worldPosition[axis] = bodyPosition[axis] +
            offset[axis] * distanceScale;
    }
}

static void vr_big_hands_build_reach_target(
    Vec3f rawWorldPosition,
    Vec3f targetWorldPosition
) {
    if (rawWorldPosition == NULL || targetWorldPosition == NULL) {
        return;
    }

    vec3f_copy(targetWorldPosition, rawWorldPosition);
    if (!vr_special_moves_big_hands_active()) {
        return;
    }

    // Extend in stable world space. The camera inverse is render-frame state
    // and may not be populated yet when gameplay samples hand targets; using
    // it here silently disabled reach during those frames. This helper uses
    // the stabilized headset position and is shared by all interaction paths.
    vr_apply_big_hands_reach_extension(targetWorldPosition);
}

bool vr_get_controller_world_fist_reach_target_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition,
    Vec3f worldVelocity
) {
    if (!vr_get_controller_world_fist_raw_from_state(
            handIndex,
            state,
            worldPosition,
            worldVelocity
        )) {
        return false;
    }
    vr_big_hands_build_reach_target(worldPosition, worldPosition);
    return true;
}
bool vr_get_controller_world_fist_raw_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition,
    Vec3f worldVelocity
) {
    const float worldUnitsPerMeter = 100.0f;

    if (!vr_is_active() ||
        handIndex >= VR_CONTROLLER_COUNT ||
        state == NULL ||
        worldPosition == NULL ||
        sCameraNode == NULL ||
        (!state->gripPoseValid && !state->aimPoseValid)) {
        return false;
    }

    const float* position = state->gripPoseValid
        ? state->gripPosition
        : state->aimPosition;
    const float* rotation = state->aimPoseValid
        ? state->aimRotation
        : state->gripRotation;
    Vec3f right = { 1.0f, 0.0f, 0.0f };
    Vec3f up = { 0.0f, 1.0f, 0.0f };
    Vec3f backward = { 0.0f, 0.0f, 1.0f };
    vr_rotate_pose_vector(rotation, right, right);
    vr_rotate_pose_vector(rotation, up, up);
    vr_rotate_pose_vector(rotation, backward, backward);

    const bool leftHand = handIndex == VR_CONTROLLER_LEFT;
    const unsigned int positionX = leftHand
        ? configVrLeftGlovePositionX
        : configVrRightGlovePositionX;
    const unsigned int positionY = leftHand
        ? configVrLeftGlovePositionY
        : configVrRightGlovePositionY;
    const unsigned int positionZ = leftHand
        ? configVrLeftGlovePositionZ
        : configVrRightGlovePositionZ;

    const float positionOffsetX =
        ((float)clamp(positionX, 0U, 200U) - 100.0f) *
            0.5f;
    const float positionOffsetY =
        ((float)clamp(positionY, 0U, 200U) - 100.0f) *
            0.5f;
    const float positionOffsetZ =
        ((float)clamp(positionZ, 0U, 200U) - 100.0f) *
            0.5f;
    const float gloveScale =
        (float)clamp(configVrGloveSize, 25U, 250U) / 70.0f;
    const float knuckleOffset = 6.0f * gloveScale;

    // Build the point in the same camera-local coordinate system used by the
    // floating glove renderer. The small -Z/aim offset places the collider at
    // the knuckles rather than inside the controller grip.
    Vec3f localPosition = {
        position[0] * worldUnitsPerMeter +
            right[0] * positionOffsetX +
            up[0] * positionOffsetY +
            backward[0] * positionOffsetZ -
            backward[0] * knuckleOffset,
        position[1] * worldUnitsPerMeter +
            right[1] * positionOffsetX +
            up[1] * positionOffsetY +
            backward[1] * positionOffsetZ -
            backward[1] * knuckleOffset,
        position[2] * worldUnitsPerMeter +
            right[2] * positionOffsetX +
            up[2] * positionOffsetY +
            backward[2] * positionOffsetZ -
            backward[2] * knuckleOffset
    };
    if (sVrRoomscaleBodyTrackingValid) {
        Vec3f compensation;
        vr_get_roomscale_tracking_compensation(compensation);
        localPosition[0] -= compensation[0];
        localPosition[2] -= compensation[2];
    }

    const f32 renderDelta = gRenderingInterpolated
        ? clamp(gRenderingDelta, 0.0f, 1.0f)
        : -1.0f;
    if (!sVrControllerCameraInverseValid ||
        sVrControllerCameraInverseTimestamp != gGlobalTimer ||
        sVrControllerCameraInverseDelta != renderDelta) {
        Mat4 cameraMatrix;
        Vec3f cameraPos;
        Vec3f cameraFocus;
        if (gRenderingInterpolated) {
            delta_interpolate_vec3f(
                cameraPos,
                sCameraNode->prevPos,
                sCameraNode->pos,
                renderDelta
            );
            delta_interpolate_vec3f(
                cameraFocus,
                sCameraNode->prevFocus,
                sCameraNode->focus,
                renderDelta
            );
        } else {
            vec3f_copy(cameraPos, sCameraNode->pos);
            vec3f_copy(cameraFocus, sCameraNode->focus);
        }
        vr_build_game_camera_matrix(
            cameraMatrix,
            cameraPos,
            cameraFocus,
            sCameraNode->roll
        );
        mtxf_inverse(sVrControllerCameraInverse, cameraMatrix);
        sVrControllerCameraInverseTimestamp = gGlobalTimer;
        sVrControllerCameraInverseDelta = renderDelta;
        sVrControllerCameraInverseValid = true;
    }

    for (u32 axis = 0; axis < 3; axis++) {
        worldPosition[axis] =
            localPosition[0] * sVrControllerCameraInverse[0][axis] +
            localPosition[1] * sVrControllerCameraInverse[1][axis] +
            localPosition[2] * sVrControllerCameraInverse[2][axis] +
            sVrControllerCameraInverse[3][axis];
    }


    if (worldVelocity != NULL) {
        vec3f_set(worldVelocity, 0.0f, 0.0f, 0.0f);
        if (state->gripLinearVelocityValid) {
            Vec3f localVelocity = {
                state->gripLinearVelocity[0] * worldUnitsPerMeter,
                state->gripLinearVelocity[1] * worldUnitsPerMeter,
                state->gripLinearVelocity[2] * worldUnitsPerMeter
            };
            for (u32 axis = 0; axis < 3; axis++) {
                worldVelocity[axis] =
                    localVelocity[0] * sVrControllerCameraInverse[0][axis] +
                    localVelocity[1] * sVrControllerCameraInverse[1][axis] +
                    localVelocity[2] * sVrControllerCameraInverse[2][axis];
            }
        }
    }

    return true;
}

bool vr_get_controller_world_fist_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition,
    Vec3f worldVelocity
) {
    if (!vr_get_controller_world_fist_raw_from_state(
            handIndex,
            state,
            worldPosition,
            worldVelocity
        )) {
        return false;
    }
    vr_hand_interaction_apply_hand_collision_position(
        handIndex,
        worldPosition
    );
    return true;
}

bool vr_get_controller_world_palm_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition
) {
    Vec3f rawFist;
    if (!vr_get_controller_world_fist_raw_from_state(
            handIndex,
            state,
            rawFist,
            NULL
        )) {
        return false;
    }

    const float* rotation = state->aimPoseValid
        ? state->aimRotation
        : state->gripRotation;
    Vec3f right = { 1.0f, 0.0f, 0.0f };
    Vec3f backward = { 0.0f, 0.0f, 1.0f };
    vr_rotate_pose_vector(rotation, right, right);
    vr_rotate_pose_vector(rotation, backward, backward);

    const f32 gloveScale =
        (f32)clamp(configVrGloveSize, 25U, 250U) / 70.0f;
    // Move the center forward toward the fingertips and farther above the
    // palm surface while preserving the controller's full orientation.
    const f32 knuckleToPalm = -4.0f * gloveScale;
    const f32 palmSurface = 16.0f * gloveScale;
    const f32 palmNormalSign =
        handIndex == VR_CONTROLLER_RIGHT ? -1.0f : 1.0f;
    Vec3f localOffset = {
        backward[0] * knuckleToPalm +
            right[0] * palmSurface * palmNormalSign,
        backward[1] * knuckleToPalm +
            right[1] * palmSurface * palmNormalSign,
        backward[2] * knuckleToPalm +
            right[2] * palmSurface * palmNormalSign
    };

    Vec3f constrainedFist;
    vec3f_copy(constrainedFist, rawFist);
    vr_hand_interaction_apply_hand_collision_position(
        handIndex,
        constrainedFist
    );
    for (u32 axis = 0; axis < 3; axis++) {
        worldPosition[axis] = constrainedFist[axis] +
            localOffset[0] * sVrControllerCameraInverse[0][axis] +
            localOffset[1] * sVrControllerCameraInverse[1][axis] +
            localOffset[2] * sVrControllerCameraInverse[2][axis];
    }
    return true;
}

bool vr_get_controller_climb_fist(
    const Vec3f worldPosition,
    Vec3f climbPosition
) {
    if (worldPosition == NULL ||
        climbPosition == NULL ||
        !sVrControllerCameraInverseValid ||
        sVrControllerCameraInverseTimestamp != gGlobalTimer) {
        return false;
    }

    // Climbing consumes controller motion in a translation-free tracking
    // frame. The ordinary world fist includes the camera/climb translation;
    // feeding that position back into the camera offset makes every pull
    // produce an equal-and-opposite delta on the next tick. Removing only the
    // inverse-camera translation preserves the correct world-axis rotation
    // while making each physical controller delta independent of the camera
    // motion it creates.
    for (u32 axis = 0; axis < 3; axis++) {
        climbPosition[axis] = worldPosition[axis] -
            sVrControllerCameraInverse[3][axis];
    }
    return true;
}

bool vr_get_controller_world_fist(
    u32 handIndex,
    Vec3f worldPosition,
    Vec3f worldVelocity
) {
    struct VrControllerState state;
    if (!vr_get_controller_state(handIndex, &state)) {
        return false;
    }
    return vr_get_controller_world_fist_from_state(
        handIndex,
        &state,
        worldPosition,
        worldVelocity
    );
}

static bool vr_is_menu_scene(void) {
    static s32 sLuaMenuModIndex = -1;
    static u32 sLuaMenuNextSearchTimestamp = 0;
    static bool sLuaFullscreenMenuActive = false;
    static bool sMenuSceneCacheValid = false;
    static u32 sMenuSceneCacheTimestamp = 0;
    static bool sMenuSceneCache = false;
    bool menuActive = false;

    // Lua and area state only advance on the 30 Hz game tick. Rendering may
    // ask this several times for both eyes at 72/90 Hz, so reuse the answer
    // for the rest of the logical tick.
    if (sMenuSceneCacheValid &&
        sMenuSceneCacheTimestamp == gGlobalTimer) {
        return sMenuSceneCache;
    }

    if (sLuaMenuModIndex >= 0 &&
        smlua_get_boolean_mod_variable(
            (u16)sLuaMenuModIndex,
            "menuAndTransition",
            &menuActive
        )) {
        sLuaFullscreenMenuActive = menuActive;
    } else {
        sLuaMenuModIndex = -1;
        sLuaFullscreenMenuActive = false;
        if (gGlobalTimer >= sLuaMenuNextSearchTimestamp) {
            sLuaMenuModIndex = smlua_find_boolean_mod_variable(
                "menuAndTransition",
                &menuActive
            );
            sLuaFullscreenMenuActive =
                sLuaMenuModIndex >= 0 && menuActive;
            sLuaMenuNextSearchTimestamp = gGlobalTimer + 30;
        }
    }

    sMenuSceneCache = sLuaFullscreenMenuActive ||
        (gCurrentArea != NULL &&
         (const Collision *)gCurrentArea->terrainData ==
             main_menu_seg7_collision);
    sMenuSceneCacheTimestamp = gGlobalTimer;
    sMenuSceneCacheValid = true;
    return sMenuSceneCache;
}

static bool sVrEyeTangentsValid[2] = { false };
static float sVrEyeFovCache[2][4] = { 0 };
static float sVrEyeTangents[2][4] = { 0 };

static bool vr_get_eye_tangents(
    uint32_t eyeIndex,
    float tangents[4]
) {
    float eyeFov[4] = { 0 };

    if (eyeIndex >= 2 ||
        tangents == NULL ||
        !vr_get_eye_fov(eyeIndex, eyeFov)) {
        return false;
    }

    bool changed = !sVrEyeTangentsValid[eyeIndex];
    for (u32 component = 0; component < 4; component++) {
        if (eyeFov[component] !=
            sVrEyeFovCache[eyeIndex][component]) {
            changed = true;
        }
    }

    if (changed) {
        for (u32 component = 0; component < 4; component++) {
            sVrEyeFovCache[eyeIndex][component] = eyeFov[component];
            sVrEyeTangents[eyeIndex][component] =
                tanf(eyeFov[component]);
        }
        sVrEyeTangentsValid[eyeIndex] = true;
    }

    for (u32 component = 0; component < 4; component++) {
        tangents[component] = sVrEyeTangents[eyeIndex][component];
    }
    return true;
}

static bool vr_get_ui_plane_ndc(
    uint32_t eyeIndex,
    f32 *centerX,
    f32 *centerY,
    f32 *halfWidth,
    f32 *halfHeight
) {
    float eyeTangents[4] = { 0 };
    float eyeOffset[3] = { 0 };

    *centerX = 0.0f;
    *centerY = 0.0f;
    *halfWidth = 1.0f;
    *halfHeight = 1.0f;

    if (eyeIndex >= 2 ||
        !vr_get_eye_tangents(eyeIndex, eyeTangents) ||
        !vr_get_eye_offset(eyeIndex, eyeOffset)) {
        return false;
    }

    const float tanLeft = eyeTangents[0];
    const float tanRight = eyeTangents[1];
    const float tanDown = eyeTangents[2];
    const float tanUp = eyeTangents[3];
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;

    if (tanWidth <= 0.000001f || tanHeight <= 0.000001f) {
        return false;
    }

    // Match the comfortable 1.5 m head-locked plane used by the rest of the
    // VR UI, including its per-eye convergence.
    const float uiDistance = 1.5f;
    const float uiHalfHeight = uiDistance * 0.45f;
    const float uiHalfWidth = uiHalfHeight *
        ((float)SCREEN_WIDTH / (float)SCREEN_HEIGHT);
    const float eyeDepth = uiDistance + eyeOffset[2];
    const float projectionScaleX = 2.0f / tanWidth;
    const float projectionScaleY = 2.0f / tanHeight;
    const float projectionOffsetX =
        (tanRight + tanLeft) / tanWidth;
    const float projectionOffsetY =
        (tanUp + tanDown) / tanHeight;

    *centerX = -projectionOffsetX -
        projectionScaleX * eyeOffset[0] / eyeDepth;
    *centerY = -projectionOffsetY -
        projectionScaleY * eyeOffset[1] / eyeDepth;
    *halfWidth = projectionScaleX * uiHalfWidth / eyeDepth;
    *halfHeight = projectionScaleY * uiHalfHeight / eyeDepth;
    return *halfWidth > 0.000001f && *halfHeight > 0.000001f;
}

static bool vr_build_head_view_matrix(
    uint32_t eyeIndex,
    Mat4 matrix
) {
    // Tracking-origin changes can also happen while third-person is active,
    // where the first-person pose helper is not called. Refresh here so the
    // same one-shot camera reset is applied for runtime and menu recentering.
    vr_refresh_tracking_origin();
    const bool menuScene = vr_is_menu_scene();
    const float worldUnitsPerMeter = menuScene ? 850.0f : 100.0f;
    float headTranslation[3] = { 0 };

    if (eyeIndex >= 2) {
        return false;
    }

    if (menuScene) {
        // Legacy file-select and star-select models use a camera roughly 1300
        // game units away. Keep that scene head-locked and give it stereo eye
        // separation that places it near the same 1.5 m depth as the 2D UI.
        mtxf_identity(matrix);
    } else if (!vr_build_head_rotation_matrix(matrix) ||
               !vr_get_head_translation(headTranslation)) {
        return false;
    }

    if (!menuScene) {
        Vec3s syntheticRotation;
        if (vr_get_true_first_person_view_rotation(
                syntheticRotation
            )) {
            Mat4 rotationMatrix;
            Mat4 combinedMatrix;
            Vec3f zeroTranslation = { 0.0f, 0.0f, 0.0f };
            mtxf_rotate_xyz_and_translate(
                rotationMatrix,
                zeroTranslation,
                syntheticRotation
            );
            /* Append in headset-local space: no yaw or heading is replaced. */
            mtxf_mul(combinedMatrix, matrix, rotationMatrix);
            mtxf_copy(matrix, combinedMatrix);
        }
    }

    if (!menuScene) {
        vr_apply_roomscale_tracking_compensation(headTranslation);
    }

    if (!menuScene &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        (configVrExperimentalTrueFirstPerson ||
         vr_first_person_true_diving_active())) {
        // True First Person and True Diving obtain vertical motion from the
        // animated body anchor. Suppress room-scale Y translation so camera
        // height and physical crouching cannot detach the view from it;
        // horizontal leaning and full head rotation remain available.
        headTranslation[1] = 0.0f;
    }

#ifndef __ANDROID__
    if (!menuScene &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON) {
        // PC runtimes can report a LOCAL-space origin above the floor (or
        // retain a stale vertical offset after a runtime recenter). The game
        // camera already has a calibrated Mario eye anchor, so applying that
        // extra PC-only Y translation makes the player appear unnaturally
        // tall. Keep horizontal room-scale movement and headset rotation, but
        // let the configured in-game eye height own the vertical position.
        headTranslation[1] = 0.0f;
    }
#endif

    const float trackedX = headTranslation[0] * worldUnitsPerMeter;
    const float trackedY = headTranslation[1] * worldUnitsPerMeter;
    const float trackedZ = headTranslation[2] * worldUnitsPerMeter;
    matrix[3][0] = -(trackedX * matrix[0][0] +
                     trackedY * matrix[1][0] +
                     trackedZ * matrix[2][0]);
    matrix[3][1] = -(trackedX * matrix[0][1] +
                     trackedY * matrix[1][1] +
                     trackedZ * matrix[2][1]);
    matrix[3][2] = -(trackedX * matrix[0][2] +
                     trackedY * matrix[1][2] +
                     trackedZ * matrix[2][2]);
    matrix[3][3] = 1.0f;

    // Eye offsets are already expressed in the headset's local coordinate
    // system, so append them after the center-head view transform.
    float eyeOffset[3] = { 0 };
    if (vr_get_eye_offset(eyeIndex, eyeOffset)) {
        matrix[3][0] -= eyeOffset[0] * worldUnitsPerMeter;
        matrix[3][1] -= eyeOffset[1] * worldUnitsPerMeter;
        matrix[3][2] -= eyeOffset[2] * worldUnitsPerMeter;
    }

    return true;
}

/*
 * The scene display list is built once and then replayed for both OpenXR
 * eyes. Keep the two headset views used by object visibility in a per-scene
 * cache instead of querying tracking and rebuilding matrices for every
 * object. This is intentionally fail-open: a temporarily unavailable pose
 * must keep geometry visible rather than introduce a visual pop-in.
 */
struct VrObjectVisibilityState {
    bool valid;
    Mat4 headView[2];
    f32 tanLeft[2];
    f32 tanRight[2];
};

static struct VrObjectVisibilityState sVrObjectVisibility = { 0 };

static void vr_prepare_object_visibility(void) {
    sVrObjectVisibility.valid = false;

    if (!vr_is_active() || vr_is_menu_scene()) {
        return;
    }

    const f32 fovScale =
        (f32)clamp(configVrFov, 70U, 120U) / 100.0f;

    for (u32 eye = 0; eye < 2; eye++) {
        float eyeTangents[4] = { 0.0f };
        if (!vr_get_eye_tangents(eye, eyeTangents) ||
            !vr_build_head_view_matrix(eye, sVrObjectVisibility.headView[eye])) {
            return;
        }

        sVrObjectVisibility.tanLeft[eye] = eyeTangents[0] * fovScale;
        sVrObjectVisibility.tanRight[eye] = eyeTangents[1] * fovScale;
        if (sVrObjectVisibility.tanRight[eye] <=
            sVrObjectVisibility.tanLeft[eye]) {
            return;
        }
    }

    sVrObjectVisibility.valid = true;
}

/*
 * Return whether the object's bounding sphere intersects either eye's
 * horizontal frustum. matrix is already in gameplay-camera space, so applying
 * the cached headset view gives the per-eye coordinates used by the VR
 * projection. Vertical culling remains disabled on purpose: this avoids
 * popping tall/overhead custom-map geometry while removing objects wholly
 * behind or to the side of both eyes.
 */
static bool vr_object_is_in_view(Mat4 matrix, f32 cullingRadius) {
    /*
     * The object matrix passed to obj_is_in_view is already in gameplay
     * camera space. Applying the headset view here double-applies head
     * rotation, which can reject objects that are visibly in front of the
     * player (especially while looking down or moving around corners).
     * Keep this path fail-open until visibility is evaluated in the same
     * coordinate space as the final eye projection. The normal distance
     * guard in obj_is_in_view still bounds far-away dynamic objects.
     */
    (void)matrix;
    (void)cullingRadius;
    return true;
}
static bool vr_world_pos_to_eye_ndc(
    Vec3f worldPosition,
    u32 eyeIndex,
    bool centerEye,
    Vec3f ndcPosition
) {
    if (!vr_is_active() || gCamera == NULL) {
        return false;
    }

    Mat4 headView;
    if (eyeIndex >= 2 || !vr_build_head_view_matrix(eyeIndex, headView)) {
        return false;
    }

    // Center-eye projection is used only for visibility/sorting before the
    // shared display list is built. The projection matrix itself is patched
    // once per submitted eye below and deliberately keeps the eye offset.
    float eyeOffset[3] = { 0 };
    if (centerEye && vr_get_eye_offset(eyeIndex, eyeOffset)) {
        const float worldUnitsPerMeter = vr_is_menu_scene() ? 850.0f : 100.0f;
        headView[3][0] += eyeOffset[0] * worldUnitsPerMeter;
        headView[3][1] += eyeOffset[1] * worldUnitsPerMeter;
        headView[3][2] += eyeOffset[2] * worldUnitsPerMeter;
    }

    Vec3f cameraPosition;
    Vec3f headPosition;
    cameraPosition[0] =
        gCamera->mtx[0][0] * worldPosition[0] +
        gCamera->mtx[1][0] * worldPosition[1] +
        gCamera->mtx[2][0] * worldPosition[2] +
        gCamera->mtx[3][0];
    cameraPosition[1] =
        gCamera->mtx[0][1] * worldPosition[0] +
        gCamera->mtx[1][1] * worldPosition[1] +
        gCamera->mtx[2][1] * worldPosition[2] +
        gCamera->mtx[3][1];
    cameraPosition[2] =
        gCamera->mtx[0][2] * worldPosition[0] +
        gCamera->mtx[1][2] * worldPosition[1] +
        gCamera->mtx[2][2] * worldPosition[2] +
        gCamera->mtx[3][2];

    headPosition[0] =
        headView[0][0] * cameraPosition[0] +
        headView[1][0] * cameraPosition[1] +
        headView[2][0] * cameraPosition[2] +
        headView[3][0];
    headPosition[1] =
        headView[0][1] * cameraPosition[0] +
        headView[1][1] * cameraPosition[1] +
        headView[2][1] * cameraPosition[2] +
        headView[3][1];
    headPosition[2] =
        headView[0][2] * cameraPosition[0] +
        headView[1][2] * cameraPosition[1] +
        headView[2][2] * cameraPosition[2] +
        headView[3][2];

    if (headPosition[2] >= -0.001f) {
        return false;
    }

    float eyeTangents[4] = { 0 };
    if (!vr_get_eye_tangents(eyeIndex, eyeTangents)) {
        return false;
    }

    const float fovScale =
        (float)clamp(configVrFov, 70U, 120U) / 100.0f;
    float tanLeft = eyeTangents[0] * fovScale;
    float tanRight = eyeTangents[1] * fovScale;
    float tanDown = eyeTangents[2] * fovScale;
    float tanUp = eyeTangents[3] * fovScale;
    if (centerEye) {
        float otherEye[4] = { 0 };
        if (!vr_get_eye_tangents(eyeIndex ^ 1U, otherEye)) {
            return false;
        }
        tanLeft = (eyeTangents[0] + otherEye[0]) * 0.5f * fovScale;
        tanRight = (eyeTangents[1] + otherEye[1]) * 0.5f * fovScale;
        tanDown = (eyeTangents[2] + otherEye[2]) * 0.5f * fovScale;
        tanUp = (eyeTangents[3] + otherEye[3]) * 0.5f * fovScale;
    }
    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;
    if (tanWidth <= 0.000001f || tanHeight <= 0.000001f) {
        return false;
    }

    const float inverseDepth = 1.0f / -headPosition[2];
    const float xSlope = headPosition[0] * inverseDepth;
    const float ySlope = headPosition[1] * inverseDepth;
    ndcPosition[0] = ((xSlope - tanLeft) / tanWidth) * 2.0f - 1.0f;
    ndcPosition[1] = ((ySlope - tanDown) / tanHeight) * 2.0f - 1.0f;
    ndcPosition[2] = headPosition[2];
    return true;
}

bool vr_world_pos_to_ndc(Vec3f worldPosition, Vec3f ndcPosition) {
    return vr_world_pos_to_eye_ndc(worldPosition, 0, true, ndcPosition);
}

static void patch_mtx_vr_billboards(uint32_t eyeIndex) {
    if (sVrBillboardHead == NULL) {
        return;
    }

    Mat4 headRotation;
    const bool useHeadRotation =
        vr_is_active() &&
        eyeIndex < 2 &&
        vr_build_head_rotation_matrix(headRotation);
    Vec3f cylindricalBackward = { 0.0f, 0.0f, 1.0f };
    Mat4 inverseFullFacingRotation;
    Mat4 inverseCylindricalFacingRotation;

    if (useHeadRotation) {
        cylindricalBackward[0] = headRotation[0][2];
        cylindricalBackward[1] = headRotation[1][2];
        cylindricalBackward[2] = headRotation[2][2];

        mtxf_identity(inverseFullFacingRotation);
        for (s32 row = 0; row < 3; row++) {
            for (s32 column = 0; column < 3; column++) {
                inverseFullFacingRotation[row][column] =
                    headRotation[column][row];
            }
        }

        Mat4 cylindricalFacingRotation;
        mtxf_identity(cylindricalFacingRotation);
        const f32 backwardX = cylindricalBackward[0];
        const f32 backwardZ = cylindricalBackward[2];
        const f32 horizontalLength = sqrtf(
            backwardX * backwardX +
            backwardZ * backwardZ
        );

        if (horizontalLength > 0.000001f) {
            const f32 normalizedX = backwardX / horizontalLength;
            const f32 normalizedZ = backwardZ / horizontalLength;

            cylindricalFacingRotation[0][0] = normalizedZ;
            cylindricalFacingRotation[0][2] = normalizedX;
            cylindricalFacingRotation[2][0] = -normalizedX;
            cylindricalFacingRotation[2][2] = normalizedZ;
        }

        mtxf_identity(inverseCylindricalFacingRotation);
        for (s32 row = 0; row < 3; row++) {
            for (s32 column = 0; column < 3; column++) {
                inverseCylindricalFacingRotation[row][column] =
                    cylindricalFacingRotation[column][row];
            }
        }
    }

    Mat4 billboardFlipRotation;
    mtxf_identity(billboardFlipRotation);
    billboardFlipRotation[0][0] = -1.0f;
    billboardFlipRotation[2][2] = -1.0f;

    for (struct MtxInterp* interp = sVrBillboardHead;
         interp != NULL;
         interp = interp->nextVrBillboard) {
        if (!interp->vrBaseReady) {
            continue;
        }

        if (!useHeadRotation) {
            mtxf_copy(interp->interp.m, interp->vrBase.m);
            continue;
        }

        Mat4 adjusted;
        if (interp->billboard == VR_BILLBOARD_CYLINDRICAL) {
            mtxf_mul(
                adjusted,
                interp->vrBase.m,
                inverseCylindricalFacingRotation
            );
        } else {
            mtxf_mul(
                adjusted,
                interp->vrBase.m,
                inverseFullFacingRotation
            );
        }

        if (configVrImmersiveFlipBillboards) {
            /*
             * Rotate the already camera-facing billboard basis 180 degrees
             * around its local up axis. This selects the reverse face while
             * retaining the existing full/cylindrical HMD-facing transform.
             */
            Mat4 flipped;
            mtxf_mul(
                flipped,
                adjusted,
                billboardFlipRotation
            );
            mtxf_copy(adjusted, flipped);
        }

        // The inverse rotation is for the billboard's axes only. Its center
        // must still receive the normal headset view transform.
        for (s32 column = 0; column < 4; column++) {
            adjusted[3][column] = interp->vrBase.m[3][column];
        }
        mtxf_copy(interp->interp.m, adjusted);
    }
}

struct Object* gCurGraphNodeProcessingObject = NULL;
struct MarioState* gCurGraphNodeMarioState = NULL;

f32 gOverrideFOV = 0;
f32 gOverrideNear = 0;
f32 gOverrideFar = 0;

static void init_mtx(void) {

    // matrices
    if (!sMtxTbl) {
        // VR retains more visible objects and matrices than the flat camera.
        // Reserve enough pointer slots for mod-heavy levels up front so the
        // first dense scene does not grow this table during a headset frame.
        sMtxTbl = growing_array_init(NULL, 4096, malloc, free);
        if (!sMtxTbl) {
            sys_fatal("Cannot allocate matrix buffer for interpolation");
        }
    }
    sMtxTbl->count = 0;
    sVrBillboardHead = NULL;
    sVrHeldMatrixHead = NULL;

    // shadows
    if (!sShadowInterp) {
        sShadowInterp = growing_array_init(NULL, 128, malloc, free);
        if (!sShadowInterp) {
            sys_fatal("Cannot allocate shadow buffer for interpolation");
        }
    }
    sShadowInterp->count = 0;
    gShadowInterpCurrent = NULL;
}

static void reset_mtx(void) {
    growing_array_free(&sMtxTbl);
    growing_array_free(&sShadowInterp);
    init_mtx();
}

void patch_mtx_before(void) {
    vr_handle_camera_mode_change();
    // A newly loaded area does not have a usable HMD/camera basis while its
    // warp is being initialized. Apply the requested destination direction
    // here, on the first rendered frame where tracking is valid, instead of
    // aligning against stale data from the previous room.
    if (sVrPendingCameraYawAlignment &&
        vr_align_first_person_camera_yaw(
            sVrPendingCameraYawTarget
        )) {
        sVrPendingCameraYawAlignment = false;
    }
    sVrHeadTrackedAudioEnabledForFrame =
        configVrImmersive3dSound &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        vr_is_active();
    sVrAudioListenerPreparedForFrame = false;
    sVrAudioListenerValid = false;
    init_mtx();
    sVrUiMatrixCount = 0;
    sVrHudMatrixCount = 0;
    sVrWorldLabelMatrixCount = 0;

    if (sPerspectiveNode != NULL) {
        sPerspectiveNode->prevFov = sPerspectiveNode->fov;
        sPerspectiveNode = NULL;
    }

    if (sViewport != NULL) {
        sViewportPrev    = *sViewport;
        sViewport        = NULL;
        sViewportPos     = NULL;
        sViewportClipPos = NULL;
    }

    if (sBackgroundNode != NULL) {
        vec3f_copy(sBackgroundNode->prevCameraPos, gLakituState.pos);
        vec3f_copy(sBackgroundNode->prevCameraFocus, gLakituState.focus);
        sBackgroundNode->prevCameraTimestamp = gGlobalTimer;
        sBackgroundNode = NULL;
        gBackgroundSkyboxGfx = NULL;
    }
}

static bool vr_grow_matrix_list(
    Mtx*** matrices,
    u32* capacity,
    u32 requiredCount
) {
    if (requiredCount <= *capacity) {
        return true;
    }

    u32 newCapacity = *capacity == 0 ? 256 : *capacity;
    while (newCapacity < requiredCount) {
        if (newCapacity > UINT32_MAX / 2) {
            return false;
        }
        newCapacity *= 2;
    }

    Mtx** grown = realloc(*matrices, sizeof(*grown) * newCapacity);
    if (grown == NULL) {
        return false;
    }

    *matrices = grown;
    *capacity = newCapacity;
    return true;
}

void register_mtx_vr_ui(Mtx *matrix) {
    if (matrix == NULL || !vr_grow_matrix_list(
            &sVrUiMatrices,
            &sVrUiMatrixCapacity,
            sVrUiMatrixCount + 1
        )) {
        return;
    }
    sVrUiMatrices[sVrUiMatrixCount++] = matrix;
}

void register_mtx_vr_hud(Mtx *matrix) {
    if (matrix == NULL || !vr_grow_matrix_list(
            &sVrHudMatrices,
            &sVrHudMatrixCapacity,
            sVrHudMatrixCount + 1
        )) {
        return;
    }

    sVrHudMatrices[sVrHudMatrixCount++] = matrix;
}

void register_mtx_vr_world_label(
    Mtx *matrix,
    Vec3f previousPosition,
    Vec3f currentPosition
) {
    if (matrix == NULL || previousPosition == NULL || currentPosition == NULL) {
        return;
    }

    if (sVrWorldLabelMatrixCount >= sVrWorldLabelMatrixCapacity) {
        u32 newCapacity = sVrWorldLabelMatrixCapacity == 0
            ? 16
            : sVrWorldLabelMatrixCapacity * 2;
        struct VrWorldLabelMatrix* grown = realloc(
            sVrWorldLabelMatrices,
            sizeof(*grown) * newCapacity
        );
        if (grown == NULL) {
            return;
        }
        sVrWorldLabelMatrices = grown;
        sVrWorldLabelMatrixCapacity = newCapacity;
    }

    struct VrWorldLabelMatrix* label =
        &sVrWorldLabelMatrices[sVrWorldLabelMatrixCount++];
    label->matrix = matrix;
    vec3f_copy(label->previousPosition, previousPosition);
    vec3f_copy(label->currentPosition, currentPosition);
}

bool vr_get_controller_world_hammer_head_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition
) {
    if (!vr_get_controller_world_fist_from_state(
            handIndex,
            state,
            worldPosition,
            NULL
        ) || !sVrControllerCameraInverseValid) {
        return false;
    }

    const float* rotation = state->aimPoseValid
        ? state->aimRotation
        : state->gripRotation;
    Vec3f trackedUp = { 0.0f, 1.0f, 0.0f };
    vr_rotate_pose_vector(rotation, trackedUp, trackedUp);
    Vec3f worldUp;
    for (u32 axis = 0; axis < 3; axis++) {
        worldUp[axis] =
            trackedUp[0] * sVrControllerCameraInverse[0][axis] +
            trackedUp[1] * sVrControllerCameraInverse[1][axis] +
            trackedUp[2] * sVrControllerCameraInverse[2][axis];
    }
    const f32 gloveScale =
        (f32)clamp(configVrGloveSize, 25U, 250U) / 70.0f;
    // The fist grips the lower handle, so the damaging head is farther up
    // the controller axis than when the model origin sat in the palm.
    const f32 fistToHead = 36.0f * gloveScale;
    for (u32 axis = 0; axis < 3; axis++) {
        worldPosition[axis] += worldUp[axis] * fistToHead;
    }
    return true;
}

static bool vr_build_hand_ui_projection(
    uint32_t eyeIndex,
    unsigned int anchor,
    Mtx* fixedProjection
) {
    if (eyeIndex >= 2 || fixedProjection == NULL ||
        anchor < VR_UI_ANCHOR_LEFT_HAND ||
        anchor > VR_UI_ANCHOR_RIGHT_HAND) {
        return false;
    }

    const u32 hand = anchor == VR_UI_ANCHOR_LEFT_HAND
        ? VR_CONTROLLER_LEFT
        : VR_CONTROLLER_RIGHT;
    struct VrControllerState state;
    float eyeTangents[4] = { 0 };
    float headTranslation[3] = { 0 };
    float eyeOffset[3] = { 0 };
    float headRotation[4] = { 0 };
    if (!vr_get_controller_state(hand, &state) ||
        (!state.gripPoseValid && !state.aimPoseValid) ||
        !vr_get_eye_tangents(eyeIndex, eyeTangents) ||
        !vr_get_head_translation(headTranslation) ||
        !vr_get_head_rotation(headRotation) ||
        !vr_get_eye_offset(eyeIndex, eyeOffset)) {
        return false;
    }

    const float* position = state.gripPoseValid
        ? state.gripPosition
        : state.aimPosition;
    const float* rotation = state.aimPoseValid
        ? state.aimRotation
        : state.gripRotation;
    Vec3f right = { 1.0f, 0.0f, 0.0f };
    Vec3f up = { 0.0f, 1.0f, 0.0f };
    Vec3f backward = { 0.0f, 0.0f, 1.0f };
    vr_rotate_pose_vector(rotation, right, right);
    vr_rotate_pose_vector(rotation, up, up);
    vr_rotate_pose_vector(rotation, backward, backward);

    // Place a readable 4:3 panel just above the controller/glove's top face.
    // Its axes come directly from the latest OpenXR pose, so rotation and
    // translation update once per submitted headset frame rather than at the
    // 30 Hz gameplay rate.
    const f32 unitsPerMeter = 100.0f;
    const f32 panelWidth = 32.0f;
    const f32 panelHeight = panelWidth *
        (f32)SCREEN_HEIGHT / (f32)SCREEN_WIDTH;
    Vec3f panelRight = { right[0], right[1], right[2] };
    Vec3f panelUp = { -backward[0], -backward[1], -backward[2] };
    Vec3f center = {
        position[0] * unitsPerMeter + up[0] * 9.0f - backward[0] * 2.0f,
        position[1] * unitsPerMeter + up[1] * 9.0f - backward[1] * 2.0f,
        position[2] * unitsPerMeter + up[2] * 9.0f - backward[2] * 2.0f
    };

    Mat4 panel;
    mtxf_identity(panel);
    for (u32 axis = 0; axis < 3; axis++) {
        panel[0][axis] = panelRight[axis] *
            panelWidth / (f32)SCREEN_WIDTH;
        panel[1][axis] = panelUp[axis] *
            panelHeight / (f32)SCREEN_HEIGHT;
        panel[3][axis] = center[axis] -
            panelRight[axis] * panelWidth * 0.5f -
            panelUp[axis] * panelHeight * 0.5f;
    }

    // Build a tracking-space view matrix even in title/file-select scenes;
    // those scenes normally use a deliberately head-locked world view, while
    // a controller-mounted panel must remain relative to the real hand.
    Vec3f viewRight = { 1.0f, 0.0f, 0.0f };
    Vec3f viewUp = { 0.0f, 1.0f, 0.0f };
    Vec3f viewBackward = { 0.0f, 0.0f, 1.0f };
    vr_rotate_pose_vector(headRotation, viewRight, viewRight);
    vr_rotate_pose_vector(headRotation, viewUp, viewUp);
    vr_rotate_pose_vector(headRotation, viewBackward, viewBackward);
    Mat4 view;
    for (u32 axis = 0; axis < 3; axis++) {
        view[axis][0] = viewRight[axis];
        view[axis][1] = viewUp[axis];
        view[axis][2] = viewBackward[axis];
        view[axis][3] = 0.0f;
    }
    const f32 trackedX = headTranslation[0] * unitsPerMeter;
    const f32 trackedY = headTranslation[1] * unitsPerMeter;
    const f32 trackedZ = headTranslation[2] * unitsPerMeter;
    view[3][0] = -(trackedX * view[0][0] +
                     trackedY * view[1][0] +
                     trackedZ * view[2][0]) -
        eyeOffset[0] * unitsPerMeter;
    view[3][1] = -(trackedX * view[0][1] +
                     trackedY * view[1][1] +
                     trackedZ * view[2][1]) -
        eyeOffset[1] * unitsPerMeter;
    view[3][2] = -(trackedX * view[0][2] +
                     trackedY * view[1][2] +
                     trackedZ * view[2][2]) -
        eyeOffset[2] * unitsPerMeter;
    view[3][3] = 1.0f;

    const f32 tanLeft = eyeTangents[0];
    const f32 tanRight = eyeTangents[1];
    const f32 tanDown = eyeTangents[2];
    const f32 tanUp = eyeTangents[3];
    const f32 tanWidth = tanRight - tanLeft;
    const f32 tanHeight = tanUp - tanDown;
    if (tanWidth <= 0.000001f || tanHeight <= 0.000001f) {
        return false;
    }

    const f32 near = 0.5f;
    const f32 far = 10000.0f;
    Mat4 perspective;
    mtxf_identity(perspective);
    perspective[0][0] = 2.0f / tanWidth;
    perspective[1][1] = 2.0f / tanHeight;
    perspective[2][0] = (tanRight + tanLeft) / tanWidth;
    perspective[2][1] = (tanUp + tanDown) / tanHeight;
    perspective[2][2] = (near + far) / (near - far);
    perspective[2][3] = -1.0f;
    perspective[3][2] = 2.0f * near * far / (near - far);
    perspective[3][3] = 0.0f;

    Mat4 viewProjection;
    Mat4 panelProjection;
    mtxf_mul(viewProjection, view, perspective);
    mtxf_mul(panelProjection, panel, viewProjection);
    mtxf_to_mtx(fixedProjection, panelProjection);
    return true;
}

static bool vr_build_world_label_projection(
    uint32_t eyeIndex,
    struct VrWorldLabelMatrix* label,
    Mtx* fixedProjection
) {
    if (eyeIndex >= 2 || label == NULL || label->matrix == NULL ||
        fixedProjection == NULL || gCamera == NULL) {
        return false;
    }

    // Interpolate the remote player's world anchor at render rate. This is
    // deliberately independent of the HUD plane: the resulting label has
    // real stereo depth and remains attached above the player when the local
    // headset translates or rotates.
    Vec3f center;
    if (gRenderingInterpolated) {
        delta_interpolate_vec3f(
            center,
            label->previousPosition,
            label->currentPosition,
            clamp(gRenderingDelta, 0.0f, 1.0f)
        );
    } else {
        vec3f_copy(center, label->currentPosition);
    }

    Vec3f eyeNdc;
    if (!vr_world_pos_to_eye_ndc(center, eyeIndex, false, eyeNdc)) {
        return false;
    }

    // Keep the glyph renderer orthographic and move only its center to the
    // target's true per-eye projection. This gives the two eyes the correct
    // depth disparity without turning the DJUI text into a giant world-space
    // quad (the source of the detached/doubled tags).
    const f32 boundsWidth = (f32)SCREEN_WIDTH;
    const f32 boundsHeight = (f32)SCREEN_HEIGHT;
    const f32 boundsCenterX = (f32)SCREEN_WIDTH * 0.5f -
        eyeNdc[0] * boundsWidth * 0.5f;
    const f32 boundsCenterY = (f32)SCREEN_HEIGHT * 0.5f -
        eyeNdc[1] * boundsHeight * 0.5f;
    guOrtho(
        fixedProjection,
        boundsCenterX - boundsWidth * 0.5f,
        boundsCenterX + boundsWidth * 0.5f,
        boundsCenterY - boundsHeight * 0.5f,
        boundsCenterY + boundsHeight * 0.5f,
        -10.0f,
        10.0f,
        1.0f
    );
    return true;
}

static void patch_mtx_vr_ui(uint32_t eyeIndex) {
    if (sVrUiMatrixCount == 0 && sVrHudMatrixCount == 0 &&
        sVrWorldLabelMatrixCount == 0) {
        return;
    }

    for (u32 i = 0; i < sVrWorldLabelMatrixCount; i++) {
        Mtx worldProjection;
        if (vr_build_world_label_projection(
                eyeIndex,
                &sVrWorldLabelMatrices[i],
                &worldProjection
            )) {
            memcpy(
                sVrWorldLabelMatrices[i].matrix,
                &worldProjection,
                sizeof(worldProjection)
            );
        }
    }

    float left = 0.0f;
    float right = SCREEN_WIDTH;
    float bottom = 0.0f;
    float top = SCREEN_HEIGHT;

    f32 centerNdcX = 0.0f;
    f32 centerNdcY = 0.0f;
    f32 halfNdcX = 1.0f;
    f32 halfNdcY = 1.0f;

    if (vr_get_ui_plane_ndc(
            eyeIndex,
            &centerNdcX,
            &centerNdcY,
            &halfNdcX,
            &halfNdcY
        )) {
        const float boundsWidth =
            (float)SCREEN_WIDTH / halfNdcX;
        const float boundsHeight =
            (float)SCREEN_HEIGHT / halfNdcY;
        const float boundsCenterX =
            (float)SCREEN_WIDTH * 0.5f -
            centerNdcX * boundsWidth * 0.5f;
        const float boundsCenterY =
            (float)SCREEN_HEIGHT * 0.5f -
            centerNdcY * boundsHeight * 0.5f;

        left = boundsCenterX - boundsWidth * 0.5f;
        right = boundsCenterX + boundsWidth * 0.5f;
        bottom = boundsCenterY - boundsHeight * 0.5f;
        top = boundsCenterY + boundsHeight * 0.5f;
    }

    // Every registered HUD/menu matrix uses the same head-locked plane for
    // this eye. Build the orthographic projection once instead of repeating
    // its divisions and matrix setup for every individual UI element.
    Mtx projection;
    guOrtho(
        &projection,
        left,
        right,
        bottom,
        top,
        -10.0f,
        10.0f,
        1.0f
    );
    Mtx menuProjection;
    const bool handMenu = vr_build_hand_ui_projection(
        eyeIndex,
        configVrMenuAnchor,
        &menuProjection
    );
    for (u32 i = 0; i < sVrUiMatrixCount; i++) {
        memcpy(
            sVrUiMatrices[i],
            handMenu ? &menuProjection : &projection,
            sizeof(projection)
        );
    }

    if (sVrHudMatrixCount > 0) {
        // Individual HUD groups apply the corner spread in logical screen
        // space. Keep the shared head-locked projection unchanged so glyphs
        // within each readout do not stretch apart.
        const f32 spread = 1.0f;
        const f32 centerX = (left + right) * 0.5f;
        const f32 centerY = (bottom + top) * 0.5f;
        const f32 halfWidth = (right - left) * 0.5f / spread;
        const f32 halfHeight = (top - bottom) * 0.5f / spread;
        Mtx hudProjection;
        guOrtho(
            &hudProjection,
            centerX - halfWidth,
            centerX + halfWidth,
            centerY - halfHeight,
            centerY + halfHeight,
            -10.0f,
            10.0f,
            1.0f
        );
        Mtx handHudProjection;
        const unsigned int hudHandAnchor =
            configVrHudAnchor == VR_HUD_ANCHOR_LEFT_HAND
                ? VR_UI_ANCHOR_LEFT_HAND
                : VR_UI_ANCHOR_RIGHT_HAND;
        bool handHud = false;
        if (configVrHudAnchor != VR_HUD_ANCHOR_HEADSET) {
            // The menu and HUD frequently share the same selected hand.
            // Reuse the already sampled controller/eye projection instead of
            // repeating OpenXR pose queries and matrix construction per eye.
            // This changes no text, scaling, filtering, or layout.
            if (handMenu && hudHandAnchor == configVrMenuAnchor) {
                memcpy(
                    &handHudProjection,
                    &menuProjection,
                    sizeof(handHudProjection)
                );
                handHud = true;
            } else {
                handHud = vr_build_hand_ui_projection(
                    eyeIndex,
                    hudHandAnchor,
                    &handHudProjection
                );
            }
        }
        for (u32 i = 0; i < sVrHudMatrixCount; i++) {
            memcpy(
                sVrHudMatrices[i],
                handHud ? &handHudProjection : &hudProjection,
                sizeof(hudProjection)
            );
        }
    }
}

static void patch_mtx_perspective(
    f32 delta,
    uint32_t eyeIndex,
    bool patchBillboards
) {
    if (sPerspectiveNode == NULL) {
        return;
    }

    if (gCamSkipInterp) {
        sPerspectiveNode->prevFov = sPerspectiveNode->fov;
    }

    f32 near = get_first_person_enabled()
        ? 1.f
        : replace_value_if_not_zero(
            MIN(
                sPerspectiveNode->near,
                gProjectionMaxNearValue
            ),
            gOverrideNear
        );
    f32 far = replace_value_if_not_zero(
        sPerspectiveNode->far,
        gOverrideFar
    );

    // Honor the existing draw-distance setting in VR as well. Infinite keeps
    // the previous VR result exactly; shorter user-selected distances let
    // large mod maps avoid submitting geometry beyond the map far plane.
    if (gOverrideFar == 0 && configDrawDistance == 6) {
        far = max(far, MAX_FAR_PLANE_DIST);
    }

    float eyeTangents[4] = { 0 };
    bool runtimeProjection = false;

    if (vr_get_eye_tangents(eyeIndex, eyeTangents)) {
        const float fovScale =
            (float)clamp(configVrFov, 70U, 120U) / 100.0f;
        const float tanLeft = eyeTangents[0] * fovScale;
        const float tanRight = eyeTangents[1] * fovScale;
        const float tanDown = eyeTangents[2] * fovScale;
        const float tanUp = eyeTangents[3] * fovScale;
        const float tanWidth = tanRight - tanLeft;
        const float tanHeight = tanUp - tanDown;

        if (tanWidth > 0.000001f &&
            tanHeight > 0.000001f) {
            runtimeProjection = true;
            guMtxIdentF(sPerspectiveMtx->m);
            sPerspectiveMtx->m[0][0] =
                2.0f / tanWidth;
            sPerspectiveMtx->m[1][1] =
                2.0f / tanHeight;
            sPerspectiveMtx->m[2][0] =
                (tanRight + tanLeft) / tanWidth;
            sPerspectiveMtx->m[2][1] =
                (tanUp + tanDown) / tanHeight;
            sPerspectiveMtx->m[2][2] =
                (near + far) / (near - far);
            sPerspectiveMtx->m[2][3] = -1.0f;
            sPerspectiveMtx->m[3][2] =
                2.0f * near * far / (near - far);
            sPerspectiveMtx->m[3][3] = 0.0f;
        }
    }

    if (!runtimeProjection) {
        u16 perspNorm;
        const f32 fovInterpolated = delta_interpolate_f32(
            sPerspectiveNode->prevFov,
            sPerspectiveNode->fov,
            delta
        );
        f32 aspect = sPerspectiveAspect;
        vr_get_render_target_aspect(&aspect);
        guPerspective(
            sPerspectiveMtx,
            &perspNorm,
            fovInterpolated,
            aspect,
            near,
            far,
            1.0f
        );
    }

    Mat4 baseProjection;
    mtxf_copy(baseProjection, sPerspectiveMtx->m);

    if (gVrSkyProjectionMtx != NULL) {
        Mat4 headRotation;

        if (eyeIndex < 2 &&
            vr_build_head_rotation_matrix(headRotation)) {
            Mat4 skyViewProjection;
            mtxf_mul(
                skyViewProjection,
                headRotation,
                baseProjection
            );
            mtxf_copy(
                gVrSkyProjectionMtx->m,
                skyViewProjection
            );
        } else {
            mtxf_copy(
                gVrSkyProjectionMtx->m,
                baseProjection
            );
        }
    }

    Mat4 headView;
    if (vr_build_head_view_matrix(eyeIndex, headView)) {
        Mat4 viewProjection;
        mtxf_mul(
            viewProjection,
            headView,
            baseProjection
        );
        mtxf_copy(sPerspectiveMtx->m, viewProjection);
    }

    if (patchBillboards) {
        patch_mtx_vr_billboards(eyeIndex);
    }
}

void patch_mtx_vr_projection(
    f32 delta,
    uint32_t eyeIndex
) {
    // Billboard orientation and tracked glove transforms are shared by both
    // eyes. Only restore their flat-render state for the desktop fallback.
    const bool desktopFallback = eyeIndex >= 2;
    patch_mtx_perspective(delta, eyeIndex, desktopFallback);
    if (desktopFallback) {
        vr_patch_controller_hand_matrices(eyeIndex);
    }
}

void patch_mtx_vr_shared(void) {
    // OpenXR eye separation belongs entirely in the projection/view matrix.
    // These model transforms are headset/controller-relative and therefore
    // identical for both eyes, so prepare them once per submitted XR frame.
    patch_mtx_vr_billboards(0);
    if (configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        (configVrMotionControllerInput || sVrHeldMatrixHead != NULL)) {
        if (configVrMotionControllerInput) {
            vr_patch_controller_hand_matrices(0);
        }

        if (sVrHeldMatrixHead != NULL) {
            // Keep an immutable copy of the complete held hierarchy before
            // applying any late hand correction. The list is patched in
            // place, so reading an already-moved root while processing a
            // later bone would otherwise give different offsets to different
            // parts of the same actor and recreate the visual deformation.
            for (struct MtxInterp *interp = sVrHeldMatrixHead;
                 interp != NULL;
                 interp = interp->nextVrHeld) {
                mtxf_copy(interp->vrBase.m, interp->interp.m);
            }

            Vec3f cameraPos;
            Vec3f cameraFocus;
            Mat4 cameraMatrix;
            if (sCameraNode != NULL) {
                delta_interpolate_vec3f(
                    cameraPos,
                    sCameraNode->prevPos,
                    sCameraNode->pos,
                    gRenderingDelta
                );
                delta_interpolate_vec3f(
                    cameraFocus,
                    sCameraNode->prevFocus,
                    sCameraNode->focus,
                    gRenderingDelta
                );
                vr_build_game_camera_matrix(
                    cameraMatrix,
                    cameraPos,
                    cameraFocus,
                    sCameraNode->roll
                );
            }

            // Sample and patch every attachment independently. Besides being
            // correct when multiple render layers/attachments are present,
            // this keeps the visual root on the latest predicted OpenXR hand
            // pose at the headset refresh rate. Gameplay state and release
            // velocity remain on the native simulation tick.
            for (struct MtxInterp *interp = sVrHeldMatrixHead;
                 interp != NULL;
                 interp = interp->nextVrHeld) {
                if (interp->owner == NULL) {
                    continue;
                }

                if (vr_hand_interaction_is_hammer_charge_object(
                        interp->owner)) {
                    Mat4 handMatrix;
                    if (vr_get_controller_hand_attachment_matrix(
                            VR_CONTROLLER_RIGHT,
                            handMatrix)) {
                        const f32 handModelScale = 0.20f *
                            (f32)clamp(
                                configVrGloveSize,
                                25U,
                                250U
                            ) / 100.0f;
                        const f32 relativeScale =
                            interp->owner->header.gfx.scale[0] /
                            fmaxf(handModelScale, 0.001f);
                        // Local +X reaches the fist from the wrist. After the
                        // X-axis flip, this Y offset puts the start of the
                        // wooden handle in the closed glove rather than the
                        // shaft directly underneath the hammer head.
                        Vec3f localPosition = {
                            92.0f,
                            -82.0f * relativeScale,
                            14.0f
                        };
                        // The Hammer Suit model's head is local +Y. Flip it
                        // around the handle axis so +Y points above the fist
                        // instead of below it while preserving the grip point.
                        // Keep the hammer head above the fist, then lean the
                        // shaft back toward the glove so its held silhouette
                        // follows the near-vertical grip line. This affects
                        // only the rendered attachment; launch direction and
                        // projectile trajectory remain unchanged.
                        Vec3s localRotation = {
                            (s16)0x8000,
                            0,
                            (s16)-0x0B00
                        };
                        Mat4 hammerMatrix;
                        mtxf_rotate_zxy_and_translate(
                            hammerMatrix,
                            localPosition,
                            localRotation
                        );
                        Vec3f hammerScale = {
                            relativeScale,
                            relativeScale,
                            relativeScale
                        };
                        mtxf_scale_vec3f(
                            hammerMatrix,
                            hammerMatrix,
                            hammerScale
                        );
                        mtxf_mul(
                            interp->interp.m,
                            hammerMatrix,
                            handMatrix
                        );
                        continue;
                    }
                }

                const u32 heldHand =
                    vr_hand_interaction_get_tracked_held_hand(
                        interp->owner
                    );
                if (interp->usingCamSpace && sCameraNode != NULL &&
                    heldHand < VR_CONTROLLER_COUNT) {
                    Mat4 handMatrix;
                    if (vr_get_controller_hand_attachment_matrix(
                            heldHand,
                            handMatrix)) {
                        Vec3f fistCameraPosition;
                        vr_get_controller_fist_from_hand_matrix(
                            handMatrix,
                            fistCameraPosition
                        );
                        Vec3f heldRenderBase;
                        delta_interpolate_vec3f(
                            heldRenderBase,
                            interp->owner->header.gfx.prevPos,
                            interp->owner->header.gfx.pos,
                            gRenderingDelta
                        );
                        const f32 centerOffset =
                            vr_hand_interaction_get_held_object_center_offset(
                                interp->owner
                            );
                        Vec3f expectedRoot;
                        for (u32 axis = 0; axis < 3; axis++) {
                            expectedRoot[axis] =
                                heldRenderBase[0] * cameraMatrix[0][axis] +
                                heldRenderBase[1] * cameraMatrix[1][axis] +
                                heldRenderBase[2] * cameraMatrix[2][axis] +
                                cameraMatrix[3][axis];
                        }

                        // The old perfectly smooth attachment overwrote the
                        // translation of every animated bone with the fist
                        // position. That removed locomotion jitter, but also
                        // folded penguins, Bob-ombs, and other articulated
                        // actors into one point. Find the matrix nearest the
                        // actor's rendered root instead, then move the whole
                        // hierarchy by the one correction derived from it.
                        // Every bone retains its relative transform while the
                        // actor follows the same predicted hand pose as the
                        // visible glove at the headset refresh rate.
                        struct MtxInterp *rootInterp = NULL;
                        if (sVrHeldAnchorOwner == interp->owner &&
                            sVrHeldAnchorDisplayList != NULL) {
                            for (struct MtxInterp *candidate =
                                     sVrHeldMatrixHead;
                                 candidate != NULL;
                                 candidate = candidate->nextVrHeld) {
                                if (candidate->owner == interp->owner &&
                                    candidate->usingCamSpace &&
                                    candidate->displayList ==
                                        sVrHeldAnchorDisplayList) {
                                    rootInterp = candidate;
                                    break;
                                }
                            }
                        }
                        f32 rootDistanceSq = FLT_MAX;
                        if (rootInterp == NULL) {
                            for (struct MtxInterp *candidate =
                                     sVrHeldMatrixHead;
                                 candidate != NULL;
                                 candidate = candidate->nextVrHeld) {
                                if (candidate->owner != interp->owner ||
                                    !candidate->usingCamSpace) {
                                    continue;
                                }
                                const f32 dx =
                                    candidate->vrBase.m[3][0] -
                                    expectedRoot[0];
                                const f32 dy =
                                    candidate->vrBase.m[3][1] -
                                    expectedRoot[1];
                                const f32 dz =
                                    candidate->vrBase.m[3][2] -
                                    expectedRoot[2];
                                const f32 distanceSq =
                                    dx * dx + dy * dy + dz * dz;
                                if (distanceSq < rootDistanceSq) {
                                    rootDistanceSq = distanceSq;
                                    rootInterp = candidate;
                                }
                            }
                            if (rootInterp != NULL) {
                                sVrHeldAnchorOwner = interp->owner;
                                sVrHeldAnchorDisplayList =
                                    rootInterp->displayList;
                            }
                        }
                        if (rootInterp == NULL) {
                            continue;
                        }

                        for (u32 axis = 0; axis < 3; axis++) {
                            const f32 desiredRoot =
                                fistCameraPosition[axis] -
                                centerOffset * cameraMatrix[1][axis];
                            interp->interp.m[3][axis] +=
                                desiredRoot -
                                rootInterp->vrBase.m[3][axis];
                        }
                        continue;
                    }
                }

                Vec3f latePosition;
                if (!vr_hand_interaction_get_late_held_object_position(
                        interp->owner,
                        latePosition
                    )) {
                    continue;
                }
                Vec3f heldRenderBase;
                delta_interpolate_vec3f(
                    heldRenderBase,
                    interp->owner->header.gfx.prevPos,
                    interp->owner->header.gfx.pos,
                    gRenderingDelta
                );
                const f32 dx = latePosition[0] - heldRenderBase[0];
                const f32 dy = latePosition[1] - heldRenderBase[1];
                const f32 dz = latePosition[2] - heldRenderBase[2];
                // Apply one common late hand-pose delta to every matrix in an
                // animated held actor. Replacing every bone's translation
                // with the fist position collapsed Bob-ombs, penguins, and
                // other articulated models into a bunched-up pose. Their
                // native animation remains simulation-driven and interpolated
                // at render rate; only the complete animated hierarchy is
                // moved to the latest tracked hand pose here.
                if (interp->usingCamSpace && sCameraNode != NULL) {
                    interp->interp.m[3][0] +=
                        dx * cameraMatrix[0][0] +
                        dy * cameraMatrix[1][0] +
                        dz * cameraMatrix[2][0];
                    interp->interp.m[3][1] +=
                        dx * cameraMatrix[0][1] +
                        dy * cameraMatrix[1][1] +
                        dz * cameraMatrix[2][1];
                    interp->interp.m[3][2] +=
                        dx * cameraMatrix[0][2] +
                        dy * cameraMatrix[1][2] +
                        dz * cameraMatrix[2][2];
                } else {
                    interp->interp.m[3][0] += dx;
                    interp->interp.m[3][1] += dy;
                    interp->interp.m[3][2] += dz;
                }
            }
        } else {
            // A release ends the stable-root lifetime. A later grab may reuse
            // the same object-pool address with a completely different graph.
            sVrHeldAnchorOwner = NULL;
            sVrHeldAnchorDisplayList = NULL;
        }
    }
}

void patch_mtx_vr_ui_projection(uint32_t eyeIndex) {
    patch_mtx_vr_ui(eyeIndex);
}

void patch_mtx_interpolated(f32 delta) {
    // Matrix interpolation below immediately replaces every model matrix.
    // Avoid walking all billboards here; eye-specific patching happens after
    // the interpolation pass and before each actual eye render.
    patch_mtx_perspective(delta, 2, false);

    if (sViewportClipPos != NULL) {
        delta_interpolate_vec3s(sViewportInterp.vp.vtrans, sViewportPrev.vp.vtrans, sViewport->vp.vtrans, delta);
        delta_interpolate_vec3s(sViewportInterp.vp.vscale, sViewportPrev.vp.vscale, sViewport->vp.vscale, delta);

        Gfx *saved = gDisplayListHead;

        gDisplayListHead = sViewportClipPos;
        make_viewport_clip_rect(&sViewportInterp);
        gSPViewport(gDisplayListHead, VIRTUAL_TO_PHYSICAL(&sViewportInterp));

        gDisplayListHead = saved;
    }

    if (sBackgroundNode != NULL) {
        Vec3f posCopy;
        Vec3f focusCopy;
        struct GraphNodeRoot* rootCopy = gCurGraphNodeRoot;

        gCurGraphNodeRoot = sBackgroundNodeRoot;
        vec3f_copy(posCopy, gLakituState.pos);
        vec3f_copy(focusCopy, gLakituState.focus);
        if (gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
            delta_interpolate_vec3f(gLakituState.pos, sBackgroundNode->prevCameraPos, posCopy, delta);
            delta_interpolate_vec3f(gLakituState.focus, sBackgroundNode->prevCameraFocus, focusCopy, delta);
        }
        sBackgroundNode->fnNode.func(GEO_CONTEXT_RENDER, &sBackgroundNode->fnNode.node, NULL);

        vec3f_copy(gLakituState.pos, posCopy);
        vec3f_copy(gLakituState.focus, focusCopy);
        gCurGraphNodeRoot = rootCopy;
    }

    struct GraphNodeObject* savedObj = gCurGraphNodeObject;
    for (u32 i = 0; i < sShadowInterp->count; i++) {
        struct ShadowInterp* interp = sShadowInterp->buffer[i];
        if (!interp->gfx) { continue; }
        gShadowInterpCurrent = interp;
        Vec3f posInterp;
        delta_interpolate_vec3f(posInterp, interp->shadowPosPrev, interp->shadowPos, delta);
        gCurGraphNodeObject = interp->obj;
        extern u8 gInterpolatingSurfaces;
        gInterpolatingSurfaces = true;
        gShadowInterpCurrent->gfx = create_shadow_below_xyz(posInterp[0], posInterp[1], posInterp[2], interp->shadowScale, interp->node->shadowSolidity, interp->node->shadowType);
        gInterpolatingSurfaces = false;
        gShadowInterpCurrent = NULL;
    }
    gCurGraphNodeObject = savedObj;

    // calculate outside of for loop to reduce overhead
    // technically this is improper use of mtxf functions, but coop doesn't target N64
    Mtx camTranfInv, prevCamTranfInv;
    Mtx camInterp;
    bool translateCamSpace = (sMtxTbl->count > 0) && sCameraNode && (sCameraNode->matrixPtr != NULL) && (sCameraNode->matrixPtrPrev != NULL);
    if (translateCamSpace) {
        // compute inverse camera matrix to transform out of camera space later
        mtxf_inverse(camTranfInv.m, *sCameraNode->matrixPtr);
        mtxf_inverse(prevCamTranfInv.m, *sCameraNode->matrixPtrPrev);

        // use camera node's stored information to calculate interpolated camera transform
        Vec3f posInterp, focusInterp;
        delta_interpolate_vec3f(posInterp, sCameraNode->prevPos, sCameraNode->pos, delta);
        delta_interpolate_vec3f(focusInterp, sCameraNode->prevFocus, sCameraNode->focus, delta);
        vr_build_game_camera_matrix(
            camInterp.m,
            posInterp,
            focusInterp,
            sCameraNode->roll
        );
        mtxf_to_mtx(&camInterp, camInterp.m);
    }

    const bool vrActive = vr_is_active();
    for (u32 i = 0; i < sMtxTbl->count; i++) {
        struct MtxInterp *interp = sMtxTbl->buffer[i];
        Gfx *pos = interp->pos;
        Mtx *srcMtx = interp->mtx;
        Mtx *srcMtxPrev = interp->mtxPrev;

        if (interp->billboard &&
            interp->usingCamSpace &&
            translateCamSpace &&
            vrActive) {
            // Reconstruct the billboard in world space before applying the
            // HMD camera. Keeping its old camera-space orientation would make
            // trees and other sprites bend or roll with the headset.
            Mtx worldMtx;
            Mtx worldMtxPrev;
            Mtx worldInterp;
            Mtx cameraSpaceInterp;
            Mtx facingInterp;

            mtxf_copy(worldMtx.m, srcMtx->m);
            mtxf_copy(worldMtxPrev.m, srcMtxPrev->m);
            mtxf_mul(worldMtx.m, worldMtx.m, camTranfInv.m);
            mtxf_mul(
                worldMtxPrev.m,
                worldMtxPrev.m,
                prevCamTranfInv.m
            );
            delta_interpolate_mtx(
                &worldInterp,
                &worldMtxPrev,
                &worldMtx,
                delta
            );
            mtxf_mul(
                cameraSpaceInterp.m,
                worldInterp.m,
                camInterp.m
            );

            // Position must follow the interpolated VR camera, but a
            // billboard's camera-space axes must remain camera-facing. If
            // the reconstructed world axes are multiplied by the side-flip
            // camera yaw, every flat model rotates edge-on and then becomes
            // back-face culled. Preserve the normally interpolated billboard
            // basis and take only the corrected translation from world space.
            delta_interpolate_mtx(
                &facingInterp,
                srcMtxPrev,
                srcMtx,
                delta
            );
            for (s32 row = 0; row < 3; row++) {
                for (s32 column = 0; column < 4; column++) {
                    cameraSpaceInterp.m[row][column] =
                        facingInterp.m[row][column];
                }
            }

            mtxf_copy(
                interp->interp.m,
                cameraSpaceInterp.m
            );
        } else {
            if (interp->usingCamSpace && translateCamSpace) {
                // transform out of camera space so the matrix can interp in world space
                Mtx bufMtx, bufMtxPrev;
                mtxf_copy(bufMtx.m, srcMtx->m);
                mtxf_copy(bufMtxPrev.m, srcMtxPrev->m);
                mtxf_mul(bufMtx.m, bufMtx.m, camTranfInv.m);
                mtxf_mul(bufMtxPrev.m, bufMtxPrev.m, prevCamTranfInv.m);
                srcMtx = &bufMtx;
                srcMtxPrev = &bufMtxPrev;
            }
            delta_interpolate_mtx(
                &interp->interp,
                srcMtxPrev,
                srcMtx,
                delta
            );
            if (interp->usingCamSpace) {
                // transform back to camera space, respecting camera interpolation
            mtxf_mul(interp->interp.m, interp->interp.m, camInterp.m);
            }
        }
        if (interp->billboard != VR_BILLBOARD_NONE) {
            mtxf_copy(interp->vrBase.m, interp->interp.m);
            interp->vrBaseReady = TRUE;
        }
        gSPMatrix(pos++, VIRTUAL_TO_PHYSICAL(&interp->interp),
                  G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    }

    gCamSkipInterp = 0;
}

/**
 * Graph node interpolation
 */
static void *sGraphNodeInterpDataMap = NULL;
#define GRAPH_NODE_INTERP_POOL_CAPACITY 8192
static struct GraphNodeInterpData *sGraphNodeInterpDataPool = NULL;
static size_t sGraphNodeInterpDataPoolCount = 0;

static bool geo_interp_data_is_pooled(
    const struct GraphNodeInterpData *interp
) {
    if (interp == NULL || sGraphNodeInterpDataPool == NULL) {
        return false;
    }

    const uintptr_t address = (uintptr_t)interp;
    const uintptr_t poolStart = (uintptr_t)sGraphNodeInterpDataPool;
    const uintptr_t poolEnd = poolStart +
        sizeof(*sGraphNodeInterpDataPool) *
            GRAPH_NODE_INTERP_POOL_CAPACITY;
    return address >= poolStart && address < poolEnd;
}

struct GraphNodeInterpData *geo_get_interp_data(void *node, struct GraphNodeObject *obj) {

    if (!sGraphNodeInterpDataMap) {
        sGraphNodeInterpDataMap = hmap_create(true);
        if (!sGraphNodeInterpDataMap) {
            return NULL;
        }
        sGraphNodeInterpDataPool = calloc(
            GRAPH_NODE_INTERP_POOL_CAPACITY,
            sizeof(*sGraphNodeInterpDataPool)
        );
        sGraphNodeInterpDataPoolCount = 0;
    }

    void *nodeInterpData = hmap_get(
        sGraphNodeInterpDataMap,
        (int64_t)node
    );
    if (!nodeInterpData) {
        nodeInterpData = hmap_create(true);
        if (!nodeInterpData) {
            return NULL;
        }
        hmap_put(
            sGraphNodeInterpDataMap,
            (int64_t)node,
            nodeInterpData
        );
    }

    struct GraphNodeInterpData *interp = hmap_get(
        nodeInterpData,
        (int64_t)obj
    );
    if (!interp) {
        if (sGraphNodeInterpDataPool != NULL &&
            sGraphNodeInterpDataPoolCount <
                GRAPH_NODE_INTERP_POOL_CAPACITY) {
            interp = &sGraphNodeInterpDataPool[
                sGraphNodeInterpDataPoolCount++
            ];
        } else {
            interp = calloc(1, sizeof(*interp));
        }
        if (!interp) {
            return NULL;
        }
        hmap_put(nodeInterpData, (int64_t)obj, interp);
    }

    return interp;
}

static void geo_init_or_update_interp_data(struct GraphNodeInterpData *interp, Vec3s translation, Vec3s rotation, Vec3f scale, bool update) {
    if (interp && (update || interp->timestamp < gGlobalTimer - 1)) {
        if (translation) { vec3s_copy(interp->translation, translation); }
        if (rotation) { vec3s_copy(interp->rotation, rotation); }
        if (scale) { vec3f_copy(interp->scale, scale); }
        interp->timestamp = gGlobalTimer;
    }
}

static bool geo_should_interpolate(struct GraphNodeInterpData *interp) {
    return interp != NULL && interp->timestamp == gGlobalTimer - 1;
}

void geo_clear_interp_data() {
    for (void *nodeInterpData = hmap_begin(sGraphNodeInterpDataMap);
         nodeInterpData;
         nodeInterpData = hmap_next(sGraphNodeInterpDataMap)) {
        for (struct GraphNodeInterpData *interp = hmap_begin(nodeInterpData);
             interp;
             interp = hmap_next(nodeInterpData)) {
            if (!geo_interp_data_is_pooled(interp)) {
                free(interp);
            }
        }
        hmap_destroy(nodeInterpData);
    }
    hmap_destroy(sGraphNodeInterpDataMap);
    sGraphNodeInterpDataMap = NULL;
    free(sGraphNodeInterpDataPool);
    sGraphNodeInterpDataPool = NULL;
    sGraphNodeInterpDataPoolCount = 0;
    reset_mtx();
}

#define geo_update_interpolation(translation, rotation, scale, ...) { \
    struct GraphNodeInterpData *interp = geo_get_interp_data(node, gCurGraphNodeObject); \
    geo_init_or_update_interp_data(interp, translation, rotation, scale, false); \
    { __VA_ARGS__; } \
    geo_init_or_update_interp_data(interp, translation, rotation, scale, true); \
}

/**
 * Increments the matrix stack index and sets the matrixs at the new index.
 */
static u8 increment_mat_stack(void) {
    Mtx *mtx = alloc_display_list(sizeof(*mtx));
    Mtx *mtxPrev = alloc_display_list(sizeof(*mtxPrev));
    if (mtx == NULL || mtxPrev == NULL) {
        LOG_ERROR("Failed to allocate our matrices for the matrix stack.");
        return FALSE;
    }

    gMatStackIndex++;
    if (gMatStackIndex >= MATRIX_STACK_SIZE) {
        LOG_ERROR("Exceeded matrix stack size.");
        gMatStackIndex = MATRIX_STACK_SIZE - 1;
        return FALSE;
    }

    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
    mtxf_to_mtx(mtxPrev, gMatStackPrev[gMatStackIndex]);
    gMatStackFixed[gMatStackIndex] = mtx;
    gMatStackPrevFixed[gMatStackIndex] = mtxPrev;
    return TRUE;
}

static void vr_append_controller_hands(
    s32 enableZBuffer,
    struct RenderModeContainer* modeList,
    struct RenderModeContainer* mode2List
) {
    if (!vr_is_active() ||
        configVrCameraMode != VR_CAMERA_MODE_FIRST_PERSON ||
        !configVrMotionControllerInput ||
        !enableZBuffer ||
        gCurGraphNodeMasterList != sVrControllerHandMasterList ||
        sPerspectiveNode == NULL ||
        sCameraNode == NULL ||
        gMarioStates[0].marioObj == NULL ||
        vr_is_menu_scene()) {
        return;
    }

    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        sVrControllerHandMatrices[hand] =
            alloc_display_list(sizeof(Mtx));
        if (sVrControllerHandMatrices[hand] == NULL) {
            return;
        }
        vr_hide_controller_hand_matrix(
            sVrControllerHandMatrices[hand]
        );
    }

    gDPPipeSync(gDisplayListHead++);
    gDPSetRenderMode(
        gDisplayListHead++,
        modeList->modes[LAYER_OPAQUE],
        mode2List->modes[LAYER_OPAQUE]
    );
    gSPDisplayList(gDisplayListHead++, obj_sanitize_gfx);

    // The floating gloves render after the scene's opaque/alpha geometry but
    // before its transparent effects. Explicitly reload the local player's
    // complete palette so the gloves—and future VR-only body parts—stay
    // matched even when another network player rendered most recently.
    Gfx* localPlayerColors =
        mario_create_local_player_colors_dl();
    if (localPlayerColors != NULL) {
        gSPDisplayList(gDisplayListHead++, localPlayerColors);
    }

    vr_update_painting_exit_hat_gesture();
    const bool victoryGestureAvailable =
        vr_victory_hand_gesture_available();
    const u8 paintingExitHatAlpha =
        vr_painting_exit_hat_alpha();

    for (uint32_t hand = 0;
         hand < VR_CONTROLLER_COUNT;
         hand++) {
        struct VrControllerState state = { 0 };
        const bool controllerAvailable =
            vr_get_controller_state(hand, &state);
        if (controllerAvailable) {
            const float grabAmount = state.squeeze;

            // Separate close/open thresholds prevent noisy analog input from
            // rapidly switching the glove pose near the midpoint.
            if (grabAmount >= 0.55f) {
                sVrControllerHandClosed[hand] = true;
            } else if (grabAmount <= 0.35f) {
                sVrControllerHandClosed[hand] = false;
            }
        } else {
            sVrControllerHandClosed[hand] = false;
        }

        const bool peaceGesture =
            controllerAvailable &&
            victoryGestureAvailable &&
            state.squeeze >= 0.55f &&
            state.trigger >= 0.55f;
        const bool paintingExitHatGesture =
            hand == sVrPaintingExitHatHand &&
            paintingExitHatAlpha > 0;
        const Gfx* handDisplayList;
        // A successful exit is also part of the normal victory action set.
        // Give the explicitly grabbed cap priority or the same trigger/grip
        // input renders a peace sign instead of the cap.
        if (paintingExitHatGesture) {
            handDisplayList = mario_right_hand_cap;
        } else if (peaceGesture) {
            handDisplayList = mario_right_hand_peace;
        } else if (hand == VR_CONTROLLER_LEFT) {
            handDisplayList = sVrControllerHandClosed[hand]
                ? mario_left_hand_closed
                : mario_left_hand_open;
        } else {
            handDisplayList = sVrControllerHandClosed[hand]
                ? mario_right_hand_closed
                : mario_right_hand_open;
        }

        // DynOS replaces Mario through an actor graph, while VR gloves are
        // drawn independently after that graph. Prefer the active pack's
        // corresponding hand display list so its hand geometry, textures,
        // lights, and palette are preserved. Packs without standard hand
        // names safely retain the built-in glove selected above.
        if (!paintingExitHatGesture && !peaceGesture &&
            gMarioStates[0].marioObj != NULL) {
            const char* handSuffix;
            if (hand == VR_CONTROLLER_LEFT) {
                handSuffix = sVrControllerHandClosed[hand]
                    ? "left_hand_closed"
                    : "left_hand_open";
            } else {
                handSuffix = sVrControllerHandClosed[hand]
                    ? "right_hand_closed"
                    : "right_hand_open";
            }
            const Gfx* dynosHand = dynos_actor_get_display_list_by_suffix(
                gMarioStates[0].marioObj->header.gfx.sharedChild,
                handSuffix
            );
            if (dynosHand != NULL) {
                handDisplayList = dynosHand;
            }
        }

        if (paintingExitHatGesture && paintingExitHatAlpha < 255) {
            gDPPipeSync(gDisplayListHead++);
            gDPSetRenderMode(
                gDisplayListHead++,
                modeList->modes[LAYER_TRANSPARENT],
                mode2List->modes[LAYER_TRANSPARENT]
            );
            gDPSetEnvColor(
                gDisplayListHead++,
                255,
                255,
                255,
                paintingExitHatAlpha
            );
        }

        gSPMatrix(
            gDisplayListHead++,
            VIRTUAL_TO_PHYSICAL(sVrControllerHandMatrices[hand]),
            G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH
        );
        gSPDisplayList(
            gDisplayListHead++,
            handDisplayList
        );
        if (paintingExitHatGesture) {
            gSPDisplayList(
                gDisplayListHead++,
                mario_right_hand_cap_decal
            );
            if (sVrPaintingExitHatWingCap) {
                gSPDisplayList(
                    gDisplayListHead++,
                    mario_right_hand_cap_wings
                );
            }
        }

        if (paintingExitHatGesture && paintingExitHatAlpha < 255) {
            gDPPipeSync(gDisplayListHead++);
            gDPSetRenderMode(
                gDisplayListHead++,
                modeList->modes[LAYER_OPAQUE],
                mode2List->modes[LAYER_OPAQUE]
            );
            gDPSetEnvColor(
                gDisplayListHead++,
                255,
                255,
                255,
                255
            );
        }
    }

    gSPDisplayList(gDisplayListHead++, obj_sanitize_gfx);
}

/**
 * Process a master list node.
 */
static void geo_process_master_list_sub(struct GraphNodeMasterList *node) {
    struct DisplayListNode *currList = NULL;
    s32 enableZBuffer = (node->node.flags & GRAPH_RENDER_Z_BUFFER) != 0;
    struct RenderModeContainer *modeList = &renderModeTable_1Cycle[enableZBuffer];
    struct RenderModeContainer *mode2List = &renderModeTable_2Cycle[enableZBuffer];

    // @bug This is where the LookAt values should be calculated but aren't.
    // As a result, environment mapping is broken on Fast3DEX2 without the
    // changes below.
#ifdef F3DEX_GBI_2
    Mtx lMtx;
    guLookAtReflect(&lMtx, &lookAt, 0, 0, 0, /* eye */ 0, 0, 1, /* at */ 1, 0, 0 /* up */);
#endif

    if (enableZBuffer != 0) {
        gDPPipeSync(gDisplayListHead++);
        gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER);
    }

    for (s32 i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
        // Floating gloves are opaque and late-latched, but historically they
        // were appended after every transparent object. That made them cover
        // a nearer translucent Rasengan solely because of command order.
        // Emit them immediately before the transparent layers instead. Their
        // normal opaque depth write then lets transparent effects blend and
        // occlude correctly without making multi-shell spheres self-occlude.
        if (i == LAYER_TRANSPARENT) {
            vr_append_controller_hands(
                enableZBuffer,
                modeList,
                mode2List
            );
        }
        if ((currList = node->listHeads[i]) != NULL) {
            gDPSetRenderMode(gDisplayListHead++, modeList->modes[i], mode2List->modes[i]);
            while (currList != NULL) {
                // The rigid render-space hand anchor removes locomotion from
                // held actors. Keep their individual bones interpolating even
                // when the generic discontinuity detector mistakes a quick
                // native animation step for a teleport; otherwise the model
                // visibly alternates between smooth and 30 Hz poses.
                if (currList->owner == NULL ||
                    !vr_hand_interaction_is_tracked_held_object(
                        currList->owner
                    )) {
                    detect_and_skip_mtx_interpolation(
                        &currList->transform,
                        &currList->transformPrev
                    );
                }

                struct MtxInterp *interp = growing_array_alloc(sMtxTbl, sizeof(struct MtxInterp));
                interp->pos = gDisplayListHead;
                interp->mtx = currList->transform;
                interp->mtxPrev = currList->transformPrev;
                interp->displayList = currList->displayList;
                interp->usingCamSpace = currList->usingCamSpace;
                interp->billboard = currList->billboard;
                interp->vrBaseReady = FALSE;
                interp->owner = currList->owner;
                interp->nextVrHeld = NULL;
                if (interp->billboard != VR_BILLBOARD_NONE) {
                    interp->nextVrBillboard = sVrBillboardHead;
                    sVrBillboardHead = interp;
                }
                if (interp->owner != NULL &&
                    vr_hand_interaction_is_tracked_held_object(
                        interp->owner
                    )) {
                    interp->nextVrHeld = sVrHeldMatrixHead;
                    sVrHeldMatrixHead = interp;
                }

                gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(currList->transformPrev),
                          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

                /*
                 * A 180-degree billboard rotation presents the reverse face.
                 * Allow either face for this draw only; restoring back-face
                 * culling keeps subsequent ordinary geometry unchanged and
                 * avoids a global culling/performance change.
                 */
                const bool flipBillboardFace =
                    vr_is_active() &&
                    configVrImmersiveFlipBillboards &&
                    currList->billboard != VR_BILLBOARD_NONE;
                if (flipBillboardFace) {
                    gSPClearGeometryMode(
                        gDisplayListHead++,
                        G_CULL_BOTH
                    );
                }
                gSPDisplayList(gDisplayListHead++, currList->displayList);
                if (flipBillboardFace) {
                    gSPSetGeometryMode(
                        gDisplayListHead++,
                        G_CULL_BACK
                    );
                }

                currList = currList->next;
            }
        }
    }
    if (enableZBuffer != 0) {
        gDPPipeSync(gDisplayListHead++);
        gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER);
    }
}

/**
 * Appends the display list to one of the master lists based on the layer
 * parameter. Look at the RenderModeContainer struct to see the corresponding
 * render modes of layers.
 */
static bool vr_hide_local_first_person_mario_part(void) {
    if (!sVrFilteringLocalMarioBody ||
        gCurMarioBodyState == NULL ||
        gCurGraphNodeHeldObject != NULL) {
        return false;
    }

    const s32 animPart = gCurMarioBodyState->currAnimPart;
    const u32 action = gMarioStates[0].action;
    const bool ghostButtonPunch =
        configVrImmersiveGhostPunchArm &&
        (gPlayer1Controller->buttonDown & B_BUTTON) != 0 &&
        (action == ACT_PUNCHING || action == ACT_MOVE_PUNCHING);
    const bool hideMountedBody =
        !configVrExperimentalMountedBody &&
        (action == ACT_FLYING ||
         (action & ACT_FLAG_SWIMMING) != 0 ||
         (action & ACT_FLAG_RIDING_SHELL) != 0);
    const bool hideTopPoleFlipBody =
        !configVrTopPoleFlipBody &&
        action == ACT_TOP_OF_POLE_JUMP;
    const bool hideLedgeBody =
        configVrHideBodyOnLedge &&
        (action == ACT_LEDGE_GRAB ||
         action == ACT_LEDGE_CLIMB_SLOW_1 ||
         action == ACT_LEDGE_CLIMB_SLOW_2 ||
         action == ACT_LEDGE_CLIMB_DOWN ||
         action == ACT_LEDGE_CLIMB_FAST);
    const bool hideCrawlingTorso =
        configVrHideTorsoWhileCrawling &&
        (action == ACT_START_CRAWLING ||
         action == ACT_CRAWLING ||
         action == ACT_STOP_CRAWLING);
    const bool hidePhysicalClimbBody =
        vr_hand_interaction_should_hide_body(&gMarioStates[0]);
    const bool hideTrueFirstPersonFlipBody =
        configVrExperimentalTrueFirstPerson &&
        (action == ACT_BACKFLIP ||
         action == ACT_SIDE_FLIP ||
         action == ACT_TRIPLE_JUMP ||
         action == ACT_FLYING_TRIPLE_JUMP ||
         action == ACT_SPECIAL_TRIPLE_JUMP ||
         action == ACT_FORWARD_ROLLOUT ||
         action == ACT_BACKWARD_ROLLOUT);

    // True First Person keeps the animated skeleton available for its camera
    // anchor, but the model itself would pass directly through the player's
    // view during these inverted animations. Floating tracked gloves are
    // rendered separately and intentionally remain visible.
    if (hideTrueFirstPersonFlipBody) {
        return true;
    }

    switch (animPart) {
        case MARIO_ANIM_PART_HEAD:
            return true;
        case MARIO_ANIM_PART_UPPER_LEFT:
        case MARIO_ANIM_PART_UPPER_RIGHT:
            return true;
        case MARIO_ANIM_PART_LEFT_ARM:
        case MARIO_ANIM_PART_LEFT_FOREARM:
        case MARIO_ANIM_PART_RIGHT_ARM:
        case MARIO_ANIM_PART_RIGHT_FOREARM:
            return !configVrExperimentalArmsMode && !ghostButtonPunch;
        case MARIO_ANIM_PART_LEFT_HAND:
        case MARIO_ANIM_PART_RIGHT_HAND:
            // The independently tracked floating gloves remain the hands in
            // both normal first person and Arms Mode.
            return !ghostButtonPunch;
        case MARIO_ANIM_PART_ROOT:
        case MARIO_ANIM_PART_BUTT:
        case MARIO_ANIM_PART_TORSO:
            return configVrFeetOnlyBody ||
                !configVrFirstPersonBody ||
                hideMountedBody ||
                hideTopPoleFlipBody ||
                hideLedgeBody ||
                hideCrawlingTorso ||
                hidePhysicalClimbBody;
        case MARIO_ANIM_PART_LOWER_LEFT:
        case MARIO_ANIM_PART_LEFT_THIGH:
        case MARIO_ANIM_PART_LEFT_LEG:
        case MARIO_ANIM_PART_LOWER_RIGHT:
        case MARIO_ANIM_PART_RIGHT_THIGH:
        case MARIO_ANIM_PART_RIGHT_LEG:
            return configVrFeetOnlyBody ||
                !configVrFirstPersonBody ||
                hideMountedBody ||
                hideTopPoleFlipBody ||
                hideLedgeBody ||
                hidePhysicalClimbBody;
        case MARIO_ANIM_PART_LEFT_FOOT:
        case MARIO_ANIM_PART_RIGHT_FOOT:
            return (!configVrFeetOnlyBody &&
                    !configVrFirstPersonBody) ||
                hideMountedBody ||
                hideTopPoleFlipBody ||
                hideLedgeBody ||
                hidePhysicalClimbBody;
        default:
            // True First Person and Arms Mode still traverse hidden skeletons
            // for their camera/IK anchors. The ordinary body visibility toggle
            // controls every remaining Mario display list.
            return !configVrFirstPersonBody;
    }
}

static bool vr_ghost_button_punch_part(void) {
    if (!sVrFilteringLocalMarioBody ||
        !configVrImmersiveGhostPunchArm ||
        gCurMarioBodyState == NULL ||
        (gPlayer1Controller->buttonDown & B_BUTTON) == 0) {
        return false;
    }
    const u32 action = gMarioStates[0].action;
    if (action != ACT_PUNCHING && action != ACT_MOVE_PUNCHING) {
        return false;
    }
    const u32 part = gCurMarioBodyState->currAnimPart;
    return part == MARIO_ANIM_PART_LEFT_ARM ||
        part == MARIO_ANIM_PART_LEFT_FOREARM ||
        part == MARIO_ANIM_PART_LEFT_HAND ||
        part == MARIO_ANIM_PART_RIGHT_ARM ||
        part == MARIO_ANIM_PART_RIGHT_FOREARM ||
        part == MARIO_ANIM_PART_RIGHT_HAND;
}

static Gfx* vr_make_ghost_button_punch_display_list(void* displayList) {
    Gfx* gfxHead = alloc_display_list(7 * sizeof(*gfxHead));
    if (gfxHead == NULL) {
        return NULL;
    }
    Gfx* gfx = gfxHead;
    const u8 opacity = (u8)((MIN(configVrGhostPunchArmOpacity, 100U) * 255U + 50U) / 100U);
    gDPPipeSync(gfx++);
    gDPSetAlphaCompare(gfx++, G_AC_DITHER);
    gDPSetEnvColor(gfx++, 255, 255, 255, opacity);
    gSPDisplayList(gfx++, displayList);
    gDPSetEnvColor(gfx++, 255, 255, 255, 255);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gSPEndDisplayList(gfx++);
    return gfxHead;
}

static void geo_append_display_list(void *displayList, s16 layer) {
    // First-person VR uses the regular animated Mario model for the torso and
    // lower body, but tracked gloves replace its head, arms, and hands. Keep
    // traversing every bone so animation attributes and modded player models
    // remain synchronized; only suppress their actual display lists here.
    if (vr_hide_local_first_person_mario_part()) {
        return;
    }

    if (vr_ghost_button_punch_part()) {
        Gfx* ghostDisplayList =
            vr_make_ghost_button_punch_display_list(displayList);
        if (ghostDisplayList == NULL) {
            return;
        }
        displayList = ghostDisplayList;
        layer = LAYER_TRANSPARENT;
    }

#ifdef F3DEX_GBI_2
    gSPLookAt(gDisplayListHead++, &lookAt);
#endif
    if (gCurGraphNodeMasterList != 0) {
        struct DisplayListNode *listNode = growing_pool_alloc(gDisplayListHeap, sizeof(struct DisplayListNode));

        listNode->transform = gMatStackFixed[gMatStackIndex];
        listNode->transformPrev = gMatStackPrevFixed[gMatStackIndex];
        listNode->displayList = displayList;
        listNode->next = 0;
        listNode->usingCamSpace = sUsingCamSpace;
        listNode->billboard = VR_BILLBOARD_NONE;
        listNode->owner = gCurGraphNodeProcessingObject;
        if (sUsingBillboard ||
            (gCurGraphNodeObject != NULL &&
             (gCurGraphNodeObject->node.flags & GRAPH_RENDER_BILLBOARD))) {
            // GEO_BILLBOARD body parts and ordinary billboard objects must
            // face the complete HMD pose. Treating all of them as cylindrical
            // kept trees upright, but flattened coins, bowling balls, and
            // Bob-omb bodies when the player looked up or down. Trees already
            // request GRAPH_RENDER_CYLBOARD explicitly in VR below.
            listNode->billboard = VR_BILLBOARD_FULL;
        } else if (gCurGraphNodeObject != NULL &&
                   (gCurGraphNodeObject->node.flags & GRAPH_RENDER_CYLBOARD)) {
            listNode->billboard = VR_BILLBOARD_CYLINDRICAL;
        }
        if (gCurGraphNodeMasterList->listHeads[layer] == 0) {
            gCurGraphNodeMasterList->listHeads[layer] = listNode;
        } else {
            gCurGraphNodeMasterList->listTails[layer]->next = listNode;
        }
        gCurGraphNodeMasterList->listTails[layer] = listNode;
    }
}

static void geo_append_display_list_to_all_layers(void *displayList) {
    geo_append_display_list(displayList, LAYER_OPAQUE);
    geo_append_display_list(displayList, LAYER_OPAQUE_DECAL);
    geo_append_display_list(displayList, LAYER_OPAQUE_INTER);
    geo_append_display_list(displayList, LAYER_ALPHA);
    geo_append_display_list(displayList, LAYER_TRANSPARENT);
    geo_append_display_list(displayList, LAYER_TRANSPARENT_DECAL);
    geo_append_display_list(displayList, LAYER_TRANSPARENT_INTER);
}

/**
 * Process the master list node.
 */
static void geo_process_master_list(struct GraphNodeMasterList *node) {
    if (gCurGraphNodeMasterList == NULL && node->node.children != NULL) {
        gCurGraphNodeMasterList = node;
        for (s32 i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
            node->listHeads[i] = NULL;
        }
        geo_process_node_and_siblings(node->node.children);
        geo_process_master_list_sub(node);
        gCurGraphNodeMasterList = NULL;
    }
}

/**
 * Process an orthographic projection node.
 */
static void geo_process_ortho_projection(struct GraphNodeOrthoProjection *node) {
    if (node->node.children != NULL) {
        Mtx *mtx = alloc_display_list(sizeof(*mtx));
        if (mtx == NULL) { return; }
        f32 left   = ((gCurGraphNodeRoot->x - gCurGraphNodeRoot->width)  / 2.0f) * node->scale;
        f32 right  = ((gCurGraphNodeRoot->x + gCurGraphNodeRoot->width)  / 2.0f) * node->scale;
        f32 top    = ((gCurGraphNodeRoot->y - gCurGraphNodeRoot->height) / 2.0f) * node->scale;
        f32 bottom = ((gCurGraphNodeRoot->y + gCurGraphNodeRoot->height) / 2.0f) * node->scale;

        guOrtho(mtx, left, right, bottom, top, -2.0f, 2.0f, 1.0f);
        gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

        geo_process_node_and_siblings(node->node.children);
    }
}

/**
 * Process a perspective projection node.
 */
static void geo_process_perspective(struct GraphNodePerspective *node) {
    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    if (node->fnNode.node.children == NULL) { return; }

    u16 perspNorm;
    Mtx *mtx = alloc_display_list(sizeof(*mtx));
    if (mtx == NULL) { return; }

    f32 divisor = (f32) gCurGraphNodeRoot->height;
    if (divisor == 0) { divisor = 1; }
#ifdef VERSION_EU
    f32 aspect = ((f32) gCurGraphNodeRoot->width / divisor) * 1.1f;
#else
    f32 aspect = (f32) gCurGraphNodeRoot->width / divisor;
#endif

    gProjectionVanillaNearValue = node->near;
    gProjectionVanillaFarValue = node->far;
    f32 near = get_first_person_enabled() ? 1.f : replace_value_if_not_zero(MIN(node->near, gProjectionMaxNearValue), gOverrideNear);
    f32 far = replace_value_if_not_zero(node->far, gOverrideFar);

    // Keep VR infinite only when Infinite is actually selected. This mirrors
    // the per-eye projection path above and leaves the default unchanged.
    if (gOverrideFar == 0 && configDrawDistance == 6) {
        far = max(far, MAX_FAR_PLANE_DIST);
    }

    guPerspective(mtx, &perspNorm, node->prevFov, aspect, near, far, 1.0f);

    sPerspectiveNode = node;
    sPerspectiveMtx = mtx;
    sPerspectiveAspect = aspect;

    gSPPerspNormalize(gDisplayListHead++, perspNorm);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    if (vr_is_active() &&
        gVrSkyDomeGfx != NULL &&
        gVrSkyProjectionMtx != NULL &&
        gVrSkyDomeFrame == gGlobalTimer) {
        // The original skybox is a flat panorama. Draw the VR replacement as
        // real spherical geometry under a rotation-only headset projection,
        // then restore the normal eye projection for the level itself.
        gSPDisplayList(
            gDisplayListHead++,
            VIRTUAL_TO_PHYSICAL(gVrSkyDomeGfx)
        );
        gSPMatrix(
            gDisplayListHead++,
            VIRTUAL_TO_PHYSICAL(mtx),
            G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH
        );
        gSPMatrix(
            gDisplayListHead++,
            VIRTUAL_TO_PHYSICAL(gMatStackFixed[gMatStackIndex]),
            G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH
        );
    }

    gCurGraphNodeCamFrustum = node;
    geo_process_node_and_siblings(node->fnNode.node.children);
    gCurGraphNodeCamFrustum = NULL;
}

/**
 * Process a level of detail node. From the current transformation matrix,
 * the perpendicular distance to the camera is extracted and the children
 * of this node are only processed if that distance is within the render
 * range of this node.
 */
static void geo_process_level_of_detail(struct GraphNodeLevelOfDetail *node) {
    Mtx *mtx = gMatStackFixed[gMatStackIndex];
    // The desktop camera does not describe the direction the player is looking
    // in VR. Select the zero-distance (highest-detail) branch while VR is active.
    f32 distanceFromCam =
        (!vr_is_active() && gBehaviorValues.ProcessLODs)
            ? (s32) -mtx->m[3][2]
            : 0; // z-component of the translation column
    if ((f32)node->minDistance <= distanceFromCam && distanceFromCam < (f32)node->maxDistance) {
        if (node->node.children != 0) {
            geo_process_node_and_siblings(node->node.children);
        }
    }
}

/**
 * Process a switch case node. The node's selection function is called
 * if it is 0, and among the node's children, only the selected child is
 * processed next.
 */
static void geo_process_switch(struct GraphNodeSwitchCase *node) {
    struct GraphNode *selectedChild = node->fnNode.node.children;

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    for (s32 i = 0; selectedChild != NULL && node->selectedCase > i; i++) {
        selectedChild = selectedChild->next;
    }
    if (selectedChild != NULL) {
        geo_process_node_and_siblings(selectedChild);
    }
}

/**
 * Process a camera node.
 */
static void geo_process_camera(struct GraphNodeCamera *node) {
    Mat4 cameraTransform;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    Mtx *rollMtx = alloc_display_list(sizeof(*rollMtx));
    if (rollMtx == NULL) { return; }

    vec3f_copy(node->prevPos, node->pos);
    vec3f_copy(node->prevFocus, node->focus);

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    mtxf_rotate_xy(rollMtx->m, node->rollScreen);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(rollMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);

    mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
    mtxf_mul(gMatStack[gMatStackIndex + 1], cameraTransform, gMatStack[gMatStackIndex]);

    if (gCamSkipInterp) {
        // apply prevpos camera offset
        vec3f_copy(node->prevPos, node->pos);
        vec3f_add(node->prevPos, gCamSkipInterpDisplacement);
        vec3f_copy(node->prevFocus, node->focus);
        vec3f_add(node->prevFocus, gCamSkipInterpDisplacement);
    }

    // save prevpos camera offset
    vec3f_copy(gCamSkipInterpDisplacement, node->prevPos);
    vec3f_sub(gCamSkipInterpDisplacement, node->pos);

    if (gGlobalTimer == node->prevTimestamp + 1 && gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
        mtxf_lookat(cameraTransform, node->prevPos, node->prevFocus, node->roll);
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], cameraTransform, gMatStackPrev[gMatStackIndex]);
    } else {
        mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], cameraTransform, gMatStackPrev[gMatStackIndex]);
    }
    node->prevTimestamp = gGlobalTimer;
    sCameraNode = node;
    sVrControllerHandMasterList = gCurGraphNodeMasterList;

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    // save the camera matrix
    if (gCamera) {
        mtxf_copy(gCamera->mtx, gMatStack[gMatStackIndex]);
    }

    // compute inverse matrix for lighting engine and fresnel
    Mat4 invCameraMatrix;
    if (mtxf_inverse_non_affine(invCameraMatrix, gCamera->mtx)) {
        Mtx *invMtx = alloc_display_list(sizeof(Mtx));
        mtxf_to_mtx(invMtx, invCameraMatrix);
        gSPMatrix(gDisplayListHead++, invMtx, G_MTX_INVERSE_CAMERA_EXT);
    }

    if (node->fnNode.node.children != 0) {
        gCurGraphNodeCamera = node;
        sUsingCamSpace = TRUE;
        node->matrixPtr = &gMatStack[gMatStackIndex];
        node->matrixPtrPrev = &gMatStackPrev[gMatStackIndex];
        geo_process_node_and_siblings(node->fnNode.node.children);
        gCurGraphNodeCamera = NULL;
        sUsingCamSpace = FALSE;
    }
    gMatStackIndex--;
}

/**
 * Process a translation / rotation node. A transformation matrix based
 * on the node's translation and rotation is created and pushed on both
 * the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_translation_rotation(struct GraphNodeTranslationRotation *node) {
    Mat4 mtxf;
    Vec3f translation;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    // current frame
    vec3s_to_vec3f(translation, node->translation);
    mtxf_rotate_zxy_and_translate(mtxf, translation, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);

    // previous frame
    geo_update_interpolation(node->translation, node->rotation, NULL,
        if (geo_should_interpolate(interp)) {
            vec3s_to_vec3f(translation, interp->translation);
            mtxf_rotate_zxy_and_translate(mtxf, translation, interp->rotation);
        }
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mtxf, gMatStackPrev[gMatStackIndex]);
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Process a translation node. A transformation matrix based on the node's
 * translation is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_translation(struct GraphNodeTranslation *node) {
    Mat4 mtxf;
    Vec3f translation;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    // current frame
    vec3s_to_vec3f(translation, node->translation);
    mtxf_rotate_zxy_and_translate(mtxf, translation, gVec3sZero);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);

    // previous frame
    geo_update_interpolation(node->translation, NULL, NULL,
        if (geo_should_interpolate(interp)) {
            vec3s_to_vec3f(translation, interp->translation);
            mtxf_rotate_zxy_and_translate(mtxf, translation, gVec3sZero);
        }
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mtxf, gMatStackPrev[gMatStackIndex]);
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Process a rotation node. A transformation matrix based on the node's
 * rotation is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_rotation(struct GraphNodeRotation *node) {
    Mat4 mtxf;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    // current frame
    mtxf_rotate_zxy_and_translate(mtxf, gVec3fZero, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);

    // previous frame
    geo_update_interpolation(NULL, node->rotation, NULL,
        if (geo_should_interpolate(interp)) {
            mtxf_rotate_zxy_and_translate(mtxf, gVec3fZero, interp->rotation);
        }
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mtxf, gMatStackPrev[gMatStackIndex]);
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Process a scaling node. A transformation matrix based on the node's
 * scale is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_scale(struct GraphNodeScale *node) {
    Vec3f scaleVec;
    Vec3f prevScaleVec;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    // current frame
    vec3f_set(scaleVec, node->scale, node->scale, node->scale);
    mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], scaleVec);

    // previous frame
    geo_update_interpolation(NULL, NULL, scaleVec,
        vec3f_copy(prevScaleVec,
            geo_should_interpolate(interp) ?
            interp->scale :
            scaleVec
        );
        mtxf_scale_vec3f(gMatStackPrev[gMatStackIndex + 1], gMatStackPrev[gMatStackIndex], prevScaleVec);
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Process an XYZ scaling node. A transformation matrix based on the node's
 * scale is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_scale_xyz(struct GraphNodeScaleXYZ *node) {

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    // current frame
    mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], node->scale);

    // previous frame
    geo_update_interpolation(NULL, NULL, node->scale,
        mtxf_scale_vec3f(gMatStackPrev[gMatStackIndex + 1], gMatStackPrev[gMatStackIndex],
            geo_should_interpolate(interp) ?
            interp->scale :
            node->scale
        );
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Process a billboard node. A transformation matrix is created that makes its
 * children face the camera, and it is pushed on the floating point and fixed
 * point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_billboard(struct GraphNodeBillboard *node) {
    Vec3f translation;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    s16 nextMatStackIndex = gMatStackIndex + 1;

    // current frame
    vec3s_to_vec3f(translation, node->translation);
    mtxf_billboard(gMatStack[nextMatStackIndex], gMatStack[gMatStackIndex], translation, gCurGraphNodeCamera->roll);

    // previous frame
    geo_update_interpolation(node->translation, NULL, NULL,
        if (geo_should_interpolate(interp)) {
            vec3s_to_vec3f(translation, interp->translation);
        }
        mtxf_billboard(gMatStackPrev[nextMatStackIndex], gMatStackPrev[gMatStackIndex], translation, gCurGraphNodeCamera->roll);
    );

    if (gCurGraphNodeHeldObject != NULL) {
        mtxf_scale_vec3f(gMatStack[nextMatStackIndex], gMatStack[nextMatStackIndex],
                         gCurGraphNodeHeldObject->objNode->header.gfx.scale);
        mtxf_scale_vec3f(gMatStackPrev[nextMatStackIndex], gMatStackPrev[nextMatStackIndex],
                         gCurGraphNodeHeldObject->objNode->header.gfx.prevScale);
    } else if (gCurGraphNodeObject != NULL) {
        mtxf_scale_vec3f(gMatStack[nextMatStackIndex], gMatStack[nextMatStackIndex],
                         gCurGraphNodeObject->scale);
        mtxf_scale_vec3f(gMatStackPrev[nextMatStackIndex], gMatStackPrev[nextMatStackIndex],
                         gCurGraphNodeObject->prevScale);
    } else {
        //LOG_ERROR("gCurGraphNodeObject and gCurGraphNodeHeldObject are both NULL!");
    }

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    const u8 previousBillboardState = sUsingBillboard;
    sUsingBillboard = TRUE;

    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }

    sUsingBillboard = previousBillboardState;
    gMatStackIndex--;
}

/**
 * Process a display list node. It draws a display list without first pushing
 * a transformation on the stack, so all transformations are inherited from the
 * parent node. It processes its children if it has them.
 */
static void geo_process_display_list(struct GraphNodeDisplayList *node) {
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
}

/**
 * Process a generated list. Instead of storing a pointer to a display list,
 * the list is generated on the fly by a function.
 */
static void geo_process_generated_list(struct GraphNodeGenerated *node) {
    if (node->fnNode.func != NULL) {
        Gfx *list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);

        if (list != NULL) {
            geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
        }
    }
    if (node->fnNode.node.children != NULL) {
        geo_process_node_and_siblings(node->fnNode.node.children);
    }
}

/**
 * Process a background node. Tries to retrieve a background display list from
 * the function of the node. If that function is null or returns null, a black
 * rectangle is drawn instead.
 */
static void geo_process_background(struct GraphNodeBackground *node) {
    Gfx *list = NULL;

    if (node->fnNode.func != NULL) {
        Vec3f posCopy;
        Vec3f focusCopy;

        vec3f_copy(posCopy, gLakituState.pos);
        vec3f_copy(focusCopy, gLakituState.focus);
        if (gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
            vec3f_copy(gLakituState.pos, node->prevCameraPos);
            vec3f_copy(gLakituState.focus, node->prevCameraFocus);
            sBackgroundNode = node;
            sBackgroundNodeRoot = gCurGraphNodeRoot;
        }
        list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, NULL);
        vec3f_copy(gLakituState.pos, posCopy);
        vec3f_copy(gLakituState.focus, focusCopy);
    }

    const bool vrSkyDomeReady =
        vr_is_active() &&
        gVrSkyDomeGfx != NULL &&
        gVrSkyDomeFrame == gGlobalTimer;

    if (vrSkyDomeReady) {
        // The perspective node draws the world-locked 3D dome. Suppress the
        // legacy orthographic panorama so it cannot follow the headset or
        // distort across the wide eye projection.
    } else if (list != NULL) {
        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
    } else if (gCurGraphNodeMasterList != NULL) {
#ifndef F3DEX_GBI_2E
        Gfx *gfxStart = alloc_display_list(sizeof(Gfx) * 7);
#else
        Gfx *gfxStart = alloc_display_list(sizeof(Gfx) * 8);
#endif
        Gfx *gfx = gfxStart;
        if (gfx == NULL) { return; }

        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_FILL);
        gDPSetFillColor(gfx++, node->background);
        gDPFillRectangle(gfx++, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(0), BORDER_HEIGHT,
        GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(0) - 1, SCREEN_HEIGHT - BORDER_HEIGHT - 1);
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gSPEndDisplayList(gfx++);
        gReadOnlyBackground = -1;

        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(gfxStart), 0);
    }
    if (node->fnNode.node.children != NULL) {
        geo_process_node_and_siblings(node->fnNode.node.children);
    }
}

static void anim_process(Vec3f translation, Vec3s rotation, Vec3f scale, u8 *animType, s16 animFrame, u16 **animAttribute) {
    if (*animType == ANIM_TYPE_TRANSLATION) {
        translation[0] += retrieve_animation_value(gCurAnim, animFrame, animAttribute) * gCurAnimTranslationMultiplier;
        translation[1] += retrieve_animation_value(gCurAnim, animFrame, animAttribute) * gCurAnimTranslationMultiplier;
        translation[2] += retrieve_animation_value(gCurAnim, animFrame, animAttribute) * gCurAnimTranslationMultiplier;
        *animType = ANIM_TYPE_ROTATION;
    } else {
        if (*animType == ANIM_TYPE_LATERAL_TRANSLATION) {
            translation[0] += retrieve_animation_value(gCurAnim, animFrame, animAttribute) * gCurAnimTranslationMultiplier;
            *animAttribute += 2;
            translation[2] += retrieve_animation_value(gCurAnim, animFrame, animAttribute) * gCurAnimTranslationMultiplier;
            *animType = ANIM_TYPE_ROTATION;
        } else {
            if (*animType == ANIM_TYPE_VERTICAL_TRANSLATION) {
                *animAttribute += 2;
                translation[1] += retrieve_animation_value(gCurAnim, animFrame, animAttribute) * gCurAnimTranslationMultiplier;
                *animAttribute += 2;
                *animType = ANIM_TYPE_ROTATION;
            } else if (*animType == ANIM_TYPE_NO_TRANSLATION) {
                *animAttribute += 6;
                *animType = ANIM_TYPE_ROTATION;
            }
        }
    }

    if (*animType == ANIM_TYPE_ROTATION) {
        // GEO_ANIMATED_PART: rotation = (0 + AnimValue)
        // GEO_BONE: rotation = (BoneRotation + AnimValue)
        rotation[0] += retrieve_animation_value(gCurAnim, animFrame, animAttribute);
        rotation[1] += retrieve_animation_value(gCurAnim, animFrame, animAttribute);
        rotation[2] += retrieve_animation_value(gCurAnim, animFrame, animAttribute);

        if (gCurAnim->flags & ANIM_FLAG_BONE_SCALE) {
            s16 scaleX = retrieve_animation_value(gCurAnim, animFrame, animAttribute);
            s16 scaleY = retrieve_animation_value(gCurAnim, animFrame, animAttribute);
            s16 scaleZ = retrieve_animation_value(gCurAnim, animFrame, animAttribute);

            if (scale != NULL) {
                scale[0] *= ((f32) scaleX) / 256.0f;
                scale[1] *= ((f32) scaleY) / 256.0f;
                scale[2] *= ((f32) scaleZ) / 256.0f;
            }
        }

        if (gCurAnim->flags & ANIM_FLAG_BONE_TRANS) {
            *animType = ANIM_TYPE_TRANSLATION;
        }
    }
}

/**
 * Render an animated part. The current animation state is not part of the node
 * but set in global variables. If an animated part is skipped, everything afterwards desyncs.
 */
static void vr_apply_world_translation_to_camera_matrix(
    Mat4 matrix,
    Mat4 cameraMatrix,
    f32 worldX,
    f32 worldY,
    f32 worldZ
) {
    matrix[3][0] +=
        worldX * cameraMatrix[0][0] +
        worldY * cameraMatrix[1][0] +
        worldZ * cameraMatrix[2][0];
    matrix[3][1] +=
        worldX * cameraMatrix[0][1] +
        worldY * cameraMatrix[1][1] +
        worldZ * cameraMatrix[2][1];
    matrix[3][2] +=
        worldX * cameraMatrix[0][2] +
        worldY * cameraMatrix[1][2] +
        worldZ * cameraMatrix[2][2];
}

static bool vr_is_left_arm_part(u32 animPart) {
    return animPart == MARIO_ANIM_PART_LEFT_ARM ||
        animPart == MARIO_ANIM_PART_LEFT_FOREARM;
}

static bool vr_is_right_arm_part(u32 animPart) {
    return animPart == MARIO_ANIM_PART_RIGHT_ARM ||
        animPart == MARIO_ANIM_PART_RIGHT_FOREARM;
}

static bool vr_get_stabilized_arm_target(
    u32 handIndex,
    Vec3f targetPosition,
    bool previousFrame
) {
    if (handIndex >= VR_CONTROLLER_COUNT ||
        targetPosition == NULL) {
        return false;
    }

    if (!sVrArmTargetSampleValid[handIndex] ||
        sVrArmTargetSampleTimestamp[handIndex] != gGlobalTimer) {
        Vec3f sampledPosition;
        if (!vr_get_controller_world_fist(
                handIndex,
                sampledPosition,
                NULL
            )) {
            sVrArmTargetSampleValid[handIndex] = false;
            return false;
        }
        if (!vr_move_world_sample_to_current_gameplay_anchor(
                sampledPosition
            )) {
            sVrArmTargetSampleValid[handIndex] = false;
            return false;
        }

        const bool continuous =
            sVrArmTargetSampleValid[handIndex] &&
            gGlobalTimer ==
                sVrArmTargetSampleTimestamp[handIndex] + 1;
        if (continuous) {
            vec3f_copy(
                sVrArmTargetSamplePrev[handIndex],
                sVrArmTargetSample[handIndex]
            );
        } else {
            vec3f_copy(
                sVrArmTargetSamplePrev[handIndex],
                sampledPosition
            );
        }
        vec3f_copy(
            sVrArmTargetSample[handIndex],
            sampledPosition
        );
        sVrArmTargetSampleTimestamp[handIndex] = gGlobalTimer;
        sVrArmTargetSampleValid[handIndex] = true;
    }

    // Both interpolation endpoints use the latest controller sample. The
    // floating glove is patched from that same pose immediately before each
    // eye draw; interpolating the arm toward last frame's controller pose made
    // the cuff visibly detach during fast hand motion and vertical actions.
    (void)previousFrame;
    vec3f_copy(targetPosition, sVrArmTargetSample[handIndex]);
    return true;
}

static void vr_get_arm_model_lengths(
    f32* upperArmLength,
    f32* forearmLength
) {
    u8 characterIndex = gNetworkPlayers[0].overrideModelIndex;
    if (characterIndex >= CT_MAX) {
        characterIndex = CT_MARIO;
    }

    switch (characterIndex) {
        case CT_LUIGI:
            *upperArmLength = 70.0f;
            *forearmLength = 65.0f;
            break;
        case CT_TOAD:
            *upperArmLength = 20.0f;
            *forearmLength = 26.0f;
            break;
        case CT_WALUIGI:
            *upperArmLength = 140.0f;
            *forearmLength = 114.0f;
            break;
        case CT_WARIO:
            *upperArmLength = 101.0f;
            *forearmLength = 82.0f;
            break;
        default:
            *upperArmLength = 65.0f;
            *forearmLength = 60.0f;
            break;
    }
}

static f32 vr_get_matrix_transverse_scale(Mat4 matrix) {
    const f32 yScale = sqrtf(
        matrix[1][0] * matrix[1][0] +
        matrix[1][1] * matrix[1][1] +
        matrix[1][2] * matrix[1][2]
    );
    const f32 zScale = sqrtf(
        matrix[2][0] * matrix[2][0] +
        matrix[2][1] * matrix[2][1] +
        matrix[2][2] * matrix[2][2]
    );
    return MAX((yScale + zScale) * 0.5f, 0.001f);
}

static bool vr_calculate_basic_arm_ik_elbow(
    u32 handIndex,
    Vec3f shoulderPosition,
    Vec3f handPosition,
    f32 upperArmLength,
    f32 forearmLength,
    f32 modelScale,
    bool previousFrame,
    Vec3f elbowPosition
) {
    Vec3f shoulderToHand = {
        handPosition[0] - shoulderPosition[0],
        handPosition[1] - shoulderPosition[1],
        handPosition[2] - shoulderPosition[2]
    };
    const f32 handDistance = sqrtf(
        shoulderToHand[0] * shoulderToHand[0] +
        shoulderToHand[1] * shoulderToHand[1] +
        shoulderToHand[2] * shoulderToHand[2]
    );
    if (handDistance <= 0.001f || !isfinite(handDistance)) {
        return false;
    }
    vec3f_mul(shoulderToHand, 1.0f / handDistance);

    f32 upperLength = upperArmLength * modelScale;
    f32 lowerLength = forearmLength * modelScale;
    const f32 naturalReach = upperLength + lowerLength;
    // Preserve the character's normal proportions inside their natural reach.
    // Beyond it, extend both segments uniformly while retaining a small elbow
    // bend. This keeps the cuff attached without producing one infinitely long
    // forearm.
    const f32 reachScale = clamp(
        handDistance / MAX(naturalReach * 0.98f, 0.001f),
        1.0f,
        3.0f
    );
    upperLength *= reachScale;
    lowerLength *= reachScale;

    const f32 solveDistance = clamp(
        handDistance,
        fabsf(upperLength - lowerLength) + 0.001f,
        upperLength + lowerLength - 0.001f
    );
    const f32 alongDistance =
        (upperLength * upperLength - lowerLength * lowerLength +
         solveDistance * solveDistance) /
        (2.0f * solveDistance);
    const f32 bendDistance = sqrtf(MAX(
        upperLength * upperLength - alongDistance * alongDistance,
        0.0f
    ));

    // Use a downward/outward pole vector. It gives both elbows a stable,
    // natural bend while still allowing the hands to cross or rise overhead.
    Vec3f headsetPosition;
    Vec3f outward = {
        handIndex == VR_CONTROLLER_LEFT ? -1.0f : 1.0f,
        0.0f,
        0.0f
    };
    if (vr_get_stabilized_headset_world_position(
            headsetPosition,
            previousFrame
        )) {
        outward[0] = shoulderPosition[0] - headsetPosition[0];
        outward[1] = 0.0f;
        outward[2] = shoulderPosition[2] - headsetPosition[2];
        const f32 outwardLength = sqrtf(
            outward[0] * outward[0] +
            outward[2] * outward[2]
        );
        if (outwardLength > 0.001f) {
            vec3f_mul(outward, 1.0f / outwardLength);
        }
    }

    Vec3f bendDirection = {
        outward[0] * 0.35f,
        -1.0f,
        outward[2] * 0.35f
    };
    const f32 bendAlong =
        bendDirection[0] * shoulderToHand[0] +
        bendDirection[1] * shoulderToHand[1] +
        bendDirection[2] * shoulderToHand[2];
    bendDirection[0] -= shoulderToHand[0] * bendAlong;
    bendDirection[1] -= shoulderToHand[1] * bendAlong;
    bendDirection[2] -= shoulderToHand[2] * bendAlong;
    f32 bendLength = sqrtf(
        bendDirection[0] * bendDirection[0] +
        bendDirection[1] * bendDirection[1] +
        bendDirection[2] * bendDirection[2]
    );
    if (bendLength <= 0.001f) {
        vec3f_cross(bendDirection, shoulderToHand, outward);
        bendLength = sqrtf(
            bendDirection[0] * bendDirection[0] +
            bendDirection[1] * bendDirection[1] +
            bendDirection[2] * bendDirection[2]
        );
    }
    if (bendLength <= 0.001f) {
        return false;
    }
    vec3f_mul(bendDirection, 1.0f / bendLength);

    for (u32 axis = 0; axis < 3; axis++) {
        elbowPosition[axis] = shoulderPosition[axis] +
            shoulderToHand[axis] * alongDistance +
            bendDirection[axis] * bendDistance;
    }
    return true;
}

static bool vr_point_arm_segment_at_target(
    Mat4 matrix,
    Mat4 cameraMatrix,
    Vec3f targetPosition,
    f32 modelLength
) {
    Vec3f partPosition;
    get_pos_from_transform_mtx(
        partPosition,
        matrix,
        cameraMatrix
    );
    Vec3f directionWorld = {
        targetPosition[0] - partPosition[0],
        targetPosition[1] - partPosition[1],
        targetPosition[2] - partPosition[2]
    };
    const f32 targetDistance = sqrtf(
        directionWorld[0] * directionWorld[0] +
        directionWorld[1] * directionWorld[1] +
        directionWorld[2] * directionWorld[2]
    );
    if (targetDistance <= 0.001f ||
        !isfinite(targetDistance) ||
        modelLength <= 0.001f) {
        return false;
    }

    Vec3f armAxis = {
        directionWorld[0] * cameraMatrix[0][0] +
            directionWorld[1] * cameraMatrix[1][0] +
            directionWorld[2] * cameraMatrix[2][0],
        directionWorld[0] * cameraMatrix[0][1] +
            directionWorld[1] * cameraMatrix[1][1] +
            directionWorld[2] * cameraMatrix[2][1],
        directionWorld[0] * cameraMatrix[0][2] +
            directionWorld[1] * cameraMatrix[1][2] +
            directionWorld[2] * cameraMatrix[2][2]
    };
    vec3f_normalize(armAxis);

    Vec3f upReference = {
        cameraMatrix[1][0],
        cameraMatrix[1][1],
        cameraMatrix[1][2]
    };
    Vec3f sideAxis;
    vec3f_cross(sideAxis, armAxis, upReference);
    f32 sideLength = sqrtf(
        sideAxis[0] * sideAxis[0] +
        sideAxis[1] * sideAxis[1] +
        sideAxis[2] * sideAxis[2]
    );
    if (sideLength <= 0.001f) {
        upReference[0] = cameraMatrix[2][0];
        upReference[1] = cameraMatrix[2][1];
        upReference[2] = cameraMatrix[2][2];
        vec3f_cross(sideAxis, armAxis, upReference);
    }
    vec3f_normalize(sideAxis);

    Vec3f upAxis;
    vec3f_cross(upAxis, sideAxis, armAxis);
    vec3f_normalize(upAxis);

    const f32 yScale = sqrtf(
        matrix[1][0] * matrix[1][0] +
        matrix[1][1] * matrix[1][1] +
        matrix[1][2] * matrix[1][2]
    );
    const f32 zScale = sqrtf(
        matrix[2][0] * matrix[2][0] +
        matrix[2][1] * matrix[2][1] +
        matrix[2][2] * matrix[2][2]
    );
    const f32 armScale = targetDistance / modelLength;

    for (u32 axis = 0; axis < 3; axis++) {
        matrix[0][axis] = armAxis[axis] * armScale;
        matrix[1][axis] = upAxis[axis] * yScale;
        matrix[2][axis] = sideAxis[axis] * zScale;
    }
    return true;
}

static void vr_lock_local_mario_arm_to_controller(
    Mat4 matrix,
    Mat4 cameraMatrix,
    bool previousFrame
) {
    if (!configVrExperimentalArmsMode ||
        gCurMarioBodyState == NULL) {
        return;
    }

    const u32 animPart = gCurMarioBodyState->currAnimPart;
    const bool leftArm = vr_is_left_arm_part(animPart);
    const bool rightArm = vr_is_right_arm_part(animPart);
    if (!leftArm && !rightArm) {
        return;
    }

    const u32 handIndex = leftArm
        ? VR_CONTROLLER_LEFT
        : VR_CONTROLLER_RIGHT;
    Vec3f targetPosition;
    if (!vr_get_stabilized_arm_target(
            handIndex,
            targetPosition,
            previousFrame
        )) {
        return;
    }

    f32 upperArmLength;
    f32 forearmLength;
    vr_get_arm_model_lengths(&upperArmLength, &forearmLength);

    if (animPart == MARIO_ANIM_PART_LEFT_ARM ||
        animPart == MARIO_ANIM_PART_RIGHT_ARM) {
        Vec3f shoulderPosition;
        Vec3f elbowPosition;
        get_pos_from_transform_mtx(
            shoulderPosition,
            matrix,
            cameraMatrix
        );
        if (!vr_calculate_basic_arm_ik_elbow(
                handIndex,
                shoulderPosition,
                targetPosition,
                upperArmLength,
                forearmLength,
                vr_get_matrix_transverse_scale(matrix),
                previousFrame,
                elbowPosition
            )) {
            return;
        }
        vr_point_arm_segment_at_target(
            matrix,
            cameraMatrix,
            elbowPosition,
            upperArmLength
        );
    } else {
        vr_point_arm_segment_at_target(
            matrix,
            cameraMatrix,
            targetPosition,
            forearmLength
        );
    }
}

static f32 vr_get_local_mario_torso_alignment(
    f32 manualHeightOffset
) {
    u8 characterIndex = gNetworkPlayers[0].overrideModelIndex;
    if (characterIndex >= CT_MAX) {
        characterIndex = CT_MARIO;
    }

    if (sVrTorsoAlignmentCharacter != characterIndex) {
        // Do not calibrate a newly selected character from the preceding
        // model's last stored head joint.
        sVrTorsoAlignmentValid = false;
        sVrTorsoAlignmentCharacter = characterIndex;
        sVrTorsoAlignmentCharacterTimestamp = gGlobalTimer;
        sVrTorsoAlignment = 0.0f;
    }

    struct MarioBodyState* bodyState = gMarioStates[0].marioBodyState;
    if (!sVrTorsoAlignmentValid &&
        gGlobalTimer > sVrTorsoAlignmentCharacterTimestamp &&
        gMarioStates[0].action == ACT_IDLE &&
        gMarioStates[0].marioObj != NULL &&
        bodyState != NULL &&
        (bodyState->updateHeadPosTime == gGlobalTimer ||
         (gGlobalTimer > 0 &&
          bodyState->updateHeadPosTime == gGlobalTimer - 1))) {
        // animPartsPos contains the actual rendered head attachment, including
        // model-pack scales and the manual torso-height adjustment. Compare
        // its unadjusted position with the built-in character's intended top
        // of torso. This corrects short-rendering tall models without moving
        // the camera or lifting the legs away from the floor.
        const f32 naturalTorsoTop =
            bodyState->animPartsPos[MARIO_ANIM_PART_HEAD][1] -
            manualHeightOffset;
        const f32 intendedTorsoTop =
            gMarioStates[0].marioObj->header.gfx.pos[1] +
            (f32)config_vr_head_attachment_height_for_character(
                characterIndex
            );
        const f32 correction = intendedTorsoTop - naturalTorsoTop;
        if (isfinite(correction)) {
            sVrTorsoAlignment = clamp(
                correction,
                -250.0f,
                250.0f
            );
            sVrTorsoAlignmentValid = true;
        }
    }

    // Keep the former Mario body relationship as an opt-in compatibility
    // offset without rewriting the player's saved camera-height profile.
    const f32 previousHeightOffset =
        configVrPreviousBodyHeight && characterIndex == CT_MARIO
            ? 20.0f
            : 0.0f;
    return sVrTorsoAlignment + previousHeightOffset;
}

static void vr_adjust_local_mario_body_transform(
    Mat4 matrix,
    bool previousFrame
) {
    if (!sVrFilteringLocalMarioBody ||
        gCurMarioBodyState == NULL ||
        gCurGraphNodeCamera == NULL) {
        return;
    }

    Mat4* cameraMatrix = previousFrame
        ? gCurGraphNodeCamera->matrixPtrPrev
        : gCurGraphNodeCamera->matrixPtr;
    if (cameraMatrix == NULL) {
        return;
    }

    vr_lock_local_mario_arm_to_controller(
        matrix,
        *cameraMatrix,
        previousFrame
    );

    unsigned int heightValue;
    switch (gCurMarioBodyState->currAnimPart) {
        case MARIO_ANIM_PART_TORSO:
            heightValue = configVrTorsoHeight;
            break;
        case MARIO_ANIM_PART_LOWER_LEFT:
        case MARIO_ANIM_PART_LOWER_RIGHT:
            heightValue = configVrLegHeight;
            break;
        default:
            return;
    }

    f32 heightOffset =
        ((f32)clamp(heightValue, 0U, 200U) - 100.0f) *
        0.5f;
    if (gCurMarioBodyState->currAnimPart ==
        MARIO_ANIM_PART_TORSO) {
        heightOffset += vr_get_local_mario_torso_alignment(
            heightOffset
        );
        if ((gMarioStates[0].action & ACT_GROUP_MASK) ==
            ACT_GROUP_MOVING) {
            // Walking/running animations lift the torso root slightly above
            // its idle placement. Counter only that small visual difference
            // so changing between standing and moving does not make the chest
            // jump, without changing Mario's camera, physics, or collision.
            heightOffset -= 2.0f;
        }
    }

    // Animated part translations are local to their parent bone. Applying
    // the setting there causes a bent/rotated torso or leg to turn "height"
    // into forward motion. The combined matrix is in camera space, so move it
    // along the camera matrix's world-up basis to produce a true world-Y
    // offset regardless of the animation or Mario's facing direction.
    vr_apply_world_translation_to_camera_matrix(
        matrix,
        *cameraMatrix,
        0.0f,
        heightOffset,
        0.0f
    );

    if (gCurMarioBodyState->currAnimPart ==
        MARIO_ANIM_PART_TORSO) {
        Vec3f headsetPosition;
        Vec3f torsoPosition;
        if (vr_get_stabilized_headset_world_position(
                headsetPosition,
                previousFrame
            )) {
            get_pos_from_transform_mtx(
                torsoPosition,
                matrix,
                *cameraMatrix
            );
            // The body root already follows horizontal room-scale tracking.
            // Cancel the torso bone's remaining animated X/Z displacement so
            // its center itself—not merely Mario's feet—stays under the HMD.
            vr_apply_world_translation_to_camera_matrix(
                matrix,
                *cameraMatrix,
                headsetPosition[0] - torsoPosition[0],
                0.0f,
                headsetPosition[2] - torsoPosition[2]
            );
        }
    }

}

static void geo_process_animated_part(struct GraphNodeAnimatedPart *node) {
    if (gCurMarioBodyState && !gCurGraphNodeHeldObject) {
        gCurMarioBodyState->currAnimPart++;
    }

    Mat4 matrix;
    Vec3s rotation;
    Vec3f translation;
    Vec3f scale;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    u16 *animAttribute = gCurrAnimAttribute;
    u8 animType = gCurAnimType;

    // current frame
    vec3s_copy(rotation, gVec3sZero);
    vec3s_to_vec3f(translation, node->translation);
    vec3f_copy(scale, gVec3fOne);
    anim_process(translation, rotation, scale, &gCurAnimType, gCurrAnimFrame, &gCurrAnimAttribute);
    mtxf_rotate_xyz_and_translate(matrix, translation, rotation);
    mtxf_scale_vec3f(matrix, matrix, scale);
    mtxf_mul(gMatStack[gMatStackIndex + 1], matrix, gMatStack[gMatStackIndex]);
    vr_adjust_local_mario_body_transform(
        gMatStack[gMatStackIndex + 1],
        false
    );

    // previous frame
    geo_update_interpolation(node->translation, NULL, NULL,
        vec3s_to_vec3f(translation,
            geo_should_interpolate(interp) ?
            interp->translation :
            node->translation
        );
        vec3s_copy(rotation, gVec3sZero);
        vec3f_copy(scale, gVec3fOne);
        anim_process(translation, rotation, scale, &animType, gPrevAnimFrame, &animAttribute);
        mtxf_rotate_xyz_and_translate(matrix, translation, rotation);
        mtxf_scale_vec3f(matrix, matrix, scale);
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], matrix, gMatStackPrev[gMatStackIndex]);
        vr_adjust_local_mario_body_transform(
            gMatStackPrev[gMatStackIndex + 1],
            true
        );
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    // Mario anim part pos and rot
    if (gCurMarioBodyState && !gCurGraphNodeHeldObject && gCurMarioBodyState->currAnimPart > MARIO_ANIM_PART_NONE && gCurMarioBodyState->currAnimPart < MARIO_ANIM_PART_MAX) {
        get_pos_from_transform_mtx(
            gCurMarioBodyState->animPartsPos[gCurMarioBodyState->currAnimPart],
            gMatStack[gMatStackIndex],
            *gCurGraphNodeCamera->matrixPtr
        );

        Vec3s rot = { rotation[2], rotation[0], rotation[1] };
        vec3s_copy(gCurMarioBodyState->animPartsRot[gCurMarioBodyState->currAnimPart], rot);
    }

    if (gCurGraphNodeMarioState != NULL) {
        Vec3f translated = { 0 };
        get_pos_from_transform_mtx(translated, gMatStack[gMatStackIndex], *gCurGraphNodeCamera->matrixPtr);
        gCurGraphNodeMarioState->minimumBoneY = fmin(gCurGraphNodeMarioState->minimumBoneY, translated[1] - gCurGraphNodeMarioState->marioObj->header.gfx.pos[1]);
    }
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Initialize the animation-related global variables for the currently drawn
 * object's animation.
 */
void geo_set_animation_globals(struct AnimInfo *node, s32 hasAnimation) {
    struct Animation *anim = node->curAnim;

    if (hasAnimation) {
        node->animFrame = geo_update_animation_frame(node, &node->animFrameAccelAssist);
    }
    node->animTimer = gAreaUpdateCounter;
    if (anim->flags & ANIM_FLAG_HOR_TRANS) {
        gCurAnimType = ANIM_TYPE_VERTICAL_TRANSLATION;
    } else if (anim->flags & ANIM_FLAG_VERT_TRANS) {
        gCurAnimType = ANIM_TYPE_LATERAL_TRANSLATION;
    } else if (anim->flags & ANIM_FLAG_6) {
        gCurAnimType = ANIM_TYPE_NO_TRANSLATION;
    } else {
        gCurAnimType = ANIM_TYPE_TRANSLATION;
    }

    gCurrAnimFrame = node->animFrame;
    if (node->prevAnimPtr == anim && node->prevAnimID == node->animID &&
        gGlobalTimer == node->prevAnimFrameTimestamp + 1) {
        gPrevAnimFrame = node->prevAnimFrame;
    } else {
        gPrevAnimFrame = node->animFrame;
    }
    node->prevAnimPtr = anim;
    node->prevAnimID = node->animID;
    node->prevAnimFrame = node->animFrame;
    node->prevAnimFrameTimestamp = gGlobalTimer;

    gCurAnimEnabled = (anim->flags & ANIM_FLAG_5) == 0;
    gCurrAnimAttribute = segmented_to_virtual((void *) anim->index);
    gCurAnim = anim;

    if (anim->animYTransDivisor == 0) {
        gCurAnimTranslationMultiplier = 1.0f;
    } else {
        gCurAnimTranslationMultiplier = (f32) node->animYTrans / (f32) anim->animYTransDivisor;
    }
}

/**
 * Process a shadow node. Renders a shadow under an object offset by the
 * translation of the first animated component and rotated according to
 * the floor below it.
 */
static void geo_process_shadow(struct GraphNodeShadow *node) {
    Mat4 mtxf;
    Vec3f shadowPosPrev;
    Vec3f animOffset;
    f32 shadowScale;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    // A shadow node can own the object's visible geometry. Ultra mode may
    // skip the expensive shadow work, but it must still traverse children.
    const bool distantChainLinkShadow =
        vr_is_active() &&
        gCurGraphNodeProcessingObject != NULL &&
        gCurGraphNodeProcessingObject->oDistanceToMario > 1000.0f &&
        obj_has_behavior(
            gCurGraphNodeProcessingObject,
            bhvChainChompChainPart
        );
    const bool skipShadow =
        (configVrUltraPerformanceMode &&
         gCurGraphNodeProcessingObject != gMarioStates[0].marioObj) ||
        distantChainLinkShadow;

    if (!skipShadow && gCurGraphNodeCamera != NULL && gCurGraphNodeObject != NULL) {
        if (gCurGraphNodeHeldObject != NULL) {
            get_pos_from_transform_mtx(gCurGraphNodeObject->shadowPos, gMatStack[gMatStackIndex],
                                       *gCurGraphNodeCamera->matrixPtr);
            shadowScale = node->shadowScale;
        } else {
            if (!gCurGraphNodeObject->disableAutomaticShadowPos) {
                vec3f_copy(gCurGraphNodeObject->shadowPos, gCurGraphNodeObject->pos);
            }
            shadowScale = node->shadowScale * gCurGraphNodeObject->scale[0];
        }

        Vec3f objScale = { 1, 1, 1 };
        if (gCurAnimEnabled) {
            if (gCurAnimType == ANIM_TYPE_TRANSLATION
                || gCurAnimType == ANIM_TYPE_LATERAL_TRANSLATION) {
                struct GraphNode *geo = node->node.children;
                if (geo != NULL) {
                    switch (geo->type) {
                        case GRAPH_NODE_TYPE_SCALE:
                            vec3f_mul(objScale, ((struct GraphNodeScale *) geo)->scale);
                            break;
                        case GRAPH_NODE_TYPE_SCALE_XYZ:
                            vec3f_copy(objScale, ((struct GraphNodeScaleXYZ *) geo)->scale);
                            break;
                    }
                }
                animOffset[0] = retrieve_animation_value(gCurAnim, gCurrAnimFrame, &gCurrAnimAttribute) * gCurAnimTranslationMultiplier * objScale[0];
                animOffset[1] = 0.0f;
                gCurrAnimAttribute += 2;
                animOffset[2] = retrieve_animation_value(gCurAnim, gCurrAnimFrame, &gCurrAnimAttribute) * gCurAnimTranslationMultiplier * objScale[2];
                gCurrAnimAttribute -= 6;

                // simple matrix rotation so the shadow offset rotates along with the object
                f32 sinAng = sins(gCurGraphNodeObject->angle[1]);
                f32 cosAng = coss(gCurGraphNodeObject->angle[1]);

                gCurGraphNodeObject->shadowPos[0] += animOffset[0] * cosAng + animOffset[2] * sinAng;
                gCurGraphNodeObject->shadowPos[2] += -animOffset[0] * sinAng + animOffset[2] * cosAng;
            }
        }

        if (gCurGraphNodeHeldObject != NULL) {

            if (gGlobalTimer == gCurGraphNodeHeldObject->prevShadowPosTimestamp + 1) {
                vec3f_copy(shadowPosPrev, gCurGraphNodeHeldObject->prevShadowPos);
            } else {
                vec3f_copy(shadowPosPrev, gCurGraphNodeObject->shadowPos);
            }

            vec3f_copy(gCurGraphNodeHeldObject->prevShadowPos, gCurGraphNodeObject->shadowPos);
            gCurGraphNodeHeldObject->prevShadowPosTimestamp = gGlobalTimer;
        } else {
            if (gGlobalTimer == gCurGraphNodeObject->prevShadowPosTimestamp + 1 &&
                gGlobalTimer != gCurGraphNodeObject->skipInterpolationTimestamp &&
                gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
                vec3f_copy(shadowPosPrev, gCurGraphNodeObject->prevShadowPos);
            } else {
                vec3f_copy(shadowPosPrev, gCurGraphNodeObject->shadowPos);
            }
            vec3f_copy(gCurGraphNodeObject->prevShadowPos, gCurGraphNodeObject->shadowPos);
            gCurGraphNodeObject->prevShadowPosTimestamp = gGlobalTimer;
        }

        struct ShadowInterp* interp = growing_array_alloc(sShadowInterp, sizeof(struct ShadowInterp));
        gShadowInterpCurrent = interp;
        interp->gfx = NULL;
        interp->node = node;
        interp->shadowScale = shadowScale;
        interp->obj = gCurGraphNodeObject;
        vec3f_copy(interp->shadowPos, gCurGraphNodeObject->shadowPos);
        vec3f_copy(interp->shadowPosPrev, shadowPosPrev);

        Gfx *shadowListPrev = create_shadow_below_xyz(shadowPosPrev[0], shadowPosPrev[1],
                                                      shadowPosPrev[2], shadowScale,
                                                      node->shadowSolidity, node->shadowType);

        if (gShadowInterpCurrent != NULL) {
            gShadowInterpCurrent->gfx = shadowListPrev;
        }

        if (gCurGraphNodeObject->shadowInvisible || (gCurGraphNodeObject == &gMarioState->marioObj->header.gfx && get_first_person_enabled())) {
            shadowListPrev = NULL;
        }

        if (shadowListPrev != NULL) {
            mtxf_translate(mtxf, gCurGraphNodeObject->shadowPos);
            mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, *gCurGraphNodeCamera->matrixPtr);
            mtxf_translate(mtxf, shadowPosPrev);
            mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mtxf, *gCurGraphNodeCamera->matrixPtrPrev);

            // Increment the matrix stack, If we fail to do so. Just return.
            if (!increment_mat_stack()) { return; }

            if (gShadowAboveWaterOrLava == TRUE) {
                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowListPrev), 4);
            } else if (gMarioOnIceOrCarpet == TRUE) {
                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowListPrev), 5);
            } else {
                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowListPrev), 6);
            }
            gMatStackIndex--;
        }
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
}

/**
 * Check whether an object is in view to determine whether it should be drawn.
 * This is known as frustum culling.
 * It checks whether the object is far away, very close / behind the camera,
 * or horizontally out of view. It does not check whether it is vertically
 * out of view. It assumes a sphere of 300 units around the object's position
 * unless the object has a culling radius node that specifies otherwise.
 *
 * The matrix parameter should be the top of the matrix stack, which is the
 * object's transformation matrix times the camera 'look-at' matrix. The math
 * is counter-intuitive, but it checks column 3 (translation vector) of this
 * matrix to determine where the origin (0,0,0) in object space will be once
 * transformed to camera space (x+ = right, y+ = up, z = 'coming out the screen').
 * In 3D graphics, you typically model the world as being moved in front of a
 * static camera instead of a moving camera through a static world, which in
 * this case simplifies calculations. Note that the perspective matrix is not
 * on the matrix stack, so there are still calculations with the fov to compute
 * the slope of the lines of the frustum.
 *
 *        z-
 *
 *  \     |     /
 *   \    |    /
 *    \   |   /
 *     \  |  /
 *      \ | /
 *       \|/
 *        C       x+
 *
 * Since (0,0,0) is unaffected by rotation, columns 0, 1 and 2 are ignored.
 */
static s32 obj_is_in_view(struct GraphNodeObject *node, Mat4 matrix) {
    if (!node || !gCurGraphNodeCamFrustum) { return FALSE; }

    if (node->node.flags & GRAPH_RENDER_INVISIBLE) {
        return FALSE;
    } else if (node->skipInViewCheck) {
        return TRUE;
    }

    s16 cullingRadius = 300;
    struct GraphNode *geo = node->sharedChild;
    if (geo != NULL && geo->type == GRAPH_NODE_TYPE_CULLING_RADIUS) {
        cullingRadius = (f32)((struct GraphNodeCullingRadius *) geo)->cullingRadius; //! Why is there a f32 cast?
    }

    if (vr_is_active()) {
        const f32 x = matrix[3][0];
        const f32 y = matrix[3][1];
        const f32 z = matrix[3][2];
        const f32 safeDistance = configDrawDistance == 6
            ? 60000.0f
            : 20000.0f;
        const f32 distanceSquared = x * x + y * y + z * z;
        const f32 maximumDistance = safeDistance + cullingRadius;

        if (distanceSquared >= maximumDistance * maximumDistance) {
            return FALSE;
        }
        return vr_object_is_in_view(matrix, (f32)cullingRadius);
    }
    // ! @bug The aspect ratio is not accounted for. When the fov value is 45,
    // the horizontal effective fov is actually 60 degrees, so you can see objects
    // visibly pop in or out at the edge of the screen.
    //
    // Half of the fov in in-game angle units instead of degrees.
    s16 halfFov = (gCurGraphNodeCamFrustum->fov / 2.0f + 1.0f) * 32768.0f / 180.0f + 0.5f;

    f32 divisor = coss(halfFov);
    if (divisor == 0) { divisor = 1; }
    f32 hScreenEdge = -matrix[3][2] * sins(halfFov) / divisor;
    // -matrix[3][2] is the depth, which gets multiplied by tan(halfFov) to get
    // the amount of units between the center of the screen and the horizontal edge
    // given the distance from the object to the camera.

    // This multiplication should really be performed on 4:3 as well,
    // but the issue will be more apparent on widescreen.
    hScreenEdge *= GFX_DIMENSIONS_ASPECT_RATIO;

    // Don't render if the object is close to or behind the camera
    if (matrix[3][2] > -100.0f + cullingRadius) {
        return FALSE;
    }

    //! This makes the HOLP not update when the camera is far away, and it
    //  makes PU travel safe when the camera is locked on the main map.
    //  If Mario were rendered with a depth over 65536 it would cause overflow
    //  when converting the transformation matrix to a fixed point matrix.
    if (configDrawDistance != 6 && matrix[3][2] < -20000.0f - cullingRadius) {
        return FALSE;
    }

    // Check whether the object is horizontally in view
    if (matrix[3][0] > hScreenEdge + cullingRadius) {
        return FALSE;
    }
    if (matrix[3][0] < -hScreenEdge - cullingRadius) {
        return FALSE;
    }
    return TRUE;
}

static void geo_sanitize_object_gfx(void) {
    geo_append_display_list_to_all_layers(obj_sanitize_gfx);
}

static void geo_load_object_gfx_state(void) {
    geo_append_display_list_to_all_layers(obj_load_gfx_state);
}

static void geo_save_object_gfx_state(void) {
    geo_append_display_list_to_all_layers(obj_save_gfx_state);
}

static struct MarioBodyState *get_mario_body_state_from_mario_object(struct Object *marioObj) {
    struct MarioState *m = get_mario_state_from_object(marioObj);
    return m ? m->marioBodyState : NULL;
}

/**
 * Process an object node.
 */
static void geo_process_object(struct Object *node) {
    struct Object* lastProcessingObject = gCurGraphNodeProcessingObject;
    struct MarioState* lastMarioState = gCurGraphNodeMarioState;
    gCurGraphNodeProcessingObject = node;
    Mat4 mtxf;
    s32 hasAnimation = (node->header.gfx.node.flags & GRAPH_RENDER_HAS_ANIMATION) != 0;
    Vec3f scalePrev;
    const bool localMarioInVrFirstPerson =
        vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        node == gMarioStates[0].marioObj;
    const bool hidePhysicalClimbBody =
        localMarioInVrFirstPerson &&
        vr_hand_interaction_should_hide_body(&gMarioStates[0]);
    const bool processLocalMarioVrSkeleton =
        localMarioInVrFirstPerson &&
        (((configVrFirstPersonBody || configVrFeetOnlyBody) &&
          !hidePhysicalClimbBody) ||
         configVrExperimentalTrueFirstPerson ||
         vr_first_person_true_diving_active() ||
        configVrExperimentalArmsMode ||
        (configVrImmersiveGhostPunchArm &&
         gPlayer1Controller != NULL &&
         (gPlayer1Controller->buttonDown & B_BUTTON) != 0 &&
         (gMarioStates[0].action == ACT_PUNCHING ||
          gMarioStates[0].action == ACT_MOVE_PUNCHING)));
    const bool hideLocalMarioInVrFirstPerson =
        localMarioInVrFirstPerson &&
        !processLocalMarioVrSkeleton;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) {
        LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex);
        gCurGraphNodeProcessingObject = lastProcessingObject;
        gCurGraphNodeMarioState = lastMarioState;
        return;
    }

    if (!node->header.gfx.inited) {
        node->header.gfx.inited = true;
        obj_update_gfx_pos_and_angle(node);
        vec3f_copy(node->header.gfx.prevPos, node->header.gfx.pos);
        vec3s_copy(node->header.gfx.prevAngle, node->header.gfx.angle);
    }

    if (node->hookRender) {
        smlua_call_event_hooks(HOOK_ON_OBJECT_RENDER, node);
    }

    if (node->header.gfx.node.flags & GRAPH_RENDER_PLAYER) {
        gCurGraphNodeMarioState = get_mario_state_from_object(node);
        if (gCurGraphNodeMarioState != NULL) {
            gCurGraphNodeMarioState->minimumBoneY = 9999;
        }
    }

    bool noBillboard = (node->header.gfx.sharedChild && node->header.gfx.sharedChild->extraFlags & GRAPH_EXTRA_FORCE_3D);
    if (node->header.gfx.areaIndex == gCurGraphNodeRoot->areaIndex) {
        if (node->header.gfx.throwMatrix != NULL) {

            mtxf_mul(gMatStack[gMatStackIndex + 1], *node->header.gfx.throwMatrix,
                     gMatStack[gMatStackIndex]);

            if (gGlobalTimer == node->header.gfx.prevThrowMatrixTimestamp + 1 &&
                gGlobalTimer != node->header.gfx.skipInterpolationTimestamp &&
                gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
                mtxf_copy(mtxf, node->header.gfx.prevThrowMatrix);
                mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mtxf, gMatStackPrev[gMatStackIndex]);
            } else {
                mtxf_mul(gMatStackPrev[gMatStackIndex + 1], (void *) node->header.gfx.throwMatrix, gMatStackPrev[gMatStackIndex]);
            }

            mtxf_copy(node->header.gfx.prevThrowMatrix, *node->header.gfx.throwMatrix);
            node->header.gfx.prevThrowMatrixTimestamp = gGlobalTimer;

        } else if (node->header.gfx.node.flags & GRAPH_RENDER_CYLBOARD && !noBillboard) {

            Vec3f posPrev;

            if (gGlobalTimer == node->header.gfx.prevTimestamp + 1 &&
                gGlobalTimer != node->header.gfx.skipInterpolationTimestamp &&
                gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
                vec3f_copy(posPrev, node->header.gfx.prevPos);
            } else {
                vec3f_copy(posPrev, node->header.gfx.pos);
            }

            vec3f_copy(node->header.gfx.prevPos, node->header.gfx.pos);
            node->header.gfx.prevTimestamp = gGlobalTimer;
            mtxf_cylboard(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], node->header.gfx.pos, gCurGraphNodeCamera->roll);
            mtxf_cylboard(gMatStackPrev[gMatStackIndex + 1], gMatStackPrev[gMatStackIndex], posPrev, gCurGraphNodeCamera->roll);

        } else if (node->header.gfx.node.flags & GRAPH_RENDER_BILLBOARD && !noBillboard) {

            Vec3f posPrev;

            if (gGlobalTimer == node->header.gfx.prevTimestamp + 1 &&
                gGlobalTimer != node->header.gfx.skipInterpolationTimestamp &&
                gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
                vec3f_copy(posPrev, node->header.gfx.prevPos);
            } else {
                vec3f_copy(posPrev, node->header.gfx.pos);
            }

            vec3f_copy(node->header.gfx.prevPos, node->header.gfx.pos);
            node->header.gfx.prevTimestamp = gGlobalTimer;
            mtxf_billboard(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], node->header.gfx.pos, gCurGraphNodeCamera->roll);
            mtxf_billboard(gMatStackPrev[gMatStackIndex + 1], gMatStackPrev[gMatStackIndex], posPrev, gCurGraphNodeCamera->roll);

        } else {

            Vec3f posPrev;
            Vec3s anglePrev;

            if (gGlobalTimer == node->header.gfx.prevTimestamp + 1 &&
                gGlobalTimer != node->header.gfx.skipInterpolationTimestamp &&
                gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
                vec3f_copy(posPrev, node->header.gfx.prevPos);
                vec3s_copy(anglePrev, node->header.gfx.prevAngle);
            } else {
                vec3f_copy(posPrev, node->header.gfx.pos);
                vec3s_copy(anglePrev, node->header.gfx.angle);
            }

            vec3f_copy(node->header.gfx.prevPos, node->header.gfx.pos);
            vec3s_copy(node->header.gfx.prevAngle, node->header.gfx.angle);
            node->header.gfx.prevTimestamp = gGlobalTimer;
            Vec3f renderPosition;
            Vec3f renderPositionPrev;
            vec3f_copy(renderPosition, node->header.gfx.pos);
            vec3f_copy(renderPositionPrev, posPrev);
            if (processLocalMarioVrSkeleton) {
                Vec3f headsetPosition;
                Vec3f headsetPositionPrev;
                Vec3f liveHeadsetPosition;
                if (vr_calculate_stabilized_headset_world_position(
                        liveHeadsetPosition
                    ) &&
                    vr_get_stabilized_headset_world_position(
                        headsetPosition,
                        false
                    ) &&
                    vr_get_stabilized_headset_world_position(
                        headsetPositionPrev,
                        true
                    )) {
                    // Preserve normal gameplay interpolation, then add the
                    // same live HMD correction to both endpoints. This keeps
                    // stick-driven movement smooth while physical walking
                    // follows the render-rate headset without a 30 Hz trail.
                    f32 liveOffsetX =
                        liveHeadsetPosition[0] - headsetPosition[0];
                    f32 liveOffsetZ =
                        liveHeadsetPosition[2] - headsetPosition[2];
                    if (fabsf(liveOffsetX) < 0.75f) liveOffsetX = 0.0f;
                    if (fabsf(liveOffsetZ) < 0.75f) liveOffsetZ = 0.0f;
                    renderPosition[0] =
                        headsetPosition[0] + liveOffsetX;
                    renderPosition[2] =
                        headsetPosition[2] + liveOffsetZ;
                    renderPositionPrev[0] =
                        headsetPositionPrev[0] + liveOffsetX;
                    renderPositionPrev[2] =
                        headsetPositionPrev[2] + liveOffsetZ;
                }
            }
            Vec3s renderAngle;
            Vec3s renderAnglePrev;
            if (processLocalMarioVrSkeleton) {
                const bool useAnimatedRootTilt =
                    configVrExperimentalTrueFirstPerson ||
                    vr_first_person_true_diving_active();
                // The first-person body skeleton follows the player's view
                // heading independently from Mario's momentum-facing yaw.
                // True First Person retains animation pitch/roll globally;
                // True Diving retains it only while a dive action is active.
                vec3s_set(
                    renderAngle,
                    useAnimatedRootTilt
                        ? node->header.gfx.angle[0]
                        : 0,
                    vr_get_stabilized_body_yaw(false),
                    useAnimatedRootTilt
                        ? node->header.gfx.angle[2]
                        : 0
                );
                vec3s_set(
                    renderAnglePrev,
                    useAnimatedRootTilt
                        ? anglePrev[0]
                        : 0,
                    vr_get_stabilized_body_yaw(true),
                    useAnimatedRootTilt
                        ? anglePrev[2]
                        : 0
                );
            } else {
                vec3s_copy(renderAngle, node->header.gfx.angle);
                vec3s_copy(renderAnglePrev, anglePrev);
            }
            mtxf_rotate_zxy_and_translate(mtxf, renderPosition, renderAngle);
            mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
            mtxf_rotate_zxy_and_translate(mtxf, renderPositionPrev, renderAnglePrev);
            mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mtxf, gMatStackPrev[gMatStackIndex]);
        }
        if (gGlobalTimer == node->header.gfx.prevScaleTimestamp + 1 &&
            gGlobalTimer != node->header.gfx.skipInterpolationTimestamp &&
            gGlobalTimer != gLakituState.skipCameraInterpolationTimestamp) {
            vec3f_copy(scalePrev, node->header.gfx.prevScale);
        } else {
            vec3f_copy(scalePrev, node->header.gfx.scale);
        }

        vec3f_copy(node->header.gfx.prevScale, node->header.gfx.scale);
        node->header.gfx.prevScaleTimestamp = gGlobalTimer;

        mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1],
                         node->header.gfx.scale);
        mtxf_scale_vec3f(gMatStackPrev[gMatStackIndex + 1], gMatStackPrev[gMatStackIndex + 1],
                         scalePrev);
        node->header.gfx.throwMatrix = &gMatStack[++gMatStackIndex];
        node->header.gfx.throwMatrixPrev = &gMatStackPrev[gMatStackIndex];
        node->header.gfx.cameraToObject[0] = gMatStack[gMatStackIndex][3][0];
        node->header.gfx.cameraToObject[1] = gMatStack[gMatStackIndex][3][1];
        node->header.gfx.cameraToObject[2] = gMatStack[gMatStackIndex][3][2];
        vr_get_head_tracked_audio_vector(
            node->header.gfx.pos,
            node->header.gfx.cameraToObject
        );
        if (sVrHeadTrackedAudioEnabledForFrame &&
            gMarioStates[0].marioObj != NULL &&
            node == gMarioStates[0].marioObj) {
            // Local footsteps, voice, landing, and movement sounds all use
            // Mario's cameraToObject source. Room-scale body correction can
            // place that root slightly left/right of the headset even though
            // the sound belongs to the player. Keep its distance/elevation,
            // but center only the local player's stereo pan. Other actors and
            // environmental sources retain full head-relative 3D placement.
            node->header.gfx.cameraToObject[0] = 0.0f;
        }

        // FIXME: correct types
        if (node->header.gfx.animInfo.curAnim != NULL) {
            dynos_gfx_swap_animations(node);
            geo_set_animation_globals(&node->header.gfx.animInfo, hasAnimation);
            if (node->hookRender) smlua_call_event_hooks(HOOK_ON_OBJECT_ANIM_UPDATE, node);
            dynos_gfx_swap_animations(node);
        }
        if (processLocalMarioVrSkeleton ||
            obj_is_in_view(
                &node->header.gfx,
                gMatStack[gMatStackIndex]
            )) {
            Mtx *mtx = alloc_display_list(sizeof(*mtx));
            Mtx *mtxPrev = alloc_display_list(sizeof(*mtxPrev));
            if (mtx == NULL || mtxPrev == NULL) {
                gMatStackIndex--;
                gCurAnimType = ANIM_TYPE_NONE;
                node->header.gfx.throwMatrix = NULL;
                node->header.gfx.throwMatrixPrev = NULL;
                gCurGraphNodeProcessingObject = lastProcessingObject;
                gCurGraphNodeMarioState = lastMarioState;
                return;
            }

            mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
            gMatStackFixed[gMatStackIndex] = mtx;
            mtxf_to_mtx(mtxPrev, gMatStackPrev[gMatStackIndex]);
            gMatStackPrevFixed[gMatStackIndex] = mtxPrev;

            if (node->header.gfx.sharedChild != NULL &&
                !hideLocalMarioInVrFirstPerson) {
                gCurMarioBodyState = get_mario_body_state_from_mario_object(node);
                if (gCurMarioBodyState) {
                    gCurMarioBodyState->currAnimPart = MARIO_ANIM_PART_NONE;
                }
                gCurGraphNodeObject = (struct GraphNodeObject *) node;
                sVrFilteringLocalMarioBody =
                    processLocalMarioVrSkeleton;
                node->header.gfx.sharedChild->parent = &node->header.gfx.node;
                geo_sanitize_object_gfx();
                geo_process_node_and_siblings(node->header.gfx.sharedChild);
                node->header.gfx.sharedChild->parent = NULL;
                sVrFilteringLocalMarioBody = false;
                gCurGraphNodeObject = NULL;
                gCurMarioBodyState = NULL;
            }

            if (node->header.gfx.node.children != NULL) {
                geo_process_node_and_siblings(node->header.gfx.node.children);
            }

        } else {
            node->header.gfx.prevThrowMatrixTimestamp = 0;
            node->header.gfx.prevTimestamp = 0;
            node->header.gfx.prevScaleTimestamp = 0;
        }

        gMatStackIndex--;
        gCurAnimType = ANIM_TYPE_NONE;
        node->header.gfx.throwMatrix = NULL;
        node->header.gfx.throwMatrixPrev = NULL;
    }
    gCurGraphNodeProcessingObject = lastProcessingObject;
    gCurGraphNodeMarioState = lastMarioState;
}

/**
 * Process an object parent node. Temporarily assigns itself as the parent of
 * the subtree rooted at 'sharedChild' and processes the subtree, after which the
 * actual children are be processed. (in practice they are null though)
 */
static void geo_process_object_parent(struct GraphNodeObjectParent *node) {
    if (node->sharedChild != NULL) {
        node->sharedChild->parent = (struct GraphNode *) node;
        geo_process_node_and_siblings(node->sharedChild);
        node->sharedChild->parent = NULL;
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
}

/**
 * Process a held object node.
 */
void geo_process_held_object(struct GraphNodeHeldObject *node) {
    Mat4 mat;
    Vec3f translation;
    Vec3f scalePrev;
    Vec3s anglePrev;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

#ifdef F3DEX_GBI_2
    gSPLookAt(gDisplayListHead++, &lookAt);
#endif

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    const bool trackedVrHeldObject =
        vr_hand_interaction_is_tracked_held_object(
            node->objNode
        );
    if (!trackedVrHeldObject &&
        node->objNode != NULL &&
        node->objNode->header.gfx.sharedChild != NULL) {
        s32 hasAnimation = (node->objNode->header.gfx.node.flags & GRAPH_RENDER_HAS_ANIMATION) != 0;

        translation[0] = node->translation[0] / 4.0f;
        translation[1] = node->translation[1] / 4.0f;
        translation[2] = node->translation[2] / 4.0f;

        if (gGlobalTimer == node->objNode->header.gfx.prevScaleTimestamp + 1) {
            vec3f_copy(scalePrev, node->objNode->header.gfx.prevScale);
            vec3s_copy(anglePrev, node->objNode->header.gfx.prevAngle);
        } else {
            vec3f_copy(scalePrev, node->objNode->header.gfx.scale);
            vec3s_copy(anglePrev, node->objNode->header.gfx.angle);
        }
        vec3f_copy(node->objNode->header.gfx.prevScale, node->objNode->header.gfx.scale);
        node->objNode->header.gfx.prevScaleTimestamp = gGlobalTimer;

        if (node->objNode->header.gfx.sharedChild->extraFlags & GRAPH_EXTRA_ROTATE_HELD) {
            vec3s_copy(node->objNode->header.gfx.prevAngle, node->objNode->header.gfx.angle);
            mtxf_rotate_zxy_and_translate(mat, translation, node->objNode->header.gfx.angle);
        } else {
            mtxf_translate(mat, translation);
        }
        mtxf_copy(gMatStack[gMatStackIndex + 1], *gCurGraphNodeObject->throwMatrix);
        gMatStack[gMatStackIndex + 1][3][0] = gMatStack[gMatStackIndex][3][0];
        gMatStack[gMatStackIndex + 1][3][1] = gMatStack[gMatStackIndex][3][1];
        gMatStack[gMatStackIndex + 1][3][2] = gMatStack[gMatStackIndex][3][2];
        mtxf_mul(gMatStack[gMatStackIndex + 1], mat, gMatStack[gMatStackIndex + 1]);
        mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1], node->objNode->header.gfx.scale);
        if (node->objNode->header.gfx.sharedChild->extraFlags & GRAPH_EXTRA_ROTATE_HELD) {
            mtxf_rotate_zxy_and_translate(mat, translation, anglePrev);
        }
        mtxf_copy(gMatStackPrev[gMatStackIndex + 1], (void *) gCurGraphNodeObject->throwMatrixPrev);
        gMatStackPrev[gMatStackIndex + 1][3][0] = gMatStackPrev[gMatStackIndex][3][0];
        gMatStackPrev[gMatStackIndex + 1][3][1] = gMatStackPrev[gMatStackIndex][3][1];
        gMatStackPrev[gMatStackIndex + 1][3][2] = gMatStackPrev[gMatStackIndex][3][2];
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], mat, gMatStackPrev[gMatStackIndex + 1]);
        mtxf_scale_vec3f(gMatStackPrev[gMatStackIndex + 1], gMatStackPrev[gMatStackIndex + 1],
                         scalePrev);

        if (node->fnNode.func != NULL) {
            node->fnNode.func(GEO_CONTEXT_HELD_OBJ, &node->fnNode.node, (struct DynamicPool *) gMatStack[gMatStackIndex + 1]);
        }

        s32 savedMatStackIndex = gMatStackIndex;
        // Increment the matrix stack, If we fail to do so. Just return.
        if (!increment_mat_stack()) { return; }

        gGeoTempState.type = gCurAnimType;
        gGeoTempState.enabled = gCurAnimEnabled;
        gGeoTempState.frame = gCurrAnimFrame;
        gGeoTempState.translationMultiplier = gCurAnimTranslationMultiplier;
        gGeoTempState.attribute = gCurrAnimAttribute;
        gGeoTempState.anim = gCurAnim;
        gGeoTempState.prevFrame = gPrevAnimFrame;
        gCurAnimType = 0;
        gCurGraphNodeHeldObject = (void *) node;
        if (node->objNode->header.gfx.animInfo.curAnim != NULL) {
            dynos_gfx_swap_animations(node->objNode);
            geo_set_animation_globals(&node->objNode->header.gfx.animInfo, hasAnimation);
            if (node->objNode->hookRender) smlua_call_event_hooks(HOOK_ON_OBJECT_ANIM_UPDATE, node->objNode);
            dynos_gfx_swap_animations(node->objNode);
        }

        // The held object is going to change the gfx state before
        // the holder finishes rendering, so let's save the state now
        geo_save_object_gfx_state();

        geo_sanitize_object_gfx();
        // While rendering the held object's geo tree, ensure "current object" globals
        // refer to the held object, otherwise Lua geo callbacks can accidentally
        // mutate the holder's render state (e.g. make Wario limbs disappear).
        struct GraphNodeObject *savedCurGraphNodeObject = gCurGraphNodeObject;
        gCurGraphNodeObject = &node->objNode->header.gfx;
        geo_process_node_and_siblings(node->objNode->header.gfx.sharedChild);
        gCurGraphNodeObject = savedCurGraphNodeObject;
        gCurGraphNodeHeldObject = NULL;
        gCurAnimType = gGeoTempState.type;
        gCurAnimEnabled = gGeoTempState.enabled;
        gCurrAnimFrame = gGeoTempState.frame;
        gCurAnimTranslationMultiplier = gGeoTempState.translationMultiplier;
        gCurrAnimAttribute = gGeoTempState.attribute;
        gCurAnim = gGeoTempState.anim;
        gPrevAnimFrame = gGeoTempState.prevFrame;

        // Force-restore matrix stack index to avoid any imbalance caused by
        // held object geo trees (including Lua geo callbacks).
        gMatStackIndex = savedMatStackIndex;

        // Restore the previously saved state before continuing
        geo_load_object_gfx_state();
    }

    if (node->fnNode.node.children != NULL) {
        geo_process_node_and_siblings(node->fnNode.node.children);
    }
}

/**
 * Render an animated part with initial rotation and scale values.
 */
static void geo_process_bone(struct GraphNodeBone *node) {
    if (gCurMarioBodyState && !gCurGraphNodeHeldObject) {
        gCurMarioBodyState->currAnimPart++;
    }

    Mat4 matrix;
    Vec3s rotation;
    Vec3f translation;
    Vec3f scale;

    // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
    if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) { LOG_ERROR("Preventing attempt to exceed the maximum size %i for our matrix stack with size of %i.", MATRIX_STACK_SIZE - 1, gMatStackIndex); return; }

    u16 *animAttribute = gCurrAnimAttribute;
    u8 animType = gCurAnimType;

    // current frame
    vec3s_copy(rotation, node->rotation);
    vec3s_to_vec3f(translation, node->translation);
    vec3f_copy(scale, node->scale);
    anim_process(translation, rotation, scale, &gCurAnimType, gCurrAnimFrame, &gCurrAnimAttribute);
    mtxf_rotate_xyz_and_translate(matrix, translation, rotation);
    mtxf_scale_vec3f(matrix, matrix, scale);
    mtxf_mul(gMatStack[gMatStackIndex + 1], matrix, gMatStack[gMatStackIndex]);
    vr_adjust_local_mario_body_transform(
        gMatStack[gMatStackIndex + 1],
        false
    );

    // previous frame
    geo_update_interpolation(node->translation, node->rotation, node->scale,
        if (geo_should_interpolate(interp)) {
            vec3s_copy(rotation, interp->rotation);
            vec3s_to_vec3f(translation, interp->translation);
            vec3f_copy(scale, interp->scale);
        } else {
            vec3s_copy(rotation, node->rotation);
            vec3s_to_vec3f(translation, node->translation);
            vec3f_copy(scale, node->scale);
        }
        anim_process(translation, rotation, scale, &animType, gPrevAnimFrame, &animAttribute);
        mtxf_rotate_xyz_and_translate(matrix, translation, rotation);
        mtxf_scale_vec3f(matrix, matrix, scale);
        mtxf_mul(gMatStackPrev[gMatStackIndex + 1], matrix, gMatStackPrev[gMatStackIndex]);
        vr_adjust_local_mario_body_transform(
            gMatStackPrev[gMatStackIndex + 1],
            true
        );
    );

    // Increment the matrix stack, If we fail to do so. Just return.
    if (!increment_mat_stack()) { return; }

    // Mario anim part pos and rot
    if (gCurMarioBodyState && !gCurGraphNodeHeldObject && gCurMarioBodyState->currAnimPart > MARIO_ANIM_PART_NONE && gCurMarioBodyState->currAnimPart < MARIO_ANIM_PART_MAX) {
        get_pos_from_transform_mtx(
            gCurMarioBodyState->animPartsPos[gCurMarioBodyState->currAnimPart],
            gMatStack[gMatStackIndex],
            *gCurGraphNodeCamera->matrixPtr
        );

        Vec3s rot = { rotation[2], rotation[0], rotation[1] };
        vec3s_copy(gCurMarioBodyState->animPartsRot[gCurMarioBodyState->currAnimPart], rot);
    }

    if (gCurGraphNodeMarioState != NULL) {
        Vec3f translated = { 0 };
        get_pos_from_transform_mtx(translated, gMatStack[gMatStackIndex], *gCurGraphNodeCamera->matrixPtr);
        gCurGraphNodeMarioState->minimumBoneY = fmin(gCurGraphNodeMarioState->minimumBoneY, translated[1] - gCurGraphNodeMarioState->marioObj->header.gfx.pos[1]);
    }
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    if (node->node.children != NULL) {
        geo_process_node_and_siblings(node->node.children);
    }
    gMatStackIndex--;
}

/**
 * Processes the children of the given GraphNode if it has any
 */
void geo_try_process_children(struct GraphNode *node) {
    if (node->children != NULL) {
        geo_process_node_and_siblings(node->children);
    }
}

#define MAX_GRAPH_NODE_DEPTH 5000
/**
 * Process a generic geo node and its siblings.
 * The first argument is the start node, and all its siblings will
 * be iterated over.
 */
void geo_process_node_and_siblings(struct GraphNode *firstNode) {
    s16 iterateChildren = TRUE;
    struct GraphNode *curGraphNode = firstNode;
    if (curGraphNode == NULL) { return; }
    u32 depthSanity = 0;

    struct GraphNode *parent = curGraphNode->parent;

    // In the case of a switch node, exactly one of the children of the node is
    // processed instead of all children like usual
    if (parent != NULL) {
        iterateChildren = (parent->type != GRAPH_NODE_TYPE_SWITCH_CASE);

        if (parent->hookProcess) smlua_call_event_hooks(HOOK_ON_GEO_PROCESS_CHILDREN, parent, gMatStackIndex);
    }

    do {
        if (curGraphNode == NULL) {
            LOG_ERROR("Graph Node null!");
            break;
        }

#ifdef DEBUG
        if (curGraphNode->_guard1 != GRAPH_NODE_GUARD || curGraphNode->_guard2 != GRAPH_NODE_GUARD) {
            LOG_ERROR("Graph Node corrupted!");
            break;
        }
#endif

        // Sanity check our stack index, If we above or equal to our stack size. Return to prevent OOB\.
        if ((gMatStackIndex + 1) >= MATRIX_STACK_SIZE) {
            LOG_ERROR("Graph Node matrix stack overflow!");
            break;
        }

        // Break out of endless loops
        if (++depthSanity > MAX_GRAPH_NODE_DEPTH) {
            LOG_ERROR("Graph Node too deep!");
            break;
        }

        if (curGraphNode->flags & GRAPH_RENDER_ACTIVE) {
            if (curGraphNode->hookProcess) smlua_call_event_hooks(HOOK_BEFORE_GEO_PROCESS, curGraphNode, gMatStackIndex);
            if (curGraphNode->flags & GRAPH_RENDER_CHILDREN_FIRST) {
                geo_try_process_children(curGraphNode);
            } else {
                switch (curGraphNode->type) {
                    case GRAPH_NODE_TYPE_ORTHO_PROJECTION:
                        geo_process_ortho_projection((struct GraphNodeOrthoProjection *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_PERSPECTIVE:
                        geo_process_perspective((struct GraphNodePerspective *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_MASTER_LIST:
                        geo_process_master_list((struct GraphNodeMasterList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_LEVEL_OF_DETAIL:
                        geo_process_level_of_detail((struct GraphNodeLevelOfDetail *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SWITCH_CASE:
                        geo_process_switch((struct GraphNodeSwitchCase *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_CAMERA:
                        geo_process_camera((struct GraphNodeCamera *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION_ROTATION:
                        geo_process_translation_rotation(
                            (struct GraphNodeTranslationRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION:
                        geo_process_translation((struct GraphNodeTranslation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ROTATION:
                        geo_process_rotation((struct GraphNodeRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT:
                        geo_process_object((struct Object *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ANIMATED_PART:
                        geo_process_animated_part((struct GraphNodeAnimatedPart *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BILLBOARD:
                        geo_process_billboard((struct GraphNodeBillboard *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_DISPLAY_LIST:
                        geo_process_display_list((struct GraphNodeDisplayList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SCALE:
                        geo_process_scale((struct GraphNodeScale *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SCALE_XYZ:
                        geo_process_scale_xyz((struct GraphNodeScaleXYZ *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SHADOW:
                        geo_process_shadow((struct GraphNodeShadow *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT_PARENT:
                        geo_process_object_parent((struct GraphNodeObjectParent *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_GENERATED_LIST:
                        geo_process_generated_list((struct GraphNodeGenerated *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BACKGROUND:
                        geo_process_background((struct GraphNodeBackground *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_HELD_OBJ:
                        geo_process_held_object((struct GraphNodeHeldObject *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BONE:
                        geo_process_bone((struct GraphNodeBone *) curGraphNode);
                        break;
                    default:
                        geo_try_process_children((struct GraphNode *) curGraphNode);
                        break;
                }
            }
            if (curGraphNode->hookProcess) smlua_call_event_hooks(HOOK_ON_GEO_PROCESS, curGraphNode, gMatStackIndex + 1);
        } else {
            if (curGraphNode && curGraphNode->type == GRAPH_NODE_TYPE_OBJECT) {
                ((struct GraphNodeObject *) curGraphNode)->throwMatrix = NULL;
            }
        }
    } while (iterateChildren && curGraphNode && (curGraphNode = curGraphNode->next) != firstNode);
}

static void geo_clear_interp_variables(void) {
    sPerspectiveNode = NULL;
    sPerspectiveMtx   = NULL;
    sPerspectiveAspect = 0;

    sViewport        = NULL;
    sViewportPos     = NULL;
    sViewportClipPos = NULL;

    sBackgroundNode = NULL;
    gBackgroundSkyboxGfx = NULL;
    gBackgroundSkyboxMtx = NULL;
    sBackgroundNodeRoot = NULL;

    sShadowInterp->count = 0;
    gShadowInterpCurrent = NULL;

    sMtxTbl->count = 0;
    sVrBillboardHead = NULL;
    sVrHeldMatrixHead = NULL;
    sUsingBillboard = FALSE;
    sCameraNode = NULL;
    sVrControllerHandMasterList = NULL;
    memset(
        sVrControllerHandMatrices,
        0,
        sizeof(sVrControllerHandMatrices)
    );
    gCurGraphNodeProcessingObject = NULL;
    gCurGraphNodeMarioState = NULL;
}

/**
 * Process a root node. This is the entry point for processing the scene graph.
 * The root node itself sets up the viewport, then all its children are processed
 * to set up the projection and draw display lists.
 */
void geo_process_root(struct GraphNodeRoot *node, Vp *b, Vp *c, s32 clearColor) {
    // clear interp stuff
    geo_clear_interp_variables();

    if (node->node.flags & GRAPH_RENDER_ACTIVE) {
        gDisplayListHeap = growing_pool_init(gDisplayListHeap, DISPLAY_LIST_HEAP_SIZE);

        Vp *viewport = alloc_display_list(sizeof(*viewport));
        if (viewport == NULL) { return; }

        Mtx *initialMatrix = alloc_display_list(sizeof(*initialMatrix));
        if (initialMatrix == NULL) { return; }

        gMatStackIndex = 0;
        gCurAnimType = 0;
        vec3s_set(viewport->vp.vtrans, node->x * 4, node->y * 4, 511);
        vec3s_set(viewport->vp.vscale, node->width * 4, node->height * 4, 511);

        if (b != NULL) {
            clear_frame_buffer(clearColor);

            sViewportClipPos = gDisplayListHead;
            make_viewport_clip_rect(&sViewportPrev);

            *viewport = *b;
        } else if (c != NULL) {
            clear_frame_buffer(clearColor);
            make_viewport_clip_rect(c);
        }

        mtxf_identity(gMatStack[gMatStackIndex]);
        mtxf_to_mtx(initialMatrix, gMatStack[gMatStackIndex]);
        gMatStackFixed[gMatStackIndex] = initialMatrix;

        sViewport = viewport;
        sViewportPos = gDisplayListHead;

        // vvv 60 FPS PATCH vvv
        mtxf_identity(gMatStackPrev[gMatStackIndex]);
        gMatStackPrevFixed[gMatStackIndex] = initialMatrix;
        // ^^^              ^^^

        gSPViewport(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(&sViewportPrev));
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(gMatStackFixed[gMatStackIndex]), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

        gCurGraphNodeRoot = node;
        if (node->node.children != NULL) {
            geo_process_node_and_siblings(node->node.children);
        }

        gCurGraphNodeRoot = NULL;
    }
}
