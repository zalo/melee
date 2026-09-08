#include "asset_archive.hpp"
#include "asset_schema.h"
#include "stage_numeric_layouts.hpp"
#include "gx_bridge.h"
#include <algorithm>
#include <map>
#include "include/melee_archive.h"
#include "include/melee_particle.h"
#include <memory>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <mutex>

namespace {
struct RumbleEntry { void* commands; std::uint8_t priority, parameter; };
struct Archive {
    melee::AssetArchive source;
    const void* raw;
    std::vector<void*> allocations;
    std::unordered_map<std::string,void*> public_objects;
    std::map<std::pair<unsigned,std::uint32_t>,void*> objects;
    std::vector<std::uint32_t> boundaries;
    std::vector<void*> vertex_buffers;
    std::map<std::uint32_t,unsigned> event_extra_types;
    std::map<std::uint32_t,unsigned> item_special_types;
    std::unordered_map<const void*,void*> stage_pointers;
    std::unordered_map<const void*,std::pair<unsigned,unsigned>> animation_objects;
    std::string fighter_name;
    std::vector<std::uint32_t> pointer_fields;
    std::map<std::string,void*> external_bindings;
    std::map<std::string,std::vector<void*>> external_destinations;
    void bindExternal(const char* name,void* value) {
        external_bindings[name]=value;
        for(auto destination:external_destinations[name]) std::memcpy(destination,&value,sizeof(value));
    }
    bool locateExternal(std::uint32_t field,void* destination) {
        auto name=source.external(field);
        if(!name) return false;
        external_destinations[*name].push_back(destination);
        auto value=external_bindings[*name];
        std::memcpy(destination,&value,sizeof(value));
        return true;
    }
    std::byte* script_arena=nullptr;
    static std::uint32_t be32(const std::byte* p) { return std::to_integer<unsigned>(p[0])<<24 | std::to_integer<unsigned>(p[1])<<16 | std::to_integer<unsigned>(p[2])<<8 | std::to_integer<unsigned>(p[3]); }
    Archive(const void* data, size_t size):source(std::vector<std::byte>((const std::byte*)data,(const std::byte*)data+size)),raw(data) {
        const auto bytes=static_cast<const std::byte*>(data);
        boundaries={0,source.data_size()};
        for (std::uint32_t i=0;i<be32(bytes+8);++i) {
            auto field=be32(bytes+32+source.data_size()+i*4);
            pointer_fields.push_back(field);
            boundaries.push_back(source.u32(field));
        }
        for(const auto& [name,offset]:source.roots()) boundaries.push_back(offset);
        std::sort(boundaries.begin(),boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
    }
    ~Archive() { for(void* p:vertex_buffers) MeleeNativeUnregisterVertexBuffer(p); for (void* p:allocations) std::free(p); }
    void* allocate(size_t size) {
        void* p=nullptr;
        if (posix_memalign(&p,32,size?size:1)) throw std::bad_alloc();
        std::memset(p,0,size?size:1); allocations.push_back(p); return p;
    }
    void* rumble(std::uint32_t root) {
        auto bytes=source.data_size()-root;
        if (bytes%8) throw std::runtime_error("Invalid rumble root array size");
        auto count=bytes/8;
        auto entries=static_cast<RumbleEntry*>(allocate(count*sizeof(RumbleEntry)));
        for(unsigned i=0;i<count;++i) {
            auto at=root+i*8;
            auto pointer=source.pointer(at);
            const auto metadata=source.bytes(at+4,2);
            entries[i].priority=std::to_integer<unsigned char>(metadata[0]);
            entries[i].parameter=std::to_integer<unsigned char>(metadata[1]);
            if (!pointer) continue;
            if (*pointer>=root || *pointer%2) throw std::runtime_error("Invalid rumble stream offset");
            // Streams end in opcode zero. Keep branch/loop instructions as words.
            std::vector<std::uint16_t> commands;
            for(auto cursor=*pointer;;cursor+=2) {
                if (cursor>=root) throw std::runtime_error("Unterminated rumble stream");
                auto word=source.u16(cursor); commands.push_back(word);
                if ((word>>13)==0) break;
            }
            entries[i].commands=allocate(commands.size()*sizeof(std::uint16_t));
            std::memcpy(entries[i].commands,commands.data(),commands.size()*sizeof(std::uint16_t));
        }
        return entries;
    }
    size_t extent(std::uint32_t offset) {
        auto next=std::upper_bound(boundaries.begin(),boundaries.end(),offset);
        return next==boundaries.end()?0:*next-offset;
    }
    void* stageParameters(std::uint32_t offset) {
        if(source.roots().contains("GrdIcemtBridgeA_CMPR_image")) return materialize(AT_STAGE_ICE,offset);
        for(const auto& layout:stage_numeric_layouts) {
            if(!source.roots().contains(layout.marker)) continue;
            auto size=layout.minimum_size?layout.minimum_size:extent(offset);
            if(extent(offset)<size) throw std::runtime_error("Truncated stage parameter block");
            for(auto field:pointer_fields) if(field>=offset&&field<offset+size&&std::find(layout.scripts.begin(),layout.scripts.end(),field-offset)==layout.scripts.end()&&std::find(layout.data_pointers.begin(),layout.data_pointers.end(),field-offset)==layout.data_pointers.end())
                throw std::runtime_error("Stage parameters need a pointer schema");
            auto result=static_cast<std::byte*>(allocate(size));
            if(size%4) throw std::runtime_error("Unaligned stage parameter block");
            for(unsigned i=0;i<size;i+=4) {
                auto value=source.u32(offset+i);std::memcpy(result+i,&value,4);
            }
            for(auto at:layout.scripts) {
                auto target=source.pointer(offset+at);
                stage_pointers[result+at]=target?materialize(AT_SCRIPT,*target):nullptr;
            }
            for(auto at:layout.data_pointers) {
                auto target=source.pointer(offset+at);
                stage_pointers[result+at]=target?materialize(AT_WORDS,*target):nullptr;
            }
            for(auto range:layout.halves) {
                if(range.offset+range.bytes>size) throw std::runtime_error("Stage halfwords outside block");
                for(unsigned i=range.offset;i<range.offset+range.bytes;i+=2) {
                    auto value=source.u16(offset+i);std::memcpy(result+i,&value,2);
                }
            }
            for(auto range:layout.bytes) {
                auto input=source.bytes(offset+range.offset,range.bytes);
                if(range.offset+range.bytes>size) throw std::runtime_error("Stage bytes outside block");
                std::memcpy(result+range.offset,input.data(),range.bytes);
            }
            return result;
        }
        throw std::runtime_error("Missing native stage parameter layout");
    }
    void* particleBank(std::uint32_t offset,bool textures) {
        const size_t size=extent(offset);
        const auto word=[&](size_t at) {
            if(at+4>size) throw std::runtime_error("Particle bank word outside block");
            return source.u32(offset+at);
        };
        auto bank=static_cast<MeleeNativeParticleBank*>(allocate(sizeof(MeleeNativeParticleBank)));
        bank->magic=MELEE_PARTICLE_BANK_MAGIC;
        unsigned first=0,header=4;
        if(textures) bank->count=word(0);
        else {
            unsigned version=source.u16(offset);
            if(version==0) {bank->count=word(4);header=8;}
            else if(version>=0x40&&version<=0x43) {first=word(4);bank->count=first+word(8);header=12;}
            else throw std::runtime_error("Unknown particle command version");
        }
        if(bank->count>65536||first>bank->count||header+4ULL*(bank->count-first)>size)
            throw std::runtime_error("Invalid particle bank count");
        bank->entries=static_cast<void**>(allocate(bank->count*sizeof(void*)));
        std::vector<unsigned> cuts{static_cast<unsigned>(size)};
        std::map<unsigned,unsigned> texture_counts;
        for(unsigned i=first;i<bank->count;++i) {
            unsigned at=word(header+(i-first)*4);
            if(!at) continue;
            if(at>=size) throw std::runtime_error("Particle entry outside bank");
            cuts.push_back(at);
            if(textures) {
                unsigned n=word(at),fmt=word(at+4);
                if(fmt>=8&&fmt<=10) {
                    auto palnum=source.u16(offset+at+20),flags=source.u16(offset+at+22);
                    n+=(flags&1)?1:palnum?palnum:n;
                }
                if(n>65536||at+24ULL+4ULL*n>size) throw std::runtime_error("Invalid particle texture group");
                texture_counts[at]=n;
                for(unsigned j=0;j<n;++j) if(auto data=word(at+24+j*4)) {
                    if(data>=size) throw std::runtime_error("Particle texture outside bank");
                    cuts.push_back(data);
                }
            }
        }
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        std::map<unsigned,void*> copied;
        const auto copy_raw=[&](unsigned at)->void* {
            if(auto found=copied.find(at);found!=copied.end()) return found->second;
            unsigned end=*std::upper_bound(cuts.begin(),cuts.end(),at);
            auto p=allocate(end-at);std::memcpy(p,source.bytes(offset+at,end-at).data(),end-at);
            copied[at]=p;return p;
        };
        for(unsigned i=first;i<bank->count;++i) {
            unsigned at=word(header+(i-first)*4);
            if(!at) continue;
            if(textures) {
                unsigned n=texture_counts.at(at);
                auto group=static_cast<std::byte*>(allocate(24+n*sizeof(void*)));
                for(unsigned j=0;j<5;++j) {auto value=word(at+j*4);std::memcpy(group+j*4,&value,4);}
                for(unsigned j=20;j<24;j+=2) {auto value=source.u16(offset+at+j);std::memcpy(group+j,&value,2);}
                for(unsigned j=0;j<n;++j) if(auto data=word(at+24+j*4)) {
                    void* p=copy_raw(data);std::memcpy(group+24+j*sizeof(void*),&p,sizeof(p));
                }
                bank->entries[i]=group;
            } else {
                unsigned end=*std::upper_bound(cuts.begin(),cuts.end(),at);
                if(end-at<60) throw std::runtime_error("Truncated particle command header");
                auto cmd=static_cast<std::byte*>(copy_raw(at));
                for(unsigned j=0;j<8;j+=2) {auto value=source.u16(offset+at+j);std::memcpy(cmd+j,&value,2);}
                for(unsigned j=8;j<60;j+=4) {auto value=word(at+j);if(j==8)value=(value&0xF1FFFFFFU)|0x08000000U;std::memcpy(cmd+j,&value,4);}
                bank->entries[i]=cmd;
            }
        }
        return bank;
    }
    unsigned array_target(unsigned type) {
        switch(type) {
        case AT_SPLINE_TABLE:return AT_SPLINE;
        case AT_JOINT_TABLE:return AT_JOINT;
        case AT_MOBJ_TABLE:return AT_MOBJ;
        case AT_SCRIPT_TABLE:return AT_SCRIPT;
        case AT_ARTICLES:return AT_ARTICLE;
        case AT_GROUND_ITEMS:return AT_GROUND_ITEM;
        case AT_FIGHTER_ITEMS:return AT_ARTICLE;
        case AT_FIGHTER_VIS_TABLE:return AT_FIGHTER_VIS;
        case AT_FIGHTER_PART_ANIM_TABLE:return AT_FIGHTER_PART_ANIM;
        case AT_HALF_TABLE:return AT_HALVES;
        case AT_FIGATREES:return AT_FIGATREE;
        case AT_FIGATREE_TABLES:return AT_FIGATREES;
        case AT_SHAPEANIMS:return AT_SHAPEJOINT;
        case AT_FT_PARTS_TABLE:return AT_FT_PARTS;
        case AT_BYTEPAIR_TABLE:return AT_BYTEPAIR;
        case AT_RAW_TABLE:return AT_RAW;
        case AT_WORD_TABLE:return AT_WORDS; case AT_EVENTS:return AT_EVENT; case AT_MODELS:return AT_MODEL; case AT_ANIMS:return AT_ANIM;
        case AT_MATANIMS:return AT_MATANIMJOINT; case AT_ENVELOPES:return AT_ENVELOPE;
        case AT_LIGHT_LISTS:return AT_LIGHT_LIST; case AT_CAMERA_ANIMS:return AT_CAMERA_ANIM;
        case AT_LIGHT_ANIMS:return AT_LIGHT_ANIM; case AT_IMAGES:return AT_IMAGE; case AT_TLUTS:return AT_TLUT;
        default:return AT_NONE;
        }
    }
    void* materialize(unsigned type,std::uint32_t offset,size_t explicit_count=0) {
        if(type==AT_ITEM_POINTER_WORDS) {
            const auto key=std::make_pair(type,offset);
            if(auto found=objects.find(key);found!=objects.end()) return found->second;
            auto size=extent(offset);
            if(size<4||size%4) throw std::runtime_error("Invalid stage item attributes");
            for(auto field:pointer_fields) if(field>offset&&field<offset+size)
                throw std::runtime_error("Unexpected pointer inside stage item numeric fields");
            auto result=static_cast<std::byte*>(allocate(size+sizeof(void*)-4));
            objects[key]=result;
            if(auto target=source.pointer(offset)) {
                auto common=materialize(AT_WORDS,*target);
                std::memcpy(result,&common,sizeof(common));
            }
            for(unsigned i=4;i<size;i+=4) {
                auto value=source.u32(offset+i);
                std::memcpy(result+sizeof(void*)+i-4,&value,4);
            }
            return result;
        }
        if(type==AT_ITEM_NUMBERS) {
            auto end=offset+extent(offset);
            for(auto field:pointer_fields) if(field>=offset&&field<end)
                throw std::runtime_error("Item attributes need a pointer schema at "+std::to_string(offset));
            return materialize(AT_WORDS,offset);
        }
        if(type==AT_SCRIPT) {
            if(offset%4||offset>=source.data_size()) throw std::runtime_error("Invalid script offset");
            if(!script_arena) {
                // Keep the whole command address space contiguous: a branch
                // target inside a stream must not split its sequential storage.
                script_arena=static_cast<std::byte*>(allocate(source.data_size()));
                for(unsigned i=0;i+4<=source.data_size();i+=4) {
                    auto word=source.u32(i);std::memcpy(script_arena+i,&word,4);
                }
            }
            return script_arena+offset;
        }
        const auto key=std::make_pair(type,offset);
        if(auto found=objects.find(key);found!=objects.end()) return found->second;
        if(type==AT_NONE) throw std::runtime_error("Missing asset field schema at offset "+std::to_string(offset));
        if(type==AT_HALVES||type==AT_RAW||type==AT_STRING||type==AT_WORDS||type==AT_VEC3||type==AT_MTX) {
            size_t size=type==AT_VEC3?12:type==AT_MTX?48:extent(offset);
            if(type==AT_STRING) {
                size=0; do { ++size; } while(source.bytes(offset+size-1,1)[0]!=std::byte{0});
            }
            auto input=source.bytes(offset,size);
            auto result=static_cast<std::byte*>(allocate(size)); objects[key]=result;
            if(type==AT_WORDS||type==AT_VEC3||type==AT_MTX) {
                if(size%4) throw std::runtime_error("Unaligned numeric asset block");
                for(size_t i=0;i<size;i+=4) {auto value=source.u32(offset+i);std::memcpy(result+i,&value,4);}
            } else if(type==AT_HALVES) {
                if(size%2) throw std::runtime_error("Unaligned halfword asset block");
                for(size_t i=0;i<size;i+=2) {auto value=source.u16(offset+i);std::memcpy(result+i,&value,2);}
            } else std::memcpy(result,input.data(),size);
            if(type==AT_RAW&&size) {MeleeNativeRegisterVertexBuffer(result,size,0);vertex_buffers.push_back(result);}
            return result;
        }
        if(auto target=array_target(type);target!=AT_NONE) {
            size_t count=explicit_count;
            if(type==AT_FT_PARTS_TABLE||type==AT_BYTEPAIR_TABLE) count=extent(offset)/4;
            if(type==AT_FIGHTER_VIS_TABLE||type==AT_HALF_TABLE||type==AT_FIGHTER_PART_ANIM_TABLE) count=extent(offset)/4;
            if(type==AT_FIGATREES||type==AT_FIGATREE_TABLES) count=extent(offset)/4;
            if(type==AT_FIGHTER_ITEMS) {
                count=extent(offset)/4;
                // Tables have explicit storage bounds and may contain interior null slots.
            }
            if (!count) {
                try {while(source.pointer(offset+count*4)) ++count;}
                catch(const std::exception& error) {throw std::runtime_error("Pointer array type "+std::to_string(type)+" at "+std::to_string(offset)+": "+error.what());}
                ++count;
            }
            auto result=static_cast<void**>(allocate(count*sizeof(void*)));objects[key]=result;
            for(size_t i=0;i<count;++i) {
                if(locateExternal(offset+i*4,&result[i])) continue;
                if(type==AT_MOBJ_TABLE&&source.u32(offset+i*4)==0xFFFFFFFFU) {
                    result[i]=reinterpret_cast<void*>(UINTPTR_MAX);
                } else if(auto pointer=source.pointer(offset+i*4)) {
                    unsigned element_type=target;
                    if(type==AT_FIGHTER_ITEMS) {
                        if((fighter_name=="ftDataYoshi"&&i==3)||(fighter_name=="ftDataKirby"&&i==4)) element_type=AT_JOINT;
                        else if(fighter_name=="ftDataFox"&&i==4) element_type=AT_WORDS;
                        else if(fighter_name=="ftDataPurin"&&i==1) element_type=AT_PURIN_PARTS;
                        else if((fighter_name=="ftDataLink"||fighter_name=="ftDataClink")&&i==6) element_type=AT_JOINT;
                        else if(fighter_name=="ftDataSamus"&&i==4) element_type=AT_SAMUS_BEAM;
                        else if(fighter_name=="ftDataSeak"&&i>=4) element_type=AT_JOINT;
                        else if(fighter_name=="ftDataGamewatch"&&i==10) element_type=AT_FIGHTER_VIS;
                        else {
                            unsigned special=AT_ITEM_NUMBERS;
                            if(fighter_name=="ftDataPopo"&&i==2) special=AT_ITEM_CLIMBER_STRING;
                            if(fighter_name=="ftDataSamus"&&i==3) special=AT_ITEM_GRAPPLE;
                            if(fighter_name=="ftDataSeak"&&i==3) special=AT_ITEM_CHAIN;
                            if(fighter_name=="ftDataNess"&&i==10) special=AT_ITEM_YOYO;
                            if(fighter_name=="ftDataGamewatch") special=i==8?AT_ITEM_CHEF:AT_ITEM_GW;
                            if(fighter_name=="ftDataLink"||fighter_name=="ftDataClink") {
                                if(i==1) special=AT_ITEM_BOOMERANG;
                                if(i==2) special=AT_ITEM_HOOKSHOT;
                                if(i==3) special=AT_ITEM_ARROW;
                            }
                            item_special_types[*pointer]=special;
                        }
                    }
                    result[i]=materialize(element_type,*pointer);
                }
            }
            return result;
        }
        const auto schema=MeleeNativeAssetSchema(type);
        if(!schema) throw std::runtime_error("Missing native asset type "+std::to_string(type));
        size_t count=explicit_count?explicit_count:1;
        if(type==AT_ITEM_STATES) {
            if(extent(offset)<16) throw std::runtime_error("Truncated item-state array");
            count=extent(offset)/16;
        }
        if(type==AT_FIGHTER_ANIMS||type==AT_FIGHTER_VIS) {
            if(extent(offset)%schema->file_size) throw std::runtime_error("Invalid fighter table size");
            count=extent(offset)/schema->file_size;
        }
        if(type==AT_RVALUES) {
            count=0;while(source.pointer(offset+count*8+4)) ++count; ++count;
        }
        if(type==AT_ITEM_FOODS) {
            count=source.u32(offset)+1;
            if((count-1)*16+4!=extent(offset)) throw std::runtime_error("Invalid food-attribute array size");
        }
        if(type==AT_TROPHIES||type==AT_TROPHY_DISPLAY||type==AT_COLOR_DESC) {
            if(extent(offset)%schema->file_size) throw std::runtime_error("Invalid trophy table size");
            count=extent(offset)/schema->file_size;
        }
        if(type==AT_VERTICES) {
            count=0; while(source.u32(offset+count*schema->file_size)!=255) ++count; ++count;
        } else if(type==AT_CAMERAS||type==AT_ENVELOPE) {
            count=0; while(source.pointer(offset+count*schema->file_size)) ++count; ++count;
        }
        auto result=static_cast<std::byte*>(allocate(schema->host_size*count)); objects[key]=result;
        if(type==AT_ANIM||type==AT_MATANIMJOINT||type==AT_SHAPEJOINT)
            animation_objects[result]={type,offset};
        for(size_t record=0;record<count;++record) {
            auto source_offset=offset+record*schema->file_size;
            auto dest=result+record*schema->host_size;
            if(type==AT_VERTICES&&source.u32(source_offset)==255) {std::uint32_t end=255;std::memcpy(dest,&end,4);continue;}
            if((type==AT_CAMERAS||type==AT_ENVELOPE)&&!source.pointer(source_offset)) continue;
            for(unsigned f=0;f<schema->field_count;++f) {
                const auto& field=schema->fields[f];
                if(type==AT_ITEM_FOODS&&record==count-1&&field.file_offset!=0) continue;
                auto at=source_offset+field.file_offset;
                if(field.kind==AF_POINTER) {
                    if(type==AT_ROBJ&&field.file_offset==8&&(source.u32(source_offset+4)&0x70000000U)==0x20000000U) {
                        auto value=source.u32(at);std::memcpy(dest+field.host_offset,&value,4);continue;
                    }
                    if(locateExternal(at,dest+field.host_offset)) continue;
                    void* pointer=nullptr;
                    if(((field.file_offset==0&&(type==AT_ANIM||type==AT_MATANIMJOINT||type==AT_SHAPEJOINT||type==AT_LIGHT_OVERRIDE||type==AT_SHADOW_ENTRY))||type==AT_MAP_MODEL)&&source.u32(at)==0xFFFFFFFFU) {
                        // Unused animation children and deferred stage-model slots.
                        // Preserve the sentinel, rather than traversing it.
                        pointer=reinterpret_cast<void*>(UINTPTR_MAX);
                        std::memcpy(dest+field.host_offset,&pointer,sizeof(pointer));
                        continue;
                    }
                    auto target=[&]() {
                        try {return source.pointer(at);}
                        catch(const std::exception& error) {
                            throw std::runtime_error("Type "+std::to_string(type)+" record "+std::to_string(source_offset)+" field "+std::to_string(field.file_offset)+": "+error.what());
                        }
                    }();
                    if(target) {
                        unsigned kind=field.target;
                        if(type==AT_ROBJ&&field.file_offset==8) {
                            switch(source.u32(source_offset+4)&0x70000000U) {
                            case 0:kind=AT_EXPRESSION;break;
                            case 0x10000000U:kind=AT_JOINT;break;
                            case 0x30000000U:kind=AT_BYTE_EXPRESSION;break;
                            case 0x40000000U:kind=AT_WORDS;break;
                            }
                        }
                        size_t elements=0;
                        if(type==AT_ITEM_PUBLIC&&field.file_offset>=4&&field.file_offset<=12) {
                            const unsigned counts[]={43,118,47};
                            const unsigned starts[]={0,43,161};
                            unsigned table=field.file_offset/4-1;
                            elements=counts[table];
                            for(unsigned i=0;i<elements;++i) if(auto article=source.pointer(*target+i*4))
                                item_special_types[*article]=MeleeNativeItemSpecialType(starts[table]+i);
                        }
                        if(type==AT_FIGHTER&&field.file_offset==4&&fighter_name=="ftDataGamewatch") kind=AT_GAMEWATCH_ATTR;
                        if(type==AT_GROUND_ITEM&&field.file_offset==4)
                            item_special_types[*target]=MeleeNativeItemSpecialType(source.u32(source_offset));
                        if(type==AT_ARTICLE&&field.file_offset==4) kind=item_special_types.at(source_offset);
                        if(type==AT_ITEM_DYNAMICS&&field.file_offset==4) elements=source.u32(source_offset);
                        if(type==AT_FIGHTER_DYNAMICS&&field.file_offset==4) elements=source.u32(source_offset);
                        if(type==AT_FIGHTER_DYNAMICS&&field.file_offset==16) elements=extent(*target)/4;
                        if(type==AT_FIGHTER_VIS&&field.file_offset==4) elements=source.u32(source_offset);
                        if(type==AT_FIGHTER_PART_ANIM&&field.file_offset==8) elements=extent(*target)/4;
                        if(type==AT_SAMUS_BEAM&&field.file_offset==4) elements=extent(*target)/4;
                        if(type==AT_FIGATREE&&field.file_offset==16) {
                            auto nodes=source.pointer(source_offset+12);
                            if(!nodes) throw std::runtime_error("FigaTree has tracks without nodes");
                            for(unsigned i=0;;++i) {
                                unsigned n=std::to_integer<unsigned>(source.bytes(*nodes+i,1)[0]);
                                if(n==255) break;
                                elements+=n;
                            }
                        }
                        if(type==AT_MAP_HEAD) elements=source.u32(source_offset+field.file_offset+4);
                        if(type==AT_MAP_HEAD&&field.file_offset==24) {
                            if(elements%2) throw std::runtime_error("Odd light-override word count");
                            elements/=2;
                        }
                        if(type==AT_COLL_MAP&&field.file_offset==36) elements=source.u32(source_offset+40);
                        if(type==AT_GROUND_PARAM&&field.file_offset==176) elements=source.u32(source_offset+180);
                        if(type==AT_JOINT&&field.file_offset==16) {
                            auto flags=source.u32(source_offset+4);
                            kind=(flags&0x4000)?AT_SPLINE:(flags&0x20)?AT_PTCL_LIST:AT_DOBJ;
                        }
                        if(type==AT_POBJ&&field.file_offset==20) {
                            unsigned flags=source.u16(source_offset+12)&0x3000;
                            kind=flags==0?AT_JOINT:flags==0x2000?AT_ENVELOPES:flags==0x1000?AT_SHAPESET:AT_NONE;
                        }
                        if(type==AT_SHAPESET&&(field.file_offset==12||field.file_offset==24)) elements=source.u16(source_offset+2);
                        if(type==AT_CPU_VERTICES&&field.file_offset==20) {
                            unsigned comp=source.u32(source_offset+12);
                            kind=comp==4?AT_WORDS:(comp==2||comp==3)?AT_HALVES:AT_RAW;
                        }
                        if(type==AT_EVENT&&field.file_offset==4) kind=event_extra_types.at(source_offset);
                        if(type==AT_CPU_CONFIG&&field.file_offset<32) elements=extent(*target)/4;
                        if(type==AT_TEXANIM&&field.file_offset==12) elements=source.u16(source_offset+20);
                        if(type==AT_TEXANIM&&field.file_offset==16) elements=source.u16(source_offset+22);
                        if(kind==AT_NONE) throw std::runtime_error("Missing field schema: type "+std::to_string(type)+" field "+std::to_string(field.file_offset)+" target "+std::to_string(*target));
                        pointer=materialize(kind,*target,elements);
                    }
                    std::memcpy(dest+field.host_offset,&pointer,sizeof(pointer));
                } else {
                    unsigned elements=field.count;
                    if(type==AT_CAMERA&&field.file_offset==48&&source.u16(source_offset+6)==1) elements=2;
                    for(unsigned i=0;i<elements;++i) {
                        auto output=dest+field.host_offset+i*field.kind;
                        if(field.kind==AF_WORD) {
                            auto value=source.u32(at+i*4);
                            // map_head stores the light-override size in words,
                            // whereas the runtime iterates eight-byte records.
                            if(type==AT_MAP_HEAD&&field.file_offset==28) value/=2;
                            std::memcpy(output,&value,4);
                        }
                        else if(field.kind==AF_HALF) {auto value=source.u16(at+i*2);std::memcpy(output,&value,2);}
                        else {
                            auto value=source.bytes(at+i,1)[0];
                            if((type==AT_LIGHT_OVERRIDE||type==AT_SHADOW_ENTRY)&&field.file_offset==4) {
                                unsigned input=std::to_integer<unsigned>(value), reversed=0;
                                for(unsigned bit=0;bit<8;++bit) reversed|=((input>>bit)&1)<<(7-bit);
                                value=std::byte(reversed);
                            }
                            *output=value;
                        }
                    }
                }
            }
            if(type==AT_EVENT_INIT) MeleeNativeEventFlags(dest,source.u16(source_offset),source.u32(source_offset+16),source.u32(source_offset+20));
            if(type==AT_ITEM_ATTR) MeleeNativeItemFlags(dest,std::to_integer<unsigned>(source.bytes(source_offset,1)[0]),std::to_integer<unsigned>(source.bytes(source_offset+1,1)[0]));
        }
        return result;
    }
    void* get(const char* name) {
        auto root=source.roots().find(name);
        if (root==source.roots().end()) return nullptr;
        if (auto found=public_objects.find(name);found!=public_objects.end()) return found->second;
        if(std::getenv("MELEE_TRACE_ASSETS")) std::fprintf(stderr,"[asset] %s\n",name);
        void* result;
        if (root->first=="lbRumbleData") result=rumble(root->second);
        else if(root->first=="lbBgFlashColAnimData") result=materialize(AT_COLOR_DESC,root->second);
        else if(root->first=="itPublicData") result=materialize(AT_ITEM_PUBLIC,root->second);
        else if(root->first=="ftDataKirbyCopyYoshi") {
            if(auto article=source.pointer(root->second+32)) item_special_types[*article]=AT_ITEM_NUMBERS;
            result=materialize(AT_KIRBY_COPY_YOSHI,root->second);
        }
        else if(root->first=="ftDataKirbyCopyFox" || root->first=="ftDataKirbyCopyMario" || root->first=="ftDataKirbyCopyDrmario" || root->first=="ftDataKirbyCopyLuigi") {
            for(unsigned field:{12U,16U}) if(auto article=source.pointer(root->second+field)) item_special_types[*article]=AT_ITEM_NUMBERS;
            result=materialize(AT_KIRBY_COPY_FOX,root->second);
        }
        else if(root->first.starts_with("ftData")) {fighter_name=root->first;result=materialize(AT_FIGHTER,root->second);}
        else if(root->first.ends_with("_figatree")) result=materialize(AT_FIGATREE,root->second);
        else if(root->first=="sqEventInitDataLevelTbl") {
            const size_t count=extent(root->second)/4;
            for(unsigned index=0;index<count;++index) {
                auto event=source.pointer(root->second+index*4);
                if(!event) throw std::runtime_error("Null event table entry");
                if(!source.pointer(*event+4)) continue;
                unsigned kind;
                switch(index) {
                case 0: kind=AT_EVENT_TIMING;break;
                case 43: kind=AT_EVENT_EXTRA;break;
                case 4: case 36: kind=AT_WORDS;break;
                case 12: kind=AT_VEC3;break;
                case 13: case 25: case 46: kind=AT_RAW;break;
                default:throw std::runtime_error("Unknown event auxiliary layout for level "+std::to_string(index));
                }
                event_extra_types[*event]=kind;
            }
            result=materialize(AT_EVENTS,root->second,count);
        }
        else if(root->first=="map_ptcl"||root->first=="map_texg") result=particleBank(root->second,root->first=="map_texg");
        else if(root->first.ends_with("_image_desc")) result=materialize(AT_IMAGE,root->second);
        else if(root->first.ends_with("_tlut_desc")) result=materialize(AT_TLUT,root->second);
        else if(root->first.starts_with("dynamicsdata_")) result=materialize(AT_DYNAMICS_DESC,root->second);
        else if(root->first=="map_head") result=materialize(AT_MAP_HEAD,root->second);
        else if(root->first=="ALDYakuAll") result=materialize(AT_SCRIPT_TABLE,root->second,extent(root->second)/4);
        else if(root->first=="coll_data") result=materialize(AT_COLL_MAP,root->second);
        else if(root->first=="grGroundParam") result=materialize(AT_GROUND_PARAM,root->second);
        else if(root->first=="map_plit") result=materialize(AT_LIGHT_LISTS,root->second);
        else if(root->first=="quake_model_set") result=materialize(AT_MODEL,root->second);
        else if(root->first=="itemdata") result=materialize(AT_GROUND_ITEMS,root->second);
        else if(root->first=="yakumono_param") result=stageParameters(root->second);
        else if(root->first=="plLoadCommonData"||root->first=="ftLoadCommonData") {
            const unsigned kinds[]={AT_FT_COMMON,AT_WORDS,AT_WORDS,AT_WORDS,AT_FT_PARTS_TABLE,AT_BYTEPAIR_TABLE,AT_COLOR_DESC,AT_COLOR_DESC,AT_JOINT_ANIM_PAIR,AT_SHAKE,AT_SHAKE,AT_SHAKE,AT_WORDS,AT_WORDS,AT_WORDS,AT_WORDS,AT_JOINT,AT_RAW,AT_RAW,AT_RAW,AT_JOINT,AT_WORDS,AT_CPU_CONFIG};
            bool fighter=root->first=="ftLoadCommonData";
            unsigned count=fighter?23:1;
            auto table=static_cast<void**>(allocate(count*sizeof(void*)));
            for(unsigned i=0;i<count;++i) if(auto target=source.pointer(root->second+i*4))
                table[i]=materialize(fighter?kinds[i]:AT_PLAYER_COMMON,*target,fighter&&i==9?3:0);
            result=table;
        }
        else if(root->first.starts_with("eff")&&root->first.ends_with("DataTable")) {
            if(extent(root->second)<8) throw std::runtime_error("Truncated effect table");
            size_t count=(extent(root->second)-8)/20;
            const auto schema=MeleeNativeAssetSchema(AT_EFFECTDESC);
            auto table=static_cast<std::byte*>(allocate(2*sizeof(void*)+count*schema->host_size));
            for(unsigned i=0;i<2;++i) if(auto target=source.pointer(root->second+i*4)) {
                auto bank=particleBank(*target,i==1);std::memcpy(table+i*sizeof(void*),&bank,sizeof(bank));
            }
            if(count) std::memcpy(table+2*sizeof(void*),materialize(AT_EFFECTDESC,root->second+8,count),count*schema->host_size);
            result=table;
        }
        else if(root->first=="lbAudioLoadData") {
            auto table=static_cast<void**>(allocate(4*sizeof(void*)));
            for(unsigned i=0;i<4;++i) {
                auto target=source.pointer(root->second+i*4);
                if(!target||extent(*target)%4) throw std::runtime_error("Invalid audio load table");
                table[i]=materialize(AT_WORD_TABLE,*target,extent(*target)/4);
            }
            result=table;
        }
        else if(root->first=="MnSelectChrDataTable"||root->first=="MnSelectStageDataTable") {
            // Four scene pointers followed by nine CSS or twelve SSS model sets.
            const unsigned count=root->first=="MnSelectChrDataTable"?40:52;
            const unsigned scene_types[]={AT_CAMERA,AT_LIGHT,AT_LIGHT,AT_FOG};
            const unsigned anim_types[]={AT_JOINT,AT_ANIM,AT_MATANIMJOINT,AT_SHAPEJOINT};
            auto table=static_cast<void**>(allocate(count*sizeof(void*)));
            for(unsigned i=0;i<count;++i) if(auto target=source.pointer(root->second+i*4))
                table[i]=materialize(i<4?scene_types[i]:anim_types[(i-4)%4],*target);
            result=table;
        }
        else if(root->first=="lbRefData") result=materialize(AT_REFRACT,root->second);
        else if(root->first=="MemCardIconData"||root->first=="MemSnapIconData") {
            unsigned count=root->first=="MemCardIconData"?5:2;
            auto table=static_cast<void**>(allocate(count*sizeof(void*)));
            for(unsigned i=0;i<count;++i) if(auto pointer=source.pointer(root->second+i*4)) table[i]=materialize(AT_RAW,*pointer);
            result=table;
        }
        else if(root->first.starts_with("SIS_")) {
            size_t count=extent(root->second)/4;
            auto table=static_cast<void**>(allocate(count*sizeof(void*)));
            for(size_t i=0;i<count;++i) if(auto pointer=source.pointer(root->second+i*4)) table[i]=materialize(AT_RAW,*pointer);
            result=table;
        }
        else if(root->first=="tyInitModelTbl"||root->first=="tyInitModelDTbl") result=materialize(AT_TROPHIES,root->second);
        else if(root->first=="tyDisplayModelTbl"||root->first=="tyDisplayModelUsTbl") result=materialize(AT_TROPHY_DISPLAY,root->second);
        else if(root->first=="tyModelSortTbl"||root->first=="tyExpDifferentTbl"||root->first=="tyNoGetUsTbl") result=materialize(AT_HALVES,root->second);
        else if(root->first.ends_with("_scene_models")||root->first=="Stc_rarwmdls"||root->first=="Stc_scemdls"||root->first=="lupe"||root->first=="tdsce") result=materialize(AT_MODELS,root->second);
        else if(root->first.ends_with("_scene_data")||root->first=="pnlsce"||root->first=="flmsce") result=materialize(AT_SCENE,root->second);
        else if(root->first.starts_with("ftDemo")&&root->first.find("MotionFile")!=std::string::npos) result=materialize(AT_RAW,root->second);
        else if(root->first.ends_with("_animjoint")) result=materialize(AT_ANIM,root->second);
        else if(root->first.ends_with("_matanim_joint")) result=materialize(AT_MATANIMJOINT,root->second);
        else if(root->first.ends_with("_shapeanim_joint")) result=materialize(AT_SHAPEJOINT,root->second);
        else if(root->first.ends_with("_camera")) result=materialize(AT_CAMERA,root->second);
        else if(root->first.ends_with("_camanim")) result=materialize(AT_CAMERA_ANIM,root->second);
        else if(root->first.ends_with("_scene_lights")) result=materialize(AT_LIGHT_LISTS,root->second);
        else if(root->first.ends_with("_fog")) result=materialize(AT_FOG,root->second);
        else if(root->first.ends_with("_sobjdesc")) result=materialize(AT_SOBJ,root->second);
        else if(root->first.ends_with("_joint")) result=materialize(AT_JOINT,root->second);
        else throw std::runtime_error("Missing native asset schema for public symbol: "+root->first);
        public_objects.emplace(name,result); return result;
    }
};
std::unordered_map<void*,std::shared_ptr<Archive>> archives;
// HSD_Archive descriptors can live on a reused stack frame. Converted assets
// belong to the source buffer and must survive rebinding that temporary key.
std::unordered_map<const void*,std::shared_ptr<Archive>> archive_storage;
std::recursive_mutex mutex;
[[noreturn]] void failure(const std::exception& error) { std::fprintf(stderr,"Native archive error: %s\n",error.what()); std::abort(); }
}
extern "C" int MeleeNativeArchiveCreate(void* key,const void* data,size_t size) {
    std::lock_guard lock(mutex);
    try {
        auto next=std::make_shared<Archive>(data,size);
        std::erase_if(archives,[&](const auto& item){return item.second->raw==data;});
        archive_storage[data]=next;
        archives[key]=std::move(next);
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr,"Native archive parse: %s\n",error.what()); return -1; }
}
extern "C" void* MeleeNativeScriptPointer(const void* field) {
    std::lock_guard lock(mutex);
    try {
        auto value=reinterpret_cast<uintptr_t>(field);
        for(auto& [raw,archive]:archive_storage) {
            if(auto found=archive->stage_pointers.find(field);found!=archive->stage_pointers.end()) return found->second;
            auto base=reinterpret_cast<uintptr_t>(archive->script_arena);
            if(base&&value>=base&&value-base<archive->source.data_size()) {
                auto target=archive->source.pointer(static_cast<unsigned>(value-base));
                if(!target) throw std::runtime_error("Null script branch target");
                return archive->materialize(AT_SCRIPT,*target);
            }
        }
        throw std::runtime_error("Script branch has no live archive owner");
    } catch(const std::exception& error) {failure(error);}
}
extern "C" void* MeleeNativeAnimationAt(const void* base,unsigned index) {
    std::lock_guard lock(mutex);
    try {
        for(auto& [raw,archive]:archive_storage) {
            auto found=archive->animation_objects.find(base);
            if(found==archive->animation_objects.end()) continue;
            auto [type,offset]=found->second;
            auto schema=MeleeNativeAssetSchema(type);
            auto target=uint64_t(offset)+uint64_t(index)*schema->file_size;
            if(target+schema->file_size>archive->source.data_size()) throw std::runtime_error("Animation index outside archive");
            return archive->materialize(type,static_cast<unsigned>(target));
        }
        throw std::runtime_error("Animation array has no live archive owner");
    } catch(const std::exception& error) {failure(error);}
}
extern "C" void* MeleeNativeArchiveAllocate(void* key,size_t size) {
    std::lock_guard lock(mutex);
    try { return archives.at(key)->allocate(size); } catch(const std::exception& error) { failure(error); }
}
extern "C" void* MeleeNativeArchivePublic(void* key,const char* name) {
    std::lock_guard lock(mutex);
    try { return archives.at(key)->get(name); } catch(const std::exception& error) { std::fprintf(stderr,"Archive public symbol: %s\n",name); failure(error); }
}
extern "C" void MeleeNativeArchiveExtern(void* key,const char* name,void* pointer) {
    std::lock_guard lock(mutex);
    try { archives.at(key)->bindExternal(name,pointer); } catch(const std::exception& error) { failure(error); }
}
extern "C" void MeleeNativeArchiveRelease(void* pointer) {
    std::lock_guard lock(mutex);
    std::erase_if(archives,[&](const auto& item){return item.first==pointer || item.second->raw==pointer;});
    archive_storage.erase(pointer);
}
extern "C" void MeleeNativeArchiveReleaseRange(void* pointer,size_t size) {
    std::lock_guard lock(mutex);
    auto contains=[&](const void* p){auto value=(uintptr_t)p,base=(uintptr_t)pointer;return value>=base&&value-base<size;};
    std::erase_if(archives,[&](const auto& item){return contains(item.first)||contains(item.second->raw);});
    std::erase_if(archive_storage,[&](const auto& item){return contains(item.first);});
}
