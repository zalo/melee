#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void MeleeNativePumpAlarms(void);
extern int MeleeNativeSkipSavePrompt;
void MeleeNativeConfigureOS(const char* user_path, void (*reset)(int));
#ifdef __cplusplus
}
#endif
