#pragma once
/* Port settings that outlive one launch: a small key = value file in the user directory
 * (next to console-settings.txt), edited from the developer menu's "Port Settings" screen.
 * Melee.sh environment variables override the file for one launch (MELEE_DEBUG_OVERLAYS,
 * MELEE_DEBUG_LEVEL); the menu then shows and saves the overridden value. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeNativeSettings {
    /* 1 lets Y on the title screen open the game's developer menu (off by default; the
     * retail Options > Port Settings screen turns it on). */
    int debug_menu;
    /* 1 enables the game's own development overlays and debug pause in matches without
     * changing DbLevel, so gameplay stays retail (see db_HasOverlays in melee/db/db.h). */
    int debug_overlays;
    /* 0 keeps the retail boot value (DbLKind_Master); 1..4 forces that DbLevel at the next
     * launch. 3 (Debug-Rom) is the full development build behaviour, which alters gameplay. */
    int debug_level;
    /* Reserved for the online-play configuration screen (not implemented): keys are parsed
     * and saved so a settings file from a newer build survives, values are unused. */
    int online_enabled;
    int online_input_delay;
} MeleeNativeSettings;

extern MeleeNativeSettings MeleeNativeSettingsData;

/* Reads <user_path>/settings.cfg (missing file = defaults), then applies the environment. */
void MeleeNativeSettingsLoad(const char* user_path);
/* Reads one file; no environment. Returns 0, or -1 when the file cannot be opened. */
int MeleeNativeSettingsLoadFile(const char* path);
/* Applies MELEE_* environment overrides to the loaded values. */
void MeleeNativeSettingsApplyEnvironment(void);
/* Writes the file loaded by MeleeNativeSettingsLoad (atomically). Returns 0 on success. */
int MeleeNativeSettingsSave(void);
/* Writes the current values to an explicit path. Returns 0 on success. */
int MeleeNativeSettingsSaveFile(const char* path);
int MeleeNativeDebugMenu(void);
int MeleeNativeDebugOverlays(void);
int MeleeNativeDebugLevel(void);

#ifdef __cplusplus
}
#endif
