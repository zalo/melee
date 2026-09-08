#include <melee/lb/types.h>
#include <melee/lb/lbcommand.h>
#include <melee/ft/types.h>
#include <melee/pl/types.h>
#include <melee/gm/types.h>
#include <melee/gm/gmresultplayer.static.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"Command test failed: %s\n",#x);abort();} } while(0)
_Static_assert(offsetof(ftCommonData,x6BC)==0x6BC,"Entry duration in common parameters");
_Static_assert(offsetof(ftCommonData,x6DC_colorsByPlayer)==0x6DC,"Common parameter color offsets");
_Static_assert(offsetof(ftCommonData,metal_armor)==0x6F0,"Common parameter numeric tail");
_Static_assert(offsetof(ftCommonData,x808)==0x808,"Common parameter final vector");
_Static_assert(sizeof(union CmdUnion)==4,"Serialized commands must stay four bytes");
_Static_assert(offsetof(struct lbl_8046B6A0_24C_t,x58)==offsetof(struct MatchEnd,player_standings),"Results player overlay");
_Static_assert(offsetof(struct lbl_8046B6A0_24C_t,x44C)==offsetof(struct MatchEnd,x44C),"Results bonus overlay");
_Static_assert(offsetof(struct S_UNK_YOSHI2,x8_end_index)==sizeof(TempS),"Yoshi second visibility group");
_Static_assert(offsetof(struct S_UNK_YOSHI2,xC_start_index)==sizeof(TempS)+offsetof(TempS,x4),"Yoshi second visibility indices");
_Static_assert(sizeof(union ColorOverlay_x8_t)==4,"Color script cells must stay four bytes");
_Static_assert(offsetof(CommandInfo,u)==offsetof(ColorOverlay,x8_ptr1),"Shared script cursor");
_Static_assert(offsetof(CommandInfo,loop_count)==offsetof(ColorOverlay,xC_loop),"Shared loop count");
_Static_assert(offsetof(CommandInfo,event_return)==offsetof(ColorOverlay,event_return),"Shared return stack");
_Static_assert(offsetof(struct ftData_80085FD4_ret,x8)==offsetof(struct Fighter_WaitAnimData,x8),"Animation size alias");
_Static_assert(offsetof(struct ftData_80085FD4_ret,x14)==offsetof(struct Fighter_WaitAnimData,x14),"Animation address alias");
_Static_assert(offsetof(ColorOverlay,x28_colanim) >= offsetof(CommandInfo,event_return)+sizeof(((CommandInfo*)0)->event_return), "Color animation state follows the complete command stack");
static u32 flash_a,flash_b;
void lbBgFlash_80021C48(u32 a,u32 b) {flash_a=a;flash_b=b;}
static union CmdUnion* branch;
void* MeleeNativeScriptPointer(const void* field) {(void)field;return branch;}
int main(void) {
    PackedS16x4 positions;
    Results_UnpackHalfWords((u16*)positions.h, 0xFFF2000E, 0x00180000);
    CHECK(positions.h[0]==-14 && positions.h[1]==14 && positions.h[2]==24 && positions.h[3]==0);
    static plActionStats stats;
    CHECK(plActionStatsHighCounter(&stats, 0x6B)==&stats.x594);
    CHECK(plActionStatsHighCounter(&stats, 0x70)==&stats.x598[4]);
    CHECK(offsetof(struct MatchPlayerData,x1C)==0x1C);
    UnkFlagStruct draw_flags = {.u8=1};
    CHECK(draw_flags.b7&&!draw_flags.b0);
    draw_flags.u8=128; CHECK(draw_flags.b0&&!draw_flags.b7);
    static Fighter fighter;
    fighter.x594_s32=0x40000000;
    CHECK(fighter.x594_b1_loop&&!fighter.x594_b0&&!fighter.x594_b3);
    fighter.x594_s32=0x10000000;
    CHECK(fighter.x594_b3&&!fighter.x594_b4);
    fighter.x594_s32=0x003FFE3F;
    CHECK(fighter.x594_bits==8191&&fighter.x597_bits==63);
    fighter.x594_s32=0x1C0;
    CHECK(fighter.x596_bits.x7==7);
    union CmdUnion command;
    u32 bits=(9U<<26)|(0xA5U<<18)|0x23456U;
    memcpy(&command,&bits,4);
    struct gmScriptEventDefault event;
    memcpy(&event,&bits,4);
    CHECK(event.opcode==9&&event.value1==(bits&0x3FFFFFF));
    CHECK(command.Command_09.id==9&&command.Command_09.param_1==0xA5&&command.Command_09.param_2==0x23456);
    bits=0xFFFE0003;memcpy(&command,&bits,4);
    CHECK(command.spawn_gfx_2.offsetZ==-2&&command.spawn_gfx_2.offsetY==3);
    bits=(17U<<26)|(1U<<25);memcpy(&command,&bits,4);
    CHECK(command.unk6.opcode==17&&command.unk6.unk1==1);
    union ColorOverlay_x8_t color;
    bits=0x12345678;memcpy(&color,&bits,4);
    CHECK(color.light_color.r==0x12&&color.light_color.g==0x34&&color.light_color.b==0x56&&color.light_color.a==0x78);
    union CmdUnion script[5]={{0}};
    CommandInfo info={0};info.u=script;
    script[0].Command_03.value=2;
    Command_03(&info);CHECK(info.u==&script[1]&&info.loop_count==2);
    info.u=&script[2];Command_04(&info);CHECK(info.u==&script[1]&&info.loop_count==2);
    info.u=&script[2];Command_04(&info);CHECK(info.u==&script[3]&&info.loop_count==0);
    ColorOverlay overlay={0};
    overlay.x28_colanim.i=1234;
    CommandInfo* shared=(CommandInfo*)&overlay;
    for (int depth=0;depth<3;++depth) { shared->u=script; Command_03(shared); }
    CHECK(shared->loop_count==6);
    for (int depth=0;depth<3;++depth) { shared->u=&script[2]; Command_04(shared); shared->u=&script[2]; Command_04(shared); }
    CHECK(shared->loop_count==0&&overlay.x28_colanim.i==1234);
    info.u=script;branch=&script[4];Command_05(&info);
    CHECK(info.u==branch&&info.loop_count==1);
    Command_06(&info);CHECK(info.u==&script[2]&&info.loop_count==0);
    info.u=script;Command_07(&info);CHECK(info.u==branch);
    info.u=script;script[0].Command_09.param_1=77;script[0].Command_09.param_2=900;
    Command_09(&info);CHECK(flash_a==77&&flash_b==900&&info.u==&script[1]);
    puts("PASS: command cell ABI, signed fields, colors, loops, calls and jumps");
}
