/* Playing a downloaded game. See play.h. */
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "play.h"
#include "state.h"
#include "util.h"

extern char **environ;

/* Launcher names by the emulator a profile id mentions. Ids are decorated:
 * RetroDECK's RetroArch is "retroarch-retroarch", EmuDeck's "retroarch-flatpak",
 * a core-based system "retroarch-snes9x" — all one launcher, retroarch.sh. And
 * a few launchers are named differently from the emulator (dolphin-emu.sh). */
static const struct {
    const char *mentions;
    const char *launcher;
} NAMES[] = {
    {"retroarch", "retroarch"},
    {"duckstation", "duckstation"},
    {"dolphin", "dolphin-emu"},
    {"pcsx2", "pcsx2-qt"},
    {"melonds", "melonds"},
    {"mgba", "mgba"},
    {"flycast", "flycast"},
    {"cemu", "cemu"},
    {"azahar", "azahar"},
    {"lime3ds", "lime3ds"},
    {"citra", "citra"},
    {"ppsspp", "ppsspp"},
    {"rpcs3", "rpcs3"},
    {"xemu", "xemu"},
    {"mame", "mame"},
    {"supermodel", "supermodel"},
    {"model2", "model2"},
    {"xenia", "xenia-emu"},
    {"bigpemu", "bigpemu"},
    {"scummvm", "scummvm"},
    {"vita3k", "vita3k"},
    {"shadps4", "shadps4"},
    {"ryujinx", "ryujinx"},
    {"ares", "ares"},
    {"rmg", "rmg"},
};

/* EmuDeck keeps its launchers beside the games folder it manages: a game at
 * <root>/roms/<system>/<file> means <root>/tools/launchers. Asking the game
 * that is about to be played keeps a second EmuDeck (a card, another user)
 * working without a setting to point at it. */
static void launcher_dir_for(const char *game, char *out, size_t outlen) {
    const char *env = getenv("ROMM_PLAY_LAUNCHERS");
    if (env && env[0]) {
        str_copy(out, outlen, env);
        return;
    }
    if (game && game[0]) {
        char dir[1200];
        str_copy(dir, sizeof dir, game);
        for (int cuts = 0; cuts < 2; cuts++) {
            char *slash = strrchr(dir, '/');
            if (!slash || slash == dir) break;
            *slash = 0;
        }
        snprintf(out, outlen, "%s/../tools/launchers", dir);
        if (dir_exists(out)) return;
    }
    const char *home = getenv("HOME");
    snprintf(out, outlen, "%s/Emulation/tools/launchers", home ? home : ".");
}

void play_launcher_dir(char *out, size_t outlen) {
    launcher_dir_for(NULL, out, outlen);
}

/* The launcher's name for a profile id, "" when nothing in it is an emulator we
 * know how to start: a made-up name would only fail at posix_spawn. */
static const char *launcher_name(const char *profile) {
    if (!profile || !profile[0]) return "";
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (strcasestr(profile, NAMES[i].mentions)) return NAMES[i].launcher;
    return "";
}

/* The game as the state keeps it: its file, the profile it came down for and
 * the system's folder. 1 when it is on the machine. */
static int game_entry(int rom_id, char *file, size_t fn, char *profile, size_t pn,
                      char *sys, size_t sn) {
    int found = 0;
    file[0] = profile[0] = sys[0] = 0;
    state_lock();
    const cJSON *e;
    cJSON_ArrayForEach(e, state_section("roms")) {
        if ((int)jget_num(e, "rom_id", 0) != rom_id) continue;
        /* 1: an extra copy of a game another profile keeps. */
        if (jget_num(e, "with", 0) > 0) continue;
        if (!file_exists(e->string) && !dir_exists(e->string)) continue;
        str_copy(file, fn, e->string);
        str_copy(profile, pn, jget_str(e, "profile", ""));
        str_copy(sys, sn, jget_str(e, "platform_dir", ""));
        found = 1;
        break;
    }
    state_unlock();
    return found;
}

/* The core a RetroArch-based profile runs this system with, from the setup's
 * own choice. Empty for standalone emulators. */
static void system_core(const char *sys, const char *profile, char *out, size_t outlen) {
    out[0] = 0;
    if (!sys[0] || !strstr(profile, "retroarch")) return;
    config_lock();
    const cJSON *p, *pl;
    cJSON_ArrayForEach(p, g_cfg.profiles) {
        const cJSON *platforms = cJSON_GetObjectItemCaseSensitive(p, "platforms");
        cJSON_ArrayForEach(pl, platforms) {
            if (strcmp(jget_str(pl, "dir", ""), sys)) continue;
            const char *core = jget_str(pl, "core_name", "");
            if (!core[0]) core = jget_str(pl, "emulator", "");
            /* "retroarch" is the launcher, not a core: it means the setup never
             * named one. */
            if (strcasecmp(core, "retroarch")) str_copy(out, outlen, core);
            break;
        }
        if (out[0]) break;
    }
    config_unlock();
}

void play_launcher_for(int rom_id, char *out, size_t outlen) {
    char file[1200], profile[64], sys[64], dir[1024];
    out[0] = 0;
    if (!game_entry(rom_id, file, sizeof file, profile, sizeof profile, sys, sizeof sys)) return;
    const char *name = launcher_name(profile);
    if (!name[0]) return;
    launcher_dir_for(file, dir, sizeof dir);
    snprintf(out, outlen, "%s/%s.sh", dir, name);
}

int play_launch(int rom_id, char *err, size_t errlen) {
    char file[1200], profile[64], sys[64], dir[1024], script[1200];
    err[0] = 0;
    if (!game_entry(rom_id, file, sizeof file, profile, sizeof profile, sys, sizeof sys)) {
        snprintf(err, errlen, "that game isn't on the machine yet");
        return -1;
    }

    const char *name = launcher_name(profile);
    if (!name[0]) {
        snprintf(err, errlen, "no launcher for '%s'", profile[0] ? profile : "an unset emulator");
        return -1;
    }

    launcher_dir_for(file, dir, sizeof dir);
    snprintf(script, sizeof script, "%s/%s.sh", dir, name);
    if (!file_exists(script)) {
        /* EmuDeck writes a launcher for the emulators it installed: a missing
         * one means that emulator isn't there, which is the caller's cue to
         * install it with setup.sh --only <emulator>. */
        snprintf(err, errlen, "%s isn't installed (no %s)", name, script);
        return -1;
    }

    char core[128], *argv[8];
    system_core(sys, profile, core, sizeof core);
    int n = 0;
    argv[n++] = script;
    if (core[0]) {
        argv[n++] = "-L";
        argv[n++] = core;
    }
    argv[n++] = file;
    argv[n] = NULL;

    pid_t pid = 0;
    int rc = posix_spawn(&pid, script, NULL, NULL, argv, environ);
    if (rc != 0) {
        snprintf(err, errlen, "%s wouldn't start (error %d)", script, rc);
        return -1;
    }
    LOGI("play: rom %d with %s%s%s", rom_id, script, core[0] ? " -L " : "", core);
    return 0;
}
