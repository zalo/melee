#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace melee {
// Disc offsets stay offsets. They never become truncated host pointers.
class AssetArchive {
public:
    explicit AssetArchive(std::vector<std::byte> bytes);
    static AssetArchive read(const std::filesystem::path& path);
    std::uint16_t u16(std::uint32_t offset) const;
    std::uint32_t u32(std::uint32_t offset) const;
    float f32(std::uint32_t offset) const;
    std::span<const std::byte> bytes(std::uint32_t offset, std::size_t size) const;
    // Relocation metadata distinguishes a pointer to data offset 0 from null.
    std::optional<std::uint32_t> pointer(std::uint32_t field) const;
    const std::string* external(std::uint32_t field) const;
    const std::map<std::string, std::uint32_t>& roots() const { return roots_; }
    std::uint32_t data_size() const { return data_size_; }
    std::size_t relocation_count() const { return relocations_.size(); }
private:
    std::vector<std::byte> storage_;
    std::uint32_t data_size_ = 0;
    std::vector<std::uint32_t> relocations_;
    std::map<std::string, std::uint32_t> roots_;
    std::map<std::uint32_t, std::string> externals_;
};
}
