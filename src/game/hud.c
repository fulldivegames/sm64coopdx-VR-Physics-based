#include <stdbool.h>
#include <PR/ultratypes.h>

#include "sm64.h"
#include "actors/common1.h"
#include "gfx_dimensions.h"
#include "game_init.h"
#include "level_update.h"
#include "camera.h"
#include "print.h"
#include "rendering_graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_load.h"
#include "ingame_menu.h"
#include "hud.h"
#include "segment2.h"
#include "area.h"
#include "save_file.h"
#include "print.h"
#include "hardcoded.h"
#include "bettercamera.h"
#include "mario_actions_automatic.h"
#include "pc/configfile.h"
#include "pc/djui/djui_fps_display.h"
#include "pc/vr/vr.h"
#include "pc/network/network.h"
#include "pc/utils/misc.h"
#include "pc/lua/smlua.h"
#include "pc/lua/utils/smlua_obj_utils.h"
#include "data/dynos_mgr_builtin_externs.h"

extern bool gDjuiInMainMenu;
u8 gOverrideHideHud;

/* @file hud.c
 * This file implements HUD rendering and power meter animations.
 * That includes stars, lives, coins, camera status, power meter, timer
 * cannon reticle, and the unused keys.
 **/

struct PowerMeterHUD {
    s8 animation;
    s16 x;
    s16 y;
    f32 unused;
};

struct UnusedHUDStruct {
    u32 unused1;
    u16 unused2;
    u16 unused3;
};

struct CameraHUD {
    s16 status;
};

// Stores health segmented value defined by numHealthWedges
// When the HUD is rendered this value is 8, full health.
static s16 sPowerMeterStoredHealth;

static struct PowerMeterHUD sPowerMeterHUD = {
    POWER_METER_HIDDEN,
    140,
    166,
    1.0,
};

// Power Meter timer that keeps counting when it's visible.
// Gets reset when the health is filled and stops counting
// when the power meter is hidden.
s32 sPowerMeterVisibleTimer = 0;

UNUSED static struct UnusedHUDStruct sUnusedHUDValues = { 0x00, 0x0A, 0x00 };

static struct CameraHUD sCameraHUD = { CAM_STATUS_NONE };

static u32 sPowerMeterPrevTimestamp;
static f32 sPowerMeterPrevY;
static Gfx *sPowerMeterDisplayListPos = NULL;
static Mtx *sPowerMeterMtx = NULL;

u8 get_hud_opacity_alpha(u8 alpha) {
    if (!vr_is_active()) {
        return alpha;
    }

    const unsigned int opacity =
        configVrHudOpacity > 100 ? 100 : configVrHudOpacity;
    return (u8)((alpha * opacity + 50) / 100);
}

static s32 vr_hud_spread_x(s32 x) {
    if (!vr_is_active()) return x;
    const f32 factor = (f32)clamp(configVrHudSpread, 80U, 200U) / 100.0f;
    return (s32)roundf(
        (f32)SCREEN_WIDTH * 0.5f +
        ((f32)x - (f32)SCREEN_WIDTH * 0.5f) * factor
    );
}

static s32 vr_hud_spread_y(s32 y) {
    if (!vr_is_active()) return y;
    const f32 factor = (f32)clamp(configVrHudSpread, 80U, 200U) / 100.0f;
    return (s32)roundf(
        (f32)SCREEN_HEIGHT * 0.5f +
        ((f32)y - (f32)SCREEN_HEIGHT * 0.5f) * factor
    );
}

// Move a complete readout by the offset of one anchor point. Applying the
// scale to every glyph separately pulls "icon x count" readouts apart.
static s32 vr_hud_group_x(s32 x, s32 anchorX) {
    return x + vr_hud_spread_x(anchorX) - anchorX;
}

static s32 vr_hud_group_y(s32 y, s32 anchorY) {
    return y + vr_hud_spread_y(anchorY) - anchorY;
}

static void get_vr_hud_ortho_bounds(
    f32* left,
    f32* right,
    f32* bottom,
    f32* top
) {
    const f32 halfWidth = (f32)SCREEN_WIDTH * 0.5f;
    const f32 halfHeight = (f32)SCREEN_HEIGHT * 0.5f;
    *left = (f32)SCREEN_WIDTH * 0.5f - halfWidth;
    *right = (f32)SCREEN_WIDTH * 0.5f + halfWidth;
    *bottom = (f32)SCREEN_HEIGHT * 0.5f - halfHeight;
    *top = (f32)SCREEN_HEIGHT * 0.5f + halfHeight;
}

void patch_hud_before(void) {
    if (sPowerMeterDisplayListPos != NULL) {
        sPowerMeterPrevY = sPowerMeterHUD.y;
        sPowerMeterPrevTimestamp = gGlobalTimer;
        sPowerMeterDisplayListPos = NULL;
        sPowerMeterMtx = NULL;
    }
}

void patch_hud_interpolated(f32 delta) {
    if (sPowerMeterDisplayListPos && sPowerMeterMtx) {
        Mtx* mtx = sPowerMeterMtx;
        f32 interpY = delta_interpolate_f32(sPowerMeterPrevY, (f32)sPowerMeterHUD.y, delta);
        guTranslate(mtx,
                    (f32)vr_hud_group_x(sPowerMeterHUD.x, 140),
                    (f32)vr_hud_group_y((s32)roundf(interpY), 166), 0);
        gSPMatrix(sPowerMeterDisplayListPos, VIRTUAL_TO_PHYSICAL(mtx),
              G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    }
}

/**
 * Renders a rgba16 16x16 glyph texture from a table list.
 */
void render_hud_tex_lut(s32 x, s32 y, u8 *texture) {
    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gSPDisplayList(gDisplayListHead++, &dl_hud_img_load_tex_block);
    render_screen_texture_rectangle(x, y, 16, 16, 16, 16);
}

/**
 * Renders a rgba16 8x8 glyph texture from a table list.
 */
void render_hud_small_tex_lut(s32 x, s32 y, u8 *texture) {
    gDPSetTile(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
                G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD);
    gDPTileSync(gDisplayListHead++);
    gDPSetTile(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 0, G_TX_RENDERTILE, 0,
                G_TX_CLAMP, 3, G_TX_NOLOD, G_TX_CLAMP, 3, G_TX_NOLOD);
    gDPSetTileSize(gDisplayListHead++, G_TX_RENDERTILE, 0, 0, (8 - 1) << G_TEXTURE_IMAGE_FRAC, (8 - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gDPLoadSync(gDisplayListHead++);
    gDPLoadBlock(gDisplayListHead++, G_TX_LOADTILE, 0, 0, 8 * 8 - 1, CALC_DXT(8, G_IM_SIZ_16b_BYTES));
    render_screen_texture_rectangle(x, y, 8, 8, 8, 8);
}

/**
 * Renders power meter health segment texture using a table list.
 */
void render_power_meter_health_segment(s16 numHealthWedges) {
    u8 *(*healthLUT)[];

    healthLUT = segmented_to_virtual(&power_meter_health_segments_lut);

    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1,
                       (*healthLUT)[numHealthWedges - 1]);
    gDPLoadSync(gDisplayListHead++);
    gDPLoadBlock(gDisplayListHead++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
    gSP1Triangle(gDisplayListHead++, 0, 1, 2, 0);
    gSP1Triangle(gDisplayListHead++, 0, 2, 3, 0);
}

/**
 * Renders power meter display lists.
 * That includes the "POWER" base and the colored health segment textures.
 */
void render_dl_power_meter(s16 numHealthWedges) {
    Mtx *mtx;

    mtx = alloc_display_list(sizeof(Mtx));

    if (mtx == NULL) {
        return;
    }

    guTranslate(mtx,
                (f32)vr_hud_group_x(sPowerMeterHUD.x, 140),
                (f32)vr_hud_group_y((s32)roundf(sPowerMeterPrevY), 166), 0);
    sPowerMeterMtx = mtx;
    sPowerMeterDisplayListPos = gDisplayListHead;

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx++),
              G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));
    gSPDisplayList(gDisplayListHead++, &dl_power_meter_base);

    if (numHealthWedges != 0) {
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_begin);
        render_power_meter_health_segment(numHealthWedges);
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_end);
    }

    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

/**
 * Power meter animation called when there's less than 8 health segments
 * Checks its timer to later change into deemphasizing mode.
 */
void animate_power_meter_emphasized(void) {
    s16 hudDisplayFlags;
    hudDisplayFlags = gHudDisplay.flags;

    if (!(hudDisplayFlags & HUD_DISPLAY_FLAG_EMPHASIZE_POWER)) {
        if (sPowerMeterVisibleTimer == 45.0) {
            sPowerMeterHUD.animation = POWER_METER_DEEMPHASIZING;
        }
    } else {
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Power meter animation called after emphasized mode.
 * Moves power meter y pos speed until it's at 200 to be visible.
 */
static void animate_power_meter_deemphasizing(void) {
    s16 speed = 5;

    if (sPowerMeterHUD.y >= 181) {
        speed = 3;
    }

    if (sPowerMeterHUD.y >= 191) {
        speed = 2;
    }

    if (sPowerMeterHUD.y >= 196) {
        speed = 1;
    }

    sPowerMeterHUD.y += speed;

    if (sPowerMeterHUD.y >= 201) {
        sPowerMeterHUD.y = 200;
        sPowerMeterPrevY = 200;
        sPowerMeterHUD.animation = POWER_METER_VISIBLE;
    }
}

/**
 * Power meter animation called when there's 8 health segments.
 * Moves power meter y pos quickly until it's at 301 to be hidden.
 */
static void animate_power_meter_hiding(void) {
    sPowerMeterHUD.y += 20;
    if (sPowerMeterHUD.y >= 301) {
        sPowerMeterHUD.animation = POWER_METER_HIDDEN;
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Handles power meter actions depending of the health segments values.
 */
void handle_power_meter_actions(s16 numHealthWedges) {
    // Show power meter if health is not full, less than 8
    if (numHealthWedges < 8 && sPowerMeterStoredHealth == 8 && sPowerMeterHUD.animation == POWER_METER_HIDDEN) {
        sPowerMeterHUD.animation = POWER_METER_EMPHASIZED;
        sPowerMeterHUD.y = 166;
        sPowerMeterPrevY = 166;
    }

    // Show power meter if health is full, has 8
    if (numHealthWedges == 8 && sPowerMeterStoredHealth == 7) {
        sPowerMeterVisibleTimer = 0;
    }

    // After health is full, hide power meter
    if (numHealthWedges == 8 && sPowerMeterVisibleTimer > 45.0) {
        sPowerMeterHUD.animation = POWER_METER_HIDING;
    }

    // Update to match health value
    sPowerMeterStoredHealth = numHealthWedges;

    // If Mario is swimming, keep power meter visible
    if (gPlayerCameraState->action & ACT_FLAG_SWIMMING) {
        if (sPowerMeterHUD.animation == POWER_METER_HIDDEN
            || sPowerMeterHUD.animation == POWER_METER_EMPHASIZED) {
            sPowerMeterHUD.animation = POWER_METER_DEEMPHASIZING;
            sPowerMeterHUD.y = 166;
            sPowerMeterPrevY = 166;
        }
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Renders the power meter that shows when Mario is in underwater
 * or has taken damage and has less than 8 health segments.
 * And calls a power meter animation function depending of the value defined.
 */
void render_hud_power_meter(void) {
    s16 shownHealthWedges = gHudDisplay.wedges;

    if (sPowerMeterHUD.animation != POWER_METER_HIDING) {
        handle_power_meter_actions(shownHealthWedges);
    }

    if (sPowerMeterHUD.animation == POWER_METER_HIDDEN) {
        return;
    }

    switch (sPowerMeterHUD.animation) {
        case POWER_METER_EMPHASIZED:
            animate_power_meter_emphasized();
            break;
        case POWER_METER_DEEMPHASIZING:
            animate_power_meter_deemphasizing();
            break;
        case POWER_METER_HIDING:
            animate_power_meter_hiding();
            break;
        default:
            break;
    }

    render_dl_power_meter(shownHealthWedges);

    sPowerMeterVisibleTimer += 1;
}

#ifdef VERSION_JP
#define HUD_TOP_Y 210
#else
#define HUD_TOP_Y 209
#endif

void render_hud_icon(Vtx *vtx, const Texture *texture, u32 fmt, u32 siz, s32 texW, s32 texH, s32 x, s32 y, s32 w, s32 h, s32 tileX, s32 tileY, s32 tileW, s32 tileH) {
    create_dl_ortho_matrix();
    if (!vtx) {
        vtx = alloc_display_list(sizeof(Vtx) * 4);
        vtx[0] = (Vtx) {{{ x,     y - h, 0 }, 0, {  tileX          << 5, (tileY + tileH) << 5 }, { 0xFF, 0xFF, 0xFF, 0xFF }}};
        vtx[1] = (Vtx) {{{ x + w, y - h, 0 }, 0, { (tileX + tileW) << 5, (tileY + tileH) << 5 }, { 0xFF, 0xFF, 0xFF, 0xFF }}};
        vtx[2] = (Vtx) {{{ x + w, y,     0 }, 0, { (tileX + tileW) << 5,  tileY          << 5 }, { 0xFF, 0xFF, 0xFF, 0xFF }}};
        vtx[3] = (Vtx) {{{ x,     y,     0 }, 0, {  tileX          << 5,  tileY          << 5 }, { 0xFF, 0xFF, 0xFF, 0xFF }}};
    }
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);
    gDPSetCombineMode(gDisplayListHead++, G_CC_FADEA, G_CC_FADEA);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(gDisplayListHead++, G_TF_POINT);
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    switch (siz) {
        case G_IM_SIZ_4b:  gDPLoadTextureBlock(gDisplayListHead++, texture, fmt, G_IM_SIZ_4b,  texW, texH, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0); break;
        case G_IM_SIZ_8b:  gDPLoadTextureBlock(gDisplayListHead++, texture, fmt, G_IM_SIZ_8b,  texW, texH, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0); break;
        case G_IM_SIZ_16b: gDPLoadTextureBlock(gDisplayListHead++, texture, fmt, G_IM_SIZ_16b, texW, texH, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0); break;
        case G_IM_SIZ_32b: gDPLoadTextureBlock(gDisplayListHead++, texture, fmt, G_IM_SIZ_32b, texW, texH, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0); break;
    }
    gSPVertexNonGlobal(gDisplayListHead++, vtx, 4, 0);
    gSP2Triangles(gDisplayListHead++, 0, 1, 2, 0x0, 0, 2, 3, 0x0);
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
}

/**
 * Renders the amount of lives Mario has.
 */
void render_hud_mario_lives(void) {
    const s32 anchorX = GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22);
    const s32 anchorY = HUD_TOP_Y;
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));
    render_hud_icon(NULL, gMarioState->character->hudHeadTexture.texture, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, vr_hud_group_x(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), anchorX), vr_hud_group_y(HUD_TOP_Y + 16, anchorY), 16, 16, 0, 0, 16, 16);
    print_text(vr_hud_group_x(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(38), anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "*"); // 'X' glyph
    print_text_fmt_int(vr_hud_group_x(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(54), anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "%d", gHudDisplay.lives);
}

static void render_vr_hud_fps(void) {
    if (!vr_is_active() || !configVrShowFps) {
        return;
    }

    const s32 anchorX = GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22);
    const s32 anchorY = HUD_TOP_Y;
    const s32 x = vr_hud_group_x(
        GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), anchorX);
    const s32 y = vr_hud_group_y(HUD_TOP_Y - 20, anchorY);

    // The performance readout deliberately stays legible regardless of the
    // player's HUD-opacity setting.
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF, 0xFF);
    print_text(x, y, "FPS");
    print_text_fmt_int(x + 32, y, "%d", djui_fps_display_get());

    // FPS intentionally ignores HUD opacity, but it shares the environment
    // color register with every HUD glyph rendered after it. Restore the
    // configured alpha immediately so the FPS counter cannot make the coin,
    // star, timer, or power-meter readouts opaque.
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));
}

/**
 * Renders the number of seconds remaining of the current cap power-ups.
 */
void render_hud_cap_timer(void) {
    static const u8 *sHudCapIcons[][4] = {
        [MARIO_WING_CAP                                     ] = { exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08015E28 },
        [                 MARIO_METAL_CAP                   ] = { exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08014628 },
        [                                   MARIO_VANISH_CAP] = { exclamation_box_seg8_texture_08012E28, exclamation_box_seg8_texture_08012E28, exclamation_box_seg8_texture_08012E28, exclamation_box_seg8_texture_08012E28 },
        [MARIO_WING_CAP | MARIO_METAL_CAP                   ] = { exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08014628 },
        [MARIO_WING_CAP                   | MARIO_VANISH_CAP] = { exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08012E28, exclamation_box_seg8_texture_08012E28 },
        [                 MARIO_METAL_CAP | MARIO_VANISH_CAP] = { exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08012E28, exclamation_box_seg8_texture_08012E28 },
        [MARIO_WING_CAP | MARIO_METAL_CAP | MARIO_VANISH_CAP] = { exclamation_box_seg8_texture_08015E28, exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08014628, exclamation_box_seg8_texture_08012E28 },
    };
    struct MarioState *m = &gMarioStates[0];
    const s32 fpsOffset =
        (vr_is_active() && configVrShowFps) ? 20 : 0;
    const s32 capTopY = HUD_TOP_Y - fpsOffset;
    u32 capFlags = m->flags & MARIO_SPECIAL_CAPS;
    if (capFlags) {
        s32 capTimer = m->capTimer;
        if (capTimer > 0) {
            s32 capSeconds = (capTimer + 29) / 30;
            const u8 **capIcons = sHudCapIcons[capFlags];
            gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                           get_hud_opacity_alpha(0xFF));
            render_hud_icon(NULL, capIcons[0], G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), capTopY - 4, 5, 16,  0, 0, 10, 32);
            render_hud_icon(NULL, capIcons[1], G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(27), capTopY - 4, 3, 16, 10, 0,  6, 32);
            render_hud_icon(NULL, capIcons[2], G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(30), capTopY - 4, 3, 16, 16, 0,  6, 32);
            render_hud_icon(NULL, capIcons[3], G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(33), capTopY - 4, 5, 16, 22, 0, 10, 32);
            print_text(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(38), capTopY - 20, "*"); // 'X' glyph
            print_text_fmt_int(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(54), capTopY - 20, "%d", capSeconds);
        }
    }
}

static void render_hud_radar(struct MarioState *m, struct Object *target, const u8 *iconTexture, u32 fmt, u32 siz, s32 texW, s32 texH, s32 x, s32 y, s32 tileX, s32 tileY, s32 tileW, s32 tileH, u8 r, u8 g, u8 b) {

    // Icon
    gDPSetEnvColor(gDisplayListHead++, r, g, b,
                   get_hud_opacity_alpha(0xFF));
    render_hud_icon(NULL, iconTexture, fmt, siz, texW, texH, x, y + 2, 12, 12, tileX, tileY, tileW, tileH);

    // Direction
    s16 angle = atan2s(
        target->oPosZ - m->pos[2],
        target->oPosX - m->pos[0]
    ) - atan2s(
        m->pos[2] - gCamera->pos[2],
        m->pos[0] - gCamera->pos[0]
    );
    f32 invSqrt2 = 1.f / sqrtf(2.f);
    Vtx *vtx = alloc_display_list(sizeof(Vtx) * 4);
    for (s32 i = 0; i != 4; ++i) {
        s16 a = angle + ((i * 0x4000) - 0x6000);
        vtx[i] = (Vtx) { { {
            x + 6 + 12 * coss(angle + 0x4000) + 8 * invSqrt2 * coss(a),
            y - 4 + 12 * sins(angle + 0x4000) + 8 * invSqrt2 * sins(a), 0,
        }, 0, {
            256 * (((i + 1) / 2) % 2), // 0, 256, 256, 0
            256 * (((i + 2) / 2) % 2), // 256, 256, 0, 0
        }, { 0xFF, 0xFF, 0xFF, 0xFF } } };
    }
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));
    render_hud_icon(vtx, texture_hud_char_arrow_up, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, 0, 8, 8, 0, 0, 8, 8);

    // Distance
    s32 dist = vec3f_dist(&target->oPosX, m->pos);
    print_text_fmt_int(x + 24, y - 12, "%d", dist);
}

static void render_vr_cannon_guidance_arrow_at(
    f32 centerX,
    f32 centerY,
    s16 angle,
    u8 alpha
) {
    const f32 halfDiagonal = 12.0f * sqrtf(2.0f);
    Vtx *vtx = alloc_display_list(sizeof(*vtx) * 4);
    if (vtx == NULL) {
        return;
    }

    for (s32 i = 0; i < 4; i++) {
        const s16 cornerAngle = (s16)(
            angle + i * 0x4000 - 0x6000
        );
        vtx[i] = (Vtx) { { {
            centerX + halfDiagonal * coss(cornerAngle),
            centerY + halfDiagonal * sins(cornerAngle),
            0,
        }, 0, {
            256 * (((i + 1) / 2) % 2),
            256 * (((i + 2) / 2) % 2),
        }, { 0xFF, 0xFF, 0xFF, 0xFF } } };
    }

    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF, alpha);
    render_hud_icon(
        vtx,
        texture_hud_char_arrow_up,
        G_IM_FMT_RGBA,
        G_IM_SIZ_16b,
        8,
        8,
        0,
        0,
        8,
        8,
        0,
        0,
        8,
        8
    );
}

static void render_vr_cannon_guidance_arrows(void) {
    u8 alpha = 0;
    if (!vr_get_cannon_vision_guidance(&alpha)) return;
    const f32 centerX = (f32)SCREEN_WIDTH * 0.5f;
    const f32 centerY = (f32)SCREEN_HEIGHT * 0.5f;
    const f32 horizontalOffset = 64.0f;
    const f32 verticalOffset = 54.0f;
    render_vr_cannon_guidance_arrow_at(centerX, centerY + verticalOffset,
        (s16)0x8000, alpha);
    render_vr_cannon_guidance_arrow_at(centerX, centerY - verticalOffset,
        0, alpha);
    render_vr_cannon_guidance_arrow_at(centerX - horizontalOffset, centerY,
        0x4000, alpha);
    render_vr_cannon_guidance_arrow_at(centerX + horizontalOffset, centerY,
        (s16)-0x4000, alpha);
}

static bool vr_face_stuck_blackout_active(void) {
    return vr_is_active() &&
        configVrCameraMode == VR_CAMERA_MODE_FIRST_PERSON &&
        configVrImmersiveFaceStuck &&
        gMarioStates[0].action == ACT_HEAD_STUCK_IN_GROUND;
}

static bool vr_underwater_filter_active(void) {
    struct MarioState* mario = &gMarioStates[0];
    Vec3f headsetPosition;
    return vr_is_active() &&
        configVrImmersiveUnderwaterFilter &&
        (mario->action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED &&
        vr_get_stabilized_headset_world_position(headsetPosition, false) &&
        headsetPosition[1] < (f32)mario->waterLevel - 8.0f;
}

static void render_vr_underwater_filter(void) {
    Vtx* vtx = alloc_display_list(sizeof(*vtx) * 4);
    if (vtx == NULL) return;
    vtx[0] = (Vtx) { { { -4096, -4096, 0 }, 0, { 0, 0 }, { 44, 120, 200, 64 } } };
    vtx[1] = (Vtx) { { {  4096, -4096, 0 }, 0, { 0, 0 }, { 44, 120, 200, 64 } } };
    vtx[2] = (Vtx) { { {  4096,  4096, 0 }, 0, { 0, 0 }, { 44, 120, 200, 64 } } };
    vtx[3] = (Vtx) { { { -4096,  4096, 0 }, 0, { 0, 0 }, { 44, 120, 200, 64 } } };
    gDPPipeSync(gDisplayListHead++);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gSPVertexNonGlobal(gDisplayListHead++, vtx, 4, 0);
    gSP2Triangles(gDisplayListHead++, 0, 1, 2, 0, 0, 2, 3, 0);
    gDPPipeSync(gDisplayListHead++);
}

static void render_vr_face_stuck_blackout(void) {
    // Oversize the HUD-plane quad so it covers the complete per-eye field;
    // the message remains on the normal fused stereo HUD plane.
    Vtx *vtx = alloc_display_list(sizeof(*vtx) * 4);
    if (vtx == NULL) return;
    vtx[0] = (Vtx) { { { -4096, -4096, 0 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } };
    vtx[1] = (Vtx) { { {  4096, -4096, 0 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } };
    vtx[2] = (Vtx) { { {  4096,  4096, 0 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } };
    vtx[3] = (Vtx) { { { -4096,  4096, 0 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } };
    gDPPipeSync(gDisplayListHead++);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSPTexture(gDisplayListHead++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gSPVertexNonGlobal(gDisplayListHead++, vtx, 4, 0);
    gSP2Triangles(gDisplayListHead++, 0, 1, 2, 0, 0, 2, 3, 0);
    gDPPipeSync(gDisplayListHead++);
    print_text_centered(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
        "Face stuck in the ground");
}

/**
 * Renders the direction and distance to the nearest red coin.
 */
void render_hud_red_coins_and_secrets_radar(void) {
    struct MarioState *m = &gMarioStates[0];
    if (m->marioObj && gCamera) {
        s32 y = 31;

        // Red coins radar
        if (gLevelValues.hudRedCoinsRadar) {
            static const u8 *sRedCoinTextures[] = {
                coin_seg3_texture_03005780,
                coin_seg3_texture_03005F80,
                coin_seg3_texture_03006780,
                coin_seg3_texture_03006F80,
            };
            struct Object *redCoin = obj_get_nearest_object_with_behavior_id(m->marioObj, id_bhvRedCoin);
            if (redCoin) {
                render_hud_radar(m, redCoin, sRedCoinTextures[(gGlobalTimer / 2) % 4], G_IM_FMT_IA, G_IM_SIZ_16b, 32, 32, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(24), y, 0, 0, 32, 32, 0xFF, 0x00, 0x00);
                y += 30;
            }
        }

        // Secrets radar
        if (gLevelValues.hudSecretsRadar) {
            struct Object *secret = obj_get_nearest_object_with_behavior_id(m->marioObj, id_bhvHiddenStarTrigger);
            if (secret) {
                render_hud_radar(m, secret, texture_hud_char_S, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(24), y, 0, 0, 14, 16, 0xFF, 0xFF, 0xFF);
                y += 30;
            }
        }
    }
}

/**
 * Renders the amount of coins collected.
 */
void render_hud_coins(void) {
    const s32 anchorX = 168;
    const s32 anchorY = HUD_TOP_Y;
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));
    print_text(vr_hud_group_x(168, anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "+"); // 'Coin' glyph
    print_text(vr_hud_group_x(184, anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "*"); // 'X' glyph
    print_text_fmt_int(vr_hud_group_x(198, anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "%d", gHudDisplay.coins);
}

#ifdef VERSION_JP
#define HUD_STARS_X 73
#else
#define HUD_STARS_X 78
#endif

/**
 * Renders the amount of stars collected.
 * Disables "X" glyph when Mario has 100 stars or more.
 */
void render_hud_stars(void) {
    s8 showX = 0;

    // prevent star count from flashing outside of castle
    if (gCurrCourseNum != COURSE_NONE) { gHudFlash = 0; }

    if (gHudFlash == 1 && gGlobalTimer & 0x08) {
        return;
    }

    if (gHudDisplay.stars < 100) {
        showX = 1;
    }

    const s32 anchorX = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X);
    const s32 anchorY = HUD_TOP_Y;
    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));
    print_text(vr_hud_group_x(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X), anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "-"); // 'Star' glyph
    if (showX == 1) {
        print_text(vr_hud_group_x(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X) + 16, anchorX), vr_hud_group_y(HUD_TOP_Y, anchorY), "*"); // 'X' glyph
    }
    print_text_fmt_int(vr_hud_group_x((showX * 14) + GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X - 16), anchorX),
                       vr_hud_group_y(HUD_TOP_Y, anchorY), "%d", gHudDisplay.stars);
}

/**
 * Unused function that renders the amount of keys collected.
 * Leftover function from the beta version of the game.
 */
void render_hud_keys(void) {
    s16 i;

    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));

    for (i = 0; i < gHudDisplay.keys; i++) {
        print_text((i * 16) + 220, 142, "/"); // unused glyph - beta key
    }
}

/**
 * Renders the timer when Mario start sliding in PSS.
 */
void render_hud_timer(void) {
    u8 *(*hudLUT)[58];
    u16 timerValFrames;
    u16 timerMins;
    u16 timerSecs;
    u16 timerFracSecs;
    const s32 anchorX = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150);
#define VR_TIMER_X(x) vr_hud_group_x((x), anchorX)

    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));

    hudLUT = segmented_to_virtual(&main_hud_lut);
    timerValFrames = gHudDisplay.timer;
#ifdef VERSION_EU
    switch (eu_get_language()) {
        case LANGUAGE_ENGLISH:
            print_text(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150)), 185, "TIME");
            break;
        case LANGUAGE_FRENCH:
            print_text(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(155)), 185, "TEMPS");
            break;
        case LANGUAGE_GERMAN:
            print_text(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150)), 185, "ZEIT");
            break;
    }
#endif
    timerMins = timerValFrames / (30 * 60);
    timerSecs = (timerValFrames - (timerMins * 1800)) / 30;

    timerFracSecs = ((timerValFrames - (timerMins * 1800) - (timerSecs * 30)) & 0xFFFF) / 3;
#ifndef VERSION_EU
    print_text(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150)), 185, "TIME");
#endif
    print_text_fmt_int(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(91)), 185, "%0d", timerMins);
    print_text_fmt_int(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(71)), 185, "%02d", timerSecs);
    print_text_fmt_int(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(37)), 185, "%d", timerFracSecs);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    render_hud_tex_lut(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(81)), 32, (*hudLUT)[GLYPH_APOSTROPHE]);
    render_hud_tex_lut(VR_TIMER_X(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(46)), 32, (*hudLUT)[GLYPH_DOUBLE_QUOTE]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
#undef VR_TIMER_X
}

s16 get_hud_camera_status(void) {
    return sCameraHUD.status;
}

/**
 * Sets HUD status camera value depending of the actions
 * defined in update_camera_status.
 */
void set_hud_camera_status(s16 status) {
    sCameraHUD.status = status;
}

/**
 * Renders camera HUD glyphs using a table list, depending of
 * the camera status called, a defined glyph is rendered.
 */
void render_hud_camera_status(void) {
    u8 *(*cameraLUT)[6];
    s32 x;
    s32 y;

    cameraLUT = segmented_to_virtual(&main_hud_camera_lut);
    x = vr_hud_group_x(
        GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(54),
        GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(54));
    y = 205;

    if (sCameraHUD.status == CAM_STATUS_NONE) {
        return;
    }

    gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                   get_hud_opacity_alpha(0xFF));

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    render_hud_tex_lut(x, y, (*cameraLUT)[GLYPH_CAM_CAMERA]);

    switch (sCameraHUD.status & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[(gMarioStates[0].character) ? gMarioStates[0].character->cameraHudHead : GLYPH_CAM_MARIO_HEAD]);
            break;
        case CAM_STATUS_LAKITU:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_LAKITU_HEAD]);
            break;
        case CAM_STATUS_FIXED:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_FIXED]);
            break;
    }

    switch (sCameraHUD.status & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            render_hud_small_tex_lut(x + 4, y + 16, (*cameraLUT)[GLYPH_CAM_ARROW_DOWN]);
            break;
        case CAM_STATUS_C_UP:
            render_hud_small_tex_lut(x + 4, y - 8, (*cameraLUT)[GLYPH_CAM_ARROW_UP]);
            break;
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

/**
 * Render HUD strings using hudDisplayFlags with it's render functions,
 * excluding the cannon reticle which detects a camera preset for it.
 */
void render_hud(void) {
    s16 hudDisplayFlags;
#ifdef VERSION_EU
    Mtx *mtx;
#endif

    hudDisplayFlags = gHudDisplay.flags;
    const bool faceStuckBlackout = vr_face_stuck_blackout_active();
    const bool underwaterFilter = vr_underwater_filter_active();
    const bool cannonHudActive =
        gMarioStates[0].action == ACT_IN_CANNON &&
        gCurrentArea != NULL &&
        gCurrentArea->camera != NULL;

    if (hudDisplayFlags == HUD_DISPLAY_NONE) {
        sPowerMeterHUD.animation = POWER_METER_HIDDEN;
        sPowerMeterStoredHealth = 8;
        sPowerMeterVisibleTimer = 0;
    }
    if (hudDisplayFlags == HUD_DISPLAY_NONE &&
        !faceStuckBlackout && !underwaterFilter && !cannonHudActive) return;
    {
#ifdef VERSION_EU
        // basically create_dl_ortho_matrix but guOrtho screen width is different

        mtx = alloc_display_list(sizeof(*mtx));
        if (mtx == NULL) {
            return;
        }
        create_dl_identity_matrix();
        f32 hudLeft, hudRight, hudBottom, hudTop;
        get_vr_hud_ortho_bounds(&hudLeft, &hudRight, &hudBottom, &hudTop);
        guOrtho(mtx, hudLeft - 16.0f, hudRight + 16.0f,
                hudBottom, hudTop, -10.0f, 10.0f, 1.0f);
        register_mtx_vr_hud(mtx);
        gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx),
                G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
#else
        if (vr_is_active()) {
            Mtx *vrHudMtx = alloc_display_list(sizeof(*vrHudMtx));
            if (vrHudMtx == NULL) return;
            create_dl_identity_matrix();
            f32 hudLeft, hudRight, hudBottom, hudTop;
            get_vr_hud_ortho_bounds(
                &hudLeft, &hudRight, &hudBottom, &hudTop);
            guOrtho(vrHudMtx, hudLeft, hudRight, hudBottom, hudTop,
                    -10.0f, 10.0f, 1.0f);
            register_mtx_vr_hud(vrHudMtx);
            gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
            gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(vrHudMtx),
                      G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
        } else {
            create_dl_ortho_matrix();
        }
#endif

        if (underwaterFilter) {
            render_vr_underwater_filter();
        }

        if (faceStuckBlackout) {
            render_vr_face_stuck_blackout();
            return;
        }

        bool showHud = (!gDjuiInMainMenu && !gOverrideHideHud);

        gDPSetEnvColor(gDisplayListHead++, 0xFF, 0xFF, 0xFF,
                       get_hud_opacity_alpha(0xFF));

        if (cannonHudActive) {
            render_hud_cannon_reticle();
            render_vr_cannon_guidance_arrows();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_LIVES && showHud) {
            render_hud_mario_lives();
        }
        if (showHud) {
            render_vr_hud_fps();
        }

        // coop hud elements
        if (showHud) {
            if (gLevelValues.hudCapTimer) {
                render_hud_cap_timer();
            }

            render_hud_red_coins_and_secrets_radar();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_COIN_COUNT && showHud) {
            render_hud_coins();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_STAR_COUNT && showHud) {
            render_hud_stars();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_KEYS && showHud) {
            render_hud_keys();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_CAMERA_AND_POWER && showHud) {
            if (hudDisplayFlags & HUD_DISPLAY_FLAG_CAMERA && showHud) {
                render_hud_camera_status();
            }

            if (hudDisplayFlags & HUD_DISPLAY_FLAG_POWER && showHud) {
                render_hud_power_meter();
            }
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_TIMER && showHud) {
            render_hud_timer();
        }
    }
}
