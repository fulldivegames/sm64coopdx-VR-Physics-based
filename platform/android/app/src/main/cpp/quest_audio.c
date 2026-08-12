#include "quest_audio.h"

#include <android/log.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "pc/utils/miniaudio.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR", __VA_ARGS__)
#define QUEST_AUDIO_FRAMES 32768u

static ma_device sDevice;
static int16_t sSamples[QUEST_AUDIO_FRAMES * 2u];
static _Atomic uint32_t sReadFrame;
static _Atomic uint32_t sWriteFrame;
static bool sInitialized;

static void quest_audio_callback(ma_device *device, void *output,
                                 const void *input, ma_uint32 frame_count) {
    (void)device;
    (void)input;
    int16_t *destination = (int16_t *)output;
    uint32_t read = atomic_load_explicit(&sReadFrame, memory_order_relaxed);
    const uint32_t write = atomic_load_explicit(&sWriteFrame, memory_order_acquire);
    uint32_t available = write - read;
    const uint32_t copy_count = available < frame_count ? available : frame_count;
    for (uint32_t frame = 0; frame < copy_count; ++frame) {
        const uint32_t index = ((read + frame) % QUEST_AUDIO_FRAMES) * 2u;
        destination[frame * 2u] = sSamples[index];
        destination[frame * 2u + 1u] = sSamples[index + 1u];
    }
    if (copy_count < frame_count) {
        memset(destination + copy_count * 2u, 0,
               (frame_count - copy_count) * 2u * sizeof(int16_t));
    }
    atomic_store_explicit(&sReadFrame, read + copy_count, memory_order_release);
}

static bool quest_audio_init(void) {
    atomic_store(&sReadFrame, 0u);
    atomic_store(&sWriteFrame, 0u);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = 32000;
    config.periodSizeInFrames = 512;
    config.periods = 3;
    config.dataCallback = quest_audio_callback;
    if (ma_device_init(NULL, &config, &sDevice) != MA_SUCCESS) {
        LOGE("Could not initialize Android audio output.");
        return false;
    }
    if (ma_device_start(&sDevice) != MA_SUCCESS) {
        LOGE("Could not start Android audio output.");
        ma_device_uninit(&sDevice);
        return false;
    }
    sInitialized = true;
    LOGI("Android audio output started at 32000 Hz stereo.");
    return true;
}

static int quest_audio_buffered(void) {
    return (int)(atomic_load_explicit(&sWriteFrame, memory_order_acquire) -
                 atomic_load_explicit(&sReadFrame, memory_order_acquire));
}

static int quest_audio_desired(void) { return 1100; }

static void quest_audio_play(const uint8_t *buffer, size_t length) {
    if (!sInitialized || buffer == NULL || length < 4u) return;
    const int16_t *source = (const int16_t *)buffer;
    uint32_t frames = (uint32_t)(length / 4u);
    uint32_t write = atomic_load_explicit(&sWriteFrame, memory_order_relaxed);
    const uint32_t read = atomic_load_explicit(&sReadFrame, memory_order_acquire);
    const uint32_t free_frames = QUEST_AUDIO_FRAMES - (write - read);
    if (frames > free_frames) frames = free_frames;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const uint32_t index = ((write + frame) % QUEST_AUDIO_FRAMES) * 2u;
        sSamples[index] = source[frame * 2u];
        sSamples[index + 1u] = source[frame * 2u + 1u];
    }
    atomic_store_explicit(&sWriteFrame, write + frames, memory_order_release);
}

static void quest_audio_shutdown(void) {
    if (!sInitialized) return;
    ma_device_uninit(&sDevice);
    sInitialized = false;
}

struct AudioAPI audio_quest = {
    quest_audio_init,
    quest_audio_buffered,
    quest_audio_desired,
    quest_audio_play,
    quest_audio_shutdown,
};
