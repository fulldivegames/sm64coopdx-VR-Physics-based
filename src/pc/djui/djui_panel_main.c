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

static void djui_panel_main_create_vr_version_status(
    struct DjuiThreePanel* panel
) {
    char versionText[96] = { 0 };
    snprintf(
        versionText,
        sizeof(versionText),
        "SM64 Co-Op DX VR %s",
        get_vr_version()
    );

    struct DjuiText* version = djui_text_create(
        &panel->base,
        versionText
    );
    djui_base_set_size_type(
        &version->base,
        DJUI_SVT_ABSOLUTE,
        DJUI_SVT_ABSOLUTE
    );
    djui_base_set_size(&version->base, 460.0f, 32.0f);
    djui_base_set_alignment(
        &version->base,
        DJUI_HALIGN_LEFT,
        DJUI_VALIGN_BOTTOM
    );
    djui_base_set_location(&version->base, 12.0f, 44.0f);
    djui_base_set_color(&version->base, 220, 220, 220, 255);
    djui_text_set_alignment(
        version,
        DJUI_HALIGN_LEFT,
        DJUI_VALIGN_BOTTOM
    );
    djui_text_set_font_scale(
        version,
        version->fontScale * 0.72f
    );
    djui_text_set_font(version, gDjuiFonts[FONT_ALIASED]);
    djui_text_set_drop_shadow(version, 0, 0, 0, 180);

    const enum VrUpdateStatus status = vr_update_get_status();
    if (status == VR_UPDATE_AVAILABLE) {
        char updateText[128] = { 0 };
        snprintf(
            updateText,
            sizeof(updateText),
            "Update available: %s - Install",
            vr_update_get_latest_version()
        );
        // Keep the install action in the same flow as the title-screen
        // buttons. Attaching it directly to the three-panel root made it
        // render in the footer area without participating in VR cursor
        // navigation, leaving the visible button impossible to select.
        struct DjuiBase* body = djui_three_panel_get_body(panel);
        body->addChildrenToHead = true;
        struct DjuiButton* updateButton = djui_button_create(
            body,
            updateText,
            DJUI_BUTTON_STYLE_NORMAL,
            djui_panel_main_open_vr_releases
        );
        body->addChildrenToHead = false;
        djui_base_set_size_type(
            &updateButton->base,
            DJUI_SVT_ABSOLUTE,
            DJUI_SVT_ABSOLUTE
        );
        djui_base_set_size(&updateButton->base, 440.0f, 36.0f);
        djui_base_set_border_width(&updateButton->base, 1.0f);
        djui_text_set_font_scale(
            updateButton->text,
            updateButton->text->fontScale * 0.52f
        );
        return;
    }

    const char* statusText = "Update not checked";
    struct DjuiColor statusColor = { 170, 170, 170, 255 };
    if (status == VR_UPDATE_UP_TO_DATE) {
        statusText = "Up to date";
        statusColor = (struct DjuiColor){ 140, 255, 160, 255 };
    } else if (status == VR_UPDATE_CHECKING) {
        statusText = "Checking for updates...";
    } else if (status == VR_UPDATE_CHECK_FAILED) {
        statusText = "Update check unavailable";
        statusColor = (struct DjuiColor){ 255, 220, 140, 255 };
    }

    struct DjuiText* statusLabel = djui_text_create(
        &panel->base,
        statusText
    );
    djui_base_set_size_type(
        &statusLabel->base,
        DJUI_SVT_ABSOLUTE,
        DJUI_SVT_ABSOLUTE
    );
    djui_base_set_size(&statusLabel->base, 440.0f, 30.0f);
    djui_base_set_alignment(
        &statusLabel->base,
        DJUI_HALIGN_LEFT,
        DJUI_VALIGN_BOTTOM
    );
    djui_base_set_location(&statusLabel->base, 12.0f, 10.0f);
    djui_base_set_color(
        &statusLabel->base,
        statusColor.r,
        statusColor.g,
        statusColor.b,
        statusColor.a
    );
    djui_text_set_alignment(
        statusLabel,
        DJUI_HALIGN_LEFT,
        DJUI_VALIGN_BOTTOM
    );
    djui_text_set_font_scale(
        statusLabel,
        statusLabel->fontScale * 0.70f
    );
    djui_text_set_font(statusLabel, gDjuiFonts[FONT_ALIASED]);
    djui_text_set_drop_shadow(statusLabel, 0, 0, 0, 180);
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

            struct DjuiButton* button1 = djui_button_create(body, DLANG(MAIN, HOST), DJUI_BUTTON_STYLE_NORMAL, djui_panel_host_create);
            if (!configExCoopTheme) { djui_base_set_location(&button1->base, 0, -30); }
            djui_cursor_input_controlled_center(&button1->base);

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

        djui_panel_main_create_vr_version_status(panel);
    }

    djui_panel_add(caller, panel, NULL);
    gInteractableOverridePad = true;
    gDjuiPanelMainCreated = true;
}
