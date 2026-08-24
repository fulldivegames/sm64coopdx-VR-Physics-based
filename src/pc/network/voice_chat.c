#include "voice_chat.h"

#include <stdatomic.h>
#include <string.h>

#include "pc/configfile.h"
#include "pc/debuglog.h"
#include "pc/network/coopnet/coopnet.h"
#include "pc/network/network.h"
#include "pc/network/network_player.h"
#include "pc/network/packets/packet.h"
#include "pc/utils/miniaudio.h"

#define VOICE_RATE 16000
#define VOICE_FRAME_SAMPLES 320
#define VOICE_CAPTURE_SAMPLES 8192
#define VOICE_PLAYBACK_SAMPLES 8192
#define VOICE_PROTOCOL_VERSION 1

struct VoicePlaybackQueue {
    s16 samples[VOICE_PLAYBACK_SAMPLES];
    u32 read;
    u32 write;
};

static ma_device sCaptureDevice;
static ma_context sCaptureContext;
static bool sCaptureContextInitialized;
static ma_device_info* sCaptureDeviceInfos;
static ma_uint32 sCaptureDeviceCount;
static bool sCaptureInitialized;
static s16 sCaptureSamples[VOICE_CAPTURE_SAMPLES];
static _Atomic u32 sCaptureRead;
static _Atomic u32 sCaptureWrite;
static struct VoicePlaybackQueue sPlayback[MAX_PLAYERS];
static bool sCapable[MAX_PLAYERS];
static bool sMuted[MAX_PLAYERS];
static u8 sSpeakingFrames[MAX_PLAYERS];
static f32 sCapabilityTimer;
static u32 sCaptureRetryFrames;

bool voice_chat_session_allowed(void) {
    return gNetworkType != NT_NONE && gNetworkPlayerLocal != NULL &&
           ns_coopnet_vr_gameplay_allowed();
}

static void voice_capture_callback(ma_device* device, void* output,
                                   const void* input, ma_uint32 frameCount) {
    (void)device;
    (void)output;
    if (input == NULL || frameCount == 0) return;
    const s16* source = (const s16*)input;
    u32 write = atomic_load_explicit(&sCaptureWrite, memory_order_relaxed);
    u32 read = atomic_load_explicit(&sCaptureRead, memory_order_acquire);
    for (ma_uint32 i = 0; i < frameCount; i++) {
        if (write - read >= VOICE_CAPTURE_SAMPLES) {
            read++;
            atomic_store_explicit(&sCaptureRead, read, memory_order_release);
        }
        sCaptureSamples[write % VOICE_CAPTURE_SAMPLES] = source[i];
        write++;
    }
    atomic_store_explicit(&sCaptureWrite, write, memory_order_release);
}

void voice_chat_init(void) {
    if (sCaptureInitialized) return;
    atomic_store(&sCaptureRead, 0);
    atomic_store(&sCaptureWrite, 0);
    if (!sCaptureContextInitialized) {
        if (ma_context_init(NULL, 0, NULL, &sCaptureContext) != MA_SUCCESS) {
            LOG_ERROR("Voice chat could not initialize audio device discovery");
            return;
        }
        sCaptureContextInitialized = true;
        ma_device_info* playbackInfos = NULL;
        ma_uint32 playbackCount = 0;
        if (ma_context_get_devices(&sCaptureContext, &playbackInfos, &playbackCount,
                                   &sCaptureDeviceInfos, &sCaptureDeviceCount) != MA_SUCCESS) {
            sCaptureDeviceInfos = NULL;
            sCaptureDeviceCount = 0;
        }
    }
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = 1;
#ifndef __ANDROID__
    if (sCaptureDeviceCount > 0) {
        if (configVoiceMicDevice >= sCaptureDeviceCount) configVoiceMicDevice = 0;
        config.capture.pDeviceID = &sCaptureDeviceInfos[configVoiceMicDevice].id;
    }
#endif
    config.sampleRate = VOICE_RATE;
    config.periodSizeInFrames = VOICE_FRAME_SAMPLES;
    config.dataCallback = voice_capture_callback;
    if (ma_device_init(sCaptureContextInitialized ? &sCaptureContext : NULL,
                       &config, &sCaptureDevice) != MA_SUCCESS) {
        LOG_ERROR("Voice chat could not open the headset microphone");
        return;
    }
    if (ma_device_start(&sCaptureDevice) != MA_SUCCESS) {
        LOG_ERROR("Voice chat could not start the headset microphone");
        ma_device_uninit(&sCaptureDevice);
        return;
    }
    sCaptureInitialized = true;
    LOG_INFO("Voice chat microphone started at %d Hz", VOICE_RATE);
}

void voice_chat_shutdown(void) {
    if (sCaptureInitialized) {
        ma_device_uninit(&sCaptureDevice);
        sCaptureInitialized = false;
    }
    if (sCaptureContextInitialized) {
        ma_context_uninit(&sCaptureContext);
        sCaptureContextInitialized = false;
        sCaptureDeviceInfos = NULL;
        sCaptureDeviceCount = 0;
    }
}

u32 voice_chat_input_device_count(void) {
#ifdef __ANDROID__
    return 1;
#else
    return sCaptureDeviceCount > 0 ? (u32)sCaptureDeviceCount : 1;
#endif
}

const char* voice_chat_input_device_name(u32 index) {
#ifdef __ANDROID__
    (void)index;
    return "Quest Headset Microphone";
#else
    if (sCaptureDeviceInfos == NULL || index >= sCaptureDeviceCount) return "Default Microphone";
    return sCaptureDeviceInfos[index].name;
#endif
}

void voice_chat_select_input_device(u32 index) {
#ifndef __ANDROID__
    if (sCaptureDeviceCount > 0 && index >= sCaptureDeviceCount) index = 0;
    configVoiceMicDevice = index;
    if (sCaptureInitialized) {
        ma_device_uninit(&sCaptureDevice);
        sCaptureInitialized = false;
    }
    voice_chat_init();
#else
    (void)index;
#endif
}

static u8 mulaw_encode(s16 sample) {
    const int bias = 0x84;
    int value = sample;
    int sign = (value < 0) ? 0x80 : 0;
    if (value < 0) value = -value;
    if (value > 32635) value = 32635;
    value += bias;
    int exponent = 7;
    for (int mask = 0x4000; exponent > 0 && !(value & mask); mask >>= 1) exponent--;
    int mantissa = (value >> (exponent + 3)) & 0x0F;
    return (u8)~(sign | (exponent << 4) | mantissa);
}

static s16 mulaw_decode(u8 value) {
    value = (u8)~value;
    int sample = ((value & 0x0F) << 3) + 0x84;
    sample <<= (value & 0x70) >> 4;
    sample -= 0x84;
    return (s16)((value & 0x80) ? -sample : sample);
}

static void voice_send_capability(void) {
    struct Packet packet = { 0 };
    packet_init(&packet, PACKET_VR_VOICE_CAPABILITY, false, PLMT_NONE);
    u8 globalIndex = gNetworkPlayerLocal->globalIndex;
    u8 version = VOICE_PROTOCOL_VERSION;
    packet_write(&packet, &globalIndex, sizeof(globalIndex));
    packet_write(&packet, &version, sizeof(version));
    network_send(&packet);
    sCapable[globalIndex] = true;
}

static void voice_send_frame(const s16* samples) {
    u8 encoded[VOICE_FRAME_SAMPLES];
    const float gain = (float)configVoiceMicLevel / 100.0f;
    for (u32 i = 0; i < VOICE_FRAME_SAMPLES; i++) {
        int scaled = (int)((float)samples[i] * gain);
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        encoded[i] = mulaw_encode((s16)scaled);
    }
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        struct NetworkPlayer* player = &gNetworkPlayers[i];
        if (!player->connected || player == gNetworkPlayerLocal ||
            player->globalIndex >= MAX_PLAYERS ||
            sMuted[player->globalIndex] ||
            !sCapable[player->globalIndex]) continue;
        struct Packet packet = { 0 };
        packet_init(&packet, PACKET_VR_VOICE_AUDIO, false, PLMT_NONE);
        u8 globalIndex = gNetworkPlayerLocal->globalIndex;
        packet_write(&packet, &globalIndex, sizeof(globalIndex));
        packet_write(&packet, encoded, sizeof(encoded));
        network_send_to(player->localIndex, &packet);
    }
}

void voice_chat_update(void) {
    static bool wasAllowed;
    bool allowed = voice_chat_session_allowed();
    for (u32 i = 0; i < MAX_PLAYERS; i++) {
        if (sSpeakingFrames[i] > 0) sSpeakingFrames[i]--;
    }
    if (!allowed) {
        if (wasAllowed) {
            memset(sCapable, 0, sizeof(sCapable));
            memset(sPlayback, 0, sizeof(sPlayback));
            sCapabilityTimer = 0.0f;
        }
        wasAllowed = false;
        atomic_store(&sCaptureRead, atomic_load(&sCaptureWrite));
        return;
    }
    if (!sCaptureInitialized && ++sCaptureRetryFrames >= 150) {
        sCaptureRetryFrames = 0;
        voice_chat_init();
    } else if (sCaptureInitialized) {
        sCaptureRetryFrames = 0;
    }
    if (!wasAllowed) {
        voice_send_capability();
        sCapabilityTimer = 0.0f;
    }
    wasAllowed = true;
    sCapabilityTimer += 1.0f / 30.0f;
    if (sCapabilityTimer >= 5.0f) {
        sCapabilityTimer = 0.0f;
        voice_send_capability();
    }

    u32 read = atomic_load_explicit(&sCaptureRead, memory_order_relaxed);
    u32 write = atomic_load_explicit(&sCaptureWrite, memory_order_acquire);
    while (write - read >= VOICE_FRAME_SAMPLES) {
        s16 frame[VOICE_FRAME_SAMPLES];
        for (u32 i = 0; i < VOICE_FRAME_SAMPLES; i++) {
            frame[i] = sCaptureSamples[(read + i) % VOICE_CAPTURE_SAMPLES];
        }
        read += VOICE_FRAME_SAMPLES;
        if (!configVoiceMicMuted && configVoiceMicLevel > 0) voice_send_frame(frame);
    }
    atomic_store_explicit(&sCaptureRead, read, memory_order_release);
}

void voice_chat_receive_capability(struct Packet* packet) {
    u8 globalIndex = UNKNOWN_GLOBAL_INDEX;
    u8 version = 0;
    packet_read(packet, &globalIndex, sizeof(globalIndex));
    packet_read(packet, &version, sizeof(version));
    if (packet->error || globalIndex >= MAX_PLAYERS ||
        packet_spoofed(packet, globalIndex) || !voice_chat_session_allowed()) return;
    sCapable[globalIndex] = version == VOICE_PROTOCOL_VERSION;
}

void voice_chat_receive_audio(struct Packet* packet) {
    u8 globalIndex = UNKNOWN_GLOBAL_INDEX;
    u8 encoded[VOICE_FRAME_SAMPLES];
    packet_read(packet, &globalIndex, sizeof(globalIndex));
    packet_read(packet, encoded, sizeof(encoded));
    if (packet->error || globalIndex >= MAX_PLAYERS || sMuted[globalIndex] ||
        packet_spoofed(packet, globalIndex) || !voice_chat_session_allowed()) return;
    sCapable[globalIndex] = true;
    struct VoicePlaybackQueue* queue = &sPlayback[globalIndex];
    int peak = 0;
    for (u32 i = 0; i < VOICE_FRAME_SAMPLES; i++) {
        if (queue->write - queue->read >= VOICE_PLAYBACK_SAMPLES) queue->read++;
        s16 decoded = mulaw_decode(encoded[i]);
        int magnitude = decoded < 0 ? -decoded : decoded;
        if (magnitude > peak) peak = magnitude;
        queue->samples[queue->write % VOICE_PLAYBACK_SAMPLES] = decoded;
        queue->write++;
    }
    if (peak >= 700) sSpeakingFrames[globalIndex] = 12;
}

void voice_chat_mix_output(s16* stereoSamples, size_t frameCount) {
    if (stereoSamples == NULL || !voice_chat_session_allowed() ||
        configVoicePlayerVolume == 0) return;
    const float gain = (float)configVoicePlayerVolume / 100.0f;
    for (size_t frame = 0; frame < frameCount; frame++) {
        int mixed = 0;
        for (u32 player = 0; player < MAX_PLAYERS; player++) {
            struct VoicePlaybackQueue* queue = &sPlayback[player];
            if (sMuted[player] || queue->write == queue->read) continue;
            // Game audio is 32 kHz; consume one 16 kHz voice sample every two frames.
            s16 sample = queue->samples[queue->read % VOICE_PLAYBACK_SAMPLES];
            mixed += (int)((float)sample * gain);
            if (frame & 1) queue->read++;
        }
        int left = stereoSamples[frame * 2] + mixed;
        int right = stereoSamples[frame * 2 + 1] + mixed;
        stereoSamples[frame * 2] = (s16)(left > 32767 ? 32767 : (left < -32768 ? -32768 : left));
        stereoSamples[frame * 2 + 1] = (s16)(right > 32767 ? 32767 : (right < -32768 ? -32768 : right));
    }
}

bool voice_chat_player_muted(u8 globalIndex) {
    return globalIndex < MAX_PLAYERS && sMuted[globalIndex];
}

bool voice_chat_player_speaking(u8 globalIndex) {
    return globalIndex < MAX_PLAYERS && sSpeakingFrames[globalIndex] > 0;
}

void voice_chat_set_player_muted(u8 globalIndex, bool muted) {
    if (globalIndex >= MAX_PLAYERS) return;
    sMuted[globalIndex] = muted;
    if (muted) memset(&sPlayback[globalIndex], 0, sizeof(sPlayback[globalIndex]));
}
