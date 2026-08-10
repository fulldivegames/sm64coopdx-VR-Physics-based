#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <stdbool.h>
#include <PR/ultratypes.h>
#include "game/player_palette.h"

#define CONFIGFILE_DEFAULT "sm64config.txt"
#define CONFIGFILE_BACKUP "sm64config-backup.txt"

#define MAX_BINDS  3
#define MAX_VOLUME 127
#define MAX_CONFIG_STRING 64
#define MAX_SAVE_NAME_STRING 32

#define DEFAULT_PORT 7777
#define DEFAULT_COOPNET_IP "net.coop64.us"
#define DEFAULT_COOPNET_PORT 34197

// Camera height is stored directly in world units above Mario's gameplay
// anchor, so the value shown in Camera Settings matches the applied height.
#define VR_CAMERA_HEIGHT_MAX 1000U
#define VR_CAMERA_HEIGHT_LEGACY_CENTER 500U
#define VR_CAMERA_HAT_INSET 2U
#define VR_HEAD_ATTACHMENT_HEIGHT_MARIO   155U
#define VR_HEAD_ATTACHMENT_HEIGHT_LUIGI   171U
#define VR_HEAD_ATTACHMENT_HEIGHT_TOAD     83U
#define VR_HEAD_ATTACHMENT_HEIGHT_WALUIGI 208U
#define VR_HEAD_ATTACHMENT_HEIGHT_WARIO   189U
#define VR_HAT_TOP_HEIGHT_MARIO   217U
#define VR_HAT_TOP_HEIGHT_LUIGI   218U
#define VR_HAT_TOP_HEIGHT_TOAD    130U
#define VR_HAT_TOP_HEIGHT_WALUIGI 255U
#define VR_HAT_TOP_HEIGHT_WARIO   236U
#define VR_CAMERA_HEIGHT_DEFAULT_MARIO 160U
#define VR_CAMERA_HEIGHT_DEFAULT_LUIGI \
    (VR_HAT_TOP_HEIGHT_LUIGI - VR_CAMERA_HAT_INSET)
#define VR_CAMERA_HEIGHT_DEFAULT_TOAD \
    (VR_HAT_TOP_HEIGHT_TOAD - VR_CAMERA_HAT_INSET)
#define VR_CAMERA_HEIGHT_DEFAULT_WALUIGI \
    (VR_HAT_TOP_HEIGHT_WALUIGI - VR_CAMERA_HAT_INSET)
#define VR_CAMERA_HEIGHT_DEFAULT_WARIO \
    (VR_HAT_TOP_HEIGHT_WARIO - VR_CAMERA_HAT_INSET)
#define VR_CAMERA_DEPTH_CENTER  200U
#define VR_CAMERA_DEPTH_MAX     400U
#define VR_BACKPEDAL_SPEED_MIN      8U
#define VR_BACKPEDAL_SPEED_MAX     32U
#define VR_BACKPEDAL_SPEED_DEFAULT 20U
#define VR_RENDER_SCALE_MIN        25U
#define VR_RENDER_SCALE_MAX       100U

typedef struct {
    unsigned int x, y, w, h;
    bool vsync;
    bool reset;
    bool fullscreen;
    bool exiting_fullscreen;
    bool settings_changed;
    unsigned int msaa;
} ConfigWindow;

typedef struct {
    bool rotateLeft;
    bool invertLeftX;
    bool invertLeftY;
    bool rotateRight;
    bool invertRightX;
    bool invertRightY;
} ConfigStick;

enum RefreshRateMode {
    RRM_AUTO,
    RRM_MANUAL,
    RRM_UNLIMITED,
    RRM_MAX
};

enum GraphicsBackend {
    GAPI_GL,
#if defined(_WIN32)
    GAPI_D3D11,
#endif
    GAPI_MAX
};

enum VrCameraMode {
    VR_CAMERA_MODE_THIRD_PERSON,
    VR_CAMERA_MODE_FIRST_PERSON,
    VR_CAMERA_MODE_COUNT
};

// Logical OpenXR controls used by the VR controller remapper. OpenXR maps
// these generic inputs onto the active headset/controller interaction profile.
enum VrControllerBinding {
    VR_CONTROLLER_BINDING_DISABLED,
    VR_CONTROLLER_BINDING_LEFT_PRIMARY,
    VR_CONTROLLER_BINDING_LEFT_SECONDARY,
    VR_CONTROLLER_BINDING_LEFT_TRIGGER,
    VR_CONTROLLER_BINDING_LEFT_GRIP,
    VR_CONTROLLER_BINDING_LEFT_STICK_CLICK,
    VR_CONTROLLER_BINDING_LEFT_MENU,
    VR_CONTROLLER_BINDING_RIGHT_PRIMARY,
    VR_CONTROLLER_BINDING_RIGHT_SECONDARY,
    VR_CONTROLLER_BINDING_RIGHT_TRIGGER,
    VR_CONTROLLER_BINDING_RIGHT_GRIP,
    VR_CONTROLLER_BINDING_RIGHT_STICK_CLICK,
    VR_CONTROLLER_BINDING_RIGHT_MENU,
    VR_CONTROLLER_BINDING_COUNT
};

enum VrControllerStick {
    VR_CONTROLLER_STICK_LEFT,
    VR_CONTROLLER_STICK_RIGHT,
    VR_CONTROLLER_STICK_DISABLED,
    VR_CONTROLLER_STICK_COUNT
};

extern char configSaveNames[4][MAX_SAVE_NAME_STRING];

// display settings
extern ConfigWindow configWindow;
extern ConfigStick configStick;
extern enum GraphicsBackend configGraphicsBackend;
extern unsigned int configFiltering;
extern bool         configShowFPS;
extern bool         configVrAutoStart;
extern unsigned int configVrCameraMode;
extern unsigned int configVrCameraDistance;
extern unsigned int configVrCameraHeight;
extern unsigned int configVrCameraDepth;
unsigned int* config_vr_camera_height_for_character(
    unsigned int characterIndex
);
unsigned int config_vr_camera_default_height_for_character(
    unsigned int characterIndex
);
unsigned int config_vr_head_attachment_height_for_character(
    unsigned int characterIndex
);
extern unsigned int configVrMovementCalibration;
extern unsigned int configVrFov;
extern unsigned int configVrRenderScale;
extern bool         configVrDesktopMirror;
extern unsigned int configVrDesktopMirrorFps;
extern unsigned int configVrHudOpacity;
extern bool         configVrMotionControllerInput;
extern bool         configVrPunchButton;
extern unsigned int configVrMoveStick;
extern unsigned int configVrCameraStick;
extern unsigned int configVrJumpBinding;
extern unsigned int configVrAttackBinding;
extern unsigned int configVrCrouchBinding;
extern unsigned int configVrLBinding;
extern unsigned int configVrRBinding;
extern unsigned int configVrPauseBinding;
extern bool         configVrPhysicalPunching;
extern bool         configVrPhysicalGrabbing;
extern bool         configVrMarioPunchSound;
extern bool         configVrMotionControlledDive;
extern bool         configVrMotionControlledGroundDive;
extern unsigned int configVrPunchSpeed;
extern unsigned int configVrPunchDistance;
extern unsigned int configVrPunchGripThreshold;
extern unsigned int configVrPunchColliderLength;
extern unsigned int configVrBowserSpinAcceleration;
extern unsigned int configVrBowserMaxSpinSpeed;
extern unsigned int configVrGloveSize;
extern unsigned int configVrLeftGloveRotationX;
extern unsigned int configVrLeftGloveRotationY;
extern unsigned int configVrLeftGloveRotationZ;
extern unsigned int configVrLeftGlovePositionX;
extern unsigned int configVrLeftGlovePositionY;
extern unsigned int configVrLeftGlovePositionZ;
extern unsigned int configVrRightGloveRotationX;
extern unsigned int configVrRightGloveRotationY;
extern unsigned int configVrRightGloveRotationZ;
extern unsigned int configVrRightGlovePositionX;
extern unsigned int configVrRightGlovePositionY;
extern unsigned int configVrRightGlovePositionZ;
extern bool         configVrFirstPersonBody;
extern unsigned int configVrTorsoHeight;
extern unsigned int configVrLegHeight;
extern bool         configVrExperimentalSideFlipFollow;
extern bool         configVrExperimentalWallJumpTurn;
extern bool         configVrExperimentalFlatFirstPerson;
extern bool         configVrExperimentalTrueFirstPerson;
extern bool         configVrExperimentalTrueDiving;
extern bool         configVrExperimentalArmsMode;
extern bool         configVrExperimentalMountedBody;
extern bool         configVrPhysicalCrouching;
extern bool         configVrOriginalMarioMovement;
extern unsigned int configVrBackpedalSpeed;
extern bool         configShowPing;
extern enum RefreshRateMode configFramerateMode;
extern unsigned int configFrameLimit;
extern unsigned int configInterpolationMode;
extern unsigned int configDrawDistance;
// sound settings
extern unsigned int configMasterVolume;
extern unsigned int configMusicVolume;
extern unsigned int configSfxVolume;
extern unsigned int configEnvVolume;
extern bool         configFadeoutDistantSounds;
extern bool         configMuteFocusLoss;
extern unsigned int configSoundOutput;
// control binds
extern unsigned int configKeyA[MAX_BINDS];
extern unsigned int configKeyB[MAX_BINDS];
extern unsigned int configKeyX[MAX_BINDS];
extern unsigned int configKeyY[MAX_BINDS];
extern unsigned int configKeyStart[MAX_BINDS];
extern unsigned int configKeyL[MAX_BINDS];
extern unsigned int configKeyR[MAX_BINDS];
extern unsigned int configKeyZ[MAX_BINDS];
extern unsigned int configKeyCUp[MAX_BINDS];
extern unsigned int configKeyCDown[MAX_BINDS];
extern unsigned int configKeyCLeft[MAX_BINDS];
extern unsigned int configKeyCRight[MAX_BINDS];
extern unsigned int configKeyStickUp[MAX_BINDS];
extern unsigned int configKeyStickDown[MAX_BINDS];
extern unsigned int configKeyStickLeft[MAX_BINDS];
extern unsigned int configKeyStickRight[MAX_BINDS];
extern unsigned int configKeyChat[MAX_BINDS];
extern unsigned int configKeyPlayerList[MAX_BINDS];
extern unsigned int configKeyDUp[MAX_BINDS];
extern unsigned int configKeyDDown[MAX_BINDS];
extern unsigned int configKeyDLeft[MAX_BINDS];
extern unsigned int configKeyDRight[MAX_BINDS];
extern unsigned int configKeyConsole[MAX_BINDS];
extern unsigned int configKeyPrevPage[MAX_BINDS];
extern unsigned int configKeyNextPage[MAX_BINDS];
extern unsigned int configKeyDisconnect[MAX_BINDS];
extern unsigned int configStickDeadzone;
extern unsigned int configRumbleStrength;
extern unsigned int configGamepadNumber;
extern bool         configBackgroundGamepad;
extern bool         configExtendedReports;
extern bool         configDisableGamepads;
extern bool         configUseStandardKeyBindingsChat;
extern bool         configSmoothScrolling;
// free camera settings
extern bool         configEnableFreeCamera;
extern bool         configFreeCameraAnalog;
extern bool         configFreeCameraLCentering;
extern bool         configFreeCameraDPadBehavior;
extern bool         configFreeCameraHasCollision;
extern bool         configFreeCameraMouse;
extern unsigned int configFreeCameraXSens;
extern unsigned int configFreeCameraYSens;
extern unsigned int configFreeCameraAggr;
extern unsigned int configFreeCameraPan;
extern unsigned int configFreeCameraDegrade;
// romhack camera settings
extern unsigned int configEnableRomhackCamera;
extern bool         configRomhackCameraBowserFights;
extern bool         configRomhackCameraHasCollision;
extern bool         configRomhackCameraSwitchable;
extern bool         configRomhackCameraDPadBehavior;
extern bool         configRomhackCameraFollowing;
// common camera settings
extern bool         configCameraInvertX;
extern bool         configCameraInvertY;
extern bool         configCameraToxicGas;
// debug
extern bool         configLuaProfiler;
extern bool         configDebugPrint;
extern bool         configDebugInfo;
extern bool         configDebugError;
#ifdef DEVELOPMENT
extern bool         configCtxProfiler;
#endif
// player settings
extern char         configPlayerName[MAX_CONFIG_STRING];
extern unsigned int configPlayerModel;
extern struct PlayerPalette configPlayerPalette;
// coop settings
extern unsigned int configAmountOfPlayers;
extern bool         configBubbleDeath;
extern unsigned int configHostPort;
extern unsigned int configHostSaveSlot;
extern char         configJoinIp[MAX_CONFIG_STRING];
extern unsigned int configJoinPort;
extern unsigned int configNetworkSystem;
extern unsigned int configPlayerInteraction;
extern unsigned int configPlayerKnockbackStrength;
extern unsigned int configStayInLevelAfterStar;
extern bool         configNametags;
extern bool         configModDevMode;
extern unsigned int configBouncyLevelBounds;
extern bool         configSkipIntro;
extern bool         configPauseAnywhere;
extern bool         configMenuStaffRoll;
extern unsigned int configMenuLevel;
extern unsigned int configMenuSound;
extern bool         configMenuRandom;
extern bool         configMenuDemos;
extern bool         configDisablePopups;
extern char         configLanguage[MAX_CONFIG_STRING];
extern bool         configForce4By3;
extern bool         configDynosLocalPlayerModelOnly;
extern unsigned int configPvpType;
// CoopNet settings
extern char         configCoopNetIp[MAX_CONFIG_STRING];
extern unsigned int configCoopNetPort;
extern char         configPassword[MAX_CONFIG_STRING];
extern char         configDestId[MAX_CONFIG_STRING];
// DJUI settings
extern unsigned int configDjuiTheme;
extern bool         configDjuiThemeCenter;
extern bool         configDjuiThemeGradients;
extern unsigned int configDjuiThemeFont;
extern unsigned int configDjuiScale;
// other
extern unsigned int configRulesVersion;
extern bool         configHideSocketWarning;
extern bool         configCompressOnStartup;
extern bool         configSkipPackGeneration;

// secrets
extern bool configExCoopTheme;

void enable_queued_mods(void);
void enable_queued_dynos_packs(void);
void configfile_reset_keybinds(bool extra);
void configfile_load(void);
void configfile_save(const char *filename);
const char *configfile_name(void);
const char *configfile_backup_name(void);

#endif // CONFIGFILE_H
