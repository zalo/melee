/* Round trip, clamping, unknown-line preservation and environment overrides of settings.cfg. */
#include "melee_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

static char dir[] = "/tmp/melee-settings-XXXXXX";
static char path[300];

static void write_file(const char* text) {
    FILE* f = fopen(path, "w");
    CHECK(f);
    CHECK(fputs(text, f) >= 0);
    CHECK(fclose(f) == 0);
}

static char* read_file(void) {
    static char buffer[4096];
    FILE* f = fopen(path, "r");
    CHECK(f);
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[n] = '\0';
    fclose(f);
    return buffer;
}

int main(void) {
    CHECK(mkdtemp(dir));
    snprintf(path, sizeof(path), "%s/settings.cfg", dir);
    unsetenv("MELEE_DEBUG_OVERLAYS");
    unsetenv("MELEE_DEBUG_LEVEL");

    /* Missing file: defaults, no crash, and the path is remembered for saving. */
    MeleeNativeSettingsLoad(dir);
    CHECK(MeleeNativeSettingsData.debug_overlays == 0);
    CHECK(MeleeNativeSettingsData.debug_level == 0);
    CHECK(MeleeNativeDebugOverlays() == 0);

    /* Values, clamping, whitespace, comments, unknown keys and junk. */
    write_file("# user note\n"
               "debug_overlays=1\n"
               "  debug_level =   9  \n"
               "future_key = 42\n"
               "not a setting\n"
               "\n");
    CHECK(MeleeNativeSettingsLoadFile(path) == 0);
    CHECK(MeleeNativeSettingsData.debug_overlays == 1);
    CHECK(MeleeNativeSettingsData.debug_level == 4);

    /* Save keeps the unknown lines and the user's comment exactly once, and the header
     * does not accumulate across round trips. */
    CHECK(MeleeNativeSettingsSave() == 0);
    CHECK(MeleeNativeSettingsLoadFile(path) == 0);
    CHECK(MeleeNativeSettingsSave() == 0);
    {
        const char* text = read_file();
        CHECK(strstr(text, "debug_overlays = 1\n"));
        CHECK(strstr(text, "debug_level = 4\n"));
        CHECK(strstr(text, "future_key = 42\n"));
        CHECK(strstr(text, "not a setting\n"));
        CHECK(strstr(text, "# user note\n"));
        const char* first = strstr(text, "# Melee native port settings");
        CHECK(first && !strstr(first + 1, "# Melee native port settings"));
        CHECK(!strstr(text, ".tmp"));
    }

    /* Environment overrides win over the file and are clamped; garbage is ignored. */
    write_file("debug_overlays = 0\ndebug_level = 0\n");
    setenv("MELEE_DEBUG_OVERLAYS", "7", 1);
    setenv("MELEE_DEBUG_LEVEL", "abc", 1);
    MeleeNativeSettingsLoad(dir);
    CHECK(MeleeNativeSettingsData.debug_overlays == 1);
    CHECK(MeleeNativeSettingsData.debug_level == 0);
    setenv("MELEE_DEBUG_LEVEL", "3", 1);
    MeleeNativeSettingsLoad(dir);
    CHECK(MeleeNativeDebugLevel() == 3);

    /* The developer menu is off by default and has its own key and override. */
    unsetenv("MELEE_DEBUG_OVERLAYS");
    unsetenv("MELEE_DEBUG_LEVEL");
    write_file("debug_menu = 1\nonline_input_delay = 40\n");
    MeleeNativeSettingsLoad(dir);
    CHECK(MeleeNativeDebugMenu() == 1);
    CHECK(MeleeNativeSettingsData.online_input_delay == 15);
    setenv("MELEE_DEBUG_MENU", "0", 1);
    MeleeNativeSettingsLoad(dir);
    CHECK(MeleeNativeDebugMenu() == 0);
    unsetenv("MELEE_DEBUG_MENU");
    write_file("");
    MeleeNativeSettingsLoad(dir);
    CHECK(MeleeNativeDebugMenu() == 0 && MeleeNativeSettingsData.online_input_delay == 2);
    CHECK(MeleeNativeSettingsSave() == 0);
    CHECK(strstr(read_file(), "debug_menu = 0\n"));

    /* Saving into a directory that cannot be written fails cleanly. */
    CHECK(MeleeNativeSettingsSaveFile("/nonexistent-dir/settings.cfg") != 0);

    unlink(path);
    rmdir(dir);
    puts("settings ok");
    return 0;
}
