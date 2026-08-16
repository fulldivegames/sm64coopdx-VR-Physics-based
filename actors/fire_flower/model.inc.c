// VR Fire Flower pickup sprite. The source art is supplied by FullDiveGames
// and reduced with nearest-neighbor sampling so its pixel edges stay crisp.
ALIGNED8 const Texture vr_fire_flower_texture[] = {
#include "actors/fire_flower/fire_flower.rgba16.inc.c"
};

static const Vtx vr_fire_flower_billboard_vtx[] = {
    {{{-64,   0, 0}, 0, {   0, 992}, {255, 255, 255, 255}}},
    {{{ 64,   0, 0}, 0, { 992, 992}, {255, 255, 255, 255}}},
    {{{ 64, 128, 0}, 0, { 992,   0}, {255, 255, 255, 255}}},
    {{{-64, 128, 0}, 0, {   0,   0}, {255, 255, 255, 255}}},
};

const Gfx vr_fire_flower_dl[] = {
    gsDPPipeSync(),
    gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureBlock(vr_fire_flower_texture, G_IM_FMT_RGBA,
        G_IM_SIZ_16b, 32, 32, 0, G_TX_CLAMP, G_TX_CLAMP, 5, 5,
        G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(vr_fire_flower_billboard_vtx, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};

#define VR_FIREBALL_FRAME(name, texture) \
    const Gfx name[] = { \
        gsDPPipeSync(), \
        gsDPSetTextureImage(G_IM_FMT_IA, G_IM_SIZ_16b, 1, texture), \
        gsSPClearGeometryMode(G_LIGHTING | G_SHADING_SMOOTH), \
        gsDPSetCombineMode(G_CC_FADEA, G_CC_FADEA), \
        gsDPSetTile(G_IM_FMT_IA, G_IM_SIZ_16b, 0, 0, \
            G_TX_LOADTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD, \
            G_TX_CLAMP, 5, G_TX_NOLOD), \
        gsDPLoadSync(), \
        gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 32 - 1, \
            CALC_DXT(32, G_IM_SIZ_16b_BYTES)), \
        gsDPSetTile(G_IM_FMT_IA, G_IM_SIZ_16b, 8, 0, \
            G_TX_RENDERTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD, \
            G_TX_CLAMP, 5, G_TX_NOLOD), \
        gsDPSetTileSize(0, 0, 0, \
            (32 - 1) << G_TEXTURE_IMAGE_FRAC, \
            (32 - 1) << G_TEXTURE_IMAGE_FRAC), \
        gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON), \
        gsSPVertex(flame_seg3_vertex_030172E0, 4, 0), \
        gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0), \
        gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF), \
        gsDPPipeSync(), \
        gsSPSetGeometryMode(G_LIGHTING | G_SHADING_SMOOTH), \
        gsDPSetEnvColor(255, 255, 255, 255), \
        gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE), \
        gsSPEndDisplayList(), \
    }

VR_FIREBALL_FRAME(vr_fireball_frame_0_dl, flame_seg3_texture_03017320);
VR_FIREBALL_FRAME(vr_fireball_frame_1_dl, flame_seg3_texture_03017B20);
VR_FIREBALL_FRAME(vr_fireball_frame_2_dl, flame_seg3_texture_03018320);
VR_FIREBALL_FRAME(vr_fireball_frame_3_dl, flame_seg3_texture_03018B20);
VR_FIREBALL_FRAME(vr_fireball_frame_4_dl, flame_seg3_texture_03019320);
VR_FIREBALL_FRAME(vr_fireball_frame_5_dl, flame_seg3_texture_03019B20);
VR_FIREBALL_FRAME(vr_fireball_frame_6_dl, flame_seg3_texture_0301A320);
VR_FIREBALL_FRAME(vr_fireball_frame_7_dl, flame_seg3_texture_0301AB20);
#undef VR_FIREBALL_FRAME
