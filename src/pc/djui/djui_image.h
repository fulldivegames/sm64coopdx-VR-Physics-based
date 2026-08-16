#pragma once
#include "djui.h"

struct DjuiImage {
    struct DjuiBase base;
    struct TextureInfo textureInfo;
    bool linearFilter;
};

void djui_image_set_linear_filter(struct DjuiImage* image, bool enabled);
struct DjuiImage* djui_image_create(struct DjuiBase* parent, const Texture* texture, u16 width, u16 height, u8 fmt, u8 siz);
