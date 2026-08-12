#ifndef QUEST_ROM_H
#define QUEST_ROM_H

#include <android_native_app_glue.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum QuestRomStatus {
    QUEST_ROM_MISSING,
    QUEST_ROM_INVALID,
    QUEST_ROM_VALID,
} QuestRomStatus;

typedef struct QuestRomResult {
    QuestRomStatus status;
    char path[512];
    char sha1[41];
} QuestRomResult;

QuestRomResult quest_rom_find_and_validate(const struct android_app *app);
bool quest_rom_load_rgba16_texture(const QuestRomResult *rom,
    size_t physical_offset, size_t physical_size, size_t segment_offset,
    unsigned width, unsigned height, unsigned char *rgba);

#endif
