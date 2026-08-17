#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "djui.h"
#include "djui_unicode.h"
#include "djui_hud_utils.h"
#include "pc/gfx/gfx_window_manager_api.h"
#include "pc/pc_main.h"
#include "game/segment2.h"
#include "pc/controller/controller_keyboard.h"

#define DJUI_INPUTBOX_YOFF (-3)
#define DJUI_INPUTBOX_MAX_BLINK 50
#define DJUI_INPUTBOX_MID_BLINK (DJUI_INPUTBOX_MAX_BLINK / 2)
#define DJUI_INPUTBOX_CURSOR_WIDTH (2.0f / 32.0f)

u8 gDjuiInputHeldShift   = 0;
u8 gDjuiInputHeldControl = 0;
u8 gDjuiInputHeldAlt     = 0;
static u8 sCursorBlink = 0;

enum DjuiKeyboardAction {
    DJUI_KEY_CHAR,
    DJUI_KEY_ESCAPE,
    DJUI_KEY_BACKSPACE,
    DJUI_KEY_TAB,
    DJUI_KEY_CAPS,
    DJUI_KEY_ENTER,
    DJUI_KEY_SHIFT,
    DJUI_KEY_SPACE,
    DJUI_KEY_LEFT,
    DJUI_KEY_RIGHT,
    DJUI_KEY_DELETE,
};

struct DjuiKeyboardKey {
    const char* label;
    const char* normal;
    const char* shifted;
    f32 width;
    enum DjuiKeyboardAction action;
};

#define KEY_CHAR(label, normal, shifted) { label, normal, shifted, 1.0f, DJUI_KEY_CHAR }
#define KEY_WIDE(label, width, action) { label, NULL, NULL, width, action }

static const struct DjuiKeyboardKey sKeyboardRow0[] = {
    KEY_WIDE("Esc", 1.4f, DJUI_KEY_ESCAPE),
    KEY_CHAR("1", "1", "!"), KEY_CHAR("2", "2", "@"),
    KEY_CHAR("3", "3", "#"), KEY_CHAR("4", "4", "$"),
    KEY_CHAR("5", "5", "%"), KEY_CHAR("6", "6", "^"),
    KEY_CHAR("7", "7", "&"), KEY_CHAR("8", "8", "*"),
    KEY_CHAR("9", "9", "("), KEY_CHAR("0", "0", ")"),
    KEY_CHAR("-", "-", "_"), KEY_CHAR("=", "=", "+"),
    KEY_WIDE("Backspace", 2.4f, DJUI_KEY_BACKSPACE),
};

static const struct DjuiKeyboardKey sKeyboardRow1[] = {
    KEY_WIDE("Tab", 1.5f, DJUI_KEY_TAB),
    KEY_CHAR("Q", "q", "Q"), KEY_CHAR("W", "w", "W"),
    KEY_CHAR("E", "e", "E"), KEY_CHAR("R", "r", "R"),
    KEY_CHAR("T", "t", "T"), KEY_CHAR("Y", "y", "Y"),
    KEY_CHAR("U", "u", "U"), KEY_CHAR("I", "i", "I"),
    KEY_CHAR("O", "o", "O"), KEY_CHAR("P", "p", "P"),
    KEY_CHAR("[", "[", "{"), KEY_CHAR("]", "]", "}"),
    { "\\", "\\", "|", 1.5f, DJUI_KEY_CHAR },
};

static const struct DjuiKeyboardKey sKeyboardRow2[] = {
    KEY_WIDE("Caps", 1.8f, DJUI_KEY_CAPS),
    KEY_CHAR("A", "a", "A"), KEY_CHAR("S", "s", "S"),
    KEY_CHAR("D", "d", "D"), KEY_CHAR("F", "f", "F"),
    KEY_CHAR("G", "g", "G"), KEY_CHAR("H", "h", "H"),
    KEY_CHAR("J", "j", "J"), KEY_CHAR("K", "k", "K"),
    KEY_CHAR("L", "l", "L"), KEY_CHAR(";", ";", ":"),
    { "'", "'", "\"", 1.0f, DJUI_KEY_CHAR },
    KEY_WIDE("Enter", 2.2f, DJUI_KEY_ENTER),
};

static const struct DjuiKeyboardKey sKeyboardRow3[] = {
    KEY_WIDE("Shift", 2.2f, DJUI_KEY_SHIFT),
    KEY_CHAR("Z", "z", "Z"), KEY_CHAR("X", "x", "X"),
    KEY_CHAR("C", "c", "C"), KEY_CHAR("V", "v", "V"),
    KEY_CHAR("B", "b", "B"), KEY_CHAR("N", "n", "N"),
    KEY_CHAR("M", "m", "M"), KEY_CHAR(",", ",", "<"),
    KEY_CHAR(".", ".", ">"), KEY_CHAR("/", "/", "?"),
    KEY_WIDE("Shift", 2.8f, DJUI_KEY_SHIFT),
};

static const struct DjuiKeyboardKey sKeyboardRow4[] = {
    KEY_WIDE("Cancel", 2.0f, DJUI_KEY_ESCAPE),
    KEY_WIDE("Space", 8.8f, DJUI_KEY_SPACE),
    KEY_WIDE("Left", 1.3f, DJUI_KEY_LEFT),
    KEY_WIDE("Right", 1.3f, DJUI_KEY_RIGHT),
    KEY_WIDE("Delete", 1.6f, DJUI_KEY_DELETE),
};

#undef KEY_CHAR
#undef KEY_WIDE

struct DjuiKeyboardRow {
    const struct DjuiKeyboardKey* keys;
    u8 count;
};

static const struct DjuiKeyboardRow sKeyboardRows[] = {
    { sKeyboardRow0, sizeof(sKeyboardRow0) / sizeof(sKeyboardRow0[0]) },
    { sKeyboardRow1, sizeof(sKeyboardRow1) / sizeof(sKeyboardRow1[0]) },
    { sKeyboardRow2, sizeof(sKeyboardRow2) / sizeof(sKeyboardRow2[0]) },
    { sKeyboardRow3, sizeof(sKeyboardRow3) / sizeof(sKeyboardRow3[0]) },
    { sKeyboardRow4, sizeof(sKeyboardRow4) / sizeof(sKeyboardRow4[0]) },
};

static struct DjuiInputbox* sKeyboardTarget;
static u8 sKeyboardRow;
static u8 sKeyboardColumn;
static s8 sKeyboardHeldDirection;
static u8 sKeyboardHeldFrames;
static bool sKeyboardShift;
static bool sKeyboardCaps;

static const struct DjuiKeyboardKey* djui_keyboard_selected_key(void) {
    const struct DjuiKeyboardRow* row = &sKeyboardRows[sKeyboardRow];
    if (sKeyboardColumn >= row->count) sKeyboardColumn = row->count - 1;
    return &row->keys[sKeyboardColumn];
}

static void djui_keyboard_move_horizontal(s8 direction) {
    const u8 count = sKeyboardRows[sKeyboardRow].count;
    sKeyboardColumn = (sKeyboardColumn + count + direction) % count;
}

static void djui_keyboard_move_vertical(s8 direction) {
    const struct DjuiKeyboardRow* previousRow = &sKeyboardRows[sKeyboardRow];
    f32 totalPreviousWidth = 0.0f;
    f32 selectedStart = 0.0f;
    for (u8 i = 0; i < previousRow->count; ++i) {
        if (i == sKeyboardColumn) selectedStart = totalPreviousWidth;
        totalPreviousWidth += previousRow->keys[i].width;
    }
    const f32 selectedCenter =
        (selectedStart + previousRow->keys[sKeyboardColumn].width * 0.5f) /
        totalPreviousWidth;

    const u8 rowCount = sizeof(sKeyboardRows) / sizeof(sKeyboardRows[0]);
    sKeyboardRow = (sKeyboardRow + rowCount + direction) % rowCount;

    // Select the key whose physical horizontal span contains the prior key's
    // center. This makes vertical navigation spatially predictable across
    // unequal-width rows (for example B -> Space rather than B -> Left).
    const struct DjuiKeyboardRow* nextRow = &sKeyboardRows[sKeyboardRow];
    f32 totalNextWidth = 0.0f;
    for (u8 i = 0; i < nextRow->count; ++i) {
        totalNextWidth += nextRow->keys[i].width;
    }
    f32 cursor = 0.0f;
    u8 closest = 0;
    f32 closestDistance = 1000.0f;
    for (u8 i = 0; i < nextRow->count; ++i) {
        const f32 start = cursor / totalNextWidth;
        cursor += nextRow->keys[i].width;
        const f32 end = cursor / totalNextWidth;
        if (selectedCenter >= start && selectedCenter <= end) {
            sKeyboardColumn = i;
            return;
        }
        const f32 distance = selectedCenter < start
            ? start - selectedCenter : selectedCenter - end;
        if (distance < closestDistance) {
            closestDistance = distance;
            closest = i;
        }
    }
    sKeyboardColumn = closest;
}

static void djui_keyboard_move_direction(s8 direction) {
    if (direction == -1) djui_keyboard_move_horizontal(-1);
    else if (direction == 1) djui_keyboard_move_horizontal(1);
    else if (direction == -2) djui_keyboard_move_vertical(-1);
    else if (direction == 2) djui_keyboard_move_vertical(1);
}

static void djui_keyboard_type_selected(void) {
    if (sKeyboardTarget == NULL) return;
    const struct DjuiKeyboardKey* key = djui_keyboard_selected_key();
    char text[8] = { 0 };
    switch (key->action) {
        case DJUI_KEY_CHAR: {
            const char* value = sKeyboardShift ? key->shifted : key->normal;
            if (value == NULL) return;
            snprintf(text, sizeof(text), "%s", value);
            if (isalpha((unsigned char)text[0])) {
                const bool uppercase = sKeyboardCaps != sKeyboardShift;
                text[0] = uppercase ? toupper((unsigned char)text[0])
                                    : tolower((unsigned char)text[0]);
            }
            djui_interactable_on_text_input(text);
            sKeyboardShift = false;
        } break;
        case DJUI_KEY_ESCAPE:
            djui_interactable_on_key_down(SCANCODE_ESCAPE);
            break;
        case DJUI_KEY_BACKSPACE:
            djui_interactable_on_key_down(SCANCODE_BACKSPACE);
            break;
        case DJUI_KEY_TAB:
            snprintf(text, sizeof(text), "    ");
            djui_interactable_on_text_input(text);
            break;
        case DJUI_KEY_CAPS:
            sKeyboardCaps = !sKeyboardCaps;
            break;
        case DJUI_KEY_ENTER:
            djui_interactable_on_key_down(SCANCODE_ENTER);
            break;
        case DJUI_KEY_SHIFT:
            sKeyboardShift = !sKeyboardShift;
            break;
        case DJUI_KEY_SPACE:
            snprintf(text, sizeof(text), " ");
            djui_interactable_on_text_input(text);
            break;
        case DJUI_KEY_LEFT:
            djui_interactable_on_key_down(SCANCODE_LEFT);
            break;
        case DJUI_KEY_RIGHT:
            djui_interactable_on_key_down(SCANCODE_RIGHT);
            break;
        case DJUI_KEY_DELETE:
            djui_interactable_on_key_down(SCANCODE_DELETE);
            break;
    }
}

bool djui_inputbox_onscreen_keyboard_is_active(void) {
    return sKeyboardTarget != NULL &&
           gInteractableFocus == &sKeyboardTarget->base;
}

void djui_inputbox_onscreen_keyboard_update(OSContPad* pad, u16 pressed) {
    if (!djui_inputbox_onscreen_keyboard_is_active() || pad == NULL) return;

    if (pressed & PAD_BUTTON_B) {
        djui_interactable_on_key_down(SCANCODE_ESCAPE);
        return;
    }

    s8 heldDirection = 0;
    if ((pad->button & L_JPAD) != 0) heldDirection = -1;
    else if ((pad->button & R_JPAD) != 0) heldDirection = 1;
    else if ((pad->button & U_JPAD) != 0) heldDirection = -2;
    else if ((pad->button & D_JPAD) != 0) heldDirection = 2;
    else if (abs(pad->stick_x) > 45 &&
             abs(pad->stick_x) > abs(pad->stick_y)) {
        heldDirection = pad->stick_x < 0 ? -1 : 1;
    } else if (abs(pad->stick_y) > 45) {
        heldDirection = pad->stick_y > 0 ? -2 : 2;
    }

    if (heldDirection == 0) {
        sKeyboardHeldDirection = 0;
        sKeyboardHeldFrames = 0;
    } else {
        const bool changed = heldDirection != sKeyboardHeldDirection;
        if (changed) {
            sKeyboardHeldDirection = heldDirection;
            sKeyboardHeldFrames = 0;
        } else if (sKeyboardHeldFrames < 255) {
            ++sKeyboardHeldFrames;
        }
        // Move immediately, then repeat after a short deliberate delay at a
        // steady rate while the stick or D-pad remains held.
        const bool repeat = changed ||
            (sKeyboardHeldFrames >= 10 &&
             ((sKeyboardHeldFrames - 10) % 3) == 0);
        if (repeat) {
            djui_keyboard_move_direction(heldDirection);
        }
    }

    if (pressed & PAD_BUTTON_A) djui_keyboard_type_selected();
}

static const char* djui_keyboard_display_label(
        const struct DjuiKeyboardKey* key, char label[8]) {
    if (key->action != DJUI_KEY_CHAR || key->normal == NULL) return key->label;
    const char* value = sKeyboardShift ? key->shifted : key->normal;
    snprintf(label, 8, "%s", value != NULL ? value : key->label);
    if (isalpha((unsigned char)label[0])) {
        const bool uppercase = sKeyboardCaps != sKeyboardShift;
        label[0] = uppercase ? toupper((unsigned char)label[0])
                             : tolower((unsigned char)label[0]);
    }
    return label;
}

void djui_inputbox_onscreen_keyboard_render(void) {
    if (!djui_inputbox_onscreen_keyboard_is_active()) return;

    // Isolate the keyboard's many alternating rectangle/font draws from the
    // surrounding menu display list without changing its original depth,
    // font, scale, or layout.
    gDPPipeSync(gDisplayListHead++);
    djui_reset_hud_params();
    djui_hud_set_resolution(RESOLUTION_DJUI);
    // The standalone keyboard is viewed at a fixed distance in the headset.
    // Use the 2x aliased atlas so its labels remain crisp after projection.
    djui_hud_set_font(FONT_ALIASED);
    const f32 screenWidth = djui_hud_get_screen_width();
    const f32 screenHeight = djui_hud_get_screen_height();
    const f32 panelWidth = fminf(760.0f, screenWidth - 32.0f);
    const f32 panelHeight = 286.0f;
    const f32 panelX = (screenWidth - panelWidth) * 0.5f;
    const f32 panelY = screenHeight - panelHeight - 18.0f;
    const f32 padding = 12.0f;
    const f32 keyGap = 4.0f;
    const f32 keyHeight = 39.0f;
    const f32 firstRowY = panelY + 58.0f;
    djui_hud_set_color(4, 8, 14, 225);
    djui_hud_render_rect(panelX - 3.0f, panelY - 3.0f,
                         panelWidth + 6.0f, panelHeight + 6.0f);
    djui_hud_set_color(17, 25, 38, 248);
    djui_hud_render_rect(panelX, panelY, panelWidth, panelHeight);
    djui_hud_set_color(31, 46, 67, 255);
    djui_hud_render_rect(panelX + padding, panelY + 10.0f,
                         panelWidth - padding * 2.0f, 38.0f);

    const char* visibleText = sKeyboardTarget->buffer;
    const size_t textLength = strlen(visibleText);
    if (textLength > 54) visibleText += textLength - 54;
    djui_hud_set_color(224, 234, 246, 255);
    djui_hud_print_text(visibleText, panelX + 22.0f, panelY + 18.0f,
                        0.55f, 0.55f);

    for (u8 rowIndex = 0;
         rowIndex < sizeof(sKeyboardRows) / sizeof(sKeyboardRows[0]);
         ++rowIndex) {
        const struct DjuiKeyboardRow* row = &sKeyboardRows[rowIndex];
        f32 totalUnits = 0.0f;
        for (u8 i = 0; i < row->count; ++i) totalUnits += row->keys[i].width;
        const f32 usableWidth = panelWidth - padding * 2.0f -
                                keyGap * (row->count - 1);
        const f32 unitWidth = usableWidth / totalUnits;
        f32 keyX = panelX + padding;
        const f32 keyY = firstRowY + rowIndex * (keyHeight + keyGap);
        for (u8 column = 0; column < row->count; ++column) {
            const struct DjuiKeyboardKey* key = &row->keys[column];
            const f32 keyWidth = key->width * unitWidth;
            const bool selected = rowIndex == sKeyboardRow &&
                                  column == sKeyboardColumn;
            const bool activeModifier =
                (key->action == DJUI_KEY_SHIFT && sKeyboardShift) ||
                (key->action == DJUI_KEY_CAPS && sKeyboardCaps);

            djui_hud_set_color(3, 7, 12, 255);
            djui_hud_render_rect(keyX, keyY, keyWidth, keyHeight);
            if (selected) djui_hud_set_color(0, 126, 214, 255);
            else if (activeModifier) djui_hud_set_color(24, 145, 117, 255);
            else djui_hud_set_color(49, 62, 79, 255);
            djui_hud_render_rect(keyX + 2.0f, keyY + 2.0f,
                                 keyWidth - 4.0f, keyHeight - 4.0f);

            char labelBuffer[8] = { 0 };
            const char* label = djui_keyboard_display_label(key, labelBuffer);
            f32 textWidth = 0.0f;
            f32 textHeight = 0.0f;
            djui_hud_measure_text(label, &textWidth, &textHeight);
            const f32 textScale = strlen(label) > 5 ? 0.43f : 0.60f;
            djui_hud_set_color(246, 249, 252, 255);
            djui_hud_print_text(label,
                                keyX + (keyWidth - textWidth * textScale) * 0.5f,
                                keyY + 9.0f,
                                textScale, textScale);
            keyX += keyWidth + keyGap;
        }
    }

    djui_hud_set_color(154, 177, 204, 255);
    djui_hud_print_text("Stick/D-pad: Select   A: Type   B: Cancel",
                        panelX + 16.0f, panelY + panelHeight - 16.0f,
                        0.38f, 0.38f);
    gDPPipeSync(gDisplayListHead++);
    djui_reset_hud_params();
}

static void djui_inputbox_update_style(struct DjuiBase* base) {
    struct DjuiInputbox* inputbox = (struct DjuiInputbox*)base;
    struct DjuiTheme* theme = gDjuiThemes[configDjuiTheme];
    if (!inputbox->base.enabled) {
        struct DjuiColor bc = djui_theme_shade_color(theme->interactables.defaultBorderColor, 0.6f);
        struct DjuiColor rc = djui_theme_shade_color(theme->interactables.defaultRectColor, 0.6f);
        djui_base_set_border_color(base, bc.r, bc.g, bc.b, bc.a);
        djui_base_set_color(&inputbox->base, rc.r, rc.g, rc.b, rc.a);
    } else if (gDjuiCursorDownOn == base) {
        struct DjuiColor bc = theme->interactables.cursorDownBorderColor;
        struct DjuiColor rc = theme->interactables.cursorDownRectColor;
        djui_base_set_border_color(base, bc.r, bc.g, bc.b, bc.a);
        djui_base_set_color(&inputbox->base, rc.r, rc.g, rc.b, rc.a);
    } else if (gDjuiHovered == base) {
        struct DjuiColor bc = theme->interactables.hoveredBorderColor;
        struct DjuiColor rc = theme->interactables.hoveredRectColor;
        djui_base_set_border_color(base, bc.r, bc.g, bc.b, bc.a);
        djui_base_set_color(&inputbox->base, rc.r, rc.g, rc.b, rc.a);
    } else {
        struct DjuiColor bc = theme->interactables.defaultBorderColor;
        struct DjuiColor rc = theme->interactables.defaultRectColor;
        djui_base_set_border_color(base, bc.r, bc.g, bc.b, bc.a);
        djui_base_set_color(&inputbox->base, rc.r, rc.g, rc.b, rc.a);
    }
}

static void djui_inputbox_on_change(struct DjuiInputbox* inputbox) {
    struct DjuiBase* base = &inputbox->base;
    if (base != NULL && base->interactable != NULL && base->interactable->on_value_change != NULL) {
        base->interactable->on_value_change(base);
    }
}

void djui_inputbox_set_text_color(struct DjuiInputbox* inputbox, u8 r, u8 g, u8 b, u8 a) {
    inputbox->textColor.r = r;
    inputbox->textColor.g = g;
    inputbox->textColor.b = b;
    inputbox->textColor.a = a;
}

void djui_inputbox_set_text(struct DjuiInputbox* inputbox, char* text) {
    snprintf(inputbox->buffer, inputbox->bufferSize, "%s", text);
}

void djui_inputbox_select_all(struct DjuiInputbox* inputbox) {
    inputbox->selection[1] = 0;
    inputbox->selection[0] = djui_unicode_len(inputbox->buffer);
}

void djui_inputbox_move_cursor_to_end(struct DjuiInputbox* inputbox) {
    inputbox->selection[1] = djui_unicode_len(inputbox->buffer);
    inputbox->selection[0] = djui_unicode_len(inputbox->buffer);
    sCursorBlink = 0;
    djui_inputbox_on_change(inputbox);
}

void djui_inputbox_move_cursor_to_position(struct DjuiInputbox* inputbox, u16 newCursorPosition) {
    inputbox->selection[1] = newCursorPosition;
    inputbox->selection[0] = newCursorPosition;
    sCursorBlink = 0;
    djui_inputbox_on_change(inputbox);
}

void djui_inputbox_hook_enter_press(struct DjuiInputbox* inputbox, void (*on_enter_press)(struct DjuiInputbox*)) {
    inputbox->on_enter_press = on_enter_press;
}

void djui_inputbox_hook_escape_press(struct DjuiInputbox* inputbox, void (*on_escape_press)(struct DjuiInputbox*)) {
    inputbox->on_escape_press = on_escape_press;
}

static u16 djui_inputbox_get_cursor_index(struct DjuiInputbox* inputbox) {
    struct DjuiBaseRect*   comp = &inputbox->base.comp;
    const struct DjuiFont* font = gDjuiFonts[configDjuiThemeFont == 0 ? FONT_NORMAL : FONT_ALIASED];

    f32 cX = (gCursorX - (comp->x + inputbox->viewX)) / font->defaultFontScale;
    f32 x = 0;
    u16 index = 0;
    u16 i = 0;
    char* c = inputbox->buffer;
    while (true) {
        if (x < cX) {
            index = i;
        }
        if (*c == '\0') { break; }
        x += font->char_width(c);
        c = djui_unicode_next_char(c);
        i++;
    }

    return index;
}

static void djui_inputbox_on_cursor_down(struct DjuiBase* base) {
    struct DjuiInputbox* inputbox = (struct DjuiInputbox*)base;
    u16 index = djui_inputbox_get_cursor_index(inputbox);
    inputbox->selection[0] = index;
}

static void djui_inputbox_on_cursor_down_begin(struct DjuiBase* base, UNUSED bool inputCursor) {
    struct DjuiInputbox* inputbox = (struct DjuiInputbox*)base;
    u16 index = djui_inputbox_get_cursor_index(inputbox);
    u16 selLength = abs(inputbox->selection[0] - inputbox->selection[1]);
    if (selLength != djui_unicode_len(inputbox->buffer) || djui_interactable_is_input_focus(base)) {
        inputbox->selection[0] = index;
        inputbox->selection[1] = index;
        djui_interactable_hook_cursor_down(base, djui_inputbox_on_cursor_down_begin, djui_inputbox_on_cursor_down, NULL);
    } else {
        djui_interactable_hook_cursor_down(base, djui_inputbox_on_cursor_down_begin, NULL, NULL);
    }
    sCursorBlink = 0;
    djui_interactable_set_input_focus(base);
}

static u16 djui_inputbox_jump_word_left(char* msg, UNUSED u16 len, u16 i) {
    if (i == 0) { return i; }

    s32 lastI = i;
    bool seenNonSpace = false;
    char* c = djui_unicode_at_index(msg, i);
    while (true) {
        if (*c == ' ' && seenNonSpace) { i = lastI; break; }
        lastI = i;
        i--;
        c = djui_unicode_at_index(msg, i);
        if (i <= 0) { i = 0; break; }
        if (*c != ' ') { seenNonSpace = true; }
    }

    return i;
}

static u16 djui_inputbox_jump_word_right(char *msg, u16 len, u16 i) {
    if (i >= len) { return len; }

    bool seenSpace = false;
    char* c = djui_unicode_at_index(msg, i);
    while (true) {
        i++;
        c = djui_unicode_at_index(msg, i);
        if (i >= len) { i = len; break; }
        if (*c != ' ' && seenSpace) { break; }
        if (*c == ' ') { seenSpace = true; }
    };

    return i;
}

static void djui_inputbox_delete_selection(struct DjuiInputbox *inputbox) {
    u16 *sel = inputbox->selection;
    char *msg = inputbox->buffer;
    u16 len = strlen(msg);

    if (sel[0] != sel[1]) {
        u16 s1 = fmin(sel[0], sel[1]);
        u16 s2 = fmax(sel[0], sel[1]);
        size_t s2len = djui_unicode_at_index(msg, s2) - msg;
        memmove(djui_unicode_at_index(msg, s1), djui_unicode_at_index(msg, s2), (len + 1) - s2len);
        sel[0] = s1;
        sel[1] = s1;
    }
    djui_inputbox_on_change(inputbox);
}

bool djui_inputbox_on_key_down(struct DjuiBase *base, int scancode) {
    struct DjuiInputbox *inputbox = (struct DjuiInputbox *) base;
    u16 *sel = inputbox->selection;
    char *msg = inputbox->buffer;
    u16 len = djui_unicode_len(msg);
    u16 s1 = fmin(sel[0], sel[1]);
    u16 s2 = fmax(sel[0], sel[1]);

    switch (scancode) {
        case SCANCODE_SHIFT_LEFT:    gDjuiInputHeldShift   |= (1 << 0); return true;
        case SCANCODE_SHIFT_RIGHT:   gDjuiInputHeldShift   |= (1 << 1); return true;
        case SCANCODE_CONTROL_LEFT:  gDjuiInputHeldControl |= (1 << 0); return true;
        case SCANCODE_CONTROL_RIGHT: gDjuiInputHeldControl |= (1 << 1); return true;
        case SCANCODE_ALT_LEFT:      gDjuiInputHeldAlt     |= (1 << 0); return true;
        case SCANCODE_ALT_RIGHT:     gDjuiInputHeldAlt     |= (1 << 1); return true;
    }

    // [Left], [Ctrl]+[Left], [Shift]+[Left], [Ctrl]+[Shift]+[Left]
    if (!gDjuiInputHeldAlt && scancode == SCANCODE_LEFT) {
        if (gDjuiInputHeldControl) {
            sel[0] = djui_inputbox_jump_word_left(msg, len, sel[0]);
        } else if (sel[0] > 0) {
            sel[0]--;
        }
        if (!gDjuiInputHeldShift) { sel[1] = sel[0]; }
        sCursorBlink = 0;
        return true;
    }

    // [Right], [Ctrl]+[Right], [Shift]+[Right], [Ctrl]+[Shift]+[Right]
    if (!gDjuiInputHeldAlt && scancode == SCANCODE_RIGHT) {
        if (gDjuiInputHeldControl) {
            sel[0] = djui_inputbox_jump_word_right(msg, len, sel[0]);
        } else if (sel[0] < len) {
            sel[0]++;
        }
        if (!gDjuiInputHeldShift) { sel[1] = sel[0]; }
        sCursorBlink = 0;
        return true;
    }

    // [Home], [Shift]+[Home]
    if (!gDjuiInputHeldAlt && scancode == SCANCODE_HOME) {
        sel[0] = 0;
        if (!gDjuiInputHeldShift) { sel[1] = sel[0]; }
        sCursorBlink = 0;
        return true;
    }

    // [End], [Shift]+[End]
    if (!gDjuiInputHeldAlt && scancode == SCANCODE_END) {
        sel[0] = len;
        if (!gDjuiInputHeldShift) { sel[1] = sel[0]; }
        sCursorBlink = 0;
        return true;
    }

    // [Backspace], [Ctrl]+[Backspace]
    if (!gDjuiInputHeldAlt && scancode == SCANCODE_BACKSPACE) {
        if (sel[0] == sel[1]) {
            if (gDjuiInputHeldControl) {
                sel[0] = djui_inputbox_jump_word_left(msg, len, sel[0]);
            } else if (sel[0] > 0) {
                sel[0]--;
            }
        }
        if (sel[0] != sel[1]) {
            djui_inputbox_delete_selection(inputbox);
        }
        sCursorBlink = 0;
        return true;
    }

    // [Delete], [Ctrl]+[Delete]
    if (!gDjuiInputHeldAlt && scancode == SCANCODE_DELETE) {
        if (sel[0] == sel[1]) {
            if (gDjuiInputHeldControl) {
                sel[1] = djui_inputbox_jump_word_right(msg, len, sel[1]);
            } else if (sel[1] < len) {
                sel[1]++;
            }
        }
        if (sel[0] != sel[1]) {
            djui_inputbox_delete_selection(inputbox);
        }
        sCursorBlink = 0;
        return true;
    }

    // [Ctrl]+[V], [Shift]+[Insert]
    if (!gDjuiInputHeldAlt &&
        ((!gDjuiInputHeldShift && gDjuiInputHeldControl && scancode == SCANCODE_V) ||
        (!gDjuiInputHeldControl && gDjuiInputHeldShift && scancode == SCANCODE_INSERT))) {
        djui_interactable_on_text_input(gWindowApi->get_clipboard_text());
        sCursorBlink = 0;
        return true;
    }

    // [Ctrl]+[C], [Ctrl]+[X]
    if (!gDjuiInputHeldAlt && !gDjuiInputHeldShift && gDjuiInputHeldControl &&
        (scancode == SCANCODE_C || scancode == SCANCODE_X)) {
        if (sel[0] != sel[1]) {
            char clipboardText[256] = { 0 };
            char* cs1 = djui_unicode_at_index(msg, s1);
            char* cs2 = djui_unicode_at_index(msg, s2);
            snprintf(clipboardText, fmin(256, 1 + cs2 - cs1), "%s", cs1);
            gWindowApi->set_clipboard_text(clipboardText);
            if (scancode == SCANCODE_X) {
                djui_inputbox_delete_selection(inputbox);
                sCursorBlink = 0;
            }
        }
        return true;
    }

    // [Ctrl]+[A]
    if (!gDjuiInputHeldAlt && !gDjuiInputHeldShift && gDjuiInputHeldControl && scancode == SCANCODE_A) {
        inputbox->selection[0] = djui_unicode_len(msg);
        inputbox->selection[1] = 0;
        sCursorBlink = 0;
        return true;
    }

    // [Esc]
    if (!gDjuiInputHeldAlt && !gDjuiInputHeldShift && !gDjuiInputHeldControl && scancode == SCANCODE_ESCAPE) {
        djui_interactable_set_input_focus(NULL);
        if (inputbox->on_escape_press) {
            inputbox->on_escape_press(inputbox);
        }
        return true;
    }

    // [Enter]
    if (!gDjuiInputHeldAlt && !gDjuiInputHeldShift && !gDjuiInputHeldControl && scancode == SCANCODE_ENTER) {
        djui_interactable_set_input_focus(NULL);
        if (inputbox->on_enter_press) {
            inputbox->on_enter_press(inputbox);
        }
        return true;
    }

    return true;
}

void djui_inputbox_on_key_up(UNUSED struct DjuiBase *base, int scancode) {
    switch (scancode) {
        case SCANCODE_SHIFT_LEFT:    gDjuiInputHeldShift   &= ~(1 << 0); break;
        case SCANCODE_SHIFT_RIGHT:   gDjuiInputHeldShift   &= ~(1 << 1); break;
        case SCANCODE_CONTROL_LEFT:  gDjuiInputHeldControl &= ~(1 << 0); break;
        case SCANCODE_CONTROL_RIGHT: gDjuiInputHeldControl &= ~(1 << 1); break;
        case SCANCODE_ALT_LEFT:      gDjuiInputHeldAlt     &= ~(1 << 0); break;
        case SCANCODE_ALT_RIGHT:     gDjuiInputHeldAlt     &= ~(1 << 1); break;
    }
}

void djui_inputbox_on_focus_begin(struct DjuiBase* base) {
    gDjuiInputHeldShift   = 0;
    gDjuiInputHeldControl = 0;
    gDjuiInputHeldAlt     = 0;
    sKeyboardTarget = (struct DjuiInputbox*)base;
    sKeyboardRow = 0;
    sKeyboardColumn = 1;
    sKeyboardHeldDirection = 0;
    sKeyboardHeldFrames = 0;
    sKeyboardShift = false;
    sKeyboardCaps = false;
    gWindowApi->start_text_input();
}

void djui_inputbox_on_focus_end(UNUSED struct DjuiBase* base) {
    sKeyboardTarget = NULL;
    sKeyboardHeldDirection = 0;
    sKeyboardHeldFrames = 0;
    gWindowApi->stop_text_input();
}

void djui_inputbox_on_text_input(struct DjuiBase *base, char* text) {
    struct DjuiInputbox *inputbox = (struct DjuiInputbox *) base;
    char* msg = inputbox->buffer;
    int msgLen = strlen(msg);
    int textLen = strlen(text);

    // make sure we're not just printing garbage characters
    bool containsValidAscii = false;
    char* tinput = text;
    while (*tinput != '\0') {
        if (djui_unicode_valid_char(tinput)) {
            containsValidAscii = true;
            break;
        }
        tinput = djui_unicode_next_char(tinput);
    }
    if (!containsValidAscii) {
        return;
    }

    // truncate
    if (textLen + msgLen >= inputbox->bufferSize) {
        int space = (inputbox->bufferSize - msgLen);
        if (space <= 1) { return; }
        text[space - 1] = '\0';
        textLen = space - 1;
    }

    // erase selection
    if (inputbox->selection[0] != inputbox->selection[1]) {
        djui_inputbox_delete_selection(inputbox);
    }

    // sanitize
    char *t = text;
    while (*t != '\0') {
        if (*t == '\n') { *t = ' '; }
        else if (*t == '\r') { *t = ' '; }
        else if (djui_unicode_valid_char(t)) { ; }

        t = djui_unicode_next_char(t);
    }

    // back up current message
    char* sMsg = calloc(inputbox->bufferSize, sizeof(char));
    memcpy(sMsg, msg, inputbox->bufferSize);

    // insert text
    size_t sel = djui_unicode_at_index(inputbox->buffer, inputbox->selection[0]) - inputbox->buffer;

    snprintf(&msg[sel], (inputbox->bufferSize - sel), "%s%s", text, &sMsg[sel]);
    free(sMsg);
    djui_unicode_cleanup_end(msg);

    // adjust cursor
    inputbox->selection[0] += djui_unicode_len(text);
    s32 ulen = djui_unicode_len(msg);
    if (inputbox->selection[0] > ulen) { inputbox->selection[0] = ulen; }
    inputbox->selection[1] = inputbox->selection[0];
    sCursorBlink = 0;
    djui_inputbox_on_change(inputbox);

    inputbox->imePos = 0;
    if (inputbox->imeBuffer != NULL) {
        free(inputbox->imeBuffer);
        inputbox->imeBuffer = NULL;
    }
}

void djui_inputbox_on_text_editing(struct DjuiBase *base, char* text, int cursorPos) {
    struct DjuiInputbox *inputbox = (struct DjuiInputbox *) base;
    inputbox->imePos = (u16)cursorPos;

    if (inputbox->imeBuffer != NULL) free(inputbox->imeBuffer);

    if (*text == '\0') {
        inputbox->imeBuffer = NULL;
    }
    else {
        size_t size = strlen(text);
        char* copy = malloc(size + 1);
        strcpy(copy,text);
        inputbox->imeBuffer = copy;
    }

    djui_inputbox_on_change(inputbox);
}

static void djui_inputbox_render_char(struct DjuiInputbox* inputbox, char* c, f32* drawX, f32* additionalShift) {
    struct DjuiBaseRect*   comp = &inputbox->base.comp;
    const struct DjuiFont* font = gDjuiFonts[configDjuiThemeFont == 0 ? FONT_NORMAL : FONT_ALIASED];
    f32 dX = comp->x + *drawX;
    f32 dY = comp->y;
    f32 dW = font->charWidth  * font->defaultFontScale;
    f32 dH = font->charHeight * font->defaultFontScale - inputbox->base.borderWidth.value * 2;

    char* dc = inputbox->passwordChar[0] ? inputbox->passwordChar : c;

    f32 charWidth = font->char_width(dc);
    *drawX += charWidth * font->defaultFontScale;

    if (*dc != ' ' && !djui_gfx_add_clipping_specific(&inputbox->base, dX, dY, dW, dH)) {
        if (*additionalShift > 0) {
            create_dl_translation_matrix(DJUI_MTX_NOPUSH, *additionalShift, 0, 0);
            *additionalShift = 0;
        }
        font->render_char(dc);
    }
    *additionalShift += charWidth;
}

static void djui_inputbox_render_selection(struct DjuiInputbox* inputbox) {
    const struct DjuiFont* font = gDjuiFonts[configDjuiThemeFont == 0 ? FONT_NORMAL : FONT_ALIASED];

    // make selection well formed
    u16 selection[2] = { 0 };
    selection[0] = fmin(inputbox->selection[0], inputbox->selection[1]);
    selection[1] = fmax(inputbox->selection[0], inputbox->selection[1]);

    char* c = inputbox->buffer;
    f32 x = 0;
    f32 width = 0;
    for (u16 i = 0; i < selection[1]; i++) {
        char* dc = inputbox->passwordChar[0] ? inputbox->passwordChar : c;
        if (i < selection[0]) {
            x += font->char_width(dc);
        } else {
            width += font->char_width(dc);
        }
        c = djui_unicode_next_char(c);
    }

    sCursorBlink = (sCursorBlink + 1) % DJUI_INPUTBOX_MAX_BLINK;

    f32 renderX = x;

    u16 imePos = inputbox->imePos;
    if (imePos != 0) {
        char* ime = inputbox->imeBuffer;
        for (u16 i = 0; i < imePos; i++) {
            renderX += font->char_width(ime);
            ime = djui_unicode_next_char(ime);
        }
    }

    // render only cursor when there is no selection width
    if (selection[0] == selection[1]) {
        if (sCursorBlink < DJUI_INPUTBOX_MID_BLINK && djui_interactable_is_input_focus(&inputbox->base)) {
            create_dl_translation_matrix(DJUI_MTX_PUSH, renderX - DJUI_INPUTBOX_CURSOR_WIDTH / 2.0f, -0.1f, 0);
            create_dl_scale_matrix(DJUI_MTX_NOPUSH, DJUI_INPUTBOX_CURSOR_WIDTH, 0.8f, 1.0f);
            struct DjuiColor *textColor = &inputbox->textColor;
            gDPSetEnvColor(gDisplayListHead++, textColor->r, textColor->g, textColor->b, textColor->a);
            gSPDisplayList(gDisplayListHead++, dl_djui_simple_rect);
            gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
        }
        return;
    }

    // clip selection box
    // note: this is incredibly confusing due to being in font-space instead of screen-space
    f32 clipLow = -inputbox->viewX / font->defaultFontScale;
    if (x < clipLow) {
        width -= clipLow - x;
        x = clipLow;
    }
    f32 clipHigh = (inputbox->base.clip.width / font->defaultFontScale) - x + clipLow;
    if (width > clipHigh) {
        width = clipHigh;
    }

    // render selection box
    create_dl_translation_matrix(DJUI_MTX_PUSH, x, -0.1f, 0);
    create_dl_scale_matrix(DJUI_MTX_NOPUSH, width, 0.8f, 1.0f);
    gDPSetEnvColor(gDisplayListHead++, 0, 120, 215, 255);
    gSPDisplayList(gDisplayListHead++, dl_djui_simple_rect);
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);

    // render selection cursor
    if (sCursorBlink < DJUI_INPUTBOX_MID_BLINK && djui_interactable_is_input_focus(&inputbox->base)) {
        f32 cX = (inputbox->selection[0] < inputbox->selection[1]) ? x : (x + width);
        create_dl_translation_matrix(DJUI_MTX_PUSH, cX - DJUI_INPUTBOX_CURSOR_WIDTH / 2.0f, -0.1f, 0);
        create_dl_scale_matrix(DJUI_MTX_NOPUSH, DJUI_INPUTBOX_CURSOR_WIDTH, 0.8f, 1.0f);
        gDPSetEnvColor(gDisplayListHead++, 255, 127, 0, 255);
        gSPDisplayList(gDisplayListHead++, dl_djui_simple_rect);
        gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
    }
}

static void djui_inputbox_keep_selection_in_view(struct DjuiInputbox* inputbox) {
    const struct DjuiFont* font = gDjuiFonts[configDjuiThemeFont == 0 ? FONT_NORMAL : FONT_ALIASED];

    // calculate where our cursor is
    f32 cursorX = inputbox->viewX;
    char* c = inputbox->buffer;
    for (u16 i = 0; i < inputbox->selection[0]; i++) {
        if (*c == '\0') { break; }
        char* dc = inputbox->passwordChar[0] ? inputbox->passwordChar : c;
        cursorX += font->char_width(dc) * font->defaultFontScale;
        c = djui_unicode_next_char(c);
    }

    // shift viewing window
    if (cursorX > inputbox->base.comp.width) {
        inputbox->viewX -= cursorX - inputbox->base.comp.width;
    } else if (cursorX < 0) {
        inputbox->viewX -= cursorX;
    }
}

static bool djui_inputbox_render(struct DjuiBase* base) {
    struct DjuiInputbox* inputbox = (struct DjuiInputbox*)base;
    struct DjuiBaseRect* comp     = &base->comp;
    const struct DjuiFont* font   = gDjuiFonts[configDjuiThemeFont == 0 ? FONT_NORMAL : FONT_ALIASED];
    djui_rect_render(base);

    // Shift the text away from the left side a tad
    comp->x += 2;
    comp->width -= 2;

    // shift the viewing window to keep the selection in view
    djui_inputbox_keep_selection_in_view(inputbox);

    // translate position
    f32 translatedX = comp->x + inputbox->viewX;
    f32 translatedY = comp->y + inputbox->yOffset;
    djui_gfx_position_translate(&translatedX, &translatedY);
    create_dl_translation_matrix(DJUI_MTX_PUSH, translatedX, translatedY, 0);

    // compute font size
    f32 translatedFontSize = font->defaultFontScale;
    djui_gfx_size_translate(&translatedFontSize);
    create_dl_scale_matrix(DJUI_MTX_NOPUSH, translatedFontSize, translatedFontSize, 1.0f);

    // render selection
    djui_inputbox_render_selection(inputbox);

    // begin font
    if (font->textBeginDisplayList != NULL) {
        gSPDisplayList(gDisplayListHead++, font->textBeginDisplayList);
    }

    // set color
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(gDisplayListHead++, inputbox->textColor.r, inputbox->textColor.g, inputbox->textColor.b, inputbox->textColor.a);

    // make selection well formed
    u16 selection[2] = { 0 };
    selection[0] = fmin(inputbox->selection[0], inputbox->selection[1]);
    selection[1] = fmax(inputbox->selection[0], inputbox->selection[1]);

    // render text
    char* c = inputbox->buffer;
    f32 drawX = inputbox->viewX;
    f32 additionalShift = 0;
    bool wasInsideSelection = false;

    font->render_begin();
    for (u16 i = 0; i < inputbox->bufferSize; i++) {

        //render composition text
        if (selection[0] == i && inputbox->imeBuffer != NULL) {
            char *ime = inputbox->imeBuffer;
            while (*ime != '\0') {
                djui_inputbox_render_char(inputbox, ime, &drawX, &additionalShift);
                ime = djui_unicode_next_char(ime);
            }
        }

        if (*c == '\0') { break; }

        // deal with seleciton color
        if (selection[0] != selection[1]) {
            bool insideSelection = (i >= selection[0]) && (i < selection[1]);
            if (insideSelection && !wasInsideSelection) {
                gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
            } else if (!insideSelection && wasInsideSelection) {
                gDPSetEnvColor(gDisplayListHead++, inputbox->textColor.r, inputbox->textColor.g, inputbox->textColor.b, inputbox->textColor.a);
            }
            wasInsideSelection = insideSelection;
        }

        // render character
        djui_inputbox_render_char(inputbox, c, &drawX, &additionalShift);
        c = djui_unicode_next_char(c);
    }
    font->render_end();

    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    return true;
}

static void djui_inputbox_destroy(struct DjuiBase* base) {
    struct DjuiInputbox* inputbox = (struct DjuiInputbox*)base;
    free(inputbox->buffer);
    free(inputbox);
}

struct DjuiInputbox* djui_inputbox_create(struct DjuiBase* parent, u16 bufferSize) {
    struct DjuiInputbox* inputbox = calloc(1, sizeof(struct DjuiInputbox));
    struct DjuiBase* base         = &inputbox->base;
    struct DjuiTheme* theme       = gDjuiThemes[configDjuiTheme];
    struct DjuiColor* textColor = &theme->interactables.textColor;
    inputbox->bufferSize = bufferSize;
    inputbox->buffer = calloc(bufferSize, sizeof(char));
    inputbox->yOffset = DJUI_INPUTBOX_YOFF;

    djui_base_init(parent, base, djui_inputbox_render, djui_inputbox_destroy);
    djui_base_set_size(base, 200, 32);
    djui_base_set_border_width(base, 2);
    djui_inputbox_set_text_color(inputbox, textColor->r, textColor->g, textColor->b, textColor->a);
    djui_interactable_create(base, djui_inputbox_update_style);
    djui_interactable_hook_cursor_down(base, djui_inputbox_on_cursor_down_begin, djui_inputbox_on_cursor_down, NULL);
    djui_interactable_hook_key(base, djui_inputbox_on_key_down, djui_inputbox_on_key_up);
    djui_interactable_hook_focus(base, djui_inputbox_on_focus_begin, NULL, djui_inputbox_on_focus_end);
    djui_interactable_hook_text_input(base, djui_inputbox_on_text_input);
    djui_interactable_hook_text_editing(base, djui_inputbox_on_text_editing);

    djui_inputbox_update_style(base);

    return inputbox;
}
