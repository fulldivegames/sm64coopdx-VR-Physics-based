#include "quest_openxr_input.h"

#include <android/log.h>
#include <string.h>

#include "pc/vr/vr.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR", __VA_ARGS__)

typedef struct QuestInput {
    XrInstance instance;
    XrSession session;
    XrActionSet set;
    XrPath hands[2];
    XrAction grip_pose;
    XrAction aim_pose;
    XrAction trigger;
    XrAction squeeze;
    XrAction thumbstick;
    XrAction primary;
    XrAction secondary;
    XrAction menu;
    XrAction thumbstick_click;
    XrSpace grip_spaces[2];
    XrSpace aim_spaces[2];
    bool ready;
    bool active;
} QuestInput;

static QuestInput sInput;
extern void quest_vr_bridge_update_controller(
    uint32_t hand, const struct VrControllerState *state, bool available);

static bool ok(XrResult result, const char *operation) {
    if (XR_SUCCEEDED(result)) return true;
    LOGE("%s failed: %d", operation, (int)result);
    return false;
}

static XrPath path(const char *value) {
    XrPath result = XR_NULL_PATH;
    xrStringToPath(sInput.instance, value, &result);
    return result;
}

static bool create_action(XrAction *action, XrActionType type,
                          const char *name, const char *label) {
    XrActionCreateInfo info = {
        .type = XR_TYPE_ACTION_CREATE_INFO,
        .actionType = type,
        .countSubactionPaths = 2,
        .subactionPaths = sInput.hands,
    };
    strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    return ok(xrCreateAction(sInput.set, &info, action), "xrCreateAction");
}

static bool suggest_bindings(void) {
    XrActionSuggestedBinding bindings[18];
    uint32_t count = 0;
#define BIND(action, binding) bindings[count++] = (XrActionSuggestedBinding){action, path(binding)}
    BIND(sInput.grip_pose, "/user/hand/left/input/grip/pose");
    BIND(sInput.grip_pose, "/user/hand/right/input/grip/pose");
    BIND(sInput.aim_pose, "/user/hand/left/input/aim/pose");
    BIND(sInput.aim_pose, "/user/hand/right/input/aim/pose");
    BIND(sInput.trigger, "/user/hand/left/input/trigger/value");
    BIND(sInput.trigger, "/user/hand/right/input/trigger/value");
    BIND(sInput.squeeze, "/user/hand/left/input/squeeze/value");
    BIND(sInput.squeeze, "/user/hand/right/input/squeeze/value");
    BIND(sInput.thumbstick, "/user/hand/left/input/thumbstick");
    BIND(sInput.thumbstick, "/user/hand/right/input/thumbstick");
    BIND(sInput.primary, "/user/hand/left/input/x/click");
    BIND(sInput.primary, "/user/hand/right/input/a/click");
    BIND(sInput.secondary, "/user/hand/left/input/y/click");
    BIND(sInput.secondary, "/user/hand/right/input/b/click");
    BIND(sInput.menu, "/user/hand/left/input/menu/click");
    BIND(sInput.thumbstick_click, "/user/hand/left/input/thumbstick/click");
    BIND(sInput.thumbstick_click, "/user/hand/right/input/thumbstick/click");
#undef BIND
    XrInteractionProfileSuggestedBinding suggested = {
        .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
        .interactionProfile = path("/interaction_profiles/oculus/touch_controller"),
        .countSuggestedBindings = count,
        .suggestedBindings = bindings,
    };
    return ok(xrSuggestInteractionProfileBindings(sInput.instance, &suggested),
              "xrSuggestInteractionProfileBindings");
}

bool quest_input_initialize(XrInstance instance, XrSession session) {
    memset(&sInput, 0, sizeof(sInput));
    sInput.instance = instance;
    sInput.session = session;
    sInput.hands[0] = path("/user/hand/left");
    sInput.hands[1] = path("/user/hand/right");
    XrActionSetCreateInfo set_info = {
        .type = XR_TYPE_ACTION_SET_CREATE_INFO,
        .priority = 0,
    };
    strcpy(set_info.actionSetName, "gameplay");
    strcpy(set_info.localizedActionSetName, "Gameplay");
    if (!ok(xrCreateActionSet(instance, &set_info, &sInput.set), "xrCreateActionSet")
        || !create_action(&sInput.grip_pose, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose")
        || !create_action(&sInput.aim_pose, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose")
        || !create_action(&sInput.trigger, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger")
        || !create_action(&sInput.squeeze, XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grip")
        || !create_action(&sInput.thumbstick, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick")
        || !create_action(&sInput.primary, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "Primary")
        || !create_action(&sInput.secondary, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "Secondary")
        || !create_action(&sInput.menu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu")
        || !create_action(&sInput.thumbstick_click, XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Stick click")
        || !suggest_bindings()) return false;

    XrSessionActionSetsAttachInfo attach = {
        .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets = 1,
        .actionSets = &sInput.set,
    };
    if (!ok(xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets")) return false;
    for (uint32_t hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo space = {
            .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
            .subactionPath = sInput.hands[hand],
            .poseInActionSpace = {.orientation = {.w = 1.0f}},
        };
        space.action = sInput.grip_pose;
        if (!ok(xrCreateActionSpace(session, &space, &sInput.grip_spaces[hand]), "xrCreateActionSpace(grip)")) return false;
        space.action = sInput.aim_pose;
        if (!ok(xrCreateActionSpace(session, &space, &sInput.aim_spaces[hand]), "xrCreateActionSpace(aim)")) return false;
    }
    sInput.ready = true;
    LOGI("Quest Touch controller actions attached.");
    return true;
}

static float float_action(XrSession session, XrAction action, XrPath hand) {
    XrActionStateGetInfo get = {.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=action,.subactionPath=hand};
    XrActionStateFloat state = {.type=XR_TYPE_ACTION_STATE_FLOAT};
    return XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &state)) && state.isActive ? state.currentState : 0.0f;
}
static bool bool_action(XrSession session, XrAction action, XrPath hand) {
    XrActionStateGetInfo get = {.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=action,.subactionPath=hand};
    XrActionStateBoolean state = {.type=XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &state)) && state.isActive && state.currentState;
}

static void locate_pose(XrSpace space, XrSpace base, XrTime time,
                        bool *valid, float position[3], float rotation[4],
                        bool *linear_valid, float linear[3],
                        bool *angular_valid, float angular[3]) {
    XrSpaceVelocity velocity = {.type = XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation location = {.type = XR_TYPE_SPACE_LOCATION, .next = &velocity};
    if (XR_FAILED(xrLocateSpace(space, base, time, &location))) return;
    const XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    *valid = (location.locationFlags & required) == required;
    if (*valid) {
        position[0]=location.pose.position.x; position[1]=location.pose.position.y; position[2]=location.pose.position.z;
        rotation[0]=location.pose.orientation.x; rotation[1]=location.pose.orientation.y; rotation[2]=location.pose.orientation.z; rotation[3]=location.pose.orientation.w;
    }
    *linear_valid = (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
    *angular_valid = (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
    if (*linear_valid) { linear[0]=velocity.linearVelocity.x; linear[1]=velocity.linearVelocity.y; linear[2]=velocity.linearVelocity.z; }
    if (*angular_valid) { angular[0]=velocity.angularVelocity.x; angular[1]=velocity.angularVelocity.y; angular[2]=velocity.angularVelocity.z; }
}

void quest_input_update(XrSession session, XrSpace base_space, XrTime time) {
    if (!sInput.ready || session == XR_NULL_HANDLE || base_space == XR_NULL_HANDLE ||
        session != sInput.session) return;
    XrActiveActionSet active = {.actionSet=sInput.set,.subactionPath=XR_NULL_PATH};
    XrActionsSyncInfo sync = {.type=XR_TYPE_ACTIONS_SYNC_INFO,.countActiveActionSets=1,.activeActionSets=&active};
    if (XR_FAILED(xrSyncActions(session, &sync))) {
        quest_input_suspend();
        return;
    }
    sInput.active = true;
    for (uint32_t hand = 0; hand < 2; ++hand) {
        struct VrControllerState state;
        memset(&state, 0, sizeof(state));
        XrActionStateGetInfo get = {.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=sInput.thumbstick,.subactionPath=sInput.hands[hand]};
        XrActionStateVector2f stick = {.type=XR_TYPE_ACTION_STATE_VECTOR2F};
        bool available = XR_SUCCEEDED(xrGetActionStateVector2f(session, &get, &stick)) && stick.isActive;
        if (available) { state.thumbstick[0]=stick.currentState.x; state.thumbstick[1]=stick.currentState.y; }
        state.trigger=float_action(session,sInput.trigger,sInput.hands[hand]);
        state.squeeze=float_action(session,sInput.squeeze,sInput.hands[hand]);
        state.primaryButton=bool_action(session,sInput.primary,sInput.hands[hand]);
        state.secondaryButton=bool_action(session,sInput.secondary,sInput.hands[hand]);
        state.menuButton=bool_action(session,sInput.menu,sInput.hands[hand]);
        state.thumbstickButton=bool_action(session,sInput.thumbstick_click,sInput.hands[hand]);
        locate_pose(sInput.grip_spaces[hand],base_space,time,&state.gripPoseValid,state.gripPosition,state.gripRotation,
                    &state.gripLinearVelocityValid,state.gripLinearVelocity,&state.gripAngularVelocityValid,state.gripAngularVelocity);
        bool unused_linear=false, unused_angular=false; float unused_l[3]={0},unused_a[3]={0};
        locate_pose(sInput.aim_spaces[hand],base_space,time,&state.aimPoseValid,state.aimPosition,state.aimRotation,
                    &unused_linear,unused_l,&unused_angular,unused_a);
        quest_vr_bridge_update_controller(hand,&state,available || state.gripPoseValid);
    }
}

void quest_input_suspend(void) {
    if (!sInput.active) return;
    struct VrControllerState state;
    memset(&state, 0, sizeof(state));
    for (uint32_t hand = 0; hand < 2; ++hand) {
        quest_vr_bridge_update_controller(hand, &state, false);
    }
    sInput.active = false;
}

void quest_input_shutdown(void) {
    quest_input_suspend();
    for (int hand=0; hand<2; ++hand) {
        if (sInput.grip_spaces[hand]) xrDestroySpace(sInput.grip_spaces[hand]);
        if (sInput.aim_spaces[hand]) xrDestroySpace(sInput.aim_spaces[hand]);
    }
    if (sInput.set) xrDestroyActionSet(sInput.set);
    memset(&sInput,0,sizeof(sInput));
}
