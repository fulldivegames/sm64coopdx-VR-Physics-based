#ifndef RENDERING_GRAPH_NODE_H
#define RENDERING_GRAPH_NODE_H

#include <PR/ultratypes.h>

#include "engine/graph_node.h"

struct VrControllerState;

#define MATRIX_STACK_SIZE 64
extern Mat4 gMatStack[MATRIX_STACK_SIZE];
extern Mat4 gMatStackPrev[MATRIX_STACK_SIZE];

extern f32 gProjectionMaxNearValue;
extern s16 gProjectionVanillaNearValue;
extern s16 gProjectionVanillaFarValue;

extern struct GrowingPool *gDisplayListHeap;
extern struct GraphNodeRoot *gCurGraphNodeRoot;
extern struct GraphNodeMasterList *gCurGraphNodeMasterList;
extern struct GraphNodePerspective *gCurGraphNodeCamFrustum;
extern struct GraphNodeCamera *gCurGraphNodeCamera;
extern struct GraphNodeObject *gCurGraphNodeObject;
extern struct GraphNodeHeldObject *gCurGraphNodeHeldObject;
extern u16 gAreaUpdateCounter;
extern struct Object* gCurGraphNodeProcessingObject;

// after processing an object, the type is reset to this
#define ANIM_TYPE_NONE                  0

// Not all parts have full animation: to save space, some animations only
// have xz, y, or no translation at all. All animations have rotations though
#define ANIM_TYPE_TRANSLATION           1
#define ANIM_TYPE_VERTICAL_TRANSLATION  2
#define ANIM_TYPE_LATERAL_TRANSLATION   3
#define ANIM_TYPE_NO_TRANSLATION        4

// Every animation includes rotation, after processing any of the above
// translation types the type is set to this
#define ANIM_TYPE_ROTATION              5

extern f32 gOverrideFOV;
extern f32 gOverrideNear;
extern f32 gOverrideFar;

void geo_process_node_and_siblings(struct GraphNode *firstNode);
void geo_process_root(struct GraphNodeRoot *node, Vp *b, Vp *c, s32 clearColor);
void register_mtx_vr_ui(Mtx *matrix);
void register_mtx_vr_hud(Mtx *matrix);
void vr_reset_first_person_calibration(void);
void vr_handle_camera_mode_change(void);
void vr_adjust_first_person_camera_direction(Vec3f direction);
bool vr_align_first_person_camera_yaw(s16 worldYaw);
s16 vr_get_first_person_action_turn_yaw(void);
bool vr_get_controller_world_fist(
    u32 handIndex,
    Vec3f worldPosition,
    Vec3f worldVelocity
);
bool vr_get_controller_world_fist_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition,
    Vec3f worldVelocity
);
bool vr_get_controller_world_fist_raw_from_state(
    u32 handIndex,
    const struct VrControllerState* state,
    Vec3f worldPosition,
    Vec3f worldVelocity
);
bool vr_get_controller_climb_fist(
    const Vec3f worldPosition,
    Vec3f climbPosition
);
bool vr_is_controller_holding_cap(u32 handIndex);
bool vr_is_controller_holding_fire_flower(u32 handIndex);
bool vr_get_stabilized_headset_world_position(
    Vec3f worldPosition,
    bool previousFrame
);
void vr_invalidate_first_person_tracked_world_cache(void);

struct GraphNodeInterpData {
    Vec3s translation;
    Vec3s rotation;
    Vec3f scale;
    u32 timestamp;
};

struct GraphNodeInterpData *geo_get_interp_data(void *node, struct GraphNodeObject *obj);
void geo_clear_interp_data();

struct ShadowInterp {
    Gfx*  gfx;
    Vec3f shadowPos;
    Vec3f shadowPosPrev;
    Vtx *verts;
    Gfx *displayList;
    struct GraphNodeShadow *node;
    f32 shadowScale;
    struct GraphNodeObject *obj;
};

#endif // RENDERING_GRAPH_NODE_H
