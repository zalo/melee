#include <cstdio>

#include "gx/flip_vertex.hpp"
#include "gfx/flip_upload.hpp"

using namespace aurora::gx;
static void check(bool ok, const char* name)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
static void check_packed(const ShaderConfig& c, const u8* raw, size_t count,
                         const std::array<AttrArray, MaxVtxAttr>& arrays, u32 pnMtx,
                         const char* name)
{
    const auto layout = flip_vertex_attributes(c);
    std::vector<u8> packed;
    flip_decode_packed(c, layout, raw, count, packed, arrays, pnMtx);
    std::vector<u8> expected(packed.size());
    for (size_t i = 0; i < count; ++i) {
        const auto vertex = flip_decode_vertex(c, raw + i * c.vtxStride, arrays, pnMtx);
        const auto* source = reinterpret_cast<const u8*>(&vertex);
        for (unsigned a = 0; a < layout.count; ++a) {
            const auto& attr = layout.attributes[a];
            std::memcpy(expected.data() + i * layout.stride + attr.offset,
                        source + layout.sources[a], flip_attribute_bytes(attr.shaderLocation));
        }
    }
    check(packed == expected, name);
    std::vector<u8> destination(packed.size()+32,0xcd);
    flip_decode_packed_into(c,layout,raw,count,{destination.data()+16,packed.size()},arrays,pnMtx);
    check(std::equal(expected.begin(),expected.end(),destination.begin()+16),"direct decode fully initializes reused storage");
    check(std::all_of(destination.begin(),destination.begin()+16,[](u8 v){return v==0xcd;})&&
          std::all_of(destination.end()-16,destination.end(),[](u8 v){return v==0xcd;}),"direct decode preserves destination guards");
}
int main()
{
    {
        using aurora::gfx::detail::flip_upload_changed;
        std::vector<uint8_t> shadow, device(64), data(32, 7);
        unsigned writes=0;
        const auto write=[&](size_t offset,size_t size) {
            check(offset%4==0&&size%4==0,"dirty upload alignment");
            std::memcpy(device.data()+offset,data.data()+offset,size);++writes;
        };
        flip_upload_changed(shadow,data,8,write);
        check(writes==1&&std::equal(data.begin(),data.end(),device.begin()),"first upload coalesces adjacent chunks");
        writes=0;flip_upload_changed(shadow,data,8,write);
        check(writes==0,"unchanged upload omitted");
        data[7]=1;data[8]=2;data[31]=3;
        flip_upload_changed(shadow,data,8,write);
        check(writes==2&&std::equal(data.begin(),data.end(),device.begin()),"boundary edits and separated dirty ranges");
        data.resize(8);writes=0;flip_upload_changed(shadow,data,8,write);
        check(writes==0&&shadow.size()==32,"short frame preserves uploaded tail");
        data.assign(shadow.begin(),shadow.end());data.resize(48,9);writes=0;
        flip_upload_changed(shadow,data,8,write);
        check(writes==1&&std::equal(data.begin(),data.end(),device.begin()),"growth uploads newly exposed bytes");
        shadow.clear();writes=0;flip_upload_changed(shadow,data,8,write);
        check(writes==1,"external-write invalidation forces complete upload");
    }
    std::array<AttrArray, MaxVtxAttr> arrays{};
    ShaderConfig c{};
    c.attrs[GX_VA_POS] = { .attrType = GX_DIRECT,
                           .cnt = 3,
                           .compType = GX_S16,
                           .offset = 1,
                           .frac = 4 };
    c.attrs[GX_VA_PNMTXIDX] = {
        .attrType = GX_DIRECT, .cnt = 1, .compType = GX_U8, .offset = 0
    };
    auto layout = flip_vertex_attributes(c);
    check(layout.count == 2 && layout.stride == 24,
          "compact position/matrix layout");
    const u8 direct[] = { 9, 0xff, 0xe0, 0, 24, 1, 0 };
    auto v = flip_decode_vertex(c, direct, arrays, 0);
    check(v.pos[0] == -2 && v.pos[1] == 1.5f && v.pos[2] == 16 &&
              v.matrices[0] == 3,
          "big endian signed positions and matrix index");
    c = {};
    c.attrs[GX_VA_POS] = { .attrType = GX_INDEX16,
                           .cnt = 2,
                           .compType = GX_F32,
                           .offset = 0,
                           .stride = 8,
                           .le = true };
    const float array[] = { 0, 0, 3.25f, -7.5f };
    arrays[GX_VA_POS] = { .data = array, .size = sizeof(array), .stride = 8 };
    const u8 indexed[] = { 0, 1 };
    v = flip_decode_vertex(c, indexed, arrays, 0);
    check(v.pos[0] == 3.25f && v.pos[1] == -7.5f && v.pos[2] == 0,
          "independent big endian index into little endian XY array");
    const u8 invalid[] = { 0, 2 };
    v = flip_decode_vertex(c, invalid, arrays, 0);
    check(v.pos[0] == 0 && v.pos[1] == 0, "out of range array access");
    c = {};
    c.attrs[GX_VA_CLR0] = {
        .attrType = GX_DIRECT, .cnt = 1, .compType = GX_RGB565, .offset = 0
    };
    const u8 red[] = { 0xf8, 0 };
    v = flip_decode_vertex(c, red, arrays, 0);
    check(v.color[0][0] == 1 && v.color[0][1] == 0 && v.color[0][2] == 0 &&
              v.color[0][3] == 1,
          "RGB565 channel order");
    c.attrs[GX_VA_CLR0].compType = GX_RGBA6;
    const u8 rgba[] = { 0xfc, 0x0f, 0xc0 };
    v = flip_decode_vertex(c, rgba, arrays, 0);
    check(v.color[0][0] == 1 && v.color[0][1] == 0 && v.color[0][2] == 1 &&
              v.color[0][3] == 0,
          "RGBA6 packed channels");
    c = {};
    c.attrs[GX_VA_NRM] = { .attrType = GX_INDEX8,
                           .cnt = 9,
                           .compType = GX_S8,
                           .offset = 0,
                           .stride = 9,
                           .frac = 6,
                           .le = true,
                           .nbt3 = true };
    const u8 nbt[] = { 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64,
                       0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64 };
    arrays[GX_VA_NRM] = { .data = nbt, .size = sizeof(nbt), .stride = 9 };
    const u8 indices[] = { 0, 1, 2 };
    v = flip_decode_vertex(c, indices, arrays, 0);
    check(v.normal[0] == 1 && v.binormal[1] == 1 && v.tangent[2] == 1,
          "NBT3 independent indices and component offsets");
    c.vtxStride = sizeof(indices);
    check_packed(c, indices, 1, arrays, 0, "packed/reference NBT3 equivalence");
    // Differential format coverage, including invalid indices and endian cases.
    for (unsigned type : {GX_U8, GX_S8, GX_U16, GX_S16, GX_F32}) {
        for (bool le : {false, true}) for (auto attr : {GX_DIRECT, GX_INDEX8, GX_INDEX16}) {
            c = {}; arrays = {};
            c.vtxStride = 16;
            c.attrs[GX_VA_POS] = {.attrType=u8(attr),.cnt=3,.compType=u8(type),.offset=0,.stride=12,.frac=4,.le=le};
            std::array<u8, 16 * 32> raw{}, source{};
            for (unsigned i=0;i<raw.size();++i) { raw[i]=(i*37+11)%256; source[i]=(i*19+7)%256; }
            arrays[GX_VA_POS]={.data=source.data(),.size=source.size(),.stride=12};
            for (unsigned i=0;i<32;++i) if (attr!=GX_DIRECT) { raw[i*16]=attr==GX_INDEX16 ? 0 : i; raw[i*16+1]=i; }
            std::vector<FlipVertex> bulk;
            check(flip_decode_bulk(c,raw.data(),32,bulk,arrays,7), "bulk supported numeric format");
            for (unsigned i=0;i<32;++i) {
                auto expected=flip_decode_vertex(c,raw.data()+i*16,arrays,7);
                check(std::memcmp(&expected,&bulk[i],sizeof(expected))==0,"bulk/reference numeric equivalence");
            }
            check_packed(c, raw.data(), 32, arrays, 7, "packed/reference numeric equivalence");
            c.attrs[GX_VA_POS].cnt=2;
            check_packed(c, raw.data(), 32, arrays, 7, "packed/reference XY equivalence");
            c.attrs[GX_VA_TEX0]=c.attrs[GX_VA_POS];
            c.attrs[GX_VA_TEX0].cnt=1;
            arrays[GX_VA_TEX0]=arrays[GX_VA_POS];
            check_packed(c, raw.data(), 32, arrays, 7, "packed/reference one-component texture coordinate");
        }
    }
    for (unsigned type : {GX_RGB565,GX_RGB8,GX_RGBX8,GX_RGBA4,GX_RGBA6,GX_RGBA8}) {
      for (bool le : {false,true}) for (auto attr : {GX_DIRECT,GX_INDEX8,GX_INDEX16}) {
        c={};arrays={}; c.vtxStride=4;
        c.attrs[GX_VA_CLR0]={.attrType=u8(attr),.cnt=1,.compType=u8(type),.offset=0,.stride=4,.le=le};
        std::array<u8,128> raw{},source{};
        for(unsigned i=0;i<raw.size();++i){raw[i]=(i*29+31)%256;source[i]=(i*23+19)%256;}
        arrays[GX_VA_CLR0]={.data=source.data(),.size=source.size(),.stride=4};
        for(unsigned i=0;i<32;++i) if(attr!=GX_DIRECT){raw[i*4]=attr==GX_INDEX16?0:i;raw[i*4+1]=i;}
        std::vector<FlipVertex> bulk;
        check(flip_decode_bulk(c,raw.data(),32,bulk,arrays,0),"bulk packed color supported");
        for(unsigned i=0;i<32;++i){auto expected=flip_decode_vertex(c,raw.data()+i*4,arrays,0);
          check(!std::memcmp(&expected,&bulk[i],sizeof(expected)),"bulk/reference packed color equivalence");}
        check_packed(c,raw.data(),32,arrays,0,"packed/reference color equivalence");
      }
    }
    c={};arrays={};c.vtxStride=1;
    c.attrs[GX_VA_POS]={.attrType=GX_INDEX8,.cnt=3,.compType=GX_S8,.offset=0,.stride=3};
    std::array<u8,6> positions{1,2,3,4,5,6}; const u8 triangle[]{0,1,0};
    arrays[GX_VA_POS]={.data=positions.data(),.size=positions.size(),.stride=3};
    auto key=flip_vertex_key(c,triangle,sizeof(triangle),arrays,0);
    positions[4]=9;
    check(key!=flip_vertex_key(c,triangle,sizeof(triangle),arrays,0),"vertex cache detects in-place array edits");
    positions[4]=5;
    check(key==flip_vertex_key(c,triangle,sizeof(triangle),arrays,0),"vertex cache content identity");
    check(key!=flip_vertex_key(c,triangle,sizeof(triangle),arrays,1),"vertex cache includes default matrix");
    c.flipFlags=FlipUberShader;
    check(key!=flip_vertex_key(c,triangle,sizeof(triangle),arrays,0),"vertex cache separates decoded layouts");
    c.flipFlags=0;
    std::array<u8,768> sparse{};
    const u8 far_indices[]{0,255,0};
    arrays[GX_VA_POS]={.data=sparse.data(),.size=sparse.size(),.stride=3};
    key=flip_vertex_key(c,far_indices,sizeof(far_indices),arrays,0);
    sparse[384]=42;
    check(key==flip_vertex_key(c,far_indices,sizeof(far_indices),arrays,0),"sparse cache excludes unreferenced records");
    sparse[767]=42;
    check(key!=flip_vertex_key(c,far_indices,sizeof(far_indices),arrays,0),"sparse cache detects referenced tail edit");
    // Specialized loaders: mixed multi-attribute records, batching matrices word,
    // single-index NBT, and out-of-range indices must match the reference decoder.
    for (unsigned variant = 0; variant < 4; ++variant) {
        c = {}; arrays = {};
        c.flipFlags = FlipVertexInput | FlipCompactVertices | FlipUniformTable | FlipBatchedDraws;
        u8 off = 0;
        c.attrs[GX_VA_PNMTXIDX] = {.attrType=GX_DIRECT,.cnt=1,.compType=GX_U8,.offset=off}; off += 1;
        c.attrs[GX_VA_TEX0MTXIDX] = {.attrType=GX_DIRECT,.cnt=1,.compType=GX_U8,.offset=off}; off += 1;
        c.attrs[GX_VA_POS] = {.attrType=GX_INDEX16,.cnt=3,.compType=variant&1 ? u8(GX_F32) : u8(GX_S16),.offset=off,.stride=12,.frac=variant&1 ? u8(0) : u8(5),.le=(variant&2)!=0}; off += 2;
        c.attrs[GX_VA_NRM] = {.attrType=GX_INDEX16,.cnt=variant<2 ? u8(3) : u8(9),.compType=GX_S8,.offset=off,.stride=9,.frac=6,.le=false,.nbt3=false}; off += 2;
        c.attrs[GX_VA_CLR0] = {.attrType=GX_INDEX8,.cnt=1,.compType=GX_RGBA8,.offset=off,.stride=4,.le=false}; off += 1;
        c.attrs[GX_VA_TEX0] = {.attrType=GX_INDEX16,.cnt=2,.compType=GX_S16,.offset=off,.stride=4,.frac=8,.le=(variant&2)!=0}; off += 2;
        c.attrs[GX_VA_TEX1] = {.attrType=GX_DIRECT,.cnt=1,.compType=GX_U8,.offset=off,.frac=7}; off += 1;
        c.vtxStride = off;
        std::array<u8, 64 * 12> pos{}, nrm{}, clr{}, tex{};
        for (unsigned i = 0; i < pos.size(); ++i) { pos[i] = (i * 41 + 3) % 256; nrm[i] = (i * 13 + 5) % 256; clr[i] = (i * 7 + 1) % 256; tex[i] = (i * 31 + 9) % 256; }
        arrays[GX_VA_POS] = {.data=pos.data(),.size=pos.size(),.stride=12};
        arrays[GX_VA_NRM] = {.data=nrm.data(),.size=nrm.size(),.stride=9};
        arrays[GX_VA_CLR0] = {.data=clr.data(),.size=clr.size(),.stride=4};
        arrays[GX_VA_TEX0] = {.data=tex.data(),.size=tex.size(),.stride=4};
        std::vector<u8> raw(c.vtxStride * 40);
        for (unsigned i = 0; i < 40; ++i) {
            u8* v = raw.data() + i * c.vtxStride;
            v[0] = u8((i * 3) % 30); v[1] = u8(i % 7);
            const unsigned posIndex = i < 36 ? i : 70 + i; // last records exceed the arrays
            v[2] = u8(posIndex >> 8); v[3] = u8(posIndex);
            v[4] = 0; v[5] = u8((i * 5) % 60);
            v[6] = u8(i * 6);
            v[7] = 0; v[8] = u8(i);
            v[9] = u8(i * 9);
        }
        check_packed(c, raw.data(), 40, arrays, 4, "specialized loader mixed-attribute equivalence");
        const auto& loader = flip_loader(c);
        check(loader.ok && loader.hasMatrices, "specialized loader accepted the mixed format");
        std::vector<u8> decoded(40 * loader.layout.stride, 0xee);
        flip_decode_with(loader, raw.data(), 40, decoded.data(), arrays, 4, (5u << 8) | (1u << 12));
        for (unsigned i = 0; i < 40; ++i) {
            uint32_t word; std::memcpy(&word, decoded.data() + i * loader.layout.stride + loader.matrixOffset + 8, 4);
            check(word == ((5u << 8) | (1u << 12)), "specialized loader writes the batch record word");
        }
    }
    std::puts("PASS Flip CPU vertex decoder");
}
