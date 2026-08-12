#include "djui.h"
#include "pc/vr/vr.h"

  ////////////////
 // properties //
////////////////

void djui_flow_layout_set_flow_direction(struct DjuiFlowLayout* layout, enum DjuiFlowDirection flowDirection) {
    layout->flowDirection = flowDirection;
}

void djui_flow_layout_set_margin(struct DjuiFlowLayout* layout, f32 margin) {
    layout->margin.value = margin;
}

void djui_flow_layout_set_margin_type(struct DjuiFlowLayout* layout, enum DjuiScreenValueType marginType) {
    layout->margin.type = marginType;
}

  ////////////
 // events //
////////////

static void djui_flow_layout_on_child_render(struct DjuiBase* base, struct DjuiBase* child) {
    if (!child->visible) { return; }
    struct DjuiFlowLayout* layout = (struct DjuiFlowLayout*)base;
    switch (layout->flowDirection) {
        case DJUI_FLOW_DIR_DOWN:
            base->comp.y      += (child->elem.height + layout->margin.value);
            base->comp.height -= (child->elem.height + layout->margin.value);
            break;
        case DJUI_FLOW_DIR_UP:
            base->comp.height -= (child->elem.height + layout->margin.value);
            break;
        case DJUI_FLOW_DIR_RIGHT:
            base->comp.x     += (child->elem.width + layout->margin.value);
            base->comp.width -= (child->elem.width + layout->margin.value);
            break;
        case DJUI_FLOW_DIR_LEFT:
            base->comp.width -= (child->elem.width + layout->margin.value);
            break;
    }
}

static bool djui_flow_layout_contains(struct DjuiBase* ancestor, struct DjuiBase* base) {
    while (base != NULL) {
        if (base == ancestor) { return true; }
        base = base->parent;
    }
    return false;
}

static bool djui_flow_layout_render(struct DjuiBase* base) {
    struct DjuiFlowLayout* layout = (struct DjuiFlowLayout*)base;
    const f32 viewportY = base->clip.y;
    const f32 viewportHeight = base->clip.height;

    if (vr_is_active() &&
        layout->flowDirection == DJUI_FLOW_DIR_DOWN) {
        // Match the comfortable visible band used by the head-locked VR
        // panel, rather than relying on the larger logical desktop canvas.
        const f32 visibleTop = fmaxf(
            viewportY,
            gDjuiRoot->base.comp.height * 0.12f
        );
        const f32 visibleBottom = fminf(
            viewportY + viewportHeight,
            gDjuiRoot->base.comp.height * 0.78f
        );
        const f32 visibleHeight = fmaxf(
            1.0f,
            visibleBottom - visibleTop
        );
        const f32 minimumScroll = fminf(
            0.0f,
            visibleBottom - (base->comp.y + layout->contentHeight)
        );
        struct DjuiBase* selected =
            djui_cursor_get_input_controlled_base();
        if (selected != NULL &&
            selected != layout->lastAutoScrollSelection &&
            djui_flow_layout_contains(base, selected) &&
            selected->elem.height > 0.0f) {
            const f32 safeTop = visibleTop + 12.0f;
            const f32 safeBottom = visibleBottom - 12.0f;
            // elem contains the previous rendered position, including the
            // prior scroll. Convert it back to an unscrolled coordinate and
            // choose one absolute target. Reapplying an incremental correction
            // every frame allowed stale text geometry to oscillate or jump
            // outside the panel after selection changes.
            const f32 unscrolledFocusTop =
                selected->elem.y - layout->scrollY;
            const f32 unscrolledFocusBottom =
                unscrolledFocusTop + selected->elem.height;
            const f32 focusTop = selected->elem.y;
            const f32 focusBottom = focusTop + selected->elem.height;
            if (focusBottom > safeBottom) {
                layout->scrollY =
                    safeBottom - unscrolledFocusBottom;
            } else if (focusTop < safeTop) {
                layout->scrollY =
                    safeTop - unscrolledFocusTop;
            }
            layout->lastAutoScrollSelection = selected;
        }
        if (layout->scrollY < minimumScroll) {
            layout->scrollY = minimumScroll;
        } else if (layout->scrollY > 0.0f) {
            layout->scrollY = 0.0f;
        }
        base->comp.y += layout->scrollY;
        base->comp.height = fmaxf(
            layout->contentHeight,
            visibleHeight
        );
    } else {
        layout->scrollY = 0.0f;
    }

    djui_rect_render(base);
    return true;
}

static void djui_flow_layout_destroy(struct DjuiBase* base) {
    struct DjuiFlowLayout* layout = (struct DjuiFlowLayout*)base;
    free(layout);
}

struct DjuiFlowLayout* djui_flow_layout_create(struct DjuiBase* parent) {
    struct DjuiFlowLayout* layout = calloc(1, sizeof(struct DjuiFlowLayout));
    struct DjuiBase* base         = &layout->base;

    djui_base_init(parent, base, djui_flow_layout_render, djui_flow_layout_destroy);
    djui_base_set_size(base, 256, 512);

    djui_flow_layout_set_flow_direction(layout, DJUI_FLOW_DIR_DOWN);
    djui_flow_layout_set_margin(layout, 8);

    layout->base.on_child_render = djui_flow_layout_on_child_render;
    return layout;
}
