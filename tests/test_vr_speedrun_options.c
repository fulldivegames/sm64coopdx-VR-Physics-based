#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
typedef unsigned int u32;
typedef short s16;
typedef float f32;
typedef float Vec3f[3];
#define ACT_FLAG_AIR 1
#define INPUT_A_PRESSED 2
#define CHAR_SOUND_PUNCH_YAH 10
#define CHAR_SOUND_PUNCH_WAH 11
#define VR_PUNCH_SOUND_COMBO_RESET_FRAMES 20
struct MarioState { u32 action,input; void* floor; float pos[3],floorHeight; s16 faceAngle[3],intendedYaw; };
static bool configVrSpeedRunningMode, configVrVanillaMovement, configVrDisablePunchSound;
static bool configVrMarioPunchSound=true, configVrTurnDuringJumps=true, networkAllowed=true;
static bool jumpPriority, directionAvailable=true;
static int sounds,sVrPunchSoundComboStep,sVrPunchSoundComboResetFrames;
static bool ns_coopnet_vr_gameplay_allowed(void){return networkAllowed;}
static bool vr_jump_gesture_has_priority(void){return jumpPriority;}
static void play_character_sound(struct MarioState*m,int s){(void)m;assert(s==10||s==11);sounds++;}
static bool vr_is_normal_jump_landing_action(u32 a){return a==100;}
static bool vr_get_first_person_aim_direction(struct MarioState*m,Vec3f out){(void)m;out[0]=1;out[1]=out[2]=0;return directionAvailable;}
static s16 atan2s(float a,float b){(void)a;(void)b;return 1234;}
#include "../build/test_vr_speedrun_options.inc"
int main(void) {
    for(int mode=0;mode<2;mode++)for(int online=0;online<2;online++){
        configVrSpeedRunningMode=mode; networkAllowed=online;
        assert(vr_gameplay_modifiers_allowed()==(!mode&&online));
        assert(configVrSpeedRunningMode==mode && networkAllowed==online);
    }
    struct MarioState m={.floor=(void*)1};
    for(int muted=0;muted<2;muted++)for(int enabled=0;enabled<2;enabled++){
        configVrDisablePunchSound=muted;configVrMarioPunchSound=enabled;
        sounds=0;sVrPunchSoundComboStep=0;
        vr_hand_interaction_update_punch_sound(&m);
        vr_hand_interaction_update_punch_sound(&m);
        assert(sounds==((!muted&&enabled)?2:0));
        assert(m.action==0 && m.input==0);
    }
    configVrDisablePunchSound=false;configVrMarioPunchSound=true;
    for(int i=0;i<3;i++){
        m.action=i==0?ACT_FLAG_AIR:0; m.input=i==1?INPUT_A_PRESSED:0;jumpPriority=i==2;
        sounds=0;vr_hand_interaction_update_punch_sound(&m);assert(!sounds);
    }
    for(int restrictive=0;restrictive<2;restrictive++)for(int turn=0;turn<2;turn++)for(int mode=0;mode<2;mode++){
        configVrVanillaMovement=restrictive;configVrTurnDuringJumps=turn;configVrSpeedRunningMode=mode;
        m.action=ACT_FLAG_AIR;m.faceAngle[1]=22;m.intendedYaw=33;
        vr_turn_toward_headset_on_jump_landing(&m,100);
        assert(m.faceAngle[1]==((!restrictive&&turn)?1234:22));
        assert(m.intendedYaw==((!restrictive&&turn)?1234:33));
        assert(configVrTurnDuringJumps==turn);
    }
    puts("PASS: temporary gameplay policy, physical-only punch mute, jump guards, independent landing restriction, saved preferences retained");
}
