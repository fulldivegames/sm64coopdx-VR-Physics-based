// Non-interactive setup regression test. Uses real browser/MD5 code and mocked
// tracked-controller input; it does not drive a desktop or claim XR device QA.
#define SDL_MAIN_HANDLED
#include "../src/pc/rom_checker.cpp"
#include <cassert>
extern "C" {
#include "pc/utils/md5.h"
}
static std::string testRoot, testSave;
static VrControllerState testHands[2];
const char *sys_exe_path_dir(void) { return testRoot.c_str(); }
bool sys_windows_short_path_from_wcs(char *out, size_t capacity, const wchar_t *path) {
    return WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)capacity, nullptr, nullptr) != 0;
}
extern "C" {
struct LoadingSegment gCurrLoadingSegment = {};
const char *fs_get_write_path(const char *) { return testSave.c_str(); }
bool path_ends_with(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    return a >= b && strcmp(path + a - b, suffix) == 0;
}
bool vr_get_controller_state(uint32_t hand, VrControllerState *state) {
    *state = testHands[hand]; return true;
}
void mod_cache_md5(const char *path, u8 *out) {
    MD5_CTX ctx = {}; MD5_Init(&ctx);
    std::ifstream file(path, std::ios::binary);
    char bytes[4096];
    while (file) { file.read(bytes, sizeof(bytes)); MD5_Update(&ctx, bytes, file.gcount()); }
    MD5_Final(out, &ctx);
}
}
#undef main
int main(int argc, char **argv) {
    assert(argc == 3);
    testRoot = fs::absolute(argv[1]).string();
    testSave = testRoot + "/save/";
    assert(!fs::exists(testRoot)); // Never reuse or delete player data.
    fs::create_directories(testSave);
    fs::create_directory(fs::path(testRoot) / "folder");
    std::ofstream(fs::path(testRoot) / "bad.z64") << "not a ROM";
    std::ofstream(fs::path(testRoot) / "ignored.txt") << "not listed";
    gRomSetupActive = true;
    browser_read(testRoot);
    assert(sBrowserEntries.size() == 3); // folder, save, bad.z64
    assert(sBrowserEntries[0].directory && sBrowserEntries[1].directory);
    rom_browser_action(-1); assert(sBrowserSelection == 0);
    testHands[0].thumbstick[1] = -1;
    rom_browser_update(); assert(sBrowserSelection == 1);
    rom_browser_update(); assert(sBrowserSelection == 1); // no per-frame repeats
    testHands[0] = {}; rom_browser_update();
    testHands[1].thumbstick[1] = -1;
    rom_browser_update(); assert(sBrowserSelection == 2);
    testHands[1] = {}; rom_browser_update();
    testHands[1].trigger = 1;
    rom_browser_update();
    assert(!gRomIsValid && sBrowserStatus.find("Not a valid") != std::string::npos);
    testHands[1] = {}; rom_browser_update();
    browser_read(fs::path(testRoot) / "folder");
    assert(sBrowserEntries.empty());
    rom_browser_action(2); assert(!gRomIsValid);
    testHands[1].secondaryButton = true; rom_browser_update();
    assert(sBrowserPath == fs::path(testRoot));
    testHands[1] = {}; rom_browser_update();
    browser_read(fs::path(testRoot) / "missing");
    assert(!sBrowserStatus.empty());
    fs::create_directory(fs::path(testRoot) / "many");
    for (int i = 0; i < 20; ++i) std::ofstream(fs::path(testRoot) / "many" / ("item" + std::to_string(i) + ".z64")) << "invalid";
    browser_read(fs::path(testRoot) / "many");
    for (int i = 0; i < 40; ++i) rom_browser_action(1);
    assert(sBrowserSelection == 19);
    assert(std::string(rom_browser_text()).find("20 / 20") != std::string::npos);
    for (int i = 0; i < 40; ++i) rom_browser_action(-1);
    assert(sBrowserSelection == 0);
    // Exercise the actual ROM validator/import with the user's local ROM,
    // copying only into this newly-created, isolated test save directory.
    sBrowserEntries = {{fs::path(argv[2]), "User ROM", false}};
    sBrowserSelection = 0;
    testHands[1].primaryButton = true; rom_browser_update();
    assert(gRomIsValid && fs::exists(fs::path(testSave) / "baserom.us.z64"));
    gRomIsValid = false;
    assert(main_rom_handler()); // Subsequent launch finds isolated imported ROM.
    std::cout << "PASS: both-hand navigation, debounce, pagination/bounds, invalid ROM, empty/inaccessible folders, back, valid ROM import and reuse\n";
    return 0;
}
