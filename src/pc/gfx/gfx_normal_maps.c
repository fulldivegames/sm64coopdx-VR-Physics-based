#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc/gfx/gfx_normal_maps.h"
#include "pc/gfx/gfx_normal_maps_manifest.h"
#include "pc/fs/fs.h"
#include "pc/platform.h"

#define NORMAL_MAPS_MAGIC 0x50414D4Eu
#define NORMAL_MAPS_VERSION 1u
#define NORMAL_MAPS_HEADER_SIZE 16u
#define NORMAL_MAPS_RECORD_SIZE 16u

static uint8_t *sNormalMapBundle;
static size_t sNormalMapBundleSize;
static bool sNormalMapsRomLoaded;
static bool sNormalMapLoadAttempted;
static char sPendingTextureName[256];

static uint32_t read_u32(const uint8_t *data) {
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool read_file(const char *path, uint8_t **data, size_t *size) {
    if (path == NULL || data == NULL || size == NULL) return false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    uint8_t *buffer = (uint8_t *)malloc((size_t)length);
    if (buffer == NULL || fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);
    *data = buffer;
    *size = (size_t)length;
    return true;
}

static bool load_bundle(void) {
    if (sNormalMapBundle != NULL) return true;
    if (sNormalMapLoadAttempted) return false;
    sNormalMapLoadAttempted = true;

    const char *user_path = fs_get_write_path("normal_maps.bin");
    if (!read_file(user_path, &sNormalMapBundle, &sNormalMapBundleSize)) {
        const char *resource_path = sys_resource_path();
        char resource_file[SYS_MAX_PATH];
        if (resource_path == NULL ||
            snprintf(resource_file, sizeof(resource_file), "%s/normal_maps.bin",
                     resource_path) <= 0 ||
            !read_file(resource_file, &sNormalMapBundle, &sNormalMapBundleSize)) {
            return false;
        }
    }

    if (sNormalMapBundleSize < NORMAL_MAPS_HEADER_SIZE ||
        read_u32(sNormalMapBundle + 0) != NORMAL_MAPS_MAGIC ||
        read_u32(sNormalMapBundle + 4) != NORMAL_MAPS_VERSION ||
        read_u32(sNormalMapBundle + 8) != GFX_NORMAL_MAP_RECORD_COUNT ||
        read_u32(sNormalMapBundle + 12) != NORMAL_MAPS_HEADER_SIZE) {
        free(sNormalMapBundle);
        sNormalMapBundle = NULL;
        sNormalMapBundleSize = 0;
        return false;
    }

    const size_t table_end = NORMAL_MAPS_HEADER_SIZE +
        (size_t)GFX_NORMAL_MAP_RECORD_COUNT * NORMAL_MAPS_RECORD_SIZE;
    if (table_end > sNormalMapBundleSize) {
        free(sNormalMapBundle);
        sNormalMapBundle = NULL;
        sNormalMapBundleSize = 0;
        return false;
    }
    return true;
}

static int find_manifest_index(const char *name) {
    int low = 0;
    int high = (int)GFX_NORMAL_MAP_NAME_COUNT - 1;
    while (low <= high) {
        const int mid = low + (high - low) / 2;
        const int comparison = strcmp(name, gGfxNormalMapNames[mid].name);
        if (comparison == 0) return (int)gGfxNormalMapNames[mid].index;
        if (comparison < 0) high = mid - 1;
        else low = mid + 1;
    }
    return -1;
}

void gfx_normal_maps_on_rom_loaded(void) {
    sNormalMapsRomLoaded = true;
    // Validate the optional resource once, after ROM loading, so the first
    // textured draw never performs filesystem I/O or discovers a bad bundle.
    (void)load_bundle();
}

void gfx_normal_maps_set_pending_texture_name(const char *name) {
    if (name == NULL || name[0] == '\0') {
        sPendingTextureName[0] = '\0';
        return;
    }
    snprintf(sPendingTextureName, sizeof(sPendingTextureName), "%s", name);
}

void gfx_normal_maps_clear_pending_texture_name(void) {
    sPendingTextureName[0] = '\0';
}

bool gfx_normal_maps_load_pending(uint8_t **rgba32, int *width, int *height,
                                  int expected_width, int expected_height) {
    if (rgba32 == NULL || width == NULL || height == NULL) return false;
    *rgba32 = NULL;
    *width = 0;
    *height = 0;

    char name[sizeof(sPendingTextureName)];
    snprintf(name, sizeof(name), "%s", sPendingTextureName);
    sPendingTextureName[0] = '\0';
    if (!sNormalMapsRomLoaded || name[0] == '\0' || !load_bundle()) return false;
    // Goomba body and billboard-face materials mix opaque and alpha passes;
    // their authored maps produce unstable lighting on the face atlas. Keep
    // the original Goomba shading while leaving every other asset eligible.
    if (strncmp(name, "goomba_", 7) == 0) return false;
    // SSL's legacy sand/ceiling materials reuse stretched UV strips whose
    // near-camera samples do not match their distant texture path. Applying
    // a normal overlay there creates the reported moving bubble/line artifact.
    // Keep the original SSL base texture and disable only this optional layer.
    if (strncmp(name, "ssl_", 4) == 0) return false;

    const int record_index = find_manifest_index(name);
    if (record_index < 0 || record_index >= (int)GFX_NORMAL_MAP_RECORD_COUNT) return false;
    const size_t record_offset = NORMAL_MAPS_HEADER_SIZE +
        (size_t)record_index * NORMAL_MAPS_RECORD_SIZE;
    const uint8_t *record = sNormalMapBundle + record_offset;
    const uint32_t data_offset = read_u32(record + 0);
    const uint32_t data_size = read_u32(record + 4);
    const uint32_t map_width = read_u32(record + 8);
    const uint32_t map_height = read_u32(record + 12);
    if (map_width == 0 || map_height == 0 || map_width > 4096 || map_height > 4096 ||
        (expected_width > 0 && map_width != (uint32_t)expected_width) ||
        (expected_height > 0 && map_height != (uint32_t)expected_height) ||
        data_size != map_width * map_height * 3u ||
        data_offset > sNormalMapBundleSize ||
        data_size > sNormalMapBundleSize - data_offset) {
        return false;
    }

    const size_t pixel_count = (size_t)map_width * map_height;
    uint8_t *buffer = (uint8_t *)malloc(pixel_count * 4);
    if (buffer == NULL) return false;
    const uint8_t *source = sNormalMapBundle + data_offset;
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        buffer[pixel * 4 + 0] = source[pixel * 3 + 0];
        buffer[pixel * 4 + 1] = source[pixel * 3 + 1];
        buffer[pixel * 4 + 2] = source[pixel * 3 + 2];
        buffer[pixel * 4 + 3] = 255;
    }
    *rgba32 = buffer;
    *width = (int)map_width;
    *height = (int)map_height;
    return true;
}
