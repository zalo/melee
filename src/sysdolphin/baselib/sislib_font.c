#include "sislib_font.h"

#ifdef MELEE_NATIVE
TextGlyphTexture HSD_SisLib_FontAtlas[287] ATTRIBUTE_ALIGN(32);
#else
TextGlyphTexture HSD_SisLib_FontAtlas[] ATTRIBUTE_ALIGN(32) = {
#include <sysdolphin/baselib/sislib_font.inc>
};
#endif
