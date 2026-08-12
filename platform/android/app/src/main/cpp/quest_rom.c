#include "quest_rom.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SM64_US_SHA1 "9bef1128717f958171a4afac3ed78ee2bb4e86ce"

typedef struct Sha1 {
    uint32_t state[5];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} Sha1;

static uint32_t rotate_left(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

static void sha1_transform(Sha1 *sha, const uint8_t block[64]) {
    uint32_t words[80];
    for (unsigned i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24)
            | ((uint32_t)block[i * 4 + 1] << 16)
            | ((uint32_t)block[i * 4 + 2] << 8)
            | (uint32_t)block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 80; ++i) {
        words[i] = rotate_left(words[i - 3] ^ words[i - 8]
            ^ words[i - 14] ^ words[i - 16], 1);
    }

    uint32_t a = sha->state[0];
    uint32_t b = sha->state[1];
    uint32_t c = sha->state[2];
    uint32_t d = sha->state[3];
    uint32_t e = sha->state[4];
    for (unsigned i = 0; i < 80; ++i) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcU;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6U;
        }
        const uint32_t temp = rotate_left(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temp;
    }
    sha->state[0] += a;
    sha->state[1] += b;
    sha->state[2] += c;
    sha->state[3] += d;
    sha->state[4] += e;
}

static void sha1_init(Sha1 *sha) {
    *sha = (Sha1){
        .state = {0x67452301U, 0xefcdab89U, 0x98badcfeU,
                  0x10325476U, 0xc3d2e1f0U},
    };
}

static void sha1_update(Sha1 *sha, const uint8_t *data, size_t size) {
    sha->bytes += size;
    while (size > 0) {
        size_t count = sizeof(sha->block) - sha->used;
        if (count > size) count = size;
        memcpy(sha->block + sha->used, data, count);
        sha->used += count;
        data += count;
        size -= count;
        if (sha->used == sizeof(sha->block)) {
            sha1_transform(sha, sha->block);
            sha->used = 0;
        }
    }
}

static void sha1_finish(Sha1 *sha, char output[41]) {
    const uint64_t bit_count = sha->bytes * 8U;
    const uint8_t marker = 0x80;
    sha1_update(sha, &marker, 1);
    const uint8_t zero = 0;
    while (sha->used != 56) sha1_update(sha, &zero, 1);
    uint8_t length[8];
    for (unsigned i = 0; i < 8; ++i) {
        length[7 - i] = (uint8_t)(bit_count >> (i * 8));
    }
    sha1_update(sha, length, sizeof(length));
    for (unsigned i = 0; i < 5; ++i) {
        snprintf(output + i * 8, 9, "%08x", sha->state[i]);
    }
    output[40] = '\0';
}

static bool hash_file(const char *path, char digest[41]) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    Sha1 sha;
    sha1_init(&sha);
    uint8_t buffer[64 * 1024];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        sha1_update(&sha, buffer, count);
    }
    const bool success = !ferror(file);
    fclose(file);
    if (success) sha1_finish(&sha, digest);
    return success;
}

QuestRomResult quest_rom_find_and_validate(const struct android_app *app) {
    QuestRomResult result = {.status = QUEST_ROM_MISSING};
    const char *roots[] = {
        app->activity->externalDataPath,
        app->activity->internalDataPath,
    };
    const char *names[] = {"baserom.us.z64", "sm64.us.z64"};
    for (size_t root = 0; root < 2; ++root) {
        if (roots[root] == NULL) continue;
        for (size_t name = 0; name < 2; ++name) {
            snprintf(result.path, sizeof(result.path), "%s/%s",
                roots[root], names[name]);
            if (!hash_file(result.path, result.sha1)) continue;
            result.status = strcmp(result.sha1, SM64_US_SHA1) == 0
                ? QUEST_ROM_VALID : QUEST_ROM_INVALID;
            return result;
        }
    }
    result.path[0] = '\0';
    result.sha1[0] = '\0';
    return result;
}

static uint32_t read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
        | ((uint32_t)data[2] << 8) | data[3];
}

static uint16_t read_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint8_t *decompress_mio0(const uint8_t *source, size_t source_size,
                                size_t *output_size) {
    if (source_size < 16 || read_be32(source) != 0x4d494f30U) return NULL;
    const size_t size = read_be32(source + 4);
    size_t compressed = read_be32(source + 8);
    size_t raw = read_be32(source + 12);
    size_t control = 16;
    if (size == 0 || compressed >= source_size || raw >= source_size) return NULL;
    uint8_t *output = (uint8_t *)calloc(size, 1);
    if (output == NULL) return NULL;
    size_t destination = 0;
    uint32_t control_bits = 0;
    unsigned remaining_bits = 0;
    while (destination < size) {
        if (remaining_bits == 0) {
            if (control + 4 > source_size) goto failure;
            control_bits = read_be32(source + control);
            control += 4;
            remaining_bits = 32;
        }
        if ((control_bits & 0x80000000U) != 0) {
            if (raw >= source_size) goto failure;
            output[destination++] = source[raw++];
        } else {
            if (compressed + 2 > source_size) goto failure;
            const uint16_t parameter = read_be16(source + compressed);
            compressed += 2;
            size_t count = (parameter >> 12) + 3U;
            const size_t distance = (parameter & 0x0fffU) + 1U;
            if (distance > destination || count > size - destination) goto failure;
            while (count-- > 0) {
                output[destination] = output[destination - distance];
                ++destination;
            }
        }
        control_bits <<= 1;
        --remaining_bits;
    }
    *output_size = size;
    return output;

failure:
    free(output);
    return NULL;
}

bool quest_rom_load_rgba16_texture(const QuestRomResult *rom,
    size_t physical_offset, size_t physical_size, size_t segment_offset,
    unsigned width, unsigned height, unsigned char *rgba) {
    if (rom == NULL || rom->status != QUEST_ROM_VALID || rgba == NULL) return false;
    FILE *file = fopen(rom->path, "rb");
    if (file == NULL || fseek(file, (long)physical_offset, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    uint8_t *compressed = (uint8_t *)malloc(physical_size);
    if (compressed == NULL) {
        fclose(file);
        return false;
    }
    const bool read_ok = fread(compressed, 1, physical_size, file) == physical_size;
    fclose(file);
    size_t segment_size = 0;
    uint8_t *segment = read_ok
        ? decompress_mio0(compressed, physical_size, &segment_size) : NULL;
    free(compressed);
    const size_t pixel_count = (size_t)width * height;
    if (segment == NULL || segment_offset + pixel_count * 2U > segment_size) {
        free(segment);
        return false;
    }
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint16_t pixel = read_be16(segment + segment_offset + i * 2U);
        rgba[i * 4] = (uint8_t)(((pixel >> 11) & 31U) * 255U / 31U);
        rgba[i * 4 + 1] = (uint8_t)(((pixel >> 6) & 31U) * 255U / 31U);
        rgba[i * 4 + 2] = (uint8_t)(((pixel >> 1) & 31U) * 255U / 31U);
        rgba[i * 4 + 3] = (pixel & 1U) != 0 ? 255 : 0;
    }
    free(segment);
    return true;
}
