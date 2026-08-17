#pragma once
#include "djui.h"

struct DjuiChatBox {
    struct DjuiBase base;
    struct DjuiRect* chatContainer;
    struct DjuiFlowLayout* chatFlow;
    struct DjuiInputbox* chatInput;
    struct DjuiButton* backButton;
    bool scrolling;
    f32 scrollY;
};

extern struct DjuiChatBox* gDjuiChatBox;
extern bool gDjuiChatBoxFocus;

void djui_chat_box_toggle(void);
void djui_chat_box_open_menu(struct DjuiBase* caller);
void djui_chat_box_close_menu(void);
bool djui_chat_box_is_menu_mode(void);
void djui_chat_box_focus_back_button(void);
void djui_chat_box_focus_input(void);
struct DjuiChatBox* djui_chat_box_create(void);
