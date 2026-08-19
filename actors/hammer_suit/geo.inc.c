const GeoLayout vr_hammer_geo[] = {
    GEO_SCALE(0x00, 65536),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, vr_hammer_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
