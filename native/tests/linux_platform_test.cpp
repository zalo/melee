#include "platform_launcher.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

int main() {
    char directory[] = "/tmp/melee-xdg-test-XXXXXX";
    assert(mkdtemp(directory));
    const std::string root = directory;
    setenv("HOME", directory, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_CACHE_HOME");
    assert(MeleeConfigPath() == root + "/.config/melee-native/");
    assert(MeleeCachePath() == root + "/.cache/melee-native/");
    setenv("XDG_CONFIG_HOME", "relative/ignored", 1);
    assert(MeleeConfigPath() == root + "/.config/melee-native/");
    const auto config = root + "/custom-config", cache = root + "/custom-cache";
    setenv("XDG_CONFIG_HOME", config.c_str(), 1);
    setenv("XDG_CACHE_HOME", cache.c_str(), 1);
    assert(MeleeConfigPath() == config + "/melee-native/");
    assert(MeleeCachePath() == cache + "/melee-native/");
    assert(std::filesystem::is_directory(config + "/melee-native"));
    assert(std::filesystem::is_directory(cache + "/melee-native"));
    std::filesystem::remove_all(root);
}
