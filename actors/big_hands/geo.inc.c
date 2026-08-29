#include "actors/mario/geo_header.h"

// A compact pair of Mario's native closed gloves. Keeping the original
// display lists preserves Mario's glove colors and low-poly silhouette.
const GeoLayout vr_big_hands_geo[] = {
    GEO_SCALE(0x00, 65536),
    GEO_OPEN_NODE(),
        GEO_TRANSLATE_ROTATE_WITH_DL(
            LAYER_OPAQUE, 0, 0, 56, 0, 0, 0, mario_left_hand_closed
        ),
        GEO_TRANSLATE_ROTATE_WITH_DL(
            LAYER_OPAQUE, 0, 0, -56, 0, 0, 0, mario_right_hand_closed
        ),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
