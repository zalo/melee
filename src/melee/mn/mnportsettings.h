#ifndef MELEE_MN_MNPORTSETTINGS_H
#define MELEE_MN_MNPORTSETTINGS_H

#include <Runtime/platform.h>

#ifdef MELEE_NATIVE
#include <dolphin/mtx.h>
#include <sysdolphin/baselib/forward.h>

/// The native port's retail-style "Port Settings" screen behind the Options
/// menu's formerly hidden fourth slot (SEL_SETTINGS_3).
void mnPort_Init(void);

/// World positions of the Options list's six bars, sampled by the list while
/// it is idle; the Port Settings screen lays its rows out on them.
extern Vec3 mn_NativeSettingsBars[6];
extern u8 mn_NativeSettingsBarsValid;
/// Description-bar sentence for the Port Settings slot.
extern const char mn_NativePortDescription[];
#endif

#endif
