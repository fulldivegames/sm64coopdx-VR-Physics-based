#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_host.h"
#include "djui_panel_join.h"
#include "djui_panel_options.h"
#include "djui_panel_vr.h"
#include "djui_panel_menu.h"
#include "djui_panel_confirm.h"
#include "djui_hud_utils.h"
#include "pc/controller/controller_sdl.h"
#include "pc/pc_main.h"
#include "pc/network/version.h"
#include "pc/release_notes.h"
#include "pc/update_checker.h"
#include "pc/utils/misc.h"

extern ALIGNED8 u8 texture_coopdx_logo[];

bool gDjuiPanelMainCreated = false;

static void djui_panel_main_quit_yes(UNUSED struct DjuiBase* caller) {
    game_exit();
}

static void djui_panel_main_quit(struct DjuiBase* caller) {
    djui_panel_confirm_create(caller,
                              DLANG(MAIN, QUIT_TITLE),
                              DLANG(MAIN, QUIT_CONFIRM),
                              djui_panel_main_quit_yes);
}

static void djui_panel_main_release_notes_create(UNUSED struct DjuiBase* caller) {
    vr_release_notes_load();
    struct DjuiThreePanel* panel = djui_panel_menu_create("Release Notes", false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    struct DjuiPaginated* paginated = djui_paginated_create(body, 1);
    for (int32_t page = 0; page < vr_release_notes_page_count(); page++) {
        struct DjuiText* text = djui_text_create(
            &paginated->layout->base,
            vr_release_notes_page_get(page)
        );
        djui_base_set_size_type(&text->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&text->base, 1.0f, 340.0f);
        djui_text_set_alignment(text, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
        djui_text_set_font(text, gDjuiFonts[FONT_ALIASED]);
        djui_text_set_font_scale(text, text->fontScale * 0.56f);
        djui_text_set_drop_shadow(text, 0, 0, 0, 180);
    }
    djui_paginated_calculate_height(paginated);
    djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
                       djui_panel_menu_back);
    djui_three_panel_recalculate_body_size(panel);
    djui_panel_add(caller, panel, NULL);
}
static void djui_panel_main_open_vr_releases(
    UNUSED struct DjuiBase* caller
) {
    struct DjuiButton* button = (struct DjuiButton*) caller;
    djui_text_set_text(button->text, "Preparing update...");
    djui_base_set_enabled(&button->base, false);
    if (!vr_update_install_latest()) {
        djui_text_set_text(button->text, "Update could not start - Retry");
        djui_base_set_enabled(&button->base, true);
    }
}

static struct DjuiButton* djui_panel_main_create_vr_update_button(
    struct DjuiBase* body
) {
    if (vr_update_get_status() != VR_UPDATE_AVAILABLE) { return NULL; }

    // Use the same default button geometry and styling as Host, Join, and
    // Release Notes so the updater is a normal directional menu item.
    return djui_button_create(
        body,
        "Install Update",
        DJUI_BUTTON_STYLE_NORMAL,
        djui_panel_main_open_vr_releases
    );
}

void djui_panel_main_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(configExCoopTheme ? "\\#ff0800\\SM\\#1be700\\64\\#00b3ff\\EX\n\\#ffef00\\COOP" : "", false);
    {
        struct DjuiBase* body = djui_three_panel_get_body(panel);
        {
            if (!configExCoopTheme) {
                struct DjuiImage* logo = djui_image_create(body, texture_coopdx_logo, 2048, 1024, G_IM_FMT_RGBA, G_IM_SIZ_32b);
                djui_image_set_linear_filter(logo, true);
                if (configDjuiThemeCenter) {
                    djui_base_set_size(&logo->base, 550, 275);
                } else {
                    djui_base_set_size(&logo->base, 480, 240);
                }
                djui_base_set_alignment(&logo->base, DJUI_HALIGN_CENTER, DJUI_VALIGN_TOP);
                djui_base_set_location_type(&logo->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
                djui_base_set_location(&logo->base, 0, -30);
            }
            // The updater is the first normal main-menu item when available.
            struct DjuiButton* updateButton = djui_panel_main_create_vr_update_button(body);
            if (updateButton != NULL && !configExCoopTheme) {
                djui_base_set_location(&updateButton->base, 0, -30);
            }

            struct DjuiButton* button1 = djui_button_create(body, DLANG(MAIN, HOST), DJUI_BUTTON_STYLE_NORMAL, djui_panel_host_create);
            if (!configExCoopTheme) { djui_base_set_location(&button1->base, 0, -30); }
            if (updateButton != NULL) {
                djui_cursor_input_controlled_center(&updateButton->base);
            } else {
                djui_cursor_input_controlled_center(&button1->base);
            }

            struct DjuiButton* button2 = djui_button_create(body, DLANG(MAIN, JOIN), DJUI_BUTTON_STYLE_NORMAL, djui_panel_join_create);
            if (!configExCoopTheme) { djui_base_set_location(&button2->base, 0, -30); }
            struct DjuiButton* vrButton = djui_button_create(body, "VR SETTINGS", DJUI_BUTTON_STYLE_NORMAL, djui_panel_vr_create);
            if (!configExCoopTheme) { djui_base_set_location(&vrButton->base, 0, -30); }
            struct DjuiButton* button3 = djui_button_create(body, DLANG(MAIN, OPTIONS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_options_create);
            if (!configExCoopTheme) { djui_base_set_location(&button3->base, 0, -30); }
            struct DjuiButton* releaseNotesButton = djui_button_create(body, "Release Notes", DJUI_BUTTON_STYLE_NORMAL, djui_panel_main_release_notes_create);
            if (!configExCoopTheme) { djui_base_set_location(&releaseNotesButton->base, 0, -30); }
            struct DjuiButton* button4 = djui_button_create(body, DLANG(MAIN, QUIT), DJUI_BUTTON_STYLE_BACK, djui_panel_main_quit);
            if (!configExCoopTheme) { djui_base_set_location(&button4->base, 0, -30); }
        }

    }

    djui_panel_add(caller, panel, NULL);
    gInteractableOverridePad = true;
    gDjuiPanelMainCreated = true;
}
