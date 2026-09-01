#include "smlua.h"
#include "pc/lua/smlua_require.h"
#include "pc/lua/smlua_live_reload.h"
#include "game/hardcoded.h"
#include "pc/mods/mods.h"
#include "pc/mods/mods_utils.h"
#include "pc/mods/mod_storage.h"
#include "pc/mods/mod_fs.h"
#include "pc/crash_handler.h"
#include "pc/lua/utils/smlua_text_utils.h"
#include "pc/lua/utils/smlua_audio_utils.h"
#include "pc/lua/utils/smlua_model_utils.h"
#include "pc/lua/utils/smlua_level_utils.h"
#include "pc/lua/utils/smlua_anim_utils.h"
#include "pc/djui/djui.h"
#include "pc/fs/fmem.h"

#ifdef __ANDROID__
#include <android/log.h>
#define SM_LUA_LOG_TAG "SM64CoopDXVR"
#endif

lua_State* gLuaState = NULL;
u8 gLuaInitializingScript = 0;
u8 gSmLuaSuppressErrors = 0;
struct Mod* gLuaLoadingMod = NULL;
struct Mod* gLuaActiveMod = NULL;
struct ModFile* gLuaActiveModFile = NULL;
struct Mod* gLuaLastHookMod = NULL;

static struct SmLuaDiagnostics sSmLuaDiagnostics;
static const struct Mod *sLastErrorMod = NULL;
static const struct ModFile *sLastErrorFile = NULL;
static char sLastErrorMessage[256] = { 0 };
static bool sLastErrorValid = false;
static bool sSuppressNextErrorReport = false;

/*
 * Quest routes legacy Lua console output through stdout, which is not
 * consistently visible in logcat. Record the first occurrence of each error
 * with its owning mod/file so compatibility failures can be diagnosed without
 * changing map files or adding extra in-game UI.
 */
static void smlua_log_android_error(const char *phase, const char *message) {
#ifdef __ANDROID__
    const struct Mod *mod = gLuaActiveMod != NULL ? gLuaActiveMod :
        (gLuaLoadingMod != NULL ? gLuaLoadingMod : gLuaLastHookMod);
    const struct ModFile *file = gLuaActiveModFile;
    __android_log_print(ANDROID_LOG_ERROR, SM_LUA_LOG_TAG,
        "Lua %s: mod='%s' file='%s': %s",
        phase != NULL ? phase : "error",
        mod != NULL ? mod->name : "<engine>",
        file != NULL ? file->relativePath : "<unknown>",
        message != NULL ? message : "<non-string Lua error>");
#else
    (void) phase;
    (void) message;
#endif
}
static bool smlua_is_repeated_error(const char *message) {
    const struct Mod *mod = gLuaActiveMod != NULL ? gLuaActiveMod :
        (gLuaLoadingMod != NULL ? gLuaLoadingMod : gLuaLastHookMod);
    const struct ModFile *file = gLuaActiveModFile;
    const char *errorMessage = message != NULL ? message : "<non-string Lua error>";
    const bool repeated = sLastErrorValid &&
        mod == sLastErrorMod &&
        file == sLastErrorFile &&
        strcmp(errorMessage, sLastErrorMessage) == 0;

    sLastErrorMod = mod;
    sLastErrorFile = file;
    snprintf(sLastErrorMessage, sizeof(sLastErrorMessage), "%s", errorMessage);
    sLastErrorValid = true;
    return repeated;
}

bool smlua_consume_suppressed_error_report(void) {
    const bool suppress = sSuppressNextErrorReport;
    sSuppressNextErrorReport = false;
    return suppress;
}

static void smlua_capture_error(const char *message) {
    const struct Mod *mod = gLuaActiveMod != NULL ? gLuaActiveMod :
        (gLuaLoadingMod != NULL ? gLuaLoadingMod : gLuaLastHookMod);
    const struct ModFile *file = gLuaActiveModFile;
    snprintf(sSmLuaDiagnostics.last_mod, sizeof(sSmLuaDiagnostics.last_mod),
        "%s", mod != NULL ? mod->name : "<engine>");
    snprintf(sSmLuaDiagnostics.last_file, sizeof(sSmLuaDiagnostics.last_file),
        "%s", file != NULL ? file->relativePath : "<unknown>");
    snprintf(sSmLuaDiagnostics.last_message,
        sizeof(sSmLuaDiagnostics.last_message), "%s",
        message != NULL ? message : "<non-string Lua error>");
}

void smlua_mod_error(void) {
    sSmLuaDiagnostics.error_reports++;
    struct Mod* mod = gLuaActiveMod;
    if (mod == NULL) { mod = gLuaLoadingMod; }
    if (mod == NULL) { mod = gLuaLastHookMod; }
    if (mod == NULL) { return; }
    char txt[255] = { 0 };
    snprintf(txt, 254, "'%s\\#ff0000\\' has script errors!", mod->name);
    static const struct DjuiColor color = { 255, 0, 0, 255 };
    djui_lua_error(txt, color);
}

bool smlua_mod_warning(bool once) {
    // Count warnings that reach this reporting path; ignored/once-suppressed warnings return earlier.
    struct Mod* mod = gLuaActiveMod;
    if (mod == NULL) { mod = gLuaLoadingMod; }
    if (mod == NULL) { mod = gLuaLastHookMod; }
    if (mod == NULL) { return true; }
    if (mod->ignoreScriptWarnings) { return false; }
    if (once && mod->showedScriptWarning) { return false; }
    if (once) { mod->showedScriptWarning = true; }
    sSmLuaDiagnostics.warning_reports++;
    char txt[255] = { 0 };
    snprintf(txt, 254, "'%s\\#ffe600\\' has script warnings!", mod->name);
    static const struct DjuiColor color = { 255, 230, 0, 255 };
    djui_lua_error(txt, color);
    return true;
}

int smlua_error_handler(lua_State* L) {
    sSmLuaDiagnostics.protected_call_errors++;
    const char *message = lua_type(L, -1) == LUA_TSTRING
        ? lua_tostring(L, -1) : "<non-string Lua error>";
    const bool repeated = smlua_is_repeated_error(message);
    smlua_capture_error(message);
    sSuppressNextErrorReport = repeated;
    if (!repeated && lua_type(L, -1) == LUA_TSTRING) {
        smlua_log_android_error("runtime", message);
        LOG_LUA("%s", lua_tostring(L, -1));
        smlua_logline();
        smlua_dump_stack();
    }
    return 0;
}

void smlua_get_diagnostics(struct SmLuaDiagnostics *out) {
    if (out != NULL) {
        memcpy(out, &sSmLuaDiagnostics, sizeof(*out));
    }
}
int smlua_pcall(lua_State* L, int nargs, int nresults, UNUSED int errfunc) {
    gSmLuaConvertSuccess = true;
    sSuppressNextErrorReport = false;
    /*
     * A failed protected call leaves its error object on the Lua stack.
     * Hook dispatchers intentionally continue after a callback failure, and
     * not every generated dispatcher has a caller-side lua_settop() on that
     * path. Restore the stack here so one incompatible map callback cannot
     * contaminate subsequent callbacks (or grow the stack once per frame).
     */
    const int callBase = lua_gettop(L) - nargs - 1;
    lua_pushcfunction(L, smlua_error_handler);
    int errorHandlerIndex = 1;
    lua_insert(L, errorHandlerIndex);

    int rc = lua_pcall(L, nargs, nresults, errorHandlerIndex);

    lua_remove(L, errorHandlerIndex);
    if (rc != LUA_OK) {
        lua_settop(L, callBase);
    }
    return rc;
}

void smlua_exec_file(const char* path) {
    lua_State* L = gLuaState;
    if (luaL_dofile(L, path) != LUA_OK) {
        sSmLuaDiagnostics.script_load_errors++;
        LOG_LUA("Failed to load lua file '%s'.", path);
        LOG_LUA("%s", smlua_to_string(L, lua_gettop(L)));
    }
    lua_pop(L, lua_gettop(L));
}

void smlua_exec_str(const char* str) {
    lua_State* L = gLuaState;
    if (luaL_dostring(L, str) != LUA_OK) {
        sSmLuaDiagnostics.script_load_errors++;
        LOG_LUA("Failed to load lua string.");
        LOG_LUA("%s", smlua_to_string(L, lua_gettop(L)));
    }
    lua_pop(L, lua_gettop(L));
}

#define LUA_BOM_11 0x0000000000005678llu
#define LUA_BOM_19 0x4077280000000000llu

static bool smlua_check_binary_header(struct ModFile *file) {
    FILE *f = f_open_r(file->cachedPath);
    if (f) {

        // Read signature
        char signature[sizeof(LUA_SIGNATURE)] = { 0 };
        if (f_read(signature, 1, sizeof(LUA_SIGNATURE) - 1, f) != sizeof(LUA_SIGNATURE) - 1) {
            LOG_LUA("Failed to load lua script '%s': File too short.", file->cachedPath);
            f_close(f);
            f_delete(f);
            return false;
        }

        // Check signature
        if (strcmp(signature, LUA_SIGNATURE) != 0) {
            f_close(f);
            return true; // Not a binary lua
        }

        // Read version number
        u8 version;
        if (f_read(&version, 1, 1, f) != 1) {
            LOG_LUA("Failed to load lua script '%s': File too short.", file->cachedPath);
            f_close(f);
            f_delete(f);
            return false;
        }

        // Check version number
        u8 expectedVersion = strtoul(LUA_VERSION_MAJOR LUA_VERSION_MINOR, NULL, 16);
        if (version != expectedVersion) {
            LOG_LUA("Failed to load lua script '%s': Lua versions don't match (%X, expected %X).", file->cachedPath, version, expectedVersion);
            f_close(f);
            f_delete(f);
            return false;
        }

        // Read the rest of the header
        u8 header[28];
        if (f_read(header, 1, 28, f) != 28) {
            LOG_LUA("Failed to load lua script '%s': File too short.", file->cachedPath);
            f_close(f);
            f_delete(f);
            return false;
        }

        // The following errors are silent (they're due to non-matching endianness/bitness and shouldn't prevent the rest of the mod from loading)

        // Check endianness
        u64 bom11 = *((u64 *) (header + 12));
        u64 bom19 = *((u64 *) (header + 20));
        if (bom11 != LUA_BOM_11) {
            LOG_ERROR("Failed to load lua script '%s': BOM at offset 0x11 don't match (%016llX, expected %016llX).", file->cachedPath, bom11, LUA_BOM_11);
            f_close(f);
            f_delete(f);
            return false;
        }
        if (bom19 != LUA_BOM_19) {
            LOG_ERROR("Failed to load lua script '%s': BOM at offset 0x19 don't match (%016llX, expected %016llX).", file->cachedPath, bom19, LUA_BOM_19);
            f_close(f);
            f_delete(f);
            return false;
        }

        // Check sizes
        u8 sizeOfCInteger = header[7];
        u8 sizeOfCPointer = header[8];
        u8 sizeOfCFloat = header[9];
        u8 sizeOfLuaInteger = header[10];
        u8 sizeOfLuaNumber = header[11];
        if (sizeOfCInteger != sizeof(int)) {
            LOG_ERROR("Failed to load lua script '%s': sizes of C Integer don't match (%d, expected %llu).", file->cachedPath, sizeOfCInteger, (long long unsigned)sizeof(int));
            f_close(f);
            f_delete(f);
            return false;
        }
        if (sizeOfCPointer != sizeof(void *)) { // 4 for 32-bit architectures, 8 for 64-bit
            LOG_ERROR("Failed to load lua script '%s': sizes of C Pointer don't match (%d, expected %llu).", file->cachedPath, sizeOfCPointer, (long long unsigned)sizeof(void *));
            f_close(f);
            f_delete(f);
            return false;
        }
        if (sizeOfCFloat != sizeof(float)) {
            LOG_ERROR("Failed to load lua script '%s': sizes of C Float don't match (%d, expected %llu).", file->cachedPath, sizeOfCFloat, (long long unsigned)sizeof(float));
            f_close(f);
            f_delete(f);
            return false;
        }
        if (sizeOfLuaInteger != sizeof(LUA_INTEGER)) {
            LOG_ERROR("Failed to load lua script '%s': sizes of Lua Integer don't match (%d, expected %llu).", file->cachedPath, sizeOfLuaInteger, (long long unsigned)sizeof(LUA_INTEGER));
            f_close(f);
            f_delete(f);
            return false;
        }
        if (sizeOfLuaNumber != sizeof(LUA_NUMBER)) {
            LOG_ERROR("Failed to load lua script '%s': sizes of Lua Number don't match (%d, expected %llu).", file->cachedPath, sizeOfLuaNumber, (long long unsigned)sizeof(LUA_NUMBER));
            f_close(f);
            f_delete(f);
            return false;
        }

        // All's good
        f_close(f);
        return true;
    }
    LOG_LUA("Failed to load lua script '%s': File not found.", file->cachedPath);
    return false;
}

/*
 * Some third-party map packages contain binary assets with a .lua suffix.
 * Feeding those bytes to luaL_loadbuffer only creates a deterministic syntax
 * error. Keep genuine Lua text permissive (including UTF-8), but reject
 * NUL/control-heavy payloads before invoking the parser. Binary .luac files
 * are validated by smlua_check_binary_header() and do not pass this check.
 */
static bool smlua_is_text_source(const void *buffer, size_t length) {
    const u8 *bytes = (const u8 *) buffer;
    size_t start = 0;
    if (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        start = 3;
    }
    const size_t sampleLength = length < 4096 ? length : 4096;
    for (size_t i = start; i < sampleLength; i++) {
        const u8 c = bytes[i];
        if (c == 0 || (c < 0x09) || (c > 0x0D && c < 0x20)) {
            return false;
        }
    }
    return true;
}

int smlua_load_script(struct Mod* mod, struct ModFile* file, u16 remoteIndex, bool isModInit) {
    int rc = LUA_OK;
    if (!smlua_check_binary_header(file)) { return LUA_ERRMEM; }

    lua_State* L = gLuaState;

    s32 prevTop = lua_gettop(L);

    gSmLuaConvertSuccess = true;
    gLuaInitializingScript = 1;
    LOG_INFO("Loading lua script '%s'", file->cachedPath);

    FILE *f = f_open_r(file->cachedPath);
    if (!f) {
        LOG_LUA("Failed to load lua script '%s': File not found.", file->cachedPath);
        gLuaInitializingScript = 0;
        lua_settop(L, prevTop);
        return LUA_ERRFILE;
    }

    f_seek(f, 0, SEEK_END);
    size_t length = f_tell(f);
    void *buffer = calloc(length + 1, 1);
    if (!buffer) {
        LOG_LUA("Failed to load lua script '%s': Cannot allocate buffer.", file->cachedPath);
        gLuaInitializingScript = 0;
        lua_settop(L, prevTop);
        return LUA_ERRMEM;
    }

    f_rewind(f);
    if (f_read(buffer, 1, length, f) < length) {
        LOG_LUA("Failed to load lua script '%s': Unexpected early end of file.", file->cachedPath);
        gLuaInitializingScript = 0;
        lua_settop(L, prevTop);
        return LUA_ERRFILE;
    }
    f_close(f);
    f_delete(f);

    if (path_ends_with(file->relativePath, ".lua") && !smlua_is_text_source(buffer, length)) {
        smlua_log_android_error("skip", "non-text .lua payload");
        LOG_INFO("Skipping non-text lua payload: %s", file->cachedPath);
        gLuaInitializingScript = 0;
        free(buffer);
        lua_settop(L, prevTop);
        return LUA_ERRFILE;
    }

    rc = luaL_loadbuffer(L, buffer, length, file->cachedPath);
    if (rc != LUA_OK) { // only run on success
        sSmLuaDiagnostics.script_load_errors++;
        const char *loadError = smlua_to_string(L, lua_gettop(L));
        smlua_capture_error(loadError);
        smlua_log_android_error("load", loadError);
        LOG_LUA("Failed to load lua script '%s'.", file->cachedPath);
        LOG_LUA("%s", smlua_to_string(L, lua_gettop(L)));
        gLuaInitializingScript = 0;
        free(buffer);
        lua_settop(L, prevTop);
        return rc;
    }
    free(buffer);

    if (isModInit) {
        // check if this is the first time this mod has been loaded
        lua_getfield(L, LUA_REGISTRYINDEX, mod->relativePath);
        bool firstInit = (lua_type(L, -1) == LUA_TNIL);
        lua_pop(L, 1);

        // create mod's "global" table
        if (firstInit) {
            lua_newtable(L); // create _ENV tables
            lua_newtable(L); // create metatable
            lua_getglobal(L, "_G"); // get global table

            // remove certain default functions
            lua_pushstring(L, "load");           lua_pushnil(L); lua_settable(L, -3);
            lua_pushstring(L, "loadfile");       lua_pushnil(L); lua_settable(L, -3);
            lua_pushstring(L, "loadstring");     lua_pushnil(L); lua_settable(L, -3);
            lua_pushstring(L, "collectgarbage"); lua_pushnil(L); lua_settable(L, -3);
            lua_pushstring(L, "dofile");         lua_pushnil(L); lua_settable(L, -3);

            // set global as the metatable
            lua_setfield(L, -2, "__index");
            lua_setmetatable(L, -2);

            // push to registry with path as name (must be unique)
            lua_setfield(L, LUA_REGISTRYINDEX, mod->relativePath);
        }

        // load mod's "global" table
        lua_getfield(L, LUA_REGISTRYINDEX, mod->relativePath);
        lua_setupvalue(L, 1, 1); // set upvalue (_ENV)

        // load per-file globals
        if (firstInit) {
            smlua_sync_table_init_globals(mod->relativePath, remoteIndex);
            smlua_cobject_init_per_file_globals(mod->relativePath);
        }
    } else {
        // this block is run on files that are loaded for 'require' function
        // get the mod's global table
        lua_getfield(L, LUA_REGISTRYINDEX, mod->relativePath);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            LOG_LUA("mod environment not found");
            lua_settop(L, prevTop);
            return LUA_ERRRUN;
        }
        lua_setupvalue(L, -2, 1); // set _ENV
    }

    // run chunks
    LOG_INFO("Executing '%s'", file->relativePath);
    rc = smlua_pcall(L, 0, 1, 0);
    if (rc != LUA_OK) {
        LOG_LUA("Failed to execute lua script '%s'.", file->cachedPath);
    }

    gLuaInitializingScript = 0;

    return rc;
}

void smlua_init(void) {
    smlua_shutdown();

    gLuaState = luaL_newstate();
    lua_State* L = gLuaState;

    // load libraries
    luaopen_base(L);
#if defined(DEVELOPMENT) && defined(LUA_UNSAFE)
    luaL_requiref(L, "debug", luaopen_debug, 1);
    luaL_requiref(L, "io", luaopen_io, 1);
    luaL_requiref(L, "os", luaopen_os, 1);
    luaL_requiref(L, "package", luaopen_package, 1);
#endif
    luaL_requiref(L, "math", luaopen_math, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1);

    smlua_bind_hooks();
    smlua_bind_cobject();
    smlua_bind_functions();
    smlua_bind_functions_autogen();
    smlua_bind_sync_table();
    smlua_init_require_system();

    extern const char gSmluaConstants[];
    smlua_exec_str(gSmluaConstants);

    smlua_cobject_init_globals();
    smlua_model_util_initialize();

    // load scripts
    mods_size_enforce(&gActiveMods);
    LOG_INFO("Loading scripts:");
    for (int i = 0; i < gActiveMods.entryCount; i++) {
        struct Mod* mod = gActiveMods.entries[i];
        LOG_INFO("    %s", mod->relativePath);
        gLuaLoadingMod = mod;
        gLuaActiveMod = mod;
        gLuaLastHookMod = mod;
        gLuaLoadingMod->customBehaviorIndex = 0;
        gPcDebug.lastModRun = gLuaActiveMod;
        for (int j = 0; j < mod->fileCount; j++) {
            struct ModFile* file = &mod->files[j];
            // skip loading non-lua files
            if (!(path_ends_with(file->relativePath, ".lua") || path_ends_with(file->relativePath, ".luac"))) {
                continue;
            }

            // skip loading scripts in subdirectories
            if (strchr(file->relativePath, *PATH_SEPARATOR) != NULL || strchr(file->relativePath, *PATH_SEPARATOR_ALT) != NULL) {
                continue;
            }

            gLuaActiveModFile = file;

            // file has been required by some module before this
            if (!smlua_get_cached_module_result(L, mod, file)) {
                smlua_mark_module_as_loading(L, mod, file);

                s32 prevTop = lua_gettop(L);
                int rc = smlua_load_script(mod, file, i, true);

                if (rc == LUA_OK) {
                    smlua_cache_module_result(L, mod, file, prevTop);
                }
            }

            lua_settop(L, 0);
        }
        gLuaActiveMod = NULL;
        gLuaActiveModFile = NULL;
        gLuaLoadingMod = NULL;
    }

    smlua_call_event_hooks(HOOK_ON_MODS_LOADED);
}

void smlua_update(void) {
    lua_State* L = gLuaState;
    if (L == NULL) { return; }

    if (network_allow_mod_dev_mode()) { smlua_live_reload_update(L); }

    audio_sample_destroy_pending_copies();

    smlua_call_event_hooks(HOOK_UPDATE);

    // Collect our garbage after calling our hooks.
    // If we don't, Lag can quickly build up from our mods.
    // Truth is smlua generates so much garbage that the
    // incremental collection fails to keep up after some time.
    // So, for now, stop the GC from running during the hooks
    // and perform a full GC at the end of the frame.
    // EDIT: That builds up lag over time, so we need to keep
    // doing incremental garbage collection.
    // The real fix would be to make smlua produce less
    // garbage.
    // lua_gc(L, LUA_GCSTOP, 0);
    // lua_gc(L, LUA_GCCOLLECT, 0);
}

void smlua_shutdown(void) {
    hardcoded_reset_default_values();
    smlua_text_utils_reset_all();
    smlua_audio_utils_reset_all();
    smlua_audio_custom_deinit();
    smlua_clear_hooks();
    smlua_model_util_clear();
    smlua_level_util_reset();
    smlua_anim_util_reset();
    mod_storage_shutdown();
    mod_fs_shutdown();
    lua_State* L = gLuaState;
    if (L != NULL) {
        lua_close(L);
        gLuaState = NULL;
    }
    gLuaLoadingMod = NULL;
    gLuaActiveMod = NULL;
    gLuaActiveModFile = NULL;
    gLuaLastHookMod = NULL;
    sLastErrorMod = NULL;
    sLastErrorFile = NULL;
    sLastErrorMessage[0] = '\0';
    sLastErrorValid = false;
    sSuppressNextErrorReport = false;
}
