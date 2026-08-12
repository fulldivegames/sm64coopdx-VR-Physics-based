#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "pc/vr/vr.h"

static bool sPoseValid;
static bool sReferenceValid;
static bool sRecenterPending = true;
static float sReferencePosition[3];
static float sReferenceRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
static float sHeadPosition[3];
static float sHeadRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
static float sCalibratedHeadHeight = 1.65f;
static uint32_t sTrackingOriginGeneration;
static float sEyePosition[2][3];
static float sEyeRotation[2][4];
static float sEyeFov[2][4];
static uint32_t sEyeWidth[2] = { 1024, 1024 };
static uint32_t sEyeHeight[2] = { 1024, 1024 };
static struct VrControllerState sControllers[2];
static bool sControllerAvailable[2];

static void rotate_vector(const float q[4], const float v[3], float out[3]) {
    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + q[1] * tz - q[2] * ty;
    out[1] = v[1] + q[3] * ty + q[2] * tx - q[0] * tz;
    out[2] = v[2] + q[3] * tz + q[0] * ty - q[1] * tx;
}

static void relative_rotation(const float current[4], float out[4]) {
    const float *r = sReferenceRotation;
    out[0] = r[3] * current[0] + r[0] * current[3] + r[1] * current[2] - r[2] * current[1];
    out[1] = r[3] * current[1] - r[0] * current[2] + r[1] * current[3] + r[2] * current[0];
    out[2] = r[3] * current[2] + r[0] * current[1] - r[1] * current[0] + r[2] * current[3];
    out[3] = r[3] * current[3] - r[0] * current[0] - r[1] * current[1] - r[2] * current[2];
    const float length = sqrtf(out[0] * out[0] + out[1] * out[1] +
                               out[2] * out[2] + out[3] * out[3]);
    if (length > 0.000001f) {
        for (int i = 0; i < 4; ++i) out[i] /= length;
    }
}

void quest_vr_bridge_update_controller(
    uint32_t hand, const struct VrControllerState *state, bool available) {
    if (hand >= 2) return;
    sControllerAvailable[hand] = available && sReferenceValid;
    if (!state) return;
    memcpy(&sControllers[hand], state, sizeof(*state));
    if (!sReferenceValid) return;

    if (state->gripPoseValid) {
        float delta[3] = {
            state->gripPosition[0] - sReferencePosition[0],
            state->gripPosition[1] - sReferencePosition[1],
            state->gripPosition[2] - sReferencePosition[2],
        };
        rotate_vector(sReferenceRotation, delta, sControllers[hand].gripPosition);
        relative_rotation(state->gripRotation, sControllers[hand].gripRotation);
    }
    if (state->aimPoseValid) {
        float delta[3] = {
            state->aimPosition[0] - sReferencePosition[0],
            state->aimPosition[1] - sReferencePosition[1],
            state->aimPosition[2] - sReferencePosition[2],
        };
        rotate_vector(sReferenceRotation, delta, sControllers[hand].aimPosition);
        relative_rotation(state->aimRotation, sControllers[hand].aimRotation);
    }
    if (state->gripLinearVelocityValid)
        rotate_vector(sReferenceRotation, state->gripLinearVelocity,
                      sControllers[hand].gripLinearVelocity);
    if (state->gripAngularVelocityValid)
        rotate_vector(sReferenceRotation, state->gripAngularVelocity,
                      sControllers[hand].gripAngularVelocity);
}

void quest_vr_bridge_update_views(const float positions[2][3],
                                  const float rotations[2][4],
                                  const float fovs[2][4],
                                  const uint32_t widths[2],
                                  const uint32_t heights[2]) {
    const float center[3] = {
        (positions[0][0] + positions[1][0]) * 0.5f,
        (positions[0][1] + positions[1][1]) * 0.5f,
        (positions[0][2] + positions[1][2]) * 0.5f,
    };
    if (sRecenterPending || !sReferenceValid) {
        memcpy(sReferencePosition, center, sizeof(sReferencePosition));
        const float yaw = atan2f(
            2.0f * (rotations[0][3] * rotations[0][1] + rotations[0][0] * rotations[0][2]),
            1.0f - 2.0f * (rotations[0][0] * rotations[0][0] + rotations[0][1] * rotations[0][1]));
        sReferenceRotation[0] = 0.0f;
        sReferenceRotation[1] = -sinf(yaw * 0.5f);
        sReferenceRotation[2] = 0.0f;
        sReferenceRotation[3] = cosf(yaw * 0.5f);
        sCalibratedHeadHeight = fabsf(center[1]);
        if (sCalibratedHeadHeight < 0.75f || sCalibratedHeadHeight > 2.50f)
            sCalibratedHeadHeight = 1.65f;
        sReferenceValid = true;
        sRecenterPending = false;
        ++sTrackingOriginGeneration;
    }
    float delta[3] = {
        center[0] - sReferencePosition[0],
        center[1] - sReferencePosition[1],
        center[2] - sReferencePosition[2],
    };
    rotate_vector(sReferenceRotation, delta, sHeadPosition);
    relative_rotation(rotations[0], sHeadRotation);

    memcpy(sEyePosition, positions, sizeof(sEyePosition));
    memcpy(sEyeRotation, rotations, sizeof(sEyeRotation));
    memcpy(sEyeFov, fovs, sizeof(sEyeFov));
    memcpy(sEyeWidth, widths, sizeof(sEyeWidth));
    memcpy(sEyeHeight, heights, sizeof(sEyeHeight));
    sPoseValid = true;
}

/*
 * Android owns the OpenXR frame loop in quest_openxr_bootstrap.c. These are
 * the backend-facing compatibility hooks used by the shared game code. Pose
 * and eye hand-off is populated by the native host as game rendering is wired
 * into that loop.
 */
bool vr_openxr_startup(void) { return true; }
bool vr_openxr_create_session(void) { return true; }
bool vr_openxr_begin_frame(void) { return true; }
bool vr_openxr_end_frame(void) { return true; }
bool vr_openxr_begin_eye(uint32_t eye, uint32_t *w, uint32_t *h) {
    if (eye >= 2 || !sPoseValid) return false;
    if (w) *w = sEyeWidth[eye];
    if (h) *h = sEyeHeight[eye];
    return true;
}
bool vr_openxr_end_eye(uint32_t eye) { (void)eye; return true; }
bool vr_openxr_mirror_eye(uint32_t eye, uint32_t w, uint32_t h) {
    (void)eye; (void)w; (void)h; return false;
}
bool vr_openxr_get_eye_offset(uint32_t eye, float out[3]) {
    if (!out || eye >= 2) return false;
    const float cx = (sEyePosition[0][0] + sEyePosition[1][0]) * 0.5f;
    const float cy = (sEyePosition[0][1] + sEyePosition[1][1]) * 0.5f;
    const float cz = (sEyePosition[0][2] + sEyePosition[1][2]) * 0.5f;
    const float delta[3] = {
        sEyePosition[eye][0] - cx,
        sEyePosition[eye][1] - cy,
        sEyePosition[eye][2] - cz,
    };
    const float inverse_view[4] = {
        -sEyeRotation[0][0], -sEyeRotation[0][1],
        -sEyeRotation[0][2], sEyeRotation[0][3],
    };
    rotate_vector(inverse_view, delta, out);
    return true;
}
bool vr_openxr_get_eye_fov(uint32_t eye, float out[4]) {
    if (!out || eye >= 2 || !sPoseValid) return false;
    memcpy(out, sEyeFov[eye], sizeof(float) * 4);
    return true;
}
bool vr_openxr_get_head_rotation(float out[4]) {
    if (!out || !sPoseValid || !sReferenceValid) return false;
    memcpy(out, sHeadRotation, sizeof(sHeadRotation)); return true;
}
bool vr_openxr_get_head_translation(float out[3]) {
    if (!out || !sPoseValid || !sReferenceValid) return false;
    memcpy(out, sHeadPosition, sizeof(sHeadPosition));
    return true;
}
bool vr_openxr_get_calibrated_head_height(float *height) {
    if (!height || !sReferenceValid) return false;
    *height = sCalibratedHeadHeight;
    return true;
}
uint32_t vr_openxr_get_tracking_origin_generation(void) { return sTrackingOriginGeneration; }
void vr_openxr_request_recenter(void) {
    sRecenterPending = true;
    sReferenceValid = false;
}
bool vr_openxr_get_controller_state(uint32_t hand, struct VrControllerState *state) {
    if (!state || hand >= 2 || !sControllerAvailable[hand]) return false;
    memcpy(state, &sControllers[hand], sizeof(*state));
    return true;
}
bool vr_openxr_apply_haptic(uint32_t hand, float amplitude, float seconds, float frequency) {
    (void)hand; (void)amplitude; (void)seconds; (void)frequency; return false;
}
void vr_openxr_shutdown(void) {}
bool vr_openxr_is_initialized(void) { return true; }
bool vr_openxr_has_session(void) { return true; }
bool vr_openxr_is_session_running(void) { return sPoseValid; }
