#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void MeleeNativePumpAlarms(void);
void MeleeNativeReportThreadInfo(void);
void MeleeNativeConfigureOS(const char* user_path, void (*reset)(int));
#ifdef __cplusplus
}
#endif
