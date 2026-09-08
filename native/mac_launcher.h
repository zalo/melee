#pragma once
#include <functional>
#include <string>

using MeleeDiscValidator = std::function<std::string(const std::string&)>;
// The validator runs serially on a worker queue while the setup window stays responsive.
std::string MeleeLaunchDisc(MeleeDiscValidator validate, bool forceSetup = false);
void MeleeInstallAppMenu(std::function<void()> changeDisc);
void MeleePrepareAppLogging();
