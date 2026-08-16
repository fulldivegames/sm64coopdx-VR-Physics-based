const GeoLayout vr_fire_flower_geo[] = {
    GEO_CULLING_RADIUS(600),
    GEO_OPEN_NODE(),
        GEO_BILLBOARD(),
        GEO_OPEN_NODE(),
            GEO_DISPLAY_LIST(LAYER_ALPHA, vr_fire_flower_dl),
        GEO_CLOSE_NODE(),
    GEO_CLOSE_NODE(),
    GEO_END(),
};

const GeoLayout vr_fireball_geo[] = {
    GEO_NODE_START(),
    GEO_OPEN_NODE(),
        GEO_ASM(0, geo_vr_fireball_color),
        GEO_SWITCH_CASE(8, geo_switch_anim_state),
        GEO_OPEN_NODE(),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_0_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_1_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_2_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_3_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_4_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_5_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_6_dl),
            GEO_DISPLAY_LIST(LAYER_TRANSPARENT, vr_fireball_frame_7_dl),
        GEO_CLOSE_NODE(),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
