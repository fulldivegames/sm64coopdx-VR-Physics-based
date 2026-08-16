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
