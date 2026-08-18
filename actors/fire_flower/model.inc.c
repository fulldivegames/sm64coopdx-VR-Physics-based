// VR Fire Flower pickup sprite. The source art is supplied by FullDiveGames
// and reduced with nearest-neighbor sampling so its pixel edges stay crisp.
ALIGNED8 const Texture vr_fire_flower_texture[] = {
#include "actors/fire_flower/fire_flower.rgba16.inc.c"
};

ALIGNED8 const Texture vr_rasengan_texture[] = {
#include "actors/fire_flower/rasengan.rgba16.inc.c"
};

ALIGNED8 const Texture vr_rasen_shuriken_texture[] = {
#include "actors/fire_flower/rasen_shuriken.rgba16.inc.c"
};

extern ALIGNED8 const Texture tornado_seg5_texture_05013128[];

// Three differently angled copies of this annulus are wrapped around both
// Rasengan forms. Only the narrow top strip of the original tornado texture
// is sampled, turning its bright crown into a compact energy ring.
static const Vtx vr_rasengan_tornado_ring_vtx[] = {
    {{{77, 0, 0}, 0, {0, 0}, {255, 255, 255, 255}}},
    {{{66, 0, 0}, 0, {0, 124}, {255, 255, 255, 255}}},
    {{{67, 38, 0}, 0, {83, 0}, {255, 255, 255, 255}}},
    {{{57, 33, 0}, 0, {83, 124}, {255, 255, 255, 255}}},
    {{{39, 67, 0}, 0, {165, 0}, {255, 255, 255, 255}}},
    {{{33, 57, 0}, 0, {165, 124}, {255, 255, 255, 255}}},
    {{{0, 77, 0}, 0, {248, 0}, {255, 255, 255, 255}}},
    {{{0, 66, 0}, 0, {248, 124}, {255, 255, 255, 255}}},
    {{{-38, 67, 0}, 0, {331, 0}, {255, 255, 255, 255}}},
    {{{-33, 57, 0}, 0, {331, 124}, {255, 255, 255, 255}}},
    {{{-67, 38, 0}, 0, {413, 0}, {255, 255, 255, 255}}},
    {{{-57, 33, 0}, 0, {413, 124}, {255, 255, 255, 255}}},
    {{{-77, 0, 0}, 0, {496, 0}, {255, 255, 255, 255}}},
    {{{-66, 0, 0}, 0, {496, 124}, {255, 255, 255, 255}}},
    {{{-67, -38, 0}, 0, {579, 0}, {255, 255, 255, 255}}},
    {{{-57, -33, 0}, 0, {579, 124}, {255, 255, 255, 255}}},
    {{{-39, -67, 0}, 0, {661, 0}, {255, 255, 255, 255}}},
    {{{-33, -57, 0}, 0, {661, 124}, {255, 255, 255, 255}}},
    {{{0, -77, 0}, 0, {744, 0}, {255, 255, 255, 255}}},
    {{{0, -66, 0}, 0, {744, 124}, {255, 255, 255, 255}}},
    {{{39, -67, 0}, 0, {827, 0}, {255, 255, 255, 255}}},
    {{{33, -57, 0}, 0, {827, 124}, {255, 255, 255, 255}}},
    {{{67, -39, 0}, 0, {909, 0}, {255, 255, 255, 255}}},
    {{{57, -33, 0}, 0, {909, 124}, {255, 255, 255, 255}}},
    {{{77, 0, 0}, 0, {992, 0}, {255, 255, 255, 255}}},
    {{{66, 0, 0}, 0, {992, 124}, {255, 255, 255, 255}}},
};

const Gfx vr_rasengan_tornado_ring_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK | G_CULL_FRONT),
    gsDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM),
    gsDPSetPrimColor(0, 0, 125, 220, 255, 175),
    gsDPSetTextureImage(G_IM_FMT_IA, G_IM_SIZ_16b, 1,
        tornado_seg5_texture_05013128),
    gsDPSetTile(G_IM_FMT_IA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
        G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD,
        G_TX_CLAMP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD),
    gsDPLoadSync(),
    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, 32 * 64 - 1,
        CALC_DXT(32, G_IM_SIZ_16b_BYTES)),
    gsDPTileSync(),
    gsDPSetTile(G_IM_FMT_IA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0,
        G_TX_WRAP | G_TX_NOMIRROR, 6, G_TX_NOLOD,
        G_TX_CLAMP | G_TX_NOMIRROR, 5, G_TX_NOLOD),
    gsDPSetTileSize(G_TX_RENDERTILE, 0, 0,
        (32 - 1) << G_TEXTURE_IMAGE_FRAC,
        (64 - 1) << G_TEXTURE_IMAGE_FRAC),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsSPVertex(vr_rasengan_tornado_ring_vtx, 26, 0),
    gsSP2Triangles(0, 1, 2, 0, 2, 1, 3, 0),
    gsSP2Triangles(2, 3, 4, 0, 4, 3, 5, 0),
    gsSP2Triangles(4, 5, 6, 0, 6, 5, 7, 0),
    gsSP2Triangles(6, 7, 8, 0, 8, 7, 9, 0),
    gsSP2Triangles(8, 9, 10, 0, 10, 9, 11, 0),
    gsSP2Triangles(10, 11, 12, 0, 12, 11, 13, 0),
    gsSP2Triangles(12, 13, 14, 0, 14, 13, 15, 0),
    gsSP2Triangles(14, 15, 16, 0, 16, 15, 17, 0),
    gsSP2Triangles(16, 17, 18, 0, 18, 17, 19, 0),
    gsSP2Triangles(18, 19, 20, 0, 20, 19, 21, 0),
    gsSP2Triangles(20, 21, 22, 0, 22, 21, 23, 0),
    gsSP2Triangles(22, 23, 24, 0, 24, 23, 25, 0),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPPipeSync(),
    gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsSPEndDisplayList(),
};


static const Vtx vr_fire_flower_billboard_vtx[] = {
    {{{-64,   0, 0}, 0, {   0, 992}, {255, 255, 255, 255}}},
    {{{ 64,   0, 0}, 0, { 992, 992}, {255, 255, 255, 255}}},
    {{{ 64, 128, 0}, 0, { 992,   0}, {255, 255, 255, 255}}},
    {{{-64, 128, 0}, 0, {   0,   0}, {255, 255, 255, 255}}},
};

const Gfx vr_fire_flower_dl[] = {
    gsDPPipeSync(),
    // Preserve the supplied texture RGB while multiplying its alpha by the
    // charge fade supplied through the environment color.
    gsDPSetCombineMode(G_CC_MODULATEIFADEA, G_CC_MODULATEIFADEA),
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

#include "actors/fire_flower/rasengan_sphere.generated.inc.c"

static const Vtx vr_rasen_shuriken_vtx[] = {
    {{{-512, -512, 0}, 0, {  0, 992}, {255, 255, 255, 255}}},
    {{{ 512, -512, 0}, 0, {992, 992}, {255, 255, 255, 255}}},
    {{{ 512,  512, 0}, 0, {992,   0}, {255, 255, 255, 255}}},
    {{{-512,  512, 0}, 0, {  0,   0}, {255, 255, 255, 255}}},
};

const Gfx vr_rasen_shuriken_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK | G_CULL_FRONT),
    // Use the supplied PNG's original RGBA values. The shared Rasengan
    // environment tint made this artwork render as a dark-blue silhouette.
    gsDPSetEnvColor(255, 255, 255, 255),
    gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
    gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA),
    gsDPLoadTextureBlock(vr_rasen_shuriken_texture, G_IM_FMT_RGBA,
        G_IM_SIZ_16b, 32, 32, 0, G_TX_CLAMP, G_TX_CLAMP, 5, 5,
        G_TX_NOLOD, G_TX_NOLOD),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsSPVertex(vr_rasen_shuriken_vtx, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSP2Triangles(2, 1, 0, 0, 3, 2, 0, 0),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),
    gsDPPipeSync(),
    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),
    gsDPSetEnvColor(255, 255, 255, 255),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};
