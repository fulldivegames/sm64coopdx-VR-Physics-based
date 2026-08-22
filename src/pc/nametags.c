#include "pc/nametags.h"
#include "pc/network/network_player.h"
#include "game/object_helpers.h"
#include "behavior_data.h"
#include "pc/djui/djui_hud_utils.h"
#include "engine/math_util.h"
#include "game/obj_behaviors.h"
#include "game/camera.h"
#include "game/rendering_graph_node.h"
#include "pc/vr/vr.h"
#include "pc/lua/utils/smlua_misc_utils.h"
#include "pc/lua/smlua_hooks.h"

#define FADE_SCALE 4.f

struct StateExtras {
    Vec3f prevPos;
    f32 prevScale;
    bool inited;
    bool worldSpace;
};
static struct StateExtras sStateExtras[MAX_PLAYERS];

static bool nametag_load_world_projection(
    Vec3f previousPosition,
    Vec3f currentPosition
) {
    Mtx* matrix = (Mtx*)alloc_display_list(sizeof(Mtx));
    if (matrix == NULL) {
        return false;
    }

    // Each submitted eye replaces this fallback with a projection centered
    // on the remote player's true world-space anchor.
    guOrtho(
        matrix,
        0.0f,
        SCREEN_WIDTH,
        0.0f,
        SCREEN_HEIGHT,
        -10.0f,
        10.0f,
        1.0f
    );
    register_mtx_vr_world_label(
        matrix,
        previousPosition,
        currentPosition
    );
    gSPMatrix(
        gDisplayListHead++,
        VIRTUAL_TO_PHYSICAL(matrix),
        G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH
    );
    return true;
}

static void nametag_restore_vr_ui_projection(void) {
    Mtx* matrix = (Mtx*)alloc_display_list(sizeof(Mtx));
    if (matrix == NULL) {
        return;
    }

    guOrtho(
        matrix,
        0.0f,
        SCREEN_WIDTH,
        0.0f,
        SCREEN_HEIGHT,
        -10.0f,
        10.0f,
        1.0f
    );
    register_mtx_vr_ui(matrix);
    gSPMatrix(
        gDisplayListHead++,
        VIRTUAL_TO_PHYSICAL(matrix),
        G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH
    );
}

static bool nametag_world_pos_to_screen(Vec3f worldPos, Vec3f screenPos) {
    if (!vr_is_active()) {
        return djui_hud_world_pos_to_screen_pos(worldPos, screenPos);
    }

    // Center-eye projection is used only for visibility, depth scaling, and
    // sorting. VR text itself is rendered through a per-eye world matrix.
    Vec3f ndc;
    if (!vr_world_pos_to_ndc(worldPos, ndc) ||
        ndc[0] < -1.2f || ndc[0] > 1.2f ||
        ndc[1] < -1.2f || ndc[1] > 1.2f) {
        return false;
    }
    screenPos[0] = (ndc[0] + 1.0f) * 0.5f * djui_hud_get_screen_width();
    screenPos[1] = (1.0f - ndc[1]) * 0.5f * djui_hud_get_screen_height();
    screenPos[2] = ndc[2];
    return true;
}

void djui_hud_print_outlined_text_interpolated(const char* text, f32 prevX, f32 prevY, f32 prevScale, f32 x, f32 y, f32 scale, u8 r, u8 g, u8 b, u8 a, f32 outlineDarkness) {
    f32 offset = 1 * (scale * 2);
    f32 prevOffset = 1 * (prevScale * 2);

    djui_hud_set_text_color(r, g, b, 255);

    // render outline
    djui_hud_set_color(255 * outlineDarkness, 255 * outlineDarkness, 255 * outlineDarkness, a);
    djui_hud_print_text_interpolated(text, prevX - prevOffset, prevY,              prevScale, prevScale, x - offset, y,          scale, scale);
    djui_hud_print_text_interpolated(text, prevX + prevOffset, prevY,              prevScale, prevScale, x + offset, y,          scale, scale);
    djui_hud_print_text_interpolated(text, prevX,              prevY - prevOffset, prevScale, prevScale, x,          y - offset, scale, scale);
    djui_hud_print_text_interpolated(text, prevX,              prevY + prevOffset, prevScale, prevScale, x,          y + offset, scale, scale);

    // render text
    djui_hud_set_color(255, 255, 255, a);
    djui_hud_print_text_interpolated(text, prevX, prevY, prevScale, prevScale, x, y, scale, scale);

    // reset colors
    djui_hud_set_color(255, 255, 255, 255);
    djui_hud_set_text_color(255, 255, 255, 255);
}

void nametags_render(void) {
    if (gNetworkType == NT_NONE ||
        (!gNametagsSettings.showSelfTag && network_player_connected_count() == 1) ||
        !gNetworkPlayerLocal->currAreaSyncValid ||
        find_object_with_behavior(bhvActSelector) != NULL) {
        return;
    }

    djui_hud_set_resolution(RESOLUTION_N64);
    djui_hud_set_font(FONT_SPECIAL);

    struct NametagInfo {
        Vec3f pos;
        f32 scale;
        f32 sortDepth;
        char name[MAX_CONFIG_STRING];
    };
    struct NametagInfo nametags[MAX_PLAYERS] = {0};
    s32 sortedNametagIndices[MAX_PLAYERS] = {0};
    s32 numNametags = 0;

    extern bool gDjuiHudToWorldCalcViewport;
    gDjuiHudToWorldCalcViewport = false;

    // sort nametags by their distance to the camera
    // insertion sort is quick enough for such small array
    for (s32 i = gNametagsSettings.showSelfTag ? 0 : 1; i < MAX_PLAYERS; i++) {
        struct MarioState* m = &gMarioStates[i];
        if (!is_player_active(m)) { continue; }
        struct NetworkPlayer* np = &gNetworkPlayers[i];
        if (!np->currAreaSyncValid) { continue; }

        switch (m->action) {
            case ACT_START_CROUCHING:
            case ACT_CROUCHING:
            case ACT_STOP_CROUCHING:
            case ACT_START_CRAWLING:
            case ACT_CRAWLING:
            case ACT_STOP_CRAWLING:
            case ACT_IN_CANNON:
            case ACT_DISAPPEARED:
            continue;
        }

        if (m->marioBodyState->mirrorMario) { continue; }

        Vec3f pos;
        Vec3f out;
        f32 modelScale = 1.0f;
        if (m->marioObj != NULL &&
            m->marioObj->header.gfx.scale[1] > 0.01f) {
            modelScale = m->marioObj->header.gfx.scale[1];
        }
        // Use the remote model's stable interpolated root. Animated head-joint
        // storage is updated at game-tick cadence and can briefly contain a
        // stale/eye-specific value, which makes a stereo label flash or jump.
        // This is the same stable owner transform used by attached effects.
        if (m->marioObj != NULL) {
            vec3f_copy(pos, m->marioObj->header.gfx.pos);
            pos[1] += 220.0f * modelScale;
        } else {
            vec3f_copy(pos, m->pos);
            pos[1] += 220.0f * modelScale;
        }

        if (i != 0 || (i == 0 && m->action != ACT_FIRST_PERSON)) {

            char name[MAX_CONFIG_STRING];
            const char* hookedString = NULL;
            smlua_call_event_hooks(HOOK_ON_NAMETAGS_RENDER, i, pos, &hookedString);
            if (hookedString) {
                snprintf(name, MAX_CONFIG_STRING, "%s", hookedString);
            } else {
                snprintf(name, MAX_CONFIG_STRING, "%s", np->name);
            }
            if (!nametag_world_pos_to_screen(pos, out)) {
                continue;
            }

            const bool worldSpace = vr_is_active();
            f32 scale = (worldSpace ? -140.0f : -300.0f) /
                out[2] * djui_hud_get_fov_coeff();
            if (worldSpace) {
                scale = clamp(scale, 0.10f, 0.32f);
            }
            const f32 sortDepth = out[2];

            s32 j = 0;
            for (; j < numNametags; ++j) {
                const struct NametagInfo *nametag = &nametags[sortedNametagIndices[j]];
                if (sortDepth < nametag->sortDepth) {
                    memmove(sortedNametagIndices + j + 1, sortedNametagIndices + j, sizeof(s32) * (numNametags - j));
                    break;
                }
            }
            sortedNametagIndices[j] = i;

            struct NametagInfo *nametag = &nametags[i];
            vec3f_copy(nametag->pos, worldSpace ? pos : out);
            nametag->scale = scale;
            nametag->sortDepth = sortDepth;
            memcpy(nametag->name, name, sizeof(name));
            djui_text_remove_alpha(nametag->name);

            numNametags++;
        }
    }

    gDjuiHudToWorldCalcViewport = true;

    // render nametags
    for (s32 k = 0; k < numNametags; ++k) {
        s32 playerIndex = sortedNametagIndices[k];
        struct NametagInfo *nametag = &nametags[playerIndex];
        struct MarioState *m = &gMarioStates[playerIndex];
        struct NetworkPlayer *np = &gNetworkPlayers[playerIndex];

        // init interpolation
        struct StateExtras* e = &sStateExtras[playerIndex];
        const bool worldSpace = vr_is_active();
        if (!e->inited || e->worldSpace != worldSpace) {
            vec3f_copy(e->prevPos, nametag->pos);
            e->prevScale = nametag->scale;
            e->inited = true;
            e->worldSpace = worldSpace;
        }

        if (worldSpace && !nametag_load_world_projection(
                e->prevPos,
                nametag->pos
            )) {
            continue;
        }

        // Apply viewport for credits
        extern Vp *gViewportOverride;
        extern Vp *gViewportClip;
        extern Vp gViewportFullscreen;
        Vp *viewport = gViewportOverride == NULL ? gViewportClip : gViewportOverride;
        if (viewport) {
            make_viewport_clip_rect(viewport);
            gSPViewport(gDisplayListHead++, viewport);
        }

        f32 width, height;
        djui_hud_measure_text(nametag->name, &width, &height);
        const f32 currCenterX = worldSpace
            ? (f32)SCREEN_WIDTH * 0.5f
            : nametag->pos[0];
        const f32 currCenterY = worldSpace
            ? (f32)SCREEN_HEIGHT * 0.5f
            : nametag->pos[1];
        const f32 prevCenterX = worldSpace
            ? (f32)SCREEN_WIDTH * 0.5f
            : e->prevPos[0];
        const f32 prevCenterY = worldSpace
            ? (f32)SCREEN_HEIGHT * 0.5f
            : e->prevPos[1];
        f32 currHalfWidth = width * nametag->scale * 0.5f;
        f32 currHalfHeight = height * nametag->scale * 0.5f;
        f32 currNametagPosY = currCenterY - currHalfHeight;
        f32 prevHalfWidth = width * e->prevScale * 0.5f;
        f32 prevHalfHeight = height * e->prevScale * 0.5f;
        f32 prevNametagPosY = prevCenterY - prevHalfHeight;
        const u8 *color = network_get_player_text_color(m->playerIndex);
        u8 alpha = (playerIndex == 0 ? 255 : MIN(np->fadeOpacity << 3, 255)) * clamp(FADE_SCALE - nametag->scale, 0.f, 1.f);

        // Multiple outline copies separate perceptibly in stereo and create
        // the doubled/flashing tag effect. VR uses one character-mounted text
        // pass; flat mode keeps the established outlined presentation.
        if (worldSpace) {
            djui_hud_set_color(color[0], color[1], color[2], alpha);
            djui_hud_set_text_color(color[0], color[1], color[2], alpha);
            djui_hud_print_text_interpolated(
                nametag->name,
                prevCenterX - prevHalfWidth,
                prevNametagPosY,
                e->prevScale,
                e->prevScale,
                currCenterX - currHalfWidth,
                currNametagPosY,
                nametag->scale,
                nametag->scale
            );
            djui_hud_set_color(255, 255, 255, 255);
            djui_hud_set_text_color(255, 255, 255, 255);
        } else {
            djui_hud_print_outlined_text_interpolated(nametag->name,
                  prevCenterX - prevHalfWidth, prevNametagPosY,   e->prevScale,
                  currCenterX - currHalfWidth, currNametagPosY, nametag->scale,
                color[0], color[1], color[2], alpha, 0.25);
        }

        // render power meter
        if (playerIndex != 0 && gNametagsSettings.showHealth) {
            djui_hud_set_color(255, 255, 255, alpha);
            f32 currHealthSize = 90 * nametag->scale;
            f32 prevHealthSize = 90 * e->prevScale;
            hud_render_power_meter_interpolated_world(m->health,
                  prevCenterX - (prevHealthSize * 0.5f), prevNametagPosY - 72 *   e->prevScale, prevHealthSize, prevHealthSize,
                  currCenterX - (currHealthSize * 0.5f), currNametagPosY - 72 * nametag->scale, currHealthSize, currHealthSize
            );
        }

        // Reset viewport
        if (viewport) {
            gDPSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, 0, BORDER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - BORDER_HEIGHT);
            gSPViewport(gDisplayListHead++, &gViewportFullscreen);
        }

        vec3f_copy(e->prevPos, nametag->pos);
        e->prevScale = nametag->scale;
    }

    if (vr_is_active() && numNametags > 0) {
        // Everything after the nametags returns to the normal VR UI plane.
        nametag_restore_vr_ui_projection();
    }
}

void nametags_reset(void) {
    for (u8 i = 0; i < MAX_PLAYERS; i++) {
        sStateExtras[i].inited = false;
    }
}
