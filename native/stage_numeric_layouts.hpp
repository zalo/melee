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
    {"GrdSamus1Bio00_CMPR_image",400,{{0xa0,240}},{},{}, {44}},
    // Home-Run Contest, trophy stages and Adventure routes.
    {"GrdHomerunBillTen_CMPR_image",0,{},{}},
    {"GrdFigure1Eye_I8_image",0,{},{}},
    {"GrdFigure2Ashike_C8_image",0,{},{}},
    {"GrdFigure3MaskA_CMPR_image",0,{},{}},
    {"GrdHealFigureCa_C8_image",0,{},{}},
    {"GrdFiguregetBG3_C8_image",0,{},{}},
    {"GrdBigblueArch2_CMPR_image",0,{},{}},
    {"GrdZebesRPipe1far_CMPR_image",0,{},{}},
    // grkinokoroute.c: int, grZakoGenerator_SpawnDesc at +4.
    {"GrdDonkeyFloor1_C8_image",0,{{4,2}},{{6,2}}},
    // grshrineroute.c: four material scripts, a DynamicsDesc at +0x10, spawn_desc at +0x28.
    {"GrdKinokoRWood3_CMPR_image",0,{{0x28,2}},{{0x2a,2}},{0,4,8,12},{16}},
    // grpushon.c: DynamicsDesc slots, then 30 {s32,s16,s16} entries and lookup pairs.
    {"GrdPushonBlue_RGB565_image",532,{{0x20,4},{0x28,4},{0x30,4},{0x38,4},{0x40,4},{0x48,4},{0x50,4},{0x58,4},{0x60,4},{0x68,4},
        {0x70,4},{0x78,4},{0x80,4},{0x88,4},{0x90,4},{0x98,4},{0xa0,4},{0xa8,4},{0xb0,4},{0xb8,4},
        {0xc0,4},{0xc8,4},{0xd0,4},{0xd8,4},{0xe0,4},{0xe8,4},{0xf0,4},{0xf8,4},{0x100,4},{0x108,4}},{},{},{0,4,8,12,16,20}},
    // Target Test: DynamicsDesc slots for the moving/damaging surfaces.
    {"GrdTFalcoFioor00_C8_image",16,{},{},{},{0,4,8,12}},
    {"GrdTFoxFioor01_RGBA8_image",16,{},{},{},{0,4,8,12}},
    {"GrdTGanonAsiba0_C8_image",12,{},{},{},{0,4,8}},
    {"GrdTMewtwoFloor01_CMPR_image",32,{},{},{},{0,4,8,12,16,20,24,28}},
    {"GrdTargetPurinFloorC_I8_image",4,{},{},{},{0}},
    // The remaining Target Test stages keep one unused word; several share this image.
    {"GrdTCaptainAdver1n_CMPR_image",0,{},{}},
    {"GrdTGamewatchAclmark_IA4_image",0,{},{}},
    {"GrdTPopoCondor0_C4_image",0,{},{}},
    {"GrdTKirbyFioor00_RGBA8_image",0,{},{}},
    {"GrdTKoopaFloor00_RGB565_image",0,{},{}},
    {"GrdTLinkRelief01_RGBA8_image",0,{},{}},
    {"GrdTNessFloor01_RGBA8_image",0,{},{}},
    {"GrdTPeachFloor00_RGBA8_image",0,{},{}},
    {"GrdTPikachuFloor01_C4_image",0,{},{}},
    {"GrdTSamusFioor02_C8_image",0,{},{}},
    {"GrdTargetBack00_CMPR_image",0,{},{}}
};
