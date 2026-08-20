// Compact N64-style Hammer Bro hammer used by the Hammer Suit volley.
// It intentionally uses vertex colors instead of a new texture, keeping the
// projectile cheap enough to render several copies in VR.
static const Vtx vr_hammer_head_vtx[] = {
    {{{-72,  24, -30}, 0, {0, 0}, {22, 24, 28, 255}}},
    {{{ 72,  24, -30}, 0, {0, 0}, {22, 24, 28, 255}}},
    {{{ 72,  78, -30}, 0, {0, 0}, {38, 40, 46, 255}}},
    {{{-72,  78, -30}, 0, {0, 0}, {38, 40, 46, 255}}},
    {{{-72,  24,  30}, 0, {0, 0}, {15, 17, 20, 255}}},
    {{{ 72,  24,  30}, 0, {0, 0}, {15, 17, 20, 255}}},
    {{{ 72,  78,  30}, 0, {0, 0}, {31, 34, 39, 255}}},
    {{{-72,  78,  30}, 0, {0, 0}, {31, 34, 39, 255}}},
};

static const Vtx vr_hammer_handle_vtx[] = {
    {{{-11, -108, -11}, 0, {0, 0}, {104, 51, 24, 255}}},
    {{{ 11, -108, -11}, 0, {0, 0}, {142, 74, 34, 255}}},
    {{{ 11,   31, -11}, 0, {0, 0}, {166, 91, 43, 255}}},
    {{{-11,   31, -11}, 0, {0, 0}, {125, 62, 29, 255}}},
    {{{-11, -108,  11}, 0, {0, 0}, {82, 39, 19, 255}}},
    {{{ 11, -108,  11}, 0, {0, 0}, {119, 58, 27, 255}}},
    {{{ 11,   31,  11}, 0, {0, 0}, {145, 76, 35, 255}}},
    {{{-11,   31,  11}, 0, {0, 0}, {101, 48, 23, 255}}},
};

static const Gfx vr_hammer_box_triangles[] = {
    gsSP2Triangles(0, 2, 1, 0, 0, 3, 2, 0),
    gsSP2Triangles(4, 5, 6, 0, 4, 6, 7, 0),
    gsSP2Triangles(0, 5, 4, 0, 0, 1, 5, 0),
    gsSP2Triangles(3, 6, 2, 0, 3, 7, 6, 0),
    gsSP2Triangles(1, 6, 5, 0, 1, 2, 6, 0),
    gsSP2Triangles(0, 7, 3, 0, 0, 4, 7, 0),
    gsSPEndDisplayList(),
};

const Gfx vr_hammer_dl[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_TEXTURE_GEN),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPVertex(vr_hammer_handle_vtx, 8, 0),
    gsSPDisplayList(vr_hammer_box_triangles),
    gsSPVertex(vr_hammer_head_vtx, 8, 0),
    gsSPDisplayList(vr_hammer_box_triangles),
    gsSPSetGeometryMode(G_LIGHTING),
    gsSPEndDisplayList(),
};
