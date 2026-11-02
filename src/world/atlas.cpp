#include "mcpp/world/atlas.hpp"

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

constexpr char kMagic[4] = {'C', 'R', 'A', 'T'};
constexpr uint32_t kVersion = 3;
constexpr size_t kHeaderBytes = 32;

} // namespace

Atlas::~Atlas() { close(); }

void Atlas::close() {
    if (base_ != nullptr) {
        ::munmap(const_cast<uint8_t*>(base_), size_);
        base_ = nullptr;
        size_ = 0;
    }
}

bool Atlas::open(const std::filesystem::path& path) {
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
        throw ProtocolError(std::format("{} is geen atlas", path.string()));
    }

    const auto* header = reinterpret_cast<const uint32_t*>(base_ + sizeof(kMagic));
    if (header[0] != kVersion) {
        const uint32_t found = header[0];
        close();
        throw ProtocolError(std::format("atlas versie {}, verwacht {}", found, kVersion));
    }

    protocol_ = static_cast<int32_t>(header[1]);
    tile_size_ = header[2];
    layers_ = header[3];
    mip_levels_ = header[4];
    pixel_bytes_ = header[5];

    pixels_ = base_ + kHeaderBytes;

    size_t expected = 0;
    for (uint32_t mip = 0; mip < mip_levels_; ++mip) expected += level_bytes(mip);
    if (pixel_bytes_ != expected || kHeaderBytes + pixel_bytes_ > size_) {
        close();
        throw ProtocolError("atlas-pixels passen niet bij de afmetingen");
    }
    return true;
}

size_t Atlas::level_bytes(uint32_t mip) const {
    const size_t side = tile_size_ >> mip;
    return side * side * 4 * layers_;
}

size_t Atlas::level_offset(uint32_t mip) const {
    size_t offset = 0;
    for (uint32_t i = 0; i < mip; ++i) offset += level_bytes(i);
    return offset;
}

std::filesystem::path Atlas::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "crit-client" / "data" / std::format("atlas-{}.crat", protocol);
}

} // namespace mcpp::world
