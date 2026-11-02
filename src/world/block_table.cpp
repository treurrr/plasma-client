#include "mcpp/world/block_table.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mcpp::world {
namespace {

constexpr char kMagic[4] = {'P', 'L', 'B', 'K'};
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderBytes = 32;

size_t align8(size_t value) { return (value + 7) & ~size_t{7}; }

} // namespace

BlockTable::~BlockTable() { close(); }

BlockTable::BlockTable(BlockTable&& other) noexcept { *this = std::move(other); }

BlockTable& BlockTable::operator=(BlockTable&& other) noexcept {
    if (this != &other) {
        close();
        std::memcpy(static_cast<void*>(this), static_cast<const void*>(&other), sizeof(BlockTable));
        other.base_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void BlockTable::close() {
    if (base_ != nullptr) {
        ::munmap(const_cast<uint8_t*>(base_), size_);
        base_ = nullptr;
        size_ = 0;
    }
}

bool BlockTable::open(const std::filesystem::path& path) {
    close();

    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    struct stat info{};
    if (::fstat(fd, &info) != 0 || info.st_size < static_cast<off_t>(kHeaderBytes)) {
        ::close(fd);
        return false;
    }

    void* mapping = ::mmap(nullptr, static_cast<size_t>(info.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapping == MAP_FAILED) return false;

    base_ = static_cast<const uint8_t*>(mapping);
    size_ = static_cast<size_t>(info.st_size);

    if (std::memcmp(base_, kMagic, sizeof(kMagic)) != 0) {
        close();
        throw ProtocolError(std::format("{} is not a block table", path.string()));
    }

    const auto* header = reinterpret_cast<const uint32_t*>(base_ + sizeof(kMagic));
    if (header[0] != kVersion) {
        const uint32_t found = header[0];
        close();
        throw ProtocolError(std::format("block table version {}, expected {}", found, kVersion));
    }

    protocol_ = static_cast<int32_t>(header[1]);
    state_count_ = header[2];
    quad_count_ = header[3];
    texture_count_ = header[4];
    block_count_ = header[5];
    string_bytes_ = header[6];

    size_t at = kHeaderBytes;
    const auto take = [&](size_t bytes) {
        const uint8_t* start = base_ + at;
        at = align8(at + bytes);
        return start;
    };

    quad_offsets_ = reinterpret_cast<const uint32_t*>(take((state_count_ + 1) * sizeof(uint32_t)));
    quads_ = reinterpret_cast<const BakedQuad*>(take(quad_count_ * sizeof(BakedQuad)));
    texture_names_ = reinterpret_cast<const uint32_t*>(take(texture_count_ * sizeof(uint32_t)));
    block_names_ = reinterpret_cast<const uint32_t*>(take(block_count_ * sizeof(uint32_t)));
    state_block_ = reinterpret_cast<const uint32_t*>(take(state_count_ * sizeof(uint32_t)));
    state_flags_ = take(state_count_);
    strings_ = reinterpret_cast<const char*>(take(string_bytes_));

    if (at > size_) {
        const size_t needed = at;
        close();
        throw ProtocolError(std::format("block table is {} bytes but describes {}", size_, needed));
    }
    return true;
}

std::span<const BakedQuad> BlockTable::quads(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= state_count_) return {};
    const uint32_t begin = quad_offsets_[state];
    const uint32_t end = quad_offsets_[state + 1];
    return {quads_ + begin, end - begin};
}

uint8_t BlockTable::flags(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= state_count_) return 0;
    return state_flags_[state];
}

std::string_view BlockTable::block_name(int32_t state) const {
    if (state < 0 || static_cast<size_t>(state) >= state_count_) return {};
    const uint32_t index = state_block_[state];
    if (index >= block_count_) return {};
    return strings_ + block_names_[index];
}

std::string_view BlockTable::texture_name(uint16_t texture) const {
    if (texture >= texture_count_) return {};
    return strings_ + texture_names_[texture];
}

std::filesystem::path BlockTable::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plasma-client" / "data" / std::format("blocks-{}.plbk", protocol);
}

} // namespace mcpp::world
