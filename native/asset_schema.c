#include "asset_schema.h"
#include <melee/sc/types.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/lb/lbanim.h>
#include <melee/pl/types.h>
#include <melee/gr/types.h>
#include <melee/gr/native_stageparams.h>
#include <melee/mp/types.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itCommonItems.h>
#include <melee/it/itCharItems.h>
#include <melee/it/itYoyo.h>
#include <melee/it/kinds/types.h>
#include <melee/ft/kinds/ftSamus/types.h>
#include <melee/ft/kinds/ftGameWatch/types.h>
#include <melee/it/kinds/itkinoko.h>
#include <melee/gm/gmeventdata.h>
#include <melee/ty/types.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/sobjlib.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/spline.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/robj.h>
#define F(T, member, offset, kind, count, target) {offset,offsetof(T,member),kind,count,target}
#define P(T,m,o,t) F(T,m,o,AF_POINTER,1,t)
#define U(T,m,o,n) F(T,m,o,AF_WORD,n,0)
#define H(T,m,o,n) F(T,m,o,AF_HALF,n,0)
#define B(T,m,o,n) F(T,m,o,AF_BYTE,n,0)
#define S(name, T, disk, ...) static const MeleeAssetField name##_fields[]={__VA_ARGS__}; static const MeleeAssetSchema name={disk,sizeof(T),name##_fields,sizeof(name##_fields)/sizeof(*name##_fields)}
S(scene,SceneDesc,16,P(SceneDesc,models,0,AT_MODELS),P(SceneDesc,cameras,4,AT_CAMERAS),P(SceneDesc,lights,8,AT_LIGHT_LISTS),P(SceneDesc,fogs,12,AT_SCENE_FOG));
S(model,DynamicModelDesc,16,P(DynamicModelDesc,joint,0,AT_JOINT),P(DynamicModelDesc,anims,4,AT_ANIMS),P(DynamicModelDesc,matanims,8,AT_MATANIMS),P(DynamicModelDesc,shapeanims,12,AT_SHAPEANIMS));
S(scene_fog,struct SceneFogDesc,8,P(struct SceneFogDesc,desc,0,AT_FOG),P(struct SceneFogDesc,anims,4,AT_CAMERA_ANIMS));
S(camera_list,struct SceneCameraDesc,8,P(struct SceneCameraDesc,desc,0,AT_CAMERA),P(struct SceneCameraDesc,anims,4,AT_CAMERA_ANIMS));
S(light_list,struct LightList,8,P(struct LightList,desc,0,AT_LIGHT),P(struct LightList,anims,4,AT_LIGHT_ANIMS));
S(camera,HSD_CObjDesc,64,P(HSD_CObjDesc,common.class_name,0,AT_STRING),H(HSD_CObjDesc,common.flags,4,2),H(HSD_CObjDesc,common.viewport,8,4),H(HSD_CObjDesc,common.scissor,16,4),P(HSD_CObjDesc,common.eyepos,24,AT_WOBJ),P(HSD_CObjDesc,common.interest,28,AT_WOBJ),U(HSD_CObjDesc,common.roll,32,1),P(HSD_CObjDesc,common.up_vector,36,AT_VEC3),U(HSD_CObjDesc,common.nnear,40,2),U(HSD_CObjDesc,frustum.top,48,4));
S(wobj,HSD_WObjDesc,20,P(HSD_WObjDesc,class_name,0,AT_STRING),U(HSD_WObjDesc,pos,4,3),P(HSD_WObjDesc,robjdesc,16,AT_ROBJ));
S(light,HSD_LightDesc,28,P(HSD_LightDesc,class_name,0,AT_STRING),P(HSD_LightDesc,next,4,AT_LIGHT),H(HSD_LightDesc,flags,8,2),B(HSD_LightDesc,color,12,4),P(HSD_LightDesc,position,16,AT_WOBJ),P(HSD_LightDesc,interest,20,AT_WOBJ),P(HSD_LightDesc,u,24,AT_WORDS));
S(joint,HSD_Joint,64,P(HSD_Joint,class_name,0,AT_STRING),U(HSD_Joint,flags,4,1),P(HSD_Joint,child,8,AT_JOINT),P(HSD_Joint,next,12,AT_JOINT),P(HSD_Joint,u,16,AT_DOBJ),U(HSD_Joint,rotation,20,9),P(HSD_Joint,mtx,56,AT_MTX),P(HSD_Joint,robjdesc,60,AT_ROBJ));
S(dobj,HSD_DObjDesc,16,P(HSD_DObjDesc,class_name,0,AT_STRING),P(HSD_DObjDesc,next,4,AT_DOBJ),P(HSD_DObjDesc,mobjdesc,8,AT_MOBJ),P(HSD_DObjDesc,pobjdesc,12,AT_POBJ));
S(mobj,HSD_MObjDesc,24,P(HSD_MObjDesc,class_name,0,AT_STRING),U(HSD_MObjDesc,rendermode,4,1),P(HSD_MObjDesc,texdesc,8,AT_TOBJ),P(HSD_MObjDesc,mat,12,AT_MATERIAL),P(HSD_MObjDesc,renderdesc,16,AT_NONE),P(HSD_MObjDesc,pedesc,20,AT_PE));
S(material,HSD_Material,20,B(HSD_Material,ambient,0,12),U(HSD_Material,alpha,12,2));
S(pe,HSD_PEDesc,12,B(HSD_PEDesc,flags,0,12));
S(tobj,HSD_TObjDesc,92,P(HSD_TObjDesc,class_name,0,AT_STRING),P(HSD_TObjDesc,next,4,AT_TOBJ),U(HSD_TObjDesc,id,8,2),U(HSD_TObjDesc,rotate,16,9),U(HSD_TObjDesc,wrap_s,52,2),B(HSD_TObjDesc,repeat_s,60,2),U(HSD_TObjDesc,blend_flags,64,3),P(HSD_TObjDesc,imagedesc,76,AT_IMAGE),P(HSD_TObjDesc,tlutdesc,80,AT_TLUT),P(HSD_TObjDesc,lod,84,AT_LOD),P(HSD_TObjDesc,tev,88,AT_TEV));
S(image,HSD_ImageDesc,24,P(HSD_ImageDesc,image_ptr,0,AT_RAW),H(HSD_ImageDesc,width,4,2),U(HSD_ImageDesc,format,8,4));
S(tlut,HSD_TlutDesc,16,P(HSD_TlutDesc,lut,0,AT_RAW),U(HSD_TlutDesc,fmt,4,2),H(HSD_TlutDesc,n_entries,12,1));
S(lod,HSD_TexLODDesc,16,U(HSD_TexLODDesc,minFilt,0,2),B(HSD_TexLODDesc,bias_clamp,8,2),U(HSD_TexLODDesc,max_anisotropy,12,1));
S(tev,HSD_TObjTevDesc,32,B(HSD_TObjTevDesc,color_op,0,28),U(HSD_TObjTevDesc,active,28,1));
S(pobj,HSD_PObjDesc,24,P(HSD_PObjDesc,class_name,0,AT_STRING),P(HSD_PObjDesc,next,4,AT_POBJ),P(HSD_PObjDesc,verts,8,AT_VERTICES),H(HSD_PObjDesc,flags,12,2),P(HSD_PObjDesc,display,16,AT_RAW),P(HSD_PObjDesc,u,20,AT_ENVELOPES));
S(vertex,HSD_VtxDescList,24,U(HSD_VtxDescList,attr,0,4),B(HSD_VtxDescList,frac,16,1),H(HSD_VtxDescList,stride,18,1),P(HSD_VtxDescList,vertex,20,AT_RAW));
S(envelope,HSD_EnvelopeDesc,8,P(HSD_EnvelopeDesc,joint,0,AT_JOINT),U(HSD_EnvelopeDesc,weight,4,1));
S(anim,HSD_AnimJoint,20,P(HSD_AnimJoint,child,0,AT_ANIM),P(HSD_AnimJoint,next,4,AT_ANIM),P(HSD_AnimJoint,aobjdesc,8,AT_AOBJ),P(HSD_AnimJoint,robj_anim,12,AT_ROBJ_ANIM),U(HSD_AnimJoint,flags,16,1));
S(aobj,HSD_AObjDesc,16,U(HSD_AObjDesc,flags,0,2),P(HSD_AObjDesc,fobjdesc,8,AT_FOBJ),P(HSD_AObjDesc,obj_id,12,AT_JOINT));
S(fobj,HSD_FObjDesc,20,P(HSD_FObjDesc,next,0,AT_FOBJ),U(HSD_FObjDesc,length,4,2),B(HSD_FObjDesc,type,12,4),P(HSD_FObjDesc,ad,16,AT_RAW));
S(matjoint,HSD_MatAnimJoint,12,P(HSD_MatAnimJoint,child,0,AT_MATANIMJOINT),P(HSD_MatAnimJoint,next,4,AT_MATANIMJOINT),P(HSD_MatAnimJoint,matanim,8,AT_MATANIM));
S(matanim,HSD_MatAnim,16,P(HSD_MatAnim,next,0,AT_MATANIM),P(HSD_MatAnim,aobjdesc,4,AT_AOBJ),P(HSD_MatAnim,texanim,8,AT_TEXANIM),P(HSD_MatAnim,renderanim,12,AT_NONE));
S(texanim,HSD_TexAnim,24,P(HSD_TexAnim,next,0,AT_TEXANIM),U(HSD_TexAnim,id,4,1),P(HSD_TexAnim,aobjdesc,8,AT_AOBJ),P(HSD_TexAnim,imagetbl,12,AT_IMAGES),P(HSD_TexAnim,tluttbl,16,AT_TLUTS),H(HSD_TexAnim,n_imagetbl,20,2));
S(camanim,HSD_CameraAnim,12,P(HSD_CameraAnim,aobjdesc,0,AT_AOBJ),P(HSD_CameraAnim,eye_anim,4,AT_WOBJ_ANIM),P(HSD_CameraAnim,interest_anim,8,AT_WOBJ_ANIM));
S(wobjanim,HSD_WObjAnim,8,P(HSD_WObjAnim,aobjdesc,0,AT_AOBJ),P(HSD_WObjAnim,robjanim,4,AT_ROBJ_ANIM));
S(lightanim,HSD_LightAnim,16,P(HSD_LightAnim,next,0,AT_LIGHT_ANIM),P(HSD_LightAnim,aobjdesc,4,AT_AOBJ),P(HSD_LightAnim,position_anim,8,AT_WOBJ_ANIM),P(HSD_LightAnim,interest_anim,12,AT_WOBJ_ANIM));
S(trophies,struct TrophyData,36,U(struct TrophyData,id,0,8),B(struct TrophyData,x20,32,4));
S(trophy_display,struct TyDspEntry,16,U(struct TyDspEntry,x00,0,1),B(struct TyDspEntry,x04,4,4),U(struct TyDspEntry,x08,8,2));
S(shapejoint,HSD_ShapeAnimJoint,12,P(HSD_ShapeAnimJoint,child,0,AT_SHAPEJOINT),P(HSD_ShapeAnimJoint,next,4,AT_SHAPEJOINT),P(HSD_ShapeAnimJoint,shapeanimdobj,8,AT_SHAPEDOBJ));
S(shapedobj,HSD_ShapeAnimDObj,8,P(HSD_ShapeAnimDObj,next,0,AT_SHAPEDOBJ),P(HSD_ShapeAnimDObj,shapeanim,4,AT_SHAPEANIM));
S(shapeanim,HSD_ShapeAnim,8,P(HSD_ShapeAnim,next,0,AT_SHAPEANIM),P(HSD_ShapeAnim,aobjdesc,4,AT_AOBJ));
S(shapeset,HSD_ShapeSetDesc,28,H(HSD_ShapeSetDesc,flags,0,2),U(HSD_ShapeSetDesc,nb_vertex_index,4,1),P(HSD_ShapeSetDesc,vertex_desc,8,AT_CPU_VERTICES),P(HSD_ShapeSetDesc,vertex_idx_list,12,AT_RAW_TABLE),U(HSD_ShapeSetDesc,nb_normal_index,16,1),P(HSD_ShapeSetDesc,normal_desc,20,AT_CPU_VERTICES),P(HSD_ShapeSetDesc,normal_idx_list,24,AT_RAW_TABLE));
S(effectdesc,EF_EffectDesc,20,U(EF_EffectDesc,lifetime,0,1),P(EF_EffectDesc,model_desc.joint,4,AT_JOINT),P(EF_EffectDesc,model_desc.animjoint,8,AT_ANIM),P(EF_EffectDesc,model_desc.matanim_joint,12,AT_MATANIMJOINT),P(EF_EffectDesc,model_desc.shapeanim_joint,16,AT_SHAPEJOINT));
S(spline,HSD_Spline,24,B(HSD_Spline,type,0,1),H(HSD_Spline,numcv,2,1),U(HSD_Spline,tension,4,1),P(HSD_Spline,cv,8,AT_WORDS),U(HSD_Spline,totalLength,12,1),P(HSD_Spline,segLength,16,AT_WORDS),P(HSD_Spline,segPoly,20,AT_WORDS));
S(ptcllist,HSD_SList,8,P(HSD_SList,next,0,AT_PTCL_LIST),U(HSD_SList,data,4,1));
S(playercommon,pl_804D6470_t,0x184,U(pl_804D6470_t,x0,0,48),B(pl_804D6470_t,xC0,0xC0,4),U(pl_804D6470_t,xC4,0xC4,48));
S(ftcommon,ftCommonData,0x818,U(ftCommonData,horizontal_stick_deadzone,0,0x6DC/4),B(ftCommonData,x6DC_colorsByPlayer,0x6DC,20),U(ftCommonData,metal_armor,0x6F0,(0x7D8-0x6F0)/4),B(ftCommonData,x7D8,0x7D8,4),U(ftCommonData,x7DC,0x7DC,(0x818-0x7DC)/4));
S(ftparts,FighterPartsTable,12,P(FighterPartsTable,joint_to_part,0,AT_RAW),P(FighterPartsTable,part_to_joint,4,AT_RAW),U(FighterPartsTable,parts_num,8,1));
S(bytepair,struct Fighter_804D6540_t,8,P(struct Fighter_804D6540_t,x0,0,AT_RAW),U(struct Fighter_804D6540_t,x4,4,1));
S(shake,struct Fighter_ShakeTable_t,8,P(struct Fighter_ShakeTable_t,x0,0,AT_WORDS),U(struct Fighter_ShakeTable_t,x4,4,1));
S(colordesc,Fighter_804D653C_t,8,P(Fighter_804D653C_t,unk,0,AT_SCRIPT),B(Fighter_804D653C_t,unk4,4,2));
S(cpuconfig,struct Fighter_804D64FC_t,40,P(struct Fighter_804D64FC_t,cmdscripts,0,AT_RAW_TABLE),P(struct Fighter_804D64FC_t,x4,4,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,x8,8,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,xC,12,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,x10,16,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,x14,20,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,x18,24,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,x1C,28,AT_WORD_TABLE),P(struct Fighter_804D64FC_t,x20,32,AT_WORDS),P(struct Fighter_804D64FC_t,x24,36,AT_WORDS));
S(jointanimpair,StaticModelDesc,8,P(StaticModelDesc,joint,0,AT_JOINT),P(StaticModelDesc,animjoint,4,AT_ANIM));
typedef struct NativeMapJointPairs { HSD_Joint* joint; s16* pairs; s32 count; } NativeMapJointPairs;
typedef struct NativeLightOverride { HSD_LightDesc* light; u8 flags; } NativeLightOverride;
S(maphead,UnkStageDat,48,P(UnkStageDat,unk0,0,AT_MAP_JOINT_PAIRS),U(UnkStageDat,unk4,4,1),P(UnkStageDat,unk8,8,AT_MAP_MODEL),U(UnkStageDat,unkC,12,1),P(UnkStageDat,unk10,16,AT_SPLINE_TABLE),U(UnkStageDat,unk14,20,1),P(UnkStageDat,unk18,24,AT_LIGHT_OVERRIDE),U(UnkStageDat,unk1C,28,1),P(UnkStageDat,unk20,32,AT_SHADOW_ENTRY),U(UnkStageDat,unk24,36,1),P(UnkStageDat,unk28,40,AT_MOBJ_TABLE),U(UnkStageDat,unk2C,44,1));
S(mapmodel,struct UnkStageDat_x8_t,52,P(struct UnkStageDat_x8_t,unk0,0,AT_JOINT),P(struct UnkStageDat_x8_t,unk4,4,AT_ANIMS),P(struct UnkStageDat_x8_t,unk8,8,AT_MATANIMS),P(struct UnkStageDat_x8_t,unkC,12,AT_SHAPEANIMS),P(struct UnkStageDat_x8_t,x10,16,AT_CAMERA),P(struct UnkStageDat_x8_t,x14,20,AT_CAMERA_ANIMS),P(struct UnkStageDat_x8_t,x18,24,AT_LIGHT_LISTS),P(struct UnkStageDat_x8_t,x1C,28,AT_FOG),P(struct UnkStageDat_x8_t,unk20,32,AT_HALVES),U(struct UnkStageDat_x8_t,unk24,36,1),P(struct UnkStageDat_x8_t,x28,40,AT_RAW),P(struct UnkStageDat_x8_t,x2C,44,AT_HALVES),U(struct UnkStageDat_x8_t,x30,48,1));
S(mapjointpairs,NativeMapJointPairs,12,P(NativeMapJointPairs,joint,0,AT_JOINT),P(NativeMapJointPairs,pairs,4,AT_HALVES),U(NativeMapJointPairs,count,8,1));
S(lightoverride,NativeLightOverride,8,P(NativeLightOverride,light,0,AT_LIGHT),B(NativeLightOverride,flags,4,1));
S(shadowentry,struct GroundShadowEntry,8,P(struct GroundShadowEntry,unk0,0,AT_LIGHT_ANIM),{4,sizeof(void*),AF_BYTE,1,0});
S(collmap,MapCollData,48,P(MapCollData,verts,0,AT_WORDS),U(MapCollData,vert_count,4,1),P(MapCollData,lines,8,AT_HALVES),U(MapCollData,line_count,12,1),H(MapCollData,floor_start,16,10),P(MapCollData,joints,36,AT_MAP_JOINT),U(MapCollData,joint_count,40,2));
S(mapjoint,MapJoint,40,H(MapJoint,floor_start,0,10),U(MapJoint,left_bound,20,4),H(MapJoint,vtx_start,36,2));
S(groundparam,GroundParam,220,U(GroundParam,y,0,1),H(GroundParam,x4,4,1),B(GroundParam,x6_pad,6,2),H(GroundParam,x8,8,2),U(GroundParam,xC,12,8),B(GroundParam,x2C_pad,44,2),H(GroundParam,x2E,46,1),U(GroundParam,x30,48,14),H(GroundParam,x68,104,36),P(GroundParam,stage_params,176,AT_STAGE_PARAM),U(GroundParam,stage_param_count,180,1),B(GroundParam,xB8,184,36));
S(robjanim,HSD_RObjAnimJoint,8,P(HSD_RObjAnimJoint,next,0,AT_ROBJ_ANIM),P(HSD_RObjAnimJoint,aobjdesc,4,AT_AOBJ));
S(robj,HSD_RObjDesc,12,P(HSD_RObjDesc,next,0,AT_ROBJ),U(HSD_RObjDesc,flags,4,1),P(HSD_RObjDesc,u,8,AT_NONE));
S(rvalues,HSD_RvalueList,8,U(HSD_RvalueList,flags,0,1),P(HSD_RvalueList,joint,4,AT_JOINT));
S(expression,HSD_ExpDesc,8,P(HSD_ExpDesc,func,0,AT_NONE),P(HSD_ExpDesc,rvalue,4,AT_RVALUES));
S(byteexpression,HSD_ByteCodeExpDesc,8,P(HSD_ByteCodeExpDesc,bytecode,0,AT_RAW),P(HSD_ByteCodeExpDesc,rvalue,4,AT_RVALUES));
S(stageparam,StageParam,100,U(StageParam,stkind,0,5),H(StageParam,x14,20,40));
S(itemstring,itClimbersStringAttributes,44,U(itClimbersStringAttributes,x0_count,0,9),P(itClimbersStringAttributes,x24_joint,36,AT_JOINT),P(itClimbersStringAttributes,x28_joint,40,AT_JOINT));
S(itemboomerang,itLinkBoomerangAttributes,100,U(itLinkBoomerangAttributes,x0,0,17),P(itLinkBoomerangAttributes,x44,68,AT_JOINT),P(itLinkBoomerangAttributes,x48,72,AT_JOINT),P(itLinkBoomerangAttributes,x4C_anim.anim,76,AT_ANIM),P(itLinkBoomerangAttributes,x4C_anim.matanim,80,AT_MATANIMJOINT),P(itLinkBoomerangAttributes,x4C_anim.shapeanim,84,AT_SHAPEJOINT),P(itLinkBoomerangAttributes,x58_anim.anim,88,AT_ANIM),P(itLinkBoomerangAttributes,x58_anim.matanim,92,AT_MATANIMJOINT),P(itLinkBoomerangAttributes,x58_anim.shapeanim,96,AT_SHAPEJOINT));
S(itemhookshot,itLinkHookshotAttributes,96,U(itLinkHookshotAttributes,x0,0,21),P(itLinkHookshotAttributes,x54,84,AT_JOINT),P(itLinkHookshotAttributes,x58,88,AT_JOINT),P(itLinkHookshotAttributes,x5C,92,AT_JOINT));
S(itemarrow,itLinkArrowAttributes,44,U(itLinkArrowAttributes,x0,0,9),P(itLinkArrowAttributes,x24,36,AT_JOINT),P(itLinkArrowAttributes,x28,40,AT_JOINT));
S(itemchain,itSeakChain_Attrs,108,U(itSeakChain_Attrs,x0,0,25),P(itSeakChain_Attrs,x64_joint,100,AT_JOINT),P(itSeakChain_Attrs,x68_joint,104,AT_JOINT));
S(itemyoyo,itYoyoAttributes,92,U(itYoyoAttributes,x0_CHARGE_SPAWN_POS,0,20),P(itYoyoAttributes,x50_string_joint,80,AT_JOINT),P(itYoyoAttributes,x54_yoyo_joint,84,AT_JOINT),P(itYoyoAttributes,x58_yoyo_matanim,88,AT_MATANIMJOINT));
S(itemdrawparts,it_266F_ItemVars,16,H(it_266F_ItemVars,x0,0,1),P(it_266F_ItemVars,x4,4,AT_RAW),H(it_266F_ItemVars,x8,8,1),P(it_266F_ItemVars,xC,12,AT_RAW));
S(itemgw,itGamewatchparachuteAttributes,4,P(itGamewatchparachuteAttributes,x0,0,AT_ITEM_DRAW_PARTS));
S(itemchef,itGamewatchchefAttributes,116,P(itGamewatchchefAttributes,x0,0,AT_ITEM_DRAW_PARTS),U(itGamewatchchefAttributes,x4,4,28));
typedef struct NativePurinParts { HSD_Joint* joint; FtPartsDesc parts; } NativePurinParts;
S(purinparts,NativePurinParts,12,P(NativePurinParts,joint,0,AT_JOINT),U(NativePurinParts,parts.model_num,4,1),P(NativePurinParts,parts.vis_table,8,AT_FIGHTER_VIS_TABLE));
S(samusbeam,struct UNK_SAMUS_S1,16,P(struct UNK_SAMUS_S1,x0_joint,0,AT_JOINT),P(struct UNK_SAMUS_S1,x4_anim_joints,4,AT_ANIMS),P(struct UNK_SAMUS_S1,x8_anim_joint,8,AT_ANIM),P(struct UNK_SAMUS_S1,xC_matanim_joint,12,AT_MATANIMJOINT));
S(itemgrapple,itSamusGrappleAttributes,176,U(itSamusGrappleAttributes,x0,0,25),P(itSamusGrappleAttributes,x64,100,AT_JOINT),P(itSamusGrappleAttributes,x68,104,AT_JOINT),P(itSamusGrappleAttributes,x6C,108,AT_JOINT),P(itSamusGrappleAttributes,x70,112,AT_JOINT),P(itSamusGrappleAttributes,x74,116,AT_ANIM),P(itSamusGrappleAttributes,x78,120,AT_MATANIMJOINT),P(itSamusGrappleAttributes,x7C,124,AT_SHAPEJOINT),P(itSamusGrappleAttributes,x80,128,AT_ANIM),P(itSamusGrappleAttributes,x84,132,AT_MATANIMJOINT),P(itSamusGrappleAttributes,x88,136,AT_SHAPEJOINT),P(itSamusGrappleAttributes,x8C,140,AT_ANIM),P(itSamusGrappleAttributes,x90,144,AT_MATANIMJOINT),P(itSamusGrappleAttributes,x94,148,AT_SHAPEJOINT),P(itSamusGrappleAttributes,x98,152,AT_ANIM),P(itSamusGrappleAttributes,x9C,156,AT_MATANIMJOINT),P(itSamusGrappleAttributes,xA0,160,AT_SHAPEJOINT),P(itSamusGrappleAttributes,xA4,164,AT_ANIM),P(itSamusGrappleAttributes,xA8,168,AT_MATANIMJOINT),P(itSamusGrappleAttributes,xAC,172,AT_SHAPEJOINT));
S(itempublic,it_804D6D20_t,24,P(it_804D6D20_t,x0,0,AT_ITEM_COMMON),P(it_804D6D20_t,x4,4,AT_ARTICLES),P(it_804D6D20_t,x8,8,AT_ARTICLES),P(it_804D6D20_t,xC,12,AT_ARTICLES),P(it_804D6D20_t,x10,16,AT_WORDS),P(it_804D6D20_t,x14,20,AT_COLOR_DESC));
S(kirbycopyfox,KirbyHatStruct,20,P(KirbyHatStruct,hat_joint,0,AT_JOINT),U(KirbyHatStruct,desc.model_num,4,1),P(KirbyHatStruct,desc.vis_table,8,AT_FIGHTER_VIS_TABLE),P(KirbyHatStruct,hat_dynamics[0],12,AT_ARTICLE),P(KirbyHatStruct,hat_dynamics[1],16,AT_ARTICLE));
S(kirbycopyyoshi,KirbyHatStruct,36,P(KirbyHatStruct,hat_joint,0,AT_JOINT),U(KirbyHatStruct,desc.model_num,4,1),P(KirbyHatStruct,desc.vis_table,8,AT_FIGHTER_VIS_TABLE),P(KirbyHatStruct,hat_dynamics[0],12,AT_JOINT),P(KirbyHatStruct,hat_dynamics[1],16,AT_ANIM),P(KirbyHatStruct,hat_dynamics[2],20,AT_ANIM),P(KirbyHatStruct,hat_dynamics[3],24,AT_ANIM),P(KirbyHatStruct,hat_dynamics[4],28,AT_ANIM),P(KirbyHatStruct,hat_dynamics[5],32,AT_ARTICLE));
S(gamewatchattr,ftGameWatchAttributes,148,U(ftGameWatchAttributes,x0_GAMEWATCH_WIDTH,0,1),B(ftGameWatchAttributes,x4_GAMEWATCH_COLOR,4,20),U(ftGameWatchAttributes,x18_GAMEWATCH_CHEF_LOOPFRAME,24,31));
S(fighter,ftData,96,P(ftData,x0,0,AT_FIGHTER_ATTR),P(ftData,ext_attr,4,AT_ITEM_NUMBERS),P(ftData,x8,8,AT_FIGHTER_PARTS),P(ftData,xC,12,AT_FIGHTER_ANIMS),P(ftData,x10,16,AT_RAW),P(ftData,x14,20,AT_FIGHTER_ANIMS),P(ftData,x18,24,AT_RAW),P(ftData,x1C,28,AT_FIGHTER_PART_ANIM_TABLE),P(ftData,x20,32,AT_FIGHTER_GUARD),P(ftData,x24,36,AT_WORDS),P(ftData,x28,40,AT_WORDS),P(ftData,x2C,44,AT_FIGHTER_DYNAMICS),P(ftData,x30,48,AT_FIGHTER_HURT),P(ftData,x34,52,AT_WORDS),P(ftData,x38,56,AT_WORDS),P(ftData,x3C,60,AT_WORDS),P(ftData,x40,64,AT_WORDS),P(ftData,x44,68,AT_FIGHTER_LEDGE),P(ftData,x48_items,72,AT_FIGHTER_ITEMS),P(ftData,x4C_sfx,76,AT_FIGHTER_SFX),P(ftData,x50,80,AT_WORDS),P(ftData,x54,84,AT_WORDS),P(ftData,x58,88,AT_FIGHTER_IK),P(ftData,x5C,92,AT_JOINT));
S(fighterattr,ftCo_DatAttrs,388,U(ftCo_DatAttrs,walk_accel_mul,0,96),B(ftCo_DatAttrs,weight_independent_throws_mask,384,1));
S(fighterparts,struct ftData_x8,24,U(struct ftData_x8,x0.model_num,0,1),P(struct ftData_x8,x0.vis_table,4,AT_FIGHTER_VIS_TABLE),U(struct ftData_x8,x8.x8,8,1),P(struct ftData_x8,x8.xC,12,AT_HALF_TABLE),B(struct ftData_x8,x10,16,5));
S(fightervis,FtPartsVisLookup,8,U(FtPartsVisLookup,x0,0,1),P(FtPartsVisLookup,x4,4,AT_PART_VIS));
S(partvis,TempS,8,U(TempS,x0,0,1),P(TempS,x4,4,AT_RAW));
S(fighteranims,struct Fighter_WaitAnimData,24,P(struct Fighter_WaitAnimData,x0,0,AT_STRING),U(struct Fighter_WaitAnimData,x4,4,2),P(struct Fighter_WaitAnimData,xC,12,AT_SCRIPT),U(struct Fighter_WaitAnimData,x10_animCurrFlags,16,1),U(struct Fighter_WaitAnimData,x14,20,1));
S(fighterpartanim,struct ftData_x1C,12,H(struct ftData_x1C,x0,0,2),P(struct ftData_x1C,x4,4,AT_RAW),P(struct ftData_x1C,x8,8,AT_ANIMS));
S(fighterguard,struct ftData_x20,8,P(struct ftData_x20,x0,0,AT_JOINT),U(struct ftData_x20,x8,4,1));
S(fighterdynamics,ftDynamics,20,U(ftDynamics,dynamicsNum,0,1),P(ftDynamics,ftDynamicBones,4,AT_BONE_DYNAMICS),U(ftDynamics,x4,8,1),P(ftDynamics,x8,12,AT_WORDS),P(ftDynamics,x10,16,AT_WORD_TABLE));
S(fighterhurt,struct ftData_x30,8,U(struct ftData_x30,count,0,1),P(struct ftData_x30,inits,4,AT_WORDS));
S(fighterledge,ftData_x44_t,28,H(ftData_x44_t,unk0,0,6),U(ftData_x44_t,unkC,12,4));
S(fightersfx,FtSFX,56,P(FtSFX,smash,0,AT_SFX_ARRAY),U(FtSFX,x4,4,6),P(FtSFX,x1C,28,AT_SFX_ARRAY),P(FtSFX,x20,32,AT_SFX_ARRAY),U(FtSFX,x24,36,5));
S(sfxarray,FtSFXArr,8,U(FtSFXArr,num,0,1),P(FtSFXArr,sfx_ids,4,AT_WORDS));
S(fighterik,ftData_x58_t,28,B(ftData_x58_t,x0,0,4),U(ftData_x58_t,x4,4,1),B(ftData_x58_t,x8,8,4),U(ftData_x58_t,xC,12,1),B(ftData_x58_t,x10,16,8),U(ftData_x58_t,x18,24,1));
S(figatree,FigaTree,20,U(FigaTree,type,0,3),P(FigaTree,nodes,12,AT_RAW),P(FigaTree,tracks,16,AT_FIGATRACKS));
S(figatracks,FigaTrack,12,H(FigaTrack,length,0,2),B(FigaTrack,obj_type,4,3),P(FigaTrack,ad_head,8,AT_RAW));
S(dynamicsdesc,DynamicsDesc,20,P(DynamicsDesc,data,0,AT_WORDS),U(DynamicsDesc,count,4,4));
S(stageice,struct grIceMt_YakumonoParam,208,H(struct grIceMt_YakumonoParam,x0,0,3),U(struct grIceMt_YakumonoParam,x8,8,11),H(struct grIceMt_YakumonoParam,x34,52,4),U(struct grIceMt_YakumonoParam,x3C,60,23),H(struct grIceMt_YakumonoParam,ft_max_y,152,2),U(struct grIceMt_YakumonoParam,x9C,156,2),H(struct grIceMt_YakumonoParam,xA4,164,3),P(struct grIceMt_YakumonoParam,field_ixs,172,AT_HALVES),P(struct grIceMt_YakumonoParam,xB0,176,AT_HALVES),P(struct grIceMt_YakumonoParam,xB4,180,AT_HALVES),H(struct grIceMt_YakumonoParam,xB8,184,2),H(struct grIceMt_YakumonoParam,xBC.kind,188,1),B(struct grIceMt_YakumonoParam,xBC.x2,190,2),U(struct grIceMt_YakumonoParam,xC0,192,4));
S(grounditem,struct GroundItemData,8,U(struct GroundItemData,unk0,0,1),P(struct GroundItemData,unk4,4,AT_ARTICLE));
S(article,Article,24,P(Article,x0_common_attr,0,AT_ITEM_ATTR),P(Article,x4_specialAttributes,4,AT_ITEM_NUMBERS),P(Article,x8_hurtbones,8,AT_ITEM_HURT),P(Article,xC_itemStates,12,AT_ITEM_STATES),P(Article,x10_modelDesc,16,AT_ITEM_MODEL),P(Article,x14_dynamics,20,AT_ITEM_DYNAMICS));
S(itemcommon,ItemCommonData,0x160,U(ItemCommonData,x0,0,18),B(ItemCommonData,x48_byte,0x48,4),U(ItemCommonData,x4C_float,0x4C,38),B(ItemCommonData,filler_1a,0xE4,4),U(ItemCommonData,xE8,0xE8,1),B(ItemCommonData,filler_1a_2,0xEC,4),U(ItemCommonData,xF0,0xF0,28));
S(itemattr,ItemAttr,0x84,B(ItemAttr,x3,2,1),U(ItemAttr,x4_throw_speed_mul,4,32));
S(itemhurt,ItHurtBoneList,8,U(ItHurtBoneList,count,0,1),P(ItHurtBoneList,descs,4,AT_WORDS));
S(itemstates,ItemStateDesc,16,P(ItemStateDesc,x0_anim_joint,0,AT_ANIM),P(ItemStateDesc,x4_matanim_joint,4,AT_MATANIMJOINT),P(ItemStateDesc,x8_parameters,8,AT_SHAPEJOINT),P(ItemStateDesc,xC_script,12,AT_SCRIPT));
S(itemmodel,ItemModelDesc,16,P(ItemModelDesc,x0_joint,0,AT_JOINT),U(ItemModelDesc,x4_bone_count,4,2),B(ItemModelDesc,xC_bit_field,12,1));
S(itemdynamics,ItemDynamics,16,U(ItemDynamics,count,0,1),P(ItemDynamics,dyn_descs,4,AT_BONE_DYNAMICS),U(ItemDynamics,collision_count,8,1),P(ItemDynamics,collision_descs,12,AT_WORDS));
// Serialized dynamics inputs are arrays of 60-byte numeric coefficient records,
// not the runtime DynamicsData linked nodes created by lb_8000FD48.
S(bonedynamics,BoneDynamicsDesc,24,U(BoneDynamicsDesc,bone_id,0,1),P(BoneDynamicsDesc,dyn_desc.data,4,AT_WORDS),U(BoneDynamicsDesc,dyn_desc.count,8,4));
S(itemsword,itSword_UnkArticle1,48,U(itSword_UnkArticle1,x0,0,9),B(itSword_UnkArticle1,x1C.x8,36,9));
S(itemfoods,itFoodsAttributes,16,U(itFoodsAttributes,x0,0,1),P(itFoodsAttributes,x4,4,AT_JOINT),U(itFoodsAttributes,x8,8,2));
S(itemkinoko,KinokoAttrs,16,U(KinokoAttrs,x0,0,2),P(KinokoAttrs,anims[0],8,AT_ANIM),P(KinokoAttrs,anims[1],12,AT_ANIM));
typedef struct NativeKuriAttrs { s32* weights; float values[4]; } NativeKuriAttrs;
S(itemkuri,NativeKuriAttrs,20,P(NativeKuriAttrs,weights,0,AT_WORDS),U(NativeKuriAttrs,values,4,4));
S(itemleadead,itLeadeadAttributes,32,P(itLeadeadAttributes,x0,0,AT_WORDS),U(itLeadeadAttributes,x4,4,5),H(itLeadeadAttributes,x18,24,3),B(itLeadeadAttributes,x1E,30,1));
S(itemocta,itOctarockAttributes,32,P(itOctarockAttributes,x0,0,AT_WORDS),U(itOctarockAttributes,x4,4,6),H(itOctarockAttributes,x1C,28,1));
S(itemotto,itOldottoseaAttributes,44,P(itOldottoseaAttributes,x0,0,AT_WORDS),U(itOldottoseaAttributes,x4,4,3),B(itOldottoseaAttributes,x10,16,4),U(itOldottoseaAttributes,x14,20,5),B(itOldottoseaAttributes,x28,40,1));
#define WSTAR_ENTRY(i) P(itWstarAttributes,x28_entries[i].x0_anim_joint,40+(i)*8,AT_ANIM),U(itWstarAttributes,x28_entries[i].x4_sfx,44+(i)*8,1)
S(itemwstar,itWstarAttributes,96,U(itWstarAttributes,x0,0,10),WSTAR_ENTRY(0),WSTAR_ENTRY(1),WSTAR_ENTRY(2),WSTAR_ENTRY(3),WSTAR_ENTRY(4),WSTAR_ENTRY(5),WSTAR_ENTRY(6));
#define TIER_ENTRY(i) P(it_2E5A_Attrs,tiers[i].joint,60+(i)*44,AT_JOINT),P(it_2E5A_Attrs,tiers[i].anim_joint,64+(i)*44,AT_ANIM),P(it_2E5A_Attrs,tiers[i].matanim_joint,68+(i)*44,AT_MATANIMJOINT),P(it_2E5A_Attrs,tiers[i].shape_anim_joint,72+(i)*44,AT_SHAPEJOINT),U(it_2E5A_Attrs,tiers[i].xD84_value,76+(i)*44,7)
S(itemtiers,it_2E5A_Attrs,192,U(it_2E5A_Attrs,x0,0,15),TIER_ENTRY(0),TIER_ENTRY(1),TIER_ENTRY(2));
S(itemunknown,itUnknownAttributes,140,U(itUnknownAttributes,x0,0,9),P(itUnknownAttributes,x24[0],36,AT_JOINT),P(itUnknownAttributes,x24[1],40,AT_JOINT),P(itUnknownAttributes,x24[2],44,AT_JOINT),P(itUnknownAttributes,x24[3],48,AT_JOINT),P(itUnknownAttributes,x24[4],52,AT_JOINT),P(itUnknownAttributes,x24[5],56,AT_JOINT),P(itUnknownAttributes,x24[6],60,AT_JOINT),P(itUnknownAttributes,x24[7],64,AT_JOINT),P(itUnknownAttributes,x24[8],68,AT_JOINT),P(itUnknownAttributes,x24[9],72,AT_JOINT),P(itUnknownAttributes,x24[10],76,AT_JOINT),P(itUnknownAttributes,x24[11],80,AT_JOINT),P(itUnknownAttributes,x24[12],84,AT_JOINT),P(itUnknownAttributes,x24[13],88,AT_JOINT),P(itUnknownAttributes,x24[14],92,AT_JOINT),P(itUnknownAttributes,x24[15],96,AT_JOINT),P(itUnknownAttributes,x24[16],100,AT_JOINT),P(itUnknownAttributes,x24[17],104,AT_JOINT),P(itUnknownAttributes,x24[18],108,AT_JOINT),P(itUnknownAttributes,x24[19],112,AT_JOINT),P(itUnknownAttributes,x24[20],116,AT_JOINT),P(itUnknownAttributes,x24[21],120,AT_JOINT),P(itUnknownAttributes,x24[22],124,AT_JOINT),P(itUnknownAttributes,x24[23],128,AT_JOINT),P(itUnknownAttributes,x24[24],132,AT_JOINT),P(itUnknownAttributes,x24[25],136,AT_JOINT));
S(itemwhitebea,itWhiteBeaAttributes,24,P(itWhiteBeaAttributes,x0,0,AT_WORDS),U(itWhiteBeaAttributes,x4,4,1),H(itWhiteBeaAttributes,x8,8,4),U(itWhiteBeaAttributes,x10,16,1),H(itWhiteBeaAttributes,x14,20,1));
S(itemtincle,itTincleAttributes,88,P(itTincleAttributes,x0,0,AT_WORDS),U(itTincleAttributes,x4,4,20),B(itTincleAttributes,x54,84,2));
S(itemapple,itWhispyAppleAttributes,28,P(itWhispyAppleAttributes,x0,0,AT_WORDS),U(itWhispyAppleAttributes,x4,4,2),B(itWhispyAppleAttributes,xC,12,8),U(itWhispyAppleAttributes,x14,20,2));
unsigned MeleeNativeItemSpecialType(unsigned kind) {
    switch(kind) {
        case It_Kind_Mato:case It_Kind_Heiho:case It_Kind_Klap:
    case It_Kind_Arwing_Laser:case It_Kind_Kyasarin:case It_Kind_Kyasarin_Egg:
        return AT_ITEM_POINTER_WORDS;
    case It_Kind_Whitebea:return AT_ITEM_WHITEBEA;
    case It_Kind_Tincle:return AT_ITEM_TINCLE;
    case It_Kind_WhispyApple:case It_Kind_WhispyHealApple:return AT_ITEM_APPLE;
    case It_Kind_Sword:return AT_ITEM_SWORD;
    case It_Kind_Foods:return AT_ITEM_FOODS;
    case It_Kind_Kinoko:case It_Kind_DKinoko:return AT_ITEM_KINOKO;
    case It_Kind_WStar:return AT_ITEM_WSTAR;
    case It_Kind_Kuriboh:return AT_ITEM_KURI;
    case It_Kind_Leadead:return AT_ITEM_LEADEAD;
    case It_Kind_Octarock:return AT_ITEM_OCTA;
    case It_Kind_Ottosea:return AT_ITEM_OTTO;
    case It_Kind_Unk4:return AT_ITEM_TIERS;
    case It_PKind_Unknown:case It_Kind_Unknown_Swarm:return AT_ITEM_UNKNOWN;
    default:return AT_ITEM_NUMBERS;
    }
}
void MeleeNativeItemFlags(void* data,unsigned first,unsigned second) {
    ItemAttr* attr=data;
    attr->x0_is_heavy=first>>7;attr->x0_78=(first>>3)&15;attr->x0_hold_kind=first&7;
    attr->x1_1=second>>6;attr->x1_3=(second>>5)&1;attr->x1_4=(second>>4)&1;
    attr->x1_5=(second>>3)&1;attr->x1_67_cam_kind=(second>>1)&3;attr->x1_8=second&1;
}
S(fog,HSD_FogDesc,20,U(HSD_FogDesc,type,0,1),P(HSD_FogDesc,fogadjdesc,4,AT_FOGADJ),U(HSD_FogDesc,start,8,2),B(HSD_FogDesc,color,16,4));
S(fogadj,HSD_FogAdjDesc,68,H(HSD_FogAdjDesc,center,0,2),U(HSD_FogAdjDesc,mtx,4,16));
S(sobj,HSD_SObjDesc,8,P(HSD_SObjDesc,image,0,AT_IMAGE),P(HSD_SObjDesc,tlut,4,AT_TLUT));
typedef struct { unsigned char count; float* values; } NativeRefractionData;
S(refract,NativeRefractionData,8,B(NativeRefractionData,count,0,1),P(NativeRefractionData,values,4,AT_WORDS));
S(event,struct gm_804D6900_t,40,B(struct gm_804D6900_t,kind,0,4),P(struct gm_804D6900_t,x4,4,AT_EVENT_EXTRA),P(struct gm_804D6900_t,x8,8,AT_EVENT_INIT),P(struct gm_804D6900_t,xC,12,AT_EVENT_BONUS),P(struct gm_804D6900_t,x10,16,AT_EVENT_STAGE),
P(struct gm_804D6900_t,player_init[0],20,AT_EVENT_PLAYER),P(struct gm_804D6900_t,player_init[1],24,AT_EVENT_PLAYER),P(struct gm_804D6900_t,player_init[2],28,AT_EVENT_PLAYER),P(struct gm_804D6900_t,player_init[3],32,AT_EVENT_PLAYER),P(struct gm_804D6900_t,player_init[4],36,AT_EVENT_PLAYER));
S(eventinit,struct gm_evinit,40,B(struct gm_evinit,unk2,2,4),H(struct gm_evinit,unk6,6,1),U(struct gm_evinit,unk8,8,1),B(struct gm_evinit,padC,12,4),U(struct gm_evinit,x18,24,4));
S(eventbonus,struct gm_evbonus,24,B(struct gm_evbonus,c_kind,0,8),U(struct gm_evbonus,x8,8,3),B(struct gm_evbonus,flags,20,4));
S(eventplayer,gm_801BAB40_src,28,B(gm_801BAB40_src,c_kind,0,12),H(gm_801BAB40_src,x12,12,2),U(gm_801BAB40_src,x18,16,3));
S(eventstage,struct gm_evstage_table,36,B(struct gm_evstage_table,count,0,2),H(struct gm_evstage_table,stage,2,7),P(struct gm_evstage_table,entries[0],16,AT_EVENT_PLAYER),P(struct gm_evstage_table,entries[1],20,AT_EVENT_PLAYER),P(struct gm_evstage_table,entries[2],24,AT_EVENT_PLAYER),P(struct gm_evstage_table,entries[3],28,AT_EVENT_PLAYER),P(struct gm_evstage_table,entries[4],32,AT_EVENT_PLAYER));
S(eventtiming,struct gm_804D6900_x4_t,8,U(struct gm_804D6900_x4_t,x0,0,1),U(struct gm_804D6900_x4_t,x4,4,1));
S(eventextra,struct gm_804D6900_x4_t,8,U(struct gm_804D6900_x4_t,x0,0,1),P(struct gm_804D6900_x4_t,x4,4,AT_EVENT_PLAYER));
void MeleeNativeEventFlags(void* data,unsigned flags,unsigned hi,unsigned lo) {
    struct gm_evinit* p=data;
    p->x0_0=(flags>>13)&7;p->x0_3=(flags>>10)&7;p->x0_6=(flags>>9)&1;p->x0_7=(flags>>8)&1;
    p->x1_0=(flags>>7)&1;p->x1_1=(flags>>6)&1;p->x1_2=(flags>>5)&1;p->x1_3=(flags>>4)&1;p->x1_4=(flags>>3)&1;p->x1_5=flags&7;
    p->x10=((u64)hi<<32)|lo;
}
const MeleeAssetSchema* MeleeNativeAssetSchema(unsigned type) {
    switch(type) {
#define CASE(k,v) case k:return &v
    CASE(AT_FIGHTER,fighter); CASE(AT_FIGHTER_ATTR,fighterattr); CASE(AT_FIGHTER_PARTS,fighterparts);
    CASE(AT_ITEM_CLIMBER_STRING,itemstring);
    CASE(AT_ITEM_BOOMERANG,itemboomerang);
    CASE(AT_ITEM_HOOKSHOT,itemhookshot);
    CASE(AT_ITEM_ARROW,itemarrow);
    CASE(AT_ITEM_GRAPPLE,itemgrapple);
    CASE(AT_ITEM_CHAIN,itemchain);
    CASE(AT_ITEM_YOYO,itemyoyo);
    CASE(AT_ITEM_GW,itemgw); CASE(AT_ITEM_DRAW_PARTS,itemdrawparts);
    CASE(AT_ITEM_CHEF,itemchef); CASE(AT_ITEM_SWORD,itemsword);
    CASE(AT_KIRBY_COPY_FOX,kirbycopyfox); CASE(AT_KIRBY_COPY_YOSHI,kirbycopyyoshi); CASE(AT_GAMEWATCH_ATTR,gamewatchattr);
    CASE(AT_PURIN_PARTS,purinparts);
    CASE(AT_SAMUS_BEAM,samusbeam);
    CASE(AT_FIGHTER_VIS,fightervis); CASE(AT_PART_VIS,partvis); CASE(AT_FIGHTER_ANIMS,fighteranims);
    CASE(AT_FIGHTER_PART_ANIM,fighterpartanim); CASE(AT_FIGHTER_GUARD,fighterguard); CASE(AT_FIGHTER_DYNAMICS,fighterdynamics);
    CASE(AT_FIGHTER_HURT,fighterhurt); CASE(AT_FIGHTER_LEDGE,fighterledge); CASE(AT_FIGHTER_SFX,fightersfx);
    CASE(AT_SFX_ARRAY,sfxarray); CASE(AT_FIGHTER_IK,fighterik); CASE(AT_FIGATREE,figatree); CASE(AT_FIGATRACKS,figatracks);
    CASE(AT_ROBJ_ANIM,robjanim); CASE(AT_ROBJ,robj); CASE(AT_RVALUES,rvalues); CASE(AT_EXPRESSION,expression); CASE(AT_BYTE_EXPRESSION,byteexpression);
    CASE(AT_ITEM_WHITEBEA,itemwhitebea); CASE(AT_ITEM_TINCLE,itemtincle); CASE(AT_ITEM_APPLE,itemapple); CASE(AT_DYNAMICS_DESC,dynamicsdesc); CASE(AT_STAGE_ICE,stageice); CASE(AT_GROUND_ITEM,grounditem); CASE(AT_ITEM_PUBLIC,itempublic); CASE(AT_ARTICLE,article); CASE(AT_ITEM_COMMON,itemcommon);
    CASE(AT_ITEM_ATTR,itemattr); CASE(AT_ITEM_HURT,itemhurt); CASE(AT_ITEM_STATES,itemstates);
    CASE(AT_ITEM_MODEL,itemmodel); CASE(AT_ITEM_DYNAMICS,itemdynamics); CASE(AT_BONE_DYNAMICS,bonedynamics);
    CASE(AT_ITEM_FOODS,itemfoods); CASE(AT_ITEM_KINOKO,itemkinoko); CASE(AT_ITEM_WSTAR,itemwstar);
    CASE(AT_ITEM_KURI,itemkuri); CASE(AT_ITEM_LEADEAD,itemleadead); CASE(AT_ITEM_OCTA,itemocta);
    CASE(AT_ITEM_OTTO,itemotto); CASE(AT_ITEM_TIERS,itemtiers); CASE(AT_ITEM_UNKNOWN,itemunknown);
    CASE(AT_EVENT,event); CASE(AT_EVENT_INIT,eventinit); CASE(AT_EVENT_PLAYER,eventplayer); CASE(AT_EVENT_BONUS,eventbonus); CASE(AT_EVENT_STAGE,eventstage); CASE(AT_EVENT_EXTRA,eventextra); CASE(AT_EVENT_TIMING,eventtiming); CASE(AT_REFRACT,refract); CASE(AT_TROPHIES,trophies); CASE(AT_TROPHY_DISPLAY,trophy_display);
    CASE(AT_SHAPEJOINT,shapejoint); CASE(AT_SHAPEDOBJ,shapedobj); CASE(AT_SHAPEANIM,shapeanim);
    CASE(AT_SHAPESET,shapeset); CASE(AT_CPU_VERTICES,vertex);
    CASE(AT_EFFECTDESC,effectdesc);
    CASE(AT_SPLINE,spline); CASE(AT_PTCL_LIST,ptcllist);
    CASE(AT_PLAYER_COMMON,playercommon); CASE(AT_FT_COMMON,ftcommon);
    CASE(AT_FT_PARTS,ftparts); CASE(AT_BYTEPAIR,bytepair); CASE(AT_SHAKE,shake);
    CASE(AT_COLOR_DESC,colordesc); CASE(AT_CPU_CONFIG,cpuconfig); CASE(AT_JOINT_ANIM_PAIR,jointanimpair);
    CASE(AT_MAP_HEAD,maphead); CASE(AT_MAP_MODEL,mapmodel); CASE(AT_MAP_JOINT_PAIRS,mapjointpairs);
    CASE(AT_LIGHT_OVERRIDE,lightoverride); CASE(AT_SHADOW_ENTRY,shadowentry); CASE(AT_COLL_MAP,collmap);
    CASE(AT_MAP_JOINT,mapjoint); CASE(AT_GROUND_PARAM,groundparam); CASE(AT_STAGE_PARAM,stageparam);
    CASE(AT_FOG,fog); CASE(AT_FOGADJ,fogadj); CASE(AT_SOBJ,sobj);
    CASE(AT_SCENE,scene); CASE(AT_SCENE_FOG,scene_fog); CASE(AT_MODEL,model); CASE(AT_CAMERAS,camera_list); CASE(AT_CAMERA,camera);
    CASE(AT_LIGHT_LIST,light_list); CASE(AT_LIGHT,light); CASE(AT_WOBJ,wobj); CASE(AT_JOINT,joint);
    CASE(AT_DOBJ,dobj); CASE(AT_MOBJ,mobj); CASE(AT_MATERIAL,material); CASE(AT_PE,pe); CASE(AT_TOBJ,tobj);
    CASE(AT_IMAGE,image); CASE(AT_TLUT,tlut); CASE(AT_LOD,lod); CASE(AT_TEV,tev); CASE(AT_POBJ,pobj);
    CASE(AT_VERTICES,vertex); CASE(AT_ENVELOPE,envelope); CASE(AT_ANIM,anim); CASE(AT_AOBJ,aobj); CASE(AT_FOBJ,fobj);
    CASE(AT_MATANIMJOINT,matjoint); CASE(AT_MATANIM,matanim); CASE(AT_TEXANIM,texanim);
    CASE(AT_CAMERA_ANIM,camanim); CASE(AT_WOBJ_ANIM,wobjanim); CASE(AT_LIGHT_ANIM,lightanim);
    default:return NULL;
    }
}
