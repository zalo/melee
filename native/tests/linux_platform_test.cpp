#include "platform_launcher.h"
#include "platform/flip/mesa_quirks.h"
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
    // Mesa releases that crash reading a cached program back: 25.3.0 up to 26.1.3.
    assert(MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 25.3.6"));   // the tester's Pi 5 on Batocera 43
    assert(MeleeMesaShaderCacheCrashes("OpenGL ES 3.2 Mesa 25.3.3-1"));
    assert(MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 26.0.8"));
    assert(MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 26.1.3"));
    assert(MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 26.1.0-devel (git-0123abc)"));
    assert(!MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 26.1.4"));
    assert(!MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 26.2.2"));
    assert(!MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 25.2.8"));
    assert(!MeleeMesaShaderCacheCrashes("OpenGL ES 3.1 Mesa 22.3.6"));
    assert(!MeleeMesaShaderCacheCrashes("OpenGL ES 3.2 v1.g29p1-01eac0,rk_so_ver:5"));
    assert(!MeleeMesaShaderCacheCrashes(""));
    assert(!MeleeMesaShaderCacheCrashes(nullptr));
}
