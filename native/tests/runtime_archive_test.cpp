#include "asset_archive.hpp"
#include "include/melee_archive.h"
#include <fstream>
#include <vector>
#include <map>
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" unsigned MeleeCheckScene(void*);
extern "C" void MeleeCheckGamewatchColors(void*,const void*);
extern "C" unsigned MeleeCheckEffects(void*);
extern "C" unsigned MeleeCheckItems(void*);
extern "C" void MeleeCheckStageFlags(void);
extern "C" void* MeleeNativeAnimationAt(const void*,unsigned);
static void put(std::vector<std::byte>& data,size_t at,unsigned value) {
    for(int i=3;i>=0;--i) {data[at+i]=std::byte(value&255);value>>=8;}
}
static void checkExternalBindings() {
    const char names[]="TitleMark_sobjdesc\0shared\0";
    std::vector<std::byte> bytes(32+8+8+8+sizeof(names));
    put(bytes,0,bytes.size());put(bytes,4,8);put(bytes,12,1);put(bytes,16,1);
    put(bytes,32,4);put(bytes,36,0xFFFFFFFF);
    put(bytes,48,0);put(bytes,52,sizeof("TitleMark_sobjdesc"));
    std::memcpy(bytes.data()+56,names,sizeof(names));
    int key,value;
    assert(MeleeNativeArchiveCreate(&key,bytes.data(),bytes.size())==0);
    MeleeNativeArchiveExtern(&key,"shared",&value);
    auto root=static_cast<void**>(MeleeNativeArchivePublic(&key,"TitleMark_sobjdesc"));
    assert(root[0]==&value&&root[1]==&value);
    MeleeNativeArchiveExtern(&key,"shared",nullptr);
    assert(!root[0]&&!root[1]);
    MeleeNativeArchiveExtern(&key,"shared",&key);
    assert(root[0]==&key&&root[1]==&key);
    MeleeNativeArchiveRelease(bytes.data());
}
static void checkScriptAddressSpace() {
    const char name[]="ALDYakuAll";
    std::vector<std::byte> bytes(32+20+8+8+sizeof(name));
    put(bytes,0,bytes.size());put(bytes,4,20);put(bytes,8,2);put(bytes,12,1);
    put(bytes,32,4);put(bytes,36,0x1C000000);put(bytes,40,16);
    put(bytes,44,0x04000003);put(bytes,48,0x04000007);
    put(bytes,52,0);put(bytes,56,8);
    std::memcpy(bytes.data()+68,name,sizeof(name));
    int key;
    assert(MeleeNativeArchiveCreate(&key,bytes.data(),bytes.size())==0);
    auto root=static_cast<unsigned**>(MeleeNativeArchivePublic(&key,name));
    auto script=root[0];
    assert(script[0]==0x1C000000&&script[2]==0x04000003);
    assert(MeleeNativeScriptPointer(script+1)==script+3);
    assert(script[3]==0x04000007);
    auto second=bytes;put(second,48,0x04000009);
    assert(MeleeNativeArchiveCreate(&key,second.data(),second.size())==0);
    auto other=static_cast<unsigned**>(MeleeNativeArchivePublic(&key,name))[0];
    assert(other!=script&&other[3]==0x04000009);
    assert(script[3]==0x04000007&&MeleeNativeScriptPointer(script+1)==script+3);
    MeleeNativeArchiveRelease(second.data());
    assert(script[3]==0x04000007);
    MeleeNativeArchiveRelease(bytes.data());
}
static void checkAnimationIndexing() {
    const char names[]="test_animjoint\0second_animjoint\0";
    std::vector<std::byte> bytes(32+40+4+16+sizeof(names));
    put(bytes,0,bytes.size());put(bytes,4,40);put(bytes,8,1);put(bytes,12,2);
    put(bytes,32,20); // First node links to the second serialized record.
    put(bytes,72,0);put(bytes,76,0);put(bytes,80,0);
    put(bytes,84,20);put(bytes,88,sizeof("test_animjoint"));
    std::memcpy(bytes.data()+92,names,sizeof(names));
    int key;
    assert(MeleeNativeArchiveCreate(&key,bytes.data(),bytes.size())==0);
    auto first=MeleeNativeArchivePublic(&key,"test_animjoint");
    auto second=MeleeNativeArchivePublic(&key,"second_animjoint");
    assert(first && second && first!=second);
    assert(MeleeNativeAnimationAt(first,0)==first);
    assert(MeleeNativeAnimationAt(first,1)==second);
    MeleeNativeArchiveRelease(bytes.data());
}
static std::map<const void*,size_t> registered;
extern "C" void MeleeNativeRegisterVertexBuffer(const void* data,size_t size,int little) {
    assert(data&&size&&!little); assert(registered.emplace(data,size).second);
}
extern "C" void MeleeNativeUnregisterVertexBuffer(const void* data) { assert(registered.erase(data)==1); }
int main(int argc,char** argv) {
    checkExternalBindings();checkScriptAddressSpace();checkAnimationIndexing();MeleeCheckStageFlags();
    int key;
    unsigned char malformed[32]={0}; assert(MeleeNativeArchiveCreate(&key,malformed,sizeof(malformed))==-1);
    bool stage_core=false, animation_bundle=false;
    for(int i=1;i<argc;++i) {
        if(std::strcmp(argv[i],"--animation-bundle")==0) {animation_bundle=true;continue;}
        if(std::strcmp(argv[i],"--stage-core")==0) {stage_core=true;continue;}
        std::fprintf(stderr,"[archive-test] %s\n",argv[i]);
        std::ifstream file(argv[i],std::ios::binary|std::ios::ate); assert(file);
        auto size=file.tellg(); std::vector<std::byte> bytes(size); file.seekg(0);file.read((char*)bytes.data(),size);assert(file);
        if(animation_bundle) {
            unsigned count=0;
            for(size_t at=0;at<bytes.size();) {
                assert(bytes.size()-at>=32);
                unsigned length=0;
                for(unsigned j=0;j<4;++j) length=(length<<8)|std::to_integer<unsigned>(bytes[at+j]);
                assert(length>=32&&length<=bytes.size()-at);
                std::vector<std::byte> chunk(bytes.begin()+at,bytes.begin()+at+length);
                melee::AssetArchive animation(chunk);
                assert(MeleeNativeArchiveCreate(&key,chunk.data(),chunk.size())==0);
                unsigned trees=0;
                for(const auto& [name,offset]:animation.roots()) {
                    if(name.ends_with("_figatree")) {
                        assert(MeleeNativeArchivePublic(&key,name.c_str()));
                        ++trees;
                    }
                }
                assert(trees>0);
                MeleeNativeArchiveRelease(chunk.data());assert(registered.empty());
                ++count;at=(at+length+31)&~size_t(31);
            }
            std::printf("%s: %u animation archives materialized\n",argv[i],count);
            continue;
        }
        melee::AssetArchive archive(bytes); assert(MeleeNativeArchiveCreate(&key,bytes.data(),bytes.size())==0);
        for(const auto& [name,offset]:archive.roots()) {
            if(stage_core&&name!="map_head"&&name!="map_ptcl"&&name!="map_texg"&&name!="coll_data"&&name!="grGroundParam"&&name!="map_plit"&&name!="quake_model_set") continue;
            if(name.ends_with("_image_desc")||name.ends_with("_tlut_desc")||name.starts_with("dynamicsdata_")||name=="lbBgFlashColAnimData"||name.ends_with("_scene_models")||name=="Stc_rarwmdls"||name=="Stc_scemdls"||name=="lupe"||name=="tdsce"||(name.ends_with("_scene_data")||name=="pnlsce"||name=="flmsce")||name=="sqEventInitDataLevelTbl"||name=="lbAudioLoadData"||name=="MnSelectChrDataTable"||name=="MnSelectStageDataTable"||name=="ftLoadCommonData"||name=="plLoadCommonData"||name=="map_head"||name=="map_ptcl"||name=="map_texg"||name=="coll_data"||name=="grGroundParam"||name=="map_plit"||name=="quake_model_set"||name.starts_with("ftData")||name.ends_with("_figatree")||name=="itPublicData"||name=="ALDYakuAll"||name=="itemdata"||name=="yakumono_param"||(name.starts_with("eff")&&name.ends_with("DataTable"))||name=="lbRefData"||name=="lbRumbleData"||name=="MemSnapIconData"||name=="MemCardIconData"||name.starts_with("SIS_")||name.starts_with("ty")||name.starts_with("MenMain")||name.starts_with("ScMenMain")||name.starts_with("Ttl")||name.starts_with("ScTitle")||name=="TitleMark_sobjdesc") {
                void* root=MeleeNativeArchivePublic(&key,name.c_str());assert(root);
                assert(MeleeNativeArchivePublic(&key,name.c_str())==root);
                if(name=="ftDataGamewatch") MeleeCheckGamewatchColors(root,archive.bytes(*archive.pointer(offset+4)+4,20).data());
                if(name=="itPublicData") std::printf("%s: %u articles checked with game C layouts\n",argv[i],MeleeCheckItems(root));
                if(name.starts_with("eff")&&name.ends_with("DataTable")) std::printf("%s: %u particle entries checked with game C layouts\n",argv[i],MeleeCheckEffects(root));
                if((name.ends_with("_scene_data")||name=="pnlsce"||name=="flmsce")) std::printf("%s: %u joints traversed by game C layouts\n",argv[i],MeleeCheckScene(root));
            }
        }
        MeleeNativeArchiveRelease(bytes.data()); assert(registered.empty());
    }
    std::puts("PASS: archive conversion, C scene layouts, root identity and release");
}
