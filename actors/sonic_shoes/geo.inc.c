const GeoLayout vr_sonic_shoes_geo[] = {
    GEO_SCALE(0x00, 65536),
    GEO_OPEN_NODE(),
        GEO_DISPLAY_LIST(LAYER_OPAQUE, vr_sonic_shoes_pair_dl),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
