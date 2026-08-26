#include <stdio.h>

#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_widdle_pets.h"
#include "pc/chat_commands.h"
#include "pc/lua/smlua.h"

#define WIDDLE_PET_NAME_LENGTH 128

static bool djui_widdle_pets_get_name(s32 petIndex, char* name, size_t nameLength) {
    lua_State* L = gLuaState;
    if (L == NULL || name == NULL || nameLength == 0) return false;

    int stackTop = lua_gettop(L);
    bool found = false;

    // WiddlePets keeps petTable inside its own mod environment. The table is
    // therefore intentionally invisible to native UI code, while its public
    // _G.wpets API is shared across mods. Query that API so built-in pets and
    // separately installed [PET] packs all appear in this VR-safe menu.
    lua_getglobal(L, "wpets");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "get_pet_field");
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, petIndex);
            lua_pushstring(L, "name");
            if (lua_pcall(L, 2, 1, 0) == LUA_OK &&
                lua_isstring(L, -1)) {
                snprintf(name, nameLength, "%s", lua_tostring(L, -1));
                found = name[0] != '\0';
            }
        }
    }

    lua_settop(L, stackTop);
    return found;
}

static s32 djui_widdle_pets_count(void) {
    char name[WIDDLE_PET_NAME_LENGTH];
    s32 count = 0;
    while (count < 256 &&
           djui_widdle_pets_get_name(count + 1, name, sizeof(name))) {
        count++;
    }
    return count;
}

static void djui_widdle_pets_select(struct DjuiBase* caller) {
    char petName[WIDDLE_PET_NAME_LENGTH] = { 0 };
    if (caller == NULL || !djui_widdle_pets_get_name((s32)caller->tag, petName, sizeof(petName))) {
        djui_chat_message_create("That pet is no longer available.");
        return;
    }

    char command[WIDDLE_PET_NAME_LENGTH + 16] = { 0 };
    snprintf(command, sizeof(command), "/wpets %s", petName);
    queue_chat_command(command);
    djui_panel_back();
}

static void djui_widdle_pets_clear(UNUSED struct DjuiBase* caller) {
    queue_chat_command("/wpets clear");
    djui_panel_back();
}

void djui_panel_widdle_pets_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create("Pets", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    struct DjuiBase* defaultElement = NULL;

    struct DjuiButton* clearButton = djui_button_create(
        body, "No Pet", DJUI_BUTTON_STYLE_NORMAL, djui_widdle_pets_clear);
    defaultElement = &clearButton->base;

    s32 count = djui_widdle_pets_count();
    for (s32 i = 1; i <= count; ++i) {
        char petName[WIDDLE_PET_NAME_LENGTH] = { 0 };
        if (!djui_widdle_pets_get_name(i, petName, sizeof(petName))) continue;
        struct DjuiButton* button = djui_button_create(
            body, petName, DJUI_BUTTON_STYLE_NORMAL, djui_widdle_pets_select);
        button->base.tag = i;
    }

    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    djui_panel_add(caller, panel, defaultElement);
}
