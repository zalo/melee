#include "include/melee_settings.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const MeleeNativeSettings defaults = { 0, 0, 0, 0, 2 };
MeleeNativeSettings MeleeNativeSettingsData = { 0, 0, 0, 0, 2 };

/* The file keeps lines it does not understand (future keys, hand-written comments), so a
 * newer build's settings survive a round trip through an older one. */
#define MAX_EXTRA_LINES 64
#define MAX_LINE 256
static char extra_lines[MAX_EXTRA_LINES][MAX_LINE];
static int extra_count;
static char settings_path[PATH_MAX];

struct key {
    const char* name;
    int* value;
    int min, max;
};

static struct key* keys(void) {
    static struct key table[] = {
        { "debug_menu", &MeleeNativeSettingsData.debug_menu, 0, 1 },
        { "debug_overlays", &MeleeNativeSettingsData.debug_overlays, 0, 1 },
        { "debug_level", &MeleeNativeSettingsData.debug_level, 0, 4 },
        { "online_enabled", &MeleeNativeSettingsData.online_enabled, 0, 1 },
        { "online_input_delay", &MeleeNativeSettingsData.online_input_delay, 0, 15 },
        { NULL, NULL, 0, 0 },
    };
    return table;
}

static const char* const header[] = {
    "# Melee native port settings. Edited by the developer menu (Y on the title screen,",
    "# Port Settings). MELEE_* environment variables from the launcher override these.",
};

static int is_header(const char* line) {
    for (size_t i = 0; i < sizeof(header) / sizeof(header[0]); ++i)
        if (strcmp(line, header[i]) == 0) return 1;
    return 0;
}

static int clamp(int value, int min, int max) {
    return value < min ? min : value > max ? max : value;
}

static char* trim(char* text) {
    while (isspace((unsigned char) *text)) text++;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char) end[-1])) *--end = '\0';
    return text;
}

/* "key = value" with a decimal value; anything else is kept verbatim. Returns 1 when consumed. */
static int apply_line(char* line) {
    char copy[MAX_LINE];
    snprintf(copy, sizeof(copy), "%s", line);
    char* equals = strchr(copy, '=');
    if (!equals || copy[0] == '#') return 0;
    *equals = '\0';
    const char* name = trim(copy);
    char* text = trim(equals + 1);
    char* end;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0') return 0;
    for (struct key* k = keys(); k->name; ++k) {
        if (strcmp(k->name, name) == 0) {
            *k->value = clamp((int) value, k->min, k->max);
            return 1;
        }
    }
    return 0;
}

int MeleeNativeSettingsLoadFile(const char* path) {
    /* Keys missing from the file take their defaults, whatever was loaded before. */
    MeleeNativeSettingsData = defaults;
    extra_count = 0;
    FILE* file = fopen(path, "r");
    if (!file) return -1;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        if (length && line[length - 1] == '\n') line[--length] = '\0';
        if (apply_line(line)) continue;
        if (trim(line)[0] == '\0' || is_header(line)) continue;
        if (extra_count < MAX_EXTRA_LINES) snprintf(extra_lines[extra_count++], MAX_LINE, "%s", line);
    }
    fclose(file);
    return 0;
}

static void override_from(const char* variable, int* value, int min, int max) {
    const char* text = getenv(variable);
    if (!text || !*text) return;
    char* end;
    long parsed = strtol(text, &end, 10);
    if (*end != '\0') {
        fprintf(stderr, "[settings] ignoring %s=%s (not a number)\n", variable, text);
        return;
    }
    *value = clamp((int) parsed, min, max);
}

void MeleeNativeSettingsApplyEnvironment(void) {
    override_from("MELEE_DEBUG_MENU", &MeleeNativeSettingsData.debug_menu, 0, 1);
    override_from("MELEE_DEBUG_OVERLAYS", &MeleeNativeSettingsData.debug_overlays, 0, 1);
    override_from("MELEE_DEBUG_LEVEL", &MeleeNativeSettingsData.debug_level, 0, 4);
}

void MeleeNativeSettingsLoad(const char* user_path) {
    if (snprintf(settings_path, sizeof(settings_path), "%s/settings.cfg", user_path) >= (int) sizeof(settings_path)) {
        fprintf(stderr, "[settings] path too long; settings will not persist\n");
        settings_path[0] = '\0';
    } else if (MeleeNativeSettingsLoadFile(settings_path) != 0) {
        fprintf(stderr, "[settings] no %s; using defaults\n", settings_path);
    }
    MeleeNativeSettingsApplyEnvironment();
    fprintf(stderr, "[settings] debug_menu=%d debug_overlays=%d debug_level=%d\n", MeleeNativeSettingsData.debug_menu,
            MeleeNativeSettingsData.debug_overlays, MeleeNativeSettingsData.debug_level);
}

int MeleeNativeSettingsSaveFile(const char* path) {
    char temp[PATH_MAX];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int) sizeof(temp)) return -1;
    FILE* file = fopen(temp, "w");
    if (!file) return -1;
    int ok = 1;
    for (size_t i = 0; i < sizeof(header) / sizeof(header[0]); ++i) ok = ok && fprintf(file, "%s\n", header[i]) >= 0;
    for (struct key* k = keys(); k->name; ++k) ok = ok && fprintf(file, "%s = %d\n", k->name, *k->value) >= 0;
    for (int i = 0; i < extra_count; ++i) ok = ok && fprintf(file, "%s\n", extra_lines[i]) >= 0;
    if (fclose(file) != 0 || !ok || rename(temp, path) != 0) {
        remove(temp);
        return -1;
    }
    return 0;
}

int MeleeNativeSettingsSave(void) {
    if (!settings_path[0]) return -1;
    int result = MeleeNativeSettingsSaveFile(settings_path);
    if (result != 0) fprintf(stderr, "[settings] cannot save %s\n", settings_path);
    return result;
}

int MeleeNativeDebugMenu(void) { return MeleeNativeSettingsData.debug_menu; }
int MeleeNativeDebugOverlays(void) { return MeleeNativeSettingsData.debug_overlays; }
int MeleeNativeDebugLevel(void) { return MeleeNativeSettingsData.debug_level; }
