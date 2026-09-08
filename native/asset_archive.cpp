#include "asset_archive.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace melee {
namespace {
std::uint32_t be32(std::span<const std::byte> data, std::size_t at) {
    if (at > data.size() || data.size() - at < 4)
        throw std::runtime_error("Truncated HSD archive field");
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result = result * 256 + std::to_integer<unsigned>(data[at+i]);
    return result;
}
}
AssetArchive AssetArchive::read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open archive: " + path.string());
    auto size = file.tellg();
    if (size < 32 || size > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Invalid HSD archive size");
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        throw std::runtime_error("Truncated HSD archive read");
    return AssetArchive(std::move(data));
}
AssetArchive::AssetArchive(std::vector<std::byte> data) : storage_(std::move(data)) {
    const auto all = std::span<const std::byte>(storage_);
    if (all.size() < 32 || be32(all, 0) != all.size())
        throw std::runtime_error("HSD archive file size mismatch");
    data_size_ = be32(all, 4);
    const std::size_t reloc_count = be32(all, 8);
    const std::size_t public_count = be32(all, 12);
    const std::size_t extern_count = be32(all, 16);
    const std::size_t reloc_start = 32ULL + data_size_;
    const std::size_t public_start = reloc_start + reloc_count * 4ULL;
    const std::size_t extern_start = public_start + public_count * 8ULL;
    const std::size_t strings_start = extern_start + extern_count * 8ULL;
    if (strings_start > all.size()) throw std::runtime_error("HSD tables exceed file bounds");
    for (std::size_t i = 0; i < reloc_count; ++i) {
        auto field = be32(all, reloc_start + i * 4);
        auto target = u32(field);
        // Packed trophy records contain unaligned pointers. Sound archives
        // also store one-past-end pointers for empty regions.
        if (target > data_size_)
            throw std::runtime_error("Invalid HSD relocation");
        relocations_.push_back(field);
    }
    std::sort(relocations_.begin(), relocations_.end());
    if (std::adjacent_find(relocations_.begin(), relocations_.end()) != relocations_.end())
        throw std::runtime_error("Duplicate HSD relocation");
    auto symbol = [&](std::size_t entry) {
        const auto offset = be32(all, entry + 4);
        if (offset >= all.size() - strings_start)
            throw std::runtime_error("HSD symbol offset exceeds string table");
        auto begin = strings_start + offset;
        auto end = begin;
        while (end < all.size() && all[end] != std::byte{0}) ++end;
        if (end == all.size() || end == begin)
            throw std::runtime_error("Invalid HSD symbol name");
        return std::string(reinterpret_cast<const char*>(all.data()+begin), end-begin);
    };
    for (std::size_t i = 0; i < public_count; ++i) {
        auto entry = public_start + i * 8;
        auto target = be32(all, entry);
        if (target >= data_size_) throw std::runtime_error("HSD root outside data section");
        if (!roots_.emplace(symbol(entry), target).second)
            throw std::runtime_error("Duplicate HSD root name");
    }
    for (std::size_t i = 0; i < extern_count; ++i) {
        auto entry = extern_start + i * 8;
        auto name=symbol(entry);
        auto field = be32(all, entry);
        std::vector<std::uint32_t> visited;
        while (field != UINT32_MAX) {
            if (std::find(visited.begin(), visited.end(), field) != visited.end())
                throw std::runtime_error("Cyclic HSD external relocation chain");
            visited.push_back(field);
            if(!externals_.emplace(field,name).second || std::binary_search(relocations_.begin(),relocations_.end(),field))
                throw std::runtime_error("Conflicting HSD external relocation");
            field = u32(field);
        }
    }
}
std::span<const std::byte> AssetArchive::bytes(std::uint32_t offset, std::size_t size) const {
    if (offset > data_size_ || size > data_size_ - offset)
        throw std::runtime_error("HSD data access out of bounds");
    return std::span<const std::byte>(storage_).subspan(32ULL + offset, size);
}
std::uint16_t AssetArchive::u16(std::uint32_t offset) const {
    auto value = bytes(offset, 2);
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(value[0]) << 8) |
                                     std::to_integer<unsigned>(value[1]));
}
std::uint32_t AssetArchive::u32(std::uint32_t offset) const { return be32(bytes(offset, 4), 0); }
float AssetArchive::f32(std::uint32_t offset) const { return std::bit_cast<float>(u32(offset)); }
std::optional<std::uint32_t> AssetArchive::pointer(std::uint32_t field) const {
    if(external(field)) return std::nullopt;
    const auto value = u32(field);
    if (std::binary_search(relocations_.begin(), relocations_.end(), field)) return value;
    if (value) throw std::runtime_error("Unresolved or non-pointer HSD field at "+std::to_string(field)+" value "+std::to_string(value));
    return std::nullopt;
}
const std::string* AssetArchive::external(std::uint32_t field) const {
    auto found=externals_.find(field);
    return found==externals_.end()?nullptr:&found->second;
}
}
