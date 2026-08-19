#include <stdlib.h>
#include <ctype.h>
#include "pc/ini.h"
#include "pc/mods/mods.h"
#include "pc/mods/mods_utils.h"
#include "pc/debuglog.h"
#include "player_palette.h"

const struct PlayerPalette DEFAULT_MARIO_PALETTE =
//  Overalls              Shirt                 Gloves                Shoes                 Hair                  Skin                  Cap                   Emblem
{ { { 0x00, 0x00, 0xff }, { 0xff, 0x00, 0x00 }, { 0xff, 0xff, 0xff }, { 0x72, 0x1c, 0x0e }, { 0x73, 0x06, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0xff, 0x00, 0x00 }, { 0xff, 0x00, 0x00 } } };

const struct PlayerPalette DEFAULT_FIRE_FLOWER_PALETTE =
//  Overalls              Shirt                 Gloves                Shoes                 Hair                  Skin                  Cap                   Emblem
{ { { 0xd2, 0x18, 0x18 }, { 0xff, 0xff, 0xff }, { 0xff, 0xff, 0xff }, { 0x5c, 0x30, 0x18 }, { 0x73, 0x06, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0xff, 0xff, 0xff }, { 0xff, 0xff, 0xff } } };

const struct PlayerPalette DEFAULT_HAMMER_SUIT_PALETTE =
//  Overalls              Shirt                 Gloves                Shoes                 Hair                  Skin                  Cap                   Emblem
{ { { 0x12, 0x14, 0x18 }, { 0xf4, 0xf4, 0xf2 }, { 0xff, 0xff, 0xff }, { 0x72, 0x38, 0x1c }, { 0x73, 0x06, 0x00 }, { 0xfe, 0xc1, 0x79 }, { 0x10, 0x12, 0x16 }, { 0xf2, 0xf2, 0xf2 } } };

static ini_t* sPalette = NULL;

#if defined(__ANDROID__)
extern const char* quest_android_shared_palette_path(void);
#endif

struct PresetPalette gPresetPalettes[MAX_PRESET_PALETTES] = { 0 };
u16 gPresetPaletteCount = 0;

static const char* FIRE_FLOWER_PALETTE_NAME = "Fireflower";

static bool player_palette_write(
    const char* palettesPath,
    const char* name,
    const struct PlayerPalette* palette
) {
    char ppath[SYS_MAX_PATH] = "";
    snprintf(ppath, SYS_MAX_PATH, "%s/%s.ini", palettesPath, name);
    fs_sys_mkdir(palettesPath);

    FILE* file = fopen(ppath, "w");
    if (file == NULL) {
        LOG_ERROR("Unable to create file '%s.ini'!", name);
        return false;
    }

    fprintf(file, "[PALETTE]\n\
PANTS_R = %d\nPANTS_G = %d\nPANTS_B = %d\n\
SHIRT_R = %d\nSHIRT_G = %d\nSHIRT_B = %d\n\
GLOVES_R = %d\nGLOVES_G = %d\nGLOVES_B = %d\n\
SHOES_R = %d\nSHOES_G = %d\nSHOES_B = %d\n\
HAIR_R = %d\nHAIR_G = %d\nHAIR_B = %d\n\
SKIN_R = %d\nSKIN_G = %d\nSKIN_B = %d\n\
CAP_R = %d\nCAP_G = %d\nCAP_B = %d\n\
EMBLEM_R = %d\nEMBLEM_G = %d\nEMBLEM_B = %d\n",
        palette->parts[PANTS][0], palette->parts[PANTS][1], palette->parts[PANTS][2],
        palette->parts[SHIRT][0], palette->parts[SHIRT][1], palette->parts[SHIRT][2],
        palette->parts[GLOVES][0], palette->parts[GLOVES][1], palette->parts[GLOVES][2],
        palette->parts[SHOES][0], palette->parts[SHOES][1], palette->parts[SHOES][2],
        palette->parts[HAIR][0], palette->parts[HAIR][1], palette->parts[HAIR][2],
        palette->parts[SKIN][0], palette->parts[SKIN][1], palette->parts[SKIN][2],
        palette->parts[CAP][0], palette->parts[CAP][1], palette->parts[CAP][2],
        palette->parts[EMBLEM][0], palette->parts[EMBLEM][1], palette->parts[EMBLEM][2]
    );
    fclose(file);
    return true;
}

static void player_palette_ensure_fire_flower(const char* palettesPath) {
    char ppath[SYS_MAX_PATH] = "";
    snprintf(
        ppath,
        SYS_MAX_PATH,
        "%s/%s.ini",
        palettesPath,
        FIRE_FLOWER_PALETTE_NAME
    );
    FILE* file = fopen(ppath, "r");
    if (file != NULL) {
        fclose(file);
        return;
    }
    player_palette_write(
        palettesPath,
        FIRE_FLOWER_PALETTE_NAME,
        &DEFAULT_FIRE_FLOWER_PALETTE
    );
}

const struct PlayerPalette* player_palette_get_fire_flower(void) {
    for (u16 i = 0; i < gPresetPaletteCount; i++) {
        if (strcmp(gPresetPalettes[i].name, FIRE_FLOWER_PALETTE_NAME) == 0) {
            return &gPresetPalettes[i].palette;
        }
    }
    return &DEFAULT_FIRE_FLOWER_PALETTE;
}

const struct PlayerPalette* player_palette_get_hammer_suit(void) {
    return &DEFAULT_HAMMER_SUIT_PALETTE;
}

static bool player_palette_init(const char* palettesPath, char* palette, bool appendPalettes) {
    // free old ini
    if (sPalette != NULL) {
        ini_free(sPalette);
        sPalette = NULL;
    }

    // construct path
    char path[SYS_MAX_PATH] = "";
    if (!palette || palette[0] == '\0') { palette = "Mario"; }
    if (appendPalettes) {
        snprintf(path, SYS_MAX_PATH, "%s/palettes/%s.ini", palettesPath, palette);
    } else {
        snprintf(path, SYS_MAX_PATH, "%s/%s.ini", palettesPath, palette);
    }

    // load
    sPalette = ini_load(path);

    return sPalette != NULL;
}

void player_palettes_reset(void) {
    memset(&gPresetPalettes, 0, sizeof(gPresetPalettes));
    gPresetPaletteCount = 0;
}

static u8 read_value(const char* data) {
    if (data == NULL) { return 0; }
    data = sys_strlwr((char*)data);
    for (size_t i = 0; i < strlen(data); i++) {
        char c = data[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == 'x')) {
            return 0;
        }
    }
    return MIN(strtol(data, NULL, 0), 255);
}

static void player_palettes_sort_characters(void) {
    struct PresetPalette charPresetPalettes[MAX_PRESET_PALETTES] = { 0 };
    u8 charPresetPaletteCount = 0;

    // copy character palettes first
    for (int c = 0; c < CT_MAX; c++) { // heh, c++
        for (int i = 0; i < gPresetPaletteCount; i++) {
            if (!strcmp(gPresetPalettes[i].name, gCharacters[c].name)) {
                charPresetPalettes[charPresetPaletteCount++] = gPresetPalettes[i];
            }
        }
    }

    // copy remaining palettes
    for (int i = 0; i < gPresetPaletteCount; i++) {
        bool isCharPalette = false;
        for (int c = 0; c < CT_MAX; c++) { // heh, c++
            if (!strcmp(gPresetPalettes[i].name, gCharacters[c].name)) {
                isCharPalette = true;
                break;
            }
        }
        if (!isCharPalette) {
            charPresetPalettes[charPresetPaletteCount++] = gPresetPalettes[i];
        }
    }

    // finally, write to gPresetPalettes
    for (int i = 0; i < gPresetPaletteCount; i++) {
        gPresetPalettes[i] = charPresetPalettes[i];
    }
}

void player_palettes_read(const char* palettesPath, bool appendPalettes) {
    // construct palette path
    char ppath[SYS_MAX_PATH] = "";
    if (appendPalettes) {
        snprintf(ppath, SYS_MAX_PATH, "%s/palettes", palettesPath);
    } else {
        snprintf(ppath, SYS_MAX_PATH, "%s", palettesPath);
#if defined(__ANDROID__)
        // Standalone user palettes live in shared storage. Avoid creating a
        // duplicate Fireflower preset in the legacy private fallback path.
        if (strcmp(ppath, quest_android_shared_palette_path()) == 0) {
            player_palette_ensure_fire_flower(ppath);
        }
#else
        player_palette_ensure_fire_flower(ppath);
#endif
    }

    // open directory
    struct dirent* dir = NULL;

    DIR* d = opendir(ppath);
    if (!d) { return; }

    // iterate
    char path[SYS_MAX_PATH] = { 0 };
    while ((dir = readdir(d)) != NULL) {
        // sanity check / fill path[]
        if (!directory_sanity_check(dir, ppath, path)) { continue; }
        snprintf(path, SYS_MAX_PATH, "%s", dir->d_name);

        // strip the name before the .
        char* c = path;
        while (*c != '\0') {
            if (*c == '.') { *c = '\0'; break; }
            c++;
        }
        if (strlen(path) == 0) { continue; }

#if defined(__ANDROID__)
        // A short-lived test build created Fireflower.ini in the legacy
        // private palette directory. Prefer the editable shared copy and do
        // not expose two identically named presets in the menu.
        if (!appendPalettes &&
            strcmp(palettesPath, quest_android_shared_palette_path()) != 0 &&
            strcmp(path, FIRE_FLOWER_PALETTE_NAME) == 0) { continue; }
#endif

        if (!player_palette_init(palettesPath, path, appendPalettes)) {
#ifdef DEVELOPMENT
            LOG_ERROR("Failed to load palette '%s.ini'", path);
#endif
            continue;
        }

        struct PlayerPalette palette = {{
            { read_value(ini_get(sPalette, "PALETTE", "PANTS_R")), read_value(ini_get(sPalette, "PALETTE", "PANTS_G")), read_value(ini_get(sPalette, "PALETTE", "PANTS_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "SHIRT_R")), read_value(ini_get(sPalette, "PALETTE", "SHIRT_G")), read_value(ini_get(sPalette, "PALETTE", "SHIRT_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "GLOVES_R")), read_value(ini_get(sPalette, "PALETTE", "GLOVES_G")), read_value(ini_get(sPalette, "PALETTE", "GLOVES_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "SHOES_R")), read_value(ini_get(sPalette, "PALETTE", "SHOES_G")), read_value(ini_get(sPalette, "PALETTE", "SHOES_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "HAIR_R")), read_value(ini_get(sPalette, "PALETTE", "HAIR_G")), read_value(ini_get(sPalette, "PALETTE", "HAIR_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "SKIN_R")), read_value(ini_get(sPalette, "PALETTE", "SKIN_G")), read_value(ini_get(sPalette, "PALETTE", "SKIN_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "CAP_R")), read_value(ini_get(sPalette, "PALETTE", "CAP_G")), read_value(ini_get(sPalette, "PALETTE", "CAP_B")) },
            { read_value(ini_get(sPalette, "PALETTE", "EMBLEM_R")), read_value(ini_get(sPalette, "PALETTE", "EMBLEM_G")), read_value(ini_get(sPalette, "PALETTE", "EMBLEM_B")) }
        }};
        // free
        ini_free(sPalette);
        sPalette = NULL;
        snprintf(gPresetPalettes[gPresetPaletteCount].name, 64, "%s", path);
        gPresetPalettes[gPresetPaletteCount].palette = palette;
        gPresetPaletteCount++;
#ifdef DEVELOPMENT
        LOG_INFO("Loaded palette '%s.ini'", path);
#endif
        if (gPresetPaletteCount >= MAX_PRESET_PALETTES) { break; }
    }

    closedir(d);

    // this should mean we are in the exe path's palette dir
    if (appendPalettes) {
        player_palettes_sort_characters();
    }
}

void player_palette_export(char* name) {
#if defined(__ANDROID__)
    const char* palettesPath = quest_android_shared_palette_path();
#else
    const char* palettesPath = fs_get_write_path(PALETTES_DIRECTORY);
#endif
    if (player_palette_write(palettesPath, name, &configPlayerPalette)) {
        LOG_INFO("Saving palette as '%s.ini'", name);
    }
}

bool player_palette_delete(const char* palettesPath, char* name, bool appendPalettes) {
    // construct palette path
    char ppath[SYS_MAX_PATH] = "";
    if (appendPalettes) {
        snprintf(ppath, SYS_MAX_PATH, "%s/palettes/%s.ini", palettesPath, name);
    } else {
        snprintf(ppath, SYS_MAX_PATH, "%s/%s.ini", palettesPath, name);
    }

    if (remove(ppath) == 0) {
        LOG_INFO("Deleting palette '%s.ini'", name);
        return true;
    }
#if defined(__ANDROID__)
    if (!appendPalettes) {
        snprintf(
            ppath,
            SYS_MAX_PATH,
            "%s/%s.ini",
            quest_android_shared_palette_path(),
            name
        );
        if (remove(ppath) == 0) {
            LOG_INFO("Deleting shared palette '%s.ini'", name);
            return true;
        }
    }
#endif
    return false;
}
