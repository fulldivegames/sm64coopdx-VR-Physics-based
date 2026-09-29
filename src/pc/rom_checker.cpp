#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <SDL2/SDL.h>

#if defined(_WIN32)
#include <windows.h>
#endif

extern "C" {
#include "platform.h"
#include "mods/mods_utils.h" // for path_ends_with
#include "mods/mod_cache.h"  // for md5 hashing
#include "mods/mods.h"
#include "loading.h"
#include "fs/fs.h"
#include "vr/vr.h"
}

namespace fs = std::filesystem;

bool gRomIsValid = false;
bool gRomSetupActive = false;
char gRomFilename[SYS_MAX_PATH] = "";

struct VanillaMD5 {
    const char *localizationName;
    const char *md5;
};

// lookup table for vanilla sm64 roms
static struct VanillaMD5 sVanillaMD5[] = {
    // { "eu", "45676429ef6b90e65b517129b700308e" },
    // { "jp", "85d61f5525af708c9f1e84dce6dc10e9" },
    // { "sh", "2d727c3278aa232d94f2fb45aec4d303" },
    { "us", "20b854b239203baf6c961b850a4a51a2" },
    { NULL, NULL },
};

inline static void rename_tmp_folder() {
    std::string userPath = fs_get_write_path("");
    std::string oldPath = userPath + "tmp";
    std::string newPath = userPath + TMP_DIRECTORY;
    if (fs::exists(oldPath) && !fs::exists(newPath)) {
#if defined(_WIN32)
        SetFileAttributesA(oldPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
        fs::rename(oldPath, newPath);
    }
}

static bool is_rom_valid(const std::string romPath) {
    u8 dataHash[16] = { 0 };
    mod_cache_md5(romPath.c_str(), dataHash);

    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(dataHash[i]);
    }

    for (VanillaMD5 *md5 = sVanillaMD5; md5->localizationName != NULL; md5++) {
        if (md5->md5 == ss.str()) {
            std::string destPath = fs_get_write_path("") + std::string("baserom.") + md5->localizationName + ".z64";

            // Copy the rom to the user path
            if (romPath != destPath && !std::filesystem::exists(std::filesystem::path(destPath))) {
                std::filesystem::copy_file(
                    std::filesystem::path(romPath),
                    std::filesystem::path(destPath)
                );
            }

            snprintf(gRomFilename, SYS_MAX_PATH, "%s", destPath.c_str()); // Load the copied rom
            gRomIsValid = true;
            return true;
        }
    }

    return false;
}

inline static bool scan_path_for_rom(const char *dir) {
    for (const auto &entry: std::filesystem::directory_iterator(dir)) {
        std::string path = entry.path().generic_string();
        if (path_ends_with(path.c_str(), ".z64")) {
            if (is_rom_valid(path)) { return true; }
        }
    }
    return false;
}

extern "C" {
// Setup owns its controller handles only until a ROM is selected. No gameplay
// bindings, saved options, or user files are changed by navigation.
struct RomBrowserEntry { fs::path path; std::string label; bool directory; };
static std::vector<RomBrowserEntry> sBrowserEntries;
static fs::path sBrowserPath;
static size_t sBrowserSelection;
static std::string sBrowserStatus;
static std::string sBrowserText;
static std::vector<SDL_GameController*> sBrowserPads;
static bool sBrowserAccept, sBrowserBack, sBrowserLocations;
static int sBrowserDirection;
static Uint32 sBrowserRepeat, sBrowserPadScan;
static int sBrowserPadCount = -1;

static std::string browser_label(const fs::path &path) {
    auto utf8 = path.u8string();
    std::string label(utf8.begin(), utf8.end());
    for (char &c : label) { if (c == '\\') c = '/'; if ((unsigned char)c < 32) c = ' '; }
    if (label.size() > 42) label = "..." + label.substr(label.size() - 39);
    return label;
}

static void browser_read(const fs::path &path) {
    sBrowserEntries.clear();
    sBrowserSelection = 0;
    sBrowserPath = path;
    sBrowserStatus.clear();
    std::error_code ec;
    if (path.empty()) {
        auto add = [](const fs::path &p, const char *label) {
            std::error_code error;
            if (!p.empty() && fs::is_directory(p, error)) sBrowserEntries.push_back({p, label, true});
        };
        const char *home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) { add(fs::path(home) / "Downloads", "Downloads"); add(home, "Home folder"); }
        add(sys_exe_path_dir(), "Game folder");
#if defined(_WIN32)
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) if (drives & (1u << i)) {
            std::string drive = "A:/"; drive[0] += i;
            sBrowserEntries.push_back({fs::path(drive), drive, true});
        }
#else
        add("/", "Filesystem");
#endif
        return;
    }
    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) { sBrowserStatus = "Cannot open this folder. Press B to go back."; return; }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code entryError;
        bool directory = it->is_directory(entryError);
        if (entryError) continue;
        auto utf8Extension = it->path().extension().u8string();
        std::string extension(utf8Extension.begin(), utf8Extension.end());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (directory || extension == ".z64")
            sBrowserEntries.push_back({it->path(), browser_label(it->path().filename()), directory});
    }
    std::sort(sBrowserEntries.begin(), sBrowserEntries.end(), [](const RomBrowserEntry &a, const RomBrowserEntry &b) {
        if (a.directory != b.directory) return a.directory > b.directory;
        return a.label < b.label;
    });
    if (sBrowserEntries.empty()) sBrowserStatus = "No .z64 files or folders here. B: parent folder";
}

void rom_browser_action(int action) {
    if (!gRomSetupActive || gRomIsValid) return;
    if (action == 4) { browser_read({}); return; }
    if (action == 3) {
        fs::path parent = sBrowserPath.parent_path();
        browser_read(parent == sBrowserPath ? fs::path() : parent);
        return;
    }
    if (sBrowserEntries.empty()) return;
    if (action == -1 && sBrowserSelection > 0) --sBrowserSelection;
    if (action == 1 && sBrowserSelection + 1 < sBrowserEntries.size()) ++sBrowserSelection;
    if (action != 2) return;
    const RomBrowserEntry entry = sBrowserEntries[sBrowserSelection];
    if (entry.directory) { browser_read(entry.path); return; }
    try {
#if defined(_WIN32)
        char portable[SYS_MAX_PATH];
        if (!sys_windows_short_path_from_wcs(portable, sizeof(portable), entry.path.c_str())) {
            sBrowserStatus = "Cannot read this path. Choose another file."; return;
        }
        bool valid = is_rom_valid(portable);
#else
        bool valid = is_rom_valid(entry.path.string());
#endif
        sBrowserStatus = valid ? "ROM accepted. Loading..." : "Not a valid US Super Mario 64 ROM. Choose another file.";
    } catch (const fs::filesystem_error &) {
        sBrowserStatus = "Could not read/import ROM. Choose another file.";
    }
}

void rom_browser_begin(void) {
    sBrowserAccept = sBrowserBack = sBrowserLocations = false;
    sBrowserDirection = 0; sBrowserRepeat = sBrowserPadScan = 0;
    sBrowserPadCount = -1;
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    browser_read({});
}

void rom_browser_end(void) {
    for (auto pad : sBrowserPads) SDL_GameControllerClose(pad);
    sBrowserPads.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    sBrowserEntries.clear();
}

void rom_browser_update(void) {
    bool accept = false, back = false, locations = false;
    float y = 0.0f;
    for (unsigned hand = 0; hand < VR_CONTROLLER_COUNT; ++hand) {
        VrControllerState state = {};
        if (!vr_get_controller_state(hand, &state)) continue;
        accept |= state.primaryButton || state.trigger > 0.7f;
        back |= state.secondaryButton;
        locations |= state.menuButton;
        if (std::abs(state.thumbstick[1]) > std::abs(y)) y = state.thumbstick[1];
    }
    const Uint32 now = SDL_GetTicks();
    if ((sBrowserPadScan == 0 || now - sBrowserPadScan > 1000) && sBrowserPadCount != SDL_NumJoysticks()) {
        for (auto pad : sBrowserPads) SDL_GameControllerClose(pad);
        sBrowserPads.clear();
        for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) {
            auto pad = SDL_GameControllerOpen(i); if (pad) sBrowserPads.push_back(pad);
        }
        sBrowserPadScan = now;
        sBrowserPadCount = SDL_NumJoysticks();
    }
    SDL_GameControllerUpdate();
    for (auto pad : sBrowserPads) {
        accept |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) != 0;
        back |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) != 0;
        locations |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) != 0;
        float axis = -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32768.0f;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) axis = 1;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) axis = -1;
        if (std::abs(axis) > std::abs(y)) y = axis;
    }
    int direction = y > 0.6f ? -1 : y < -0.6f ? 1 : 0;
    if (direction && (direction != sBrowserDirection || now - sBrowserRepeat > 250)) {
        rom_browser_action(direction); sBrowserRepeat = now;
    }
    sBrowserDirection = direction;
    if (locations && !sBrowserLocations) rom_browser_action(4);
    else if (back && !sBrowserBack) rom_browser_action(3);
    else if (accept && !sBrowserAccept) rom_browser_action(2);
    sBrowserAccept = accept; sBrowserBack = back; sBrowserLocations = locations;
}

const char *rom_browser_text(void) {
    std::ostringstream out;
    out << "SELECT YOUR SUPER MARIO 64 US ROM\nStick: move   A / trigger: open\nB: back   Menu: locations\n";
    out << (sBrowserPath.empty() ? "Locations" : browser_label(sBrowserPath)) << "\n\n";
    size_t first = (sBrowserSelection / 7) * 7;
    for (size_t i = first; i < sBrowserEntries.size() && i < first + 7; ++i) {
        out << (i == sBrowserSelection ? "> " : "  ") << (sBrowserEntries[i].directory ? "[Folder] " : "") << sBrowserEntries[i].label << "\n";
    }
    out << "\n" << sBrowserStatus << "\n";
    if (!sBrowserEntries.empty()) out << (sBrowserSelection + 1) << " / " << sBrowserEntries.size();
    sBrowserText = out.str();
    return sBrowserText.c_str();
}

void legacy_folder_handler(void) {
    rename_tmp_folder();
}

bool main_rom_handler(void) {
    if (scan_path_for_rom(fs_get_write_path(""))) { return true; }
    scan_path_for_rom(sys_exe_path_dir());
    return gRomIsValid;
}

void rom_on_drop_file(const char *path) {
    static bool hasDroppedInvalidFile = false;
    if (strlen(path) > 0 && !is_rom_valid(path) && !hasDroppedInvalidFile) {
        hasDroppedInvalidFile = true;
        strcat(gCurrLoadingSegment.str, "\n\\#ffc000\\The file you last dropped was not a valid, vanilla SM64 rom.");
    }
}
}
