#pragma once
#include <initializer_list>

// Serialized stage parameters retain 32-bit storage. Pointer fields are
// resolved through archive ownership, not cast to host addresses. Offsets are the GameCube
// layouts in src/melee/gr/gr*.c; numeric words are the default, with mixed
// halfword and byte fields explicitly listed below.
struct StageNumericRange { unsigned offset, bytes; };
struct StageNumericLayout {
    const char* marker;
    unsigned minimum_size;
    std::initializer_list<StageNumericRange> halves;
    std::initializer_list<StageNumericRange> bytes;
    std::initializer_list<unsigned> scripts;
    std::initializer_list<unsigned> data_pointers;
};
inline const StageNumericLayout stage_numeric_layouts[] = {
    {"GrdFzeroAdver1_CMPR_image",80,{},{},{0,4},{8,12}},
    {"GrdBattle2Color_RGB565_image",8,{},{},{0,4}},
    {"GrdLastCloud2_I8_image",16,{},{},{0,4,8,12}},
    {"GrdEffIzumFunsui_RGB5A3_image",84,{},{}},
    {"GrdOnettBicycleA_CMPR_image",104,{},{}},
    {"GrdBigBlueArch2_CMPR_image",324,{},{}},
    {"GrdCorneriaBuilda1_CMPR_image",140,{},{},{132}},
    {"GrdCastleBlockBlue2_CMPR_image",324,{{0,16},{0x40,8},{0x54,8},{0x5c,4},{0x70,4},{0x84,4},{0x98,4},{0xac,4},{0xc0,4},{0xd4,4},{0xe8,4},{0xfc,4},{0x12c,8}},{},{276}},
    {"GrdFoursideBillHeri_CMPR_image",76,{{0x44,8}},{}},
    {"GrdFlatzoneAclmark_IA4_image",64,{},{}},
    {"GrdGreatbayBeach1_CMPR_image",164,{{0,4},{0x44,8},{0x70,8},{0x7c,40}},{}},
    {"GrdGardenCloud_I4_image",32,{},{}},
    {"GrdGreenBBlockA_CMPR_image",124,{},{}},
    {"GrdInishie1BBlkA_CMPR_image",84,{{0x14,12}},{}},
    {"GrdInishie2CherryA_CMPR_image",76,{{0,20},{0x48,4}},{}},
    {"GrdDonkeyEda1_CMPR_image",188,{{0x44,16}},{},{132}},
    {"GrdKraidAntenna1_RGBA8_image",52,{},{}},
    {"GrdOldkongoCask1_C4_image",112,{{0,4},{0x2c,16}},{},{108}},
    {"GrdOldpupupuBgn1_RGBA8_image",52,{{0,8}},{}},
    {"GrdOldyoshHeyhoA_C4_image",28,{{0,4},{0x10,12}},{}},
    {"GrdPSNormalBaseA_CMPR_image",84,{{0x48,12}},{{0x1c,4}}},
    {"GrdPSRockGrassB_CMPR_image",84,{{0x48,12}},{{0x1c,4}}},
    {"GrdEffPStadiumFunsui1_CMPR_image",84,{{0x48,12}},{{0x1c,4}}},
    {"GrdPSGrassGrassA_CMPR_image",84,{{0x48,12}},{{0x1c,4}}},
    {"GrdPSFireFGroundA_CMPR_image",84,{{0x48,12}},{{0x1c,4}}},
    {"GrdPuraBGNishihoIA8_IA8_image",0,{},{}},
    {"GrdRCruiseCarpet_C8_image",72,{},{}},
    {"GrdShirineUka2_MIPMAP_CMPR_image",0,{},{}},
    {"GrdStoryBFarTreeA_CMPR_image",36,{},{}},
    {"GrdVenomBase1_CMPR_image",60,{},{},{56}},
    {"GrdYorsterAppleA_CMPR_image",32,{},{}},
    {"GrdSamus1Bio00_CMPR_image",400,{{0xa0,240}},{},{}, {44}}
};
