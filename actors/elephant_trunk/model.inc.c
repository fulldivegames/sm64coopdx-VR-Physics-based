// SM64-style elephant trunk: four short octagonal sections, 64 vertices and
// 76 triangles. Joint overlap prevents animation gaps at close VR distances.
#define VR_TRUNK_RING(Z, Y, RX, RY) \
    {{{ (RX),       (Y),           (Z) }, 0, {0, 0}, {148, 152, 165, 255}}}, \
    {{{ (RX)*7/10,  (Y)+(RY)*7/10, (Z) }, 0, {0, 0}, {169, 173, 186, 255}}}, \
    {{{ 0,           (Y)+(RY),      (Z) }, 0, {0, 0}, {180, 184, 197, 255}}}, \
    {{{-(RX)*7/10,  (Y)+(RY)*7/10, (Z) }, 0, {0, 0}, {157, 161, 174, 255}}}, \
    {{{-(RX),       (Y),           (Z) }, 0, {0, 0}, {113, 118, 132, 255}}}, \
    {{{-(RX)*7/10,  (Y)-(RY)*7/10, (Z) }, 0, {0, 0}, { 86,  91, 105, 255}}}, \
    {{{ 0,           (Y)-(RY),      (Z) }, 0, {0, 0}, { 78,  83,  97, 255}}}, \
    {{{ (RX)*7/10,  (Y)-(RY)*7/10, (Z) }, 0, {0, 0}, {106, 111, 125, 255}}}

static const Vtx vr_elephant_trunk_base_vtx[] = {
    VR_TRUNK_RING(0, 0, 24, 22), VR_TRUNK_RING(25, -2, 21, 19),
};
static const Vtx vr_elephant_trunk_upper_mid_vtx[] = {
    VR_TRUNK_RING(0, 0, 21, 19), VR_TRUNK_RING(25, -3, 17, 16),
};
static const Vtx vr_elephant_trunk_lower_mid_vtx[] = {
    VR_TRUNK_RING(0, 0, 17, 16), VR_TRUNK_RING(23, -4, 13, 12),
};
static const Vtx vr_elephant_trunk_tip_vtx[] = {
    VR_TRUNK_RING(0, 0, 13, 12), VR_TRUNK_RING(19, -5, 7, 7),
};
#undef VR_TRUNK_RING

static const Gfx vr_elephant_trunk_sides[] = {
    gsSP2Triangles(0, 8, 9, 0, 0, 9, 1, 0),
    gsSP2Triangles(1, 9, 10, 0, 1, 10, 2, 0),
    gsSP2Triangles(2, 10, 11, 0, 2, 11, 3, 0),
    gsSP2Triangles(3, 11, 12, 0, 3, 12, 4, 0),
    gsSP2Triangles(4, 12, 13, 0, 4, 13, 5, 0),
    gsSP2Triangles(5, 13, 14, 0, 5, 14, 6, 0),
    gsSP2Triangles(6, 14, 15, 0, 6, 15, 7, 0),
    gsSP2Triangles(7, 15, 8, 0, 7, 8, 0, 0),
    gsSPEndDisplayList(),
};
static const Gfx vr_elephant_trunk_material_begin[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_LIGHTING | G_TEXTURE_GEN),
    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
    gsSPEndDisplayList(),
};
static const Gfx vr_elephant_trunk_material_end[] = {
    gsSPSetGeometryMode(G_LIGHTING),
    gsSPEndDisplayList(),
};
const Gfx vr_elephant_trunk_base_dl[] = {
    gsSPDisplayList(vr_elephant_trunk_material_begin),
    gsSPVertex(vr_elephant_trunk_base_vtx, 16, 0),
    gsSPDisplayList(vr_elephant_trunk_sides),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSP2Triangles(0, 3, 4, 0, 0, 4, 5, 0),
    gsSP2Triangles(0, 5, 6, 0, 0, 6, 7, 0),
    gsSPDisplayList(vr_elephant_trunk_material_end), gsSPEndDisplayList(),
};
const Gfx vr_elephant_trunk_upper_mid_dl[] = {
    gsSPDisplayList(vr_elephant_trunk_material_begin),
    gsSPVertex(vr_elephant_trunk_upper_mid_vtx, 16, 0),
    gsSPDisplayList(vr_elephant_trunk_sides),
    gsSPDisplayList(vr_elephant_trunk_material_end), gsSPEndDisplayList(),
};
const Gfx vr_elephant_trunk_lower_mid_dl[] = {
    gsSPDisplayList(vr_elephant_trunk_material_begin),
    gsSPVertex(vr_elephant_trunk_lower_mid_vtx, 16, 0),
    gsSPDisplayList(vr_elephant_trunk_sides),
    gsSPDisplayList(vr_elephant_trunk_material_end), gsSPEndDisplayList(),
};
const Gfx vr_elephant_trunk_tip_dl[] = {
    gsSPDisplayList(vr_elephant_trunk_material_begin),
    gsSPVertex(vr_elephant_trunk_tip_vtx, 16, 0),
    gsSPDisplayList(vr_elephant_trunk_sides),
    gsSP2Triangles(8, 10, 9, 0, 8, 11, 10, 0),
    gsSP2Triangles(8, 12, 11, 0, 8, 13, 12, 0),
    gsSP2Triangles(8, 14, 13, 0, 8, 15, 14, 0),
    gsSPDisplayList(vr_elephant_trunk_material_end), gsSPEndDisplayList(),
};
