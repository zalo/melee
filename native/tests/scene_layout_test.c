#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <melee_particle.h>
#include <sysdolphin/baselib/psstructs.h>
#include <stddef.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itCommonItems.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftGameWatch/types.h>
#include <string.h>
#include <melee/it/kinds/itkinoko.h>
_Static_assert(offsetof(HSD_PSTexGroup,texTable)==24,"Particle texture ABI");
_Static_assert(offsetof(HSD_PSCmdList,cmdList)==60,"Particle bytecode ABI");
static unsigned joints;
static void visit(HSD_Joint* joint) {
    if(!joint) return;
    if(++joints>4096 || !isfinite(joint->position.x) || !isfinite(joint->scale.x)) abort();
    for(HSD_DObjDesc* object=joint->u.dobjdesc;object;object=object->next) {
        if(!object->mobjdesc || !object->pobjdesc || !isfinite(object->mobjdesc->mat->alpha)) abort();
        for(HSD_PObjDesc* polygon=object->pobjdesc;polygon;polygon=polygon->next) {
            if(!polygon->display || !polygon->verts) abort();
            unsigned i=0; while(polygon->verts[i].attr!=GX_VA_NULL) {if(++i>32) abort();}
        }
    }
    visit(joint->child); visit(joint->next);
}
unsigned MeleeCheckScene(void* root) {
    SceneDesc* scene=root;
    if(!scene->models || !scene->models[0] || !scene->cameras || !scene->cameras[0].desc) abort();
    HSD_CameraDescCommon* camera=&scene->cameras[0].desc->common;
    if(!camera->eyepos || !camera->interest || camera->ffar<=camera->nnear) abort();
    joints=0; for(unsigned i=0;scene->models[i];++i) {if(i>100) abort();visit(scene->models[i]->joint);}
    return joints;
}
unsigned MeleeCheckEffects(void* root) {
    MeleeNativeParticleBank** banks=root;
    unsigned checked=0;
    for(unsigned i=0;i<2;++i) {
        MeleeNativeParticleBank* bank=banks[i];
        if(!bank) continue;
        if(bank->magic!=MELEE_PARTICLE_BANK_MAGIC||bank->count>65536||!bank->entries) abort();
        for(unsigned j=0;j<bank->count;++j) {
            if(!bank->entries[j]) continue;
            if(i==0) {
                HSD_PSCmdList* cmd=bank->entries[j];
                if((cmd->kind&0x0E000000U)!=0x08000000U||!isfinite(cmd->size)||!isfinite(cmd->grav)) abort();
            } else {
                HSD_PSTexGroup* group=bank->entries[j];
                if(group->num>65536||group->width>1024||group->height>1024) abort();
                for(unsigned k=0;k<group->num;++k) if(!group->texTable[k]) abort();
            }
            ++checked;
        }
    }
    return checked;
}
unsigned MeleeCheckItems(void* root) {
    it_804D6D20_t* data=root;
    if(!data->x0||!data->x4||!data->x8||!data->xC||!data->x14) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
    Article** tables[]={data->x4,data->x8,data->xC};
    unsigned counts[]={43,118,47},checked=0;
    for(unsigned t=0;t<3;++t) for(unsigned i=0;i<counts[t];++i) {
        Article* article=tables[t][i];
        if(!article) continue;
        if(!article->x10_modelDesc) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
        ItemAttr* attr=article->x0_common_attr;
        if(attr&&(!isfinite(attr->x60_scale)||attr->x60_scale<=0)) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
        if(article->x8_hurtbones&&article->x8_hurtbones->count>0&&!article->x8_hurtbones->descs) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
        ItemDynamics* dynamics=article->x14_dynamics;
        if(dynamics) {
            if(dynamics->collision_count<0 || dynamics->collision_count>2 ||
               (dynamics->collision_count && !dynamics->collision_descs)) abort();
            for(int j=0;j<dynamics->collision_count;++j) {
                ItemCollisionDesc* desc=&dynamics->collision_descs[j];
                if(desc->bone_id<0 || !isfinite(desc->offset.x) ||
                   !isfinite(desc->offset.y) || !isfinite(desc->offset.z) ||
                   !isfinite(desc->size) || desc->size<0) abort();
            }
        }
        if(dynamics) for(int j=0;j<dynamics->count;++j) {
            DynamicsDesc* desc=&dynamics->dyn_descs[j].dyn_desc;
            if(!desc->data||!desc->count||!isfinite(desc->pos.x)) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
        }
        ++checked;
    }
    if(!data->x4[It_Kind_Box]->x0_common_attr->x0_is_heavy) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
    KinokoAttrs* kinoko=data->x4[It_Kind_Kinoko]->x4_specialAttributes;
    if(!kinoko->anims[0]||!kinoko->anims[1]||!isfinite(kinoko->x0)) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
    itFoodsAttributes* foods=data->x4[It_Kind_Foods]->x4_specialAttributes;
    if(foods->x0!=28||!foods[27].x4) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
    itWstarAttributes* star=data->x4[It_Kind_WStar]->x4_specialAttributes;
    if(star->x24_count!=7||!star->x28_entries[6].x0_anim_joint) {fprintf(stderr,"Item layout check failed at line %d\n",__LINE__);abort();}
    return checked;
}

void MeleeCheckGamewatchColors(void* root, const void* bytes) {
    ftGameWatchAttributes* attrs=((ftData*) root)->ext_attr;
    if(memcmp(attrs->x4_GAMEWATCH_COLOR,bytes,20)!=0) abort();
    if(!isfinite(attrs->x0_GAMEWATCH_WIDTH)||attrs->x0_GAMEWATCH_WIDTH<=0) abort();
}

#include <melee/gr/types.h>
void MeleeCheckStageFlags(void) {
    StageCallbacks callbacks = {0};
    callbacks.flags = 0xC0000000U;
    if(!callbacks.flags_b0 || !callbacks.flags_b1 || callbacks.flags_b2) abort();
    callbacks.flags_b0 = 0;
    if(callbacks.flags != 0x40000000U) abort();
}
