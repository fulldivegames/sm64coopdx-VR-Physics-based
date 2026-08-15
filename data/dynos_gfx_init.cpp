#include "dynos.cpp.h"
extern "C" {
#include "pc/loading.h"
}

#define MOD_PATH_LEN 1024

void DynOS_Gfx_GenerateModPacks(char* modPath) {
    // If pack folder exists, generate bins
    SysPath _LevelPackFolder = fstring("%s/levels", modPath);
    if (fs_sys_dir_exists(_LevelPackFolder.c_str())) {
        DynOS_Lvl_GeneratePack(_LevelPackFolder);
    }

    SysPath _ActorPackFolder = fstring("%s/actors", modPath);
    if (fs_sys_dir_exists(_ActorPackFolder.c_str())) {
        DynOS_Actor_GeneratePack(_ActorPackFolder);
    }

    SysPath _BehaviorPackFolder = fstring("%s/data", modPath);
    if (fs_sys_dir_exists(_BehaviorPackFolder.c_str())) {
        DynOS_Bhv_GeneratePack(_BehaviorPackFolder);
    }

    SysPath _TexturePackFolder = fstring("%s", modPath);
    SysPath _TexturePackOutputFolder = fstring("%s/textures", modPath);
    if (fs_sys_dir_exists(_TexturePackFolder.c_str())) {
        DynOS_Tex_GeneratePack(_TexturePackFolder, _TexturePackOutputFolder, true);
    }
}

void DynOS_Gfx_GeneratePacks(const char* directory) {
    if (configSkipPackGeneration) { return; }

    static char sModPath[MOD_PATH_LEN] = "";

    LOADING_SCREEN_MUTEX(
        loading_screen_reset_progress_bar();
        snprintf(gCurrLoadingSegment.str, 256, "Generating DynOS Packs In Path:\n\\#808080\\%s", directory);
    );

    DIR *modsDir = opendir(directory);
    if (!modsDir) { return; }

    struct dirent *dir = NULL;
    u32 pathCount = 0;
    while ((dir = readdir(modsDir)) != NULL) {
        if (SysPath(dir->d_name) == "." ||
            SysPath(dir->d_name) == "..") continue;
        pathCount++;
    }
    rewinddir(modsDir);

    u32 processedCount = 0;
    while ((dir = readdir(modsDir)) != NULL) {
        // Skip . and ..
        if (SysPath(dir->d_name) == ".") continue;
        if (SysPath(dir->d_name) == "..") continue;

        // build mod path
        snprintf(sModPath, MOD_PATH_LEN, "%s/%s", directory, dir->d_name);

        // generate packs
        DynOS_Gfx_GenerateModPacks(sModPath);
        processedCount++;
        LOADING_SCREEN_MUTEX(
            gCurrLoadingSegment.percentage = pathCount > 0
                ? (f32) processedCount / (f32) pathCount
                : 1.0f
        );
    }

    closedir(modsDir);
}

static void ScanPacksFolder(SysPath _DynosPacksFolder) {
    DIR *_DynosPacksDir = opendir(_DynosPacksFolder.c_str());
    if (_DynosPacksDir) {
        struct dirent *_DynosPacksEnt = NULL;
        while ((_DynosPacksEnt = readdir(_DynosPacksDir)) != NULL) {

            // Skip . and ..
            if (SysPath(_DynosPacksEnt->d_name) == ".") continue;
            if (SysPath(_DynosPacksEnt->d_name) == "..") continue;

            // If pack folder exists, add it to the pack list
            SysPath _PackFolder = fstring("%s/%s", _DynosPacksFolder.c_str(), _DynosPacksEnt->d_name);
            if (fs_sys_dir_exists(_PackFolder.c_str())) {
                LOADING_SCREEN_MUTEX(snprintf(gCurrLoadingSegment.str, 256, "Generating DynOS Pack:\n\\#808080\\%s", _PackFolder.c_str()));
                DynOS_Pack_Add(_PackFolder);
                DynOS_Actor_GeneratePack(_PackFolder);
                // Some downloadable packs retain the normal mod wrapper and
                // place actor folders under PackName/actors/. Texture scanning
                // is recursive enough to make those packs appear enabled, but
                // the player actor was previously never generated.
                SysPath _WrappedActorsFolder = fstring("%s/actors", _PackFolder.c_str());
                if (fs_sys_dir_exists(_WrappedActorsFolder.c_str())) {
                    DynOS_Actor_GeneratePack(_WrappedActorsFolder);
                }
                DynOS_Tex_GeneratePack(_PackFolder, _PackFolder, false);
            }
        }
        closedir(_DynosPacksDir);
    }
}

void DynOS_Gfx_Init() {
    // Scan the DynOS packs folder
    SysPath _DynosPacksFolder = fstring("%s/%s", DYNOS_EXE_FOLDER, DYNOS_PACKS_FOLDER);
    ScanPacksFolder(_DynosPacksFolder);

    // Scan the user path folder
    SysPath _DynosPacksUserFolder = fstring("%s%s", DYNOS_USER_FOLDER, DYNOS_PACKS_FOLDER);
    ScanPacksFolder(_DynosPacksUserFolder);
}
