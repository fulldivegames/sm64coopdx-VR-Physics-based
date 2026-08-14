#ifndef VERSION_H
#define VERSION_H

#define SM64COOPDX_VERSION "v1.5.1"

// Public release version for the VR fork. Keep this separate from
// SM64COOPDX_VERSION because the latter is part of multiplayer compatibility.
#if defined(__ANDROID__)
#define SM64COOPDX_VR_VERSION "v0.5.7"
#else
#define SM64COOPDX_VR_VERSION "v0.4.0"
#endif

// internal version
#define VERSION_TEXT "v"
#define VERSION_NUMBER 42
#define MINOR_VERSION_NUMBER 1

#define VERSION_OFFSET 37 // difference from old versioning system

#if defined(VERSION_JP)
#define VERSION_REGION "JP"
#elif defined(VERSION_EU)
#define VERSION_REGION "EU"
#elif defined(VERSION_SH)
#define VERSION_REGION "SH"
#else
#define VERSION_REGION "US"
#endif

#ifdef DEVELOPMENT
#define GAME_NAME "sm64coopdx-dev"
#define WINDOW_NAME "Super Mario 64 Coop Deluxe (DEV)"
#elif !defined(VERSION_US)
#define GAME_NAME "sm64coopdx-intl"
#define WINDOW_NAME "Super Mario 64 Coop Deluxe (INTL)"
#else
#define GAME_NAME "sm64coopdx"
#define WINDOW_NAME "SM64 Co-Op DX VR"
#endif

#define MAX_VERSION_LENGTH 128

const char* get_version(void);
const char* get_vr_version(void);
#ifdef COMPILE_TIME
const char* get_version_with_build_date(void);
#endif

#endif
