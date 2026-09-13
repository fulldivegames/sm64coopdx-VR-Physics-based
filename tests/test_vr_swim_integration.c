#include <assert.h>
#include <stdio.h>
// Exercise the real game-tick update, including its gating, resets and drift.
#include "../build/test_vr_swim_update.inc"

bool configVrPhysicalSwimming=true, configVrMotionControllerInput=true;
unsigned int configVrSwimmingSpeed=100;
bool gInteractableOverridePad=false, gDjuiInMainMenu=false;
s16 gMenuMode=-1;
u32 gGlobalTimer;
static double testTime;
static u32 testOrigin;
static struct VrControllerState controllers[2];
static float basisYaw;
static float headPose[4]={0,0,0,1};
static float handHeight=-.4f;
bool vr_get_head_rotation(float out[4]) { memcpy(out,headPose,sizeof(headPose)); return true; }
f64 clock_elapsed_f64(void) { return testTime; }
bool vr_is_active(void) { return true; }
bool vr_gameplay_modifiers_allowed(void) { return true; }
u32 vr_get_tracking_origin_generation(void) { return testOrigin; }
bool vr_get_head_translation(float out[3]) { memset(out,0,12); return true; }
bool vr_get_controller_state(u32 hand,struct VrControllerState* out) {
    *out=controllers[hand]; return true;
}
bool vr_hand_interaction_is_physical_climb_active(struct MarioState* m) { (void)m; return false; }
bool vr_get_gameplay_tracking_basis(Mat4 out) {
    memset(out,0,sizeof(Mat4));
    out[0][0]=out[2][2]=cosf(basisYaw); out[1][1]=out[3][3]=1;
    out[0][2]=-sinf(basisYaw); out[2][0]=sinf(basisYaw); return true;
}
struct Surface;
f32 find_floor(f32 x,f32 y,f32 z,struct Surface** floor) {
    (void)x;(void)y;(void)z;(void)floor; assert(!"unexpected terrain query"); return 0;
}
static Vec3f result;
static void tick(struct MarioState* m, float z) {
    ++gGlobalTimer; testTime+=1.0/30;
    for(int h=0;h<2;h++) {
        controllers[h].gripPoseValid=true;
        controllers[h].gripPosition[0]=h ? .3f:-.3f;
        controllers[h].gripPosition[1]=handHeight;
        controllers[h].gripPosition[2]=z;
        // Turn the physical stroke with the user for yaw-directed test cases.
        if (headPose[1]>.5f) {
            controllers[h].gripPosition[2]=-controllers[h].gripPosition[0];
            controllers[h].gripPosition[0]=z;
        }
    }
    vec3f_set(result,0,0,0);
    vr_physical_swim_step(m,result);
}
int main(void) {
    struct MarioState m={.playerIndex=0,.action=ACT_WATER_IDLE,.health=0x880};
    for(int turn=0;turn<4;turn++) {
        basisYaw=turn*1.5707963268f; ++testOrigin;
        for(int cycle=0;cycle<10;cycle++) {
            for(int n=0;n<=10;n++) tick(&m,-.1f-n*.04f);
            float before=sPhysicalSwim.drift[0]*-sinf(basisYaw)+sPhysicalSwim.drift[2]*-cosf(basisYaw);
            for(int n=1;n<=10;n++) tick(&m,-.5f+n*.04f);
            float after=result[0]*-sinf(basisYaw)+result[2]*-cosf(basisYaw);
            assert(after>before && after>5);
        }
    }
    // Normal pulls follow head yaw and pitch, not the hand's travel axis.
    basisYaw=0; ++testOrigin;
    headPose[1]=.70710678f; headPose[3]=.70710678f;
    tick(&m,-.5f);
    for(int n=1;n<=10;n++) tick(&m,-.5f+n*.04f);
    assert(result[0]<-5 && fabsf(result[2])<.001f);
    ++testOrigin; headPose[1]=0; headPose[0]=-.5f; headPose[3]=.8660254f;
    tick(&m,-.5f);
    for(int n=1;n<=10;n++) tick(&m,-.5f+n*.04f);
    assert(result[1]<-5 && result[2]<-5);
    // Overhead pull overrides a level head and stays upward below head height.
    ++testOrigin; headPose[0]=0; headPose[3]=1; handHeight=.5f;
    tick(&m,-.1f);
    for(int n=1;n<=8;n++) { handHeight=.5f-n*.05f; tick(&m,-.1f); }
    assert(result[1]>5 && fabsf(result[2])<.001f);
    float upwardBefore=result[1];
    handHeight=.5f; tick(&m,-.1f);
    assert(result[1]<upwardBefore); // Recovery cannot add upward thrust.
    handHeight=-.4f;
    configVrPhysicalSwimming=false; tick(&m,-.5f);
    assert(!sPhysicalSwim.valid && result[0]==0 && result[2]==0);
    configVrPhysicalSwimming=true; tick(&m,-.5f);
    for(int n=1;n<=8;n++) tick(&m,-.5f+n*.04f);
    assert(sPhysicalSwim.valid);
    gMenuMode=0; tick(&m,-.1f); assert(!sPhysicalSwim.valid);
    puts("PASS: actual swim update, 40 repeated two-hand strokes, headset yaw/pitch direction, overhead upward pull while looking forward, recovery, disabled/menu reset");
}
