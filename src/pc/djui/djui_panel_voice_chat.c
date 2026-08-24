#include "djui_panel_voice_chat.h"

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "pc/configfile.h"
#include "pc/network/voice_chat.h"

#define VOICE_DEVICE_MENU_MAX 32

static char* sDeviceNames[VOICE_DEVICE_MENU_MAX];

static void voice_device_changed(UNUSED struct DjuiBase* caller) {
    voice_chat_select_input_device(configVoiceMicDevice);
}

void djui_panel_voice_chat_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Voice Chat", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    djui_checkbox_create(body, "Mute My Microphone", &configVoiceMicMuted, NULL);
    djui_slider_create(body, "My Microphone Level", &configVoiceMicLevel,
                       0, 100, NULL);
    djui_slider_create(body, "Player Voice Volume", &configVoicePlayerVolume,
                       0, 100, NULL);
#ifndef __ANDROID__
    u32 count = voice_chat_input_device_count();
    if (count > VOICE_DEVICE_MENU_MAX) count = VOICE_DEVICE_MENU_MAX;
    for (u32 i = 0; i < count; i++) sDeviceNames[i] = (char*)voice_chat_input_device_name(i);
    if (configVoiceMicDevice >= count) configVoiceMicDevice = 0;
    djui_selectionbox_create(body, "Microphone Source", sDeviceNames, count,
                             &configVoiceMicDevice, voice_device_changed);
#else
    struct DjuiText* mic = djui_text_create(body, "Microphone: Quest Headset");
    djui_base_set_size_type(&mic->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
    djui_base_set_size(&mic->base, 1.0f, 32.0f);
#endif
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
                       djui_panel_menu_back);
    djui_panel_add(caller, panel, NULL);
}
