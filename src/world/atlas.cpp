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

constexpr char kMagic[4] = {'P', 'L', 'A', 'T'};
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderBytes = 32;

size_t align8(size_t value) { return (value + 7) & ~size_t{7}; }

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
    width_ = header[2];
    height_ = header[3];
    tile_size_ = header[4];
    texture_count_ = header[5];
    pixel_bytes_ = header[6];

    const size_t rects = align8(static_cast<size_t>(texture_count_) * 4 * sizeof(uint16_t));
    pixels_ = base_ + kHeaderBytes + rects;

    if (kHeaderBytes + rects + pixel_bytes_ > size_) {
        close();
        throw ProtocolError("atlas is korter dan de header beschrijft");
    }
    if (pixel_bytes_ != static_cast<size_t>(width_) * height_ * 4) {
        close();
        throw ProtocolError("atlas-pixels passen niet bij de afmetingen");
    }
    return true;
}

std::filesystem::path Atlas::default_path(int32_t protocol) {
    std::filesystem::path base;
    if (const char* data_home = std::getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    }
    return base / "plodder-client" / "data" / std::format("atlas-{}.plat", protocol);
}

} // namespace mcpp::world
