#pragma once
#include <cstdio>
#include <cstring>

// Mesa 25.3.0 to 26.1.3 mishandle a uniform array with an explicit location whose unused trailing
// elements the linker trimmed: the trimmed range no longer matches the one reserved for the location,
// so the location keeps its "inactive explicit location" marker, (void*)-1. Dawn's GLSL declares its
// immediates exactly that way. Two things follow. glUniform on that location is ignored without an
// error, so the shaders never get their immediates (the driver option disable_uniform_array_resize
// stops the trimming). And reading a linked program back from a cache (their own on-disk shader cache
// as well as glProgramBinary) builds a resource-name hash that dereferences the marker (SIGSEGV at
// address 0xffffffffffffffff under glLinkProgram or glProgramBinary), so the first launch works and
// every later one dies on the first pipeline. Upstream fix: "util/u_range_remap: allow insert to
// truncate range", released in Mesa 26.1.4 and 26.2.0. The argument is the GL_VERSION string
// ("OpenGL ES 3.1 Mesa 25.3.6").
inline bool MeleeMesaShaderCacheCrashes(const char* glVersion) {
    const char* mesa = glVersion ? std::strstr(glVersion, "Mesa ") : nullptr;
    int major = 0, minor = 0, patch = 0;
    if (!mesa || std::sscanf(mesa + 5, "%d.%d.%d", &major, &minor, &patch) < 2) return false;
    if (major == 25) return minor >= 3;
    return major == 26 && (minor == 0 || (minor == 1 && patch < 4));
}
