#include "asset_archive.hpp"
#include <cstdio>
#include <stdexcept>

static void check(bool ok) { if (!ok) throw std::runtime_error("Archive test failed"); }
static void put(std::vector<std::byte>& b, std::size_t at, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) { b[at+i] = std::byte(v & 255); v >>= 8; }
}
int main(int argc, char** argv) {
    try {
        // data: valid offset-zero pointer, null, float 1.0, big-endian u16 pair.
        std::vector<std::byte> raw(65);
        put(raw,0,65); put(raw,4,16); put(raw,8,1); put(raw,12,1);
        put(raw,40,0x3f800000); put(raw,44,0x12345678);
        put(raw,48,0); put(raw,52,0); put(raw,56,0);
        raw[60]=std::byte{'r'}; raw[61]=std::byte{'o'}; raw[62]=std::byte{'o'}; raw[63]=std::byte{'t'};
        melee::AssetArchive a(raw);
        check(a.roots().at("root") == 0);
        check(a.pointer(0).has_value() && *a.pointer(0) == 0);
        check(!a.pointer(4).has_value());
        check(a.f32(8) == 1.0f && a.u16(12) == 0x1234);
        auto rejects = [](const std::vector<std::byte>& bytes) {
            try { melee::AssetArchive invalid(bytes); } catch (const std::runtime_error&) { return; }
            throw std::runtime_error("Malformed archive was accepted");
        };
        auto bad = raw; put(bad,4,0xffffffff); rejects(bad);
        bad = raw; put(bad,48,16); rejects(bad);
        bad = raw; put(bad,32,17); rejects(bad);
        auto end_pointer = raw; put(end_pointer,32,16);
        check(melee::AssetArchive(end_pointer).pointer(0) == 16);
        auto packed = raw; put(packed,48,2);
        check(melee::AssetArchive(packed).pointer(2) == 0);
        bad = raw; bad[64]=std::byte{'x'}; rejects(bad);
        bool rejected = false;
        try { (void)a.u32(14); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected);
        for (int i = 1; i < argc; ++i) {
            auto asset = melee::AssetArchive::read(argv[i]);
            check(!asset.roots().empty());
            std::printf("PASS: %s: %u data bytes, %zu relocations, %zu roots\n", argv[i],
                asset.data_size(), asset.relocation_count(), asset.roots().size());
        }
        std::puts("PASS: big-endian scalars, null vs offset-zero pointers, malformed archive bounds");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
