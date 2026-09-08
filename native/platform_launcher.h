#pragma once
#include <string>

std::string MeleeChooseDisc(const std::string& error = {});
void MeleeShowLaunchError(const std::string& error);
void MeleePrepareAppLogging();

// Linux returns XDG paths; other platforms retain Aurora defaults.
#ifdef __linux__
std::string MeleeConfigPath();
std::string MeleeCachePath();
#endif
