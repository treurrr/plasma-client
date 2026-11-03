#pragma once
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <optional>

namespace mcpp::gpu {

// First fit over a buffer counted in elements. A freed span is merged into
// whatever free span touches it on either side, so a world that loads and
// unloads chunks for hours does not wear its buffer down to dust.
class SpanAllocator {
public:
    void reset(size_t capacity) {
        free_.clear();
        if (capacity != 0) free_[0] = capacity;
        capacity_ = capacity;
        used_ = 0;
    }

    std::optional<size_t> allocate(size_t count) {
        if (count == 0) return 0;
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            if (it->second < count) continue;
            const size_t offset = it->first;
            const size_t left = it->second - count;
            free_.erase(it);
            if (left != 0) free_[offset + count] = left;
            used_ += count;
            return offset;
        }
        return std::nullopt;
    }

    void release(size_t offset, size_t count) {
        if (count == 0) return;
        used_ -= std::min(used_, count);
        auto next = free_.lower_bound(offset);
        if (next != free_.end() && offset + count == next->first) {
            count += next->second;
            next = free_.erase(next);
        }
        if (next != free_.begin()) {
            const auto previous = std::prev(next);
            if (previous->first + previous->second == offset) {
                previous->second += count;
                return;
            }
        }
        free_[offset] = count;
    }

    size_t used() const { return used_; }
    size_t capacity() const { return capacity_; }
    size_t spans() const { return free_.size(); }
    size_t largest_free() const {
        size_t largest = 0;
        for (const auto& [offset, count] : free_) largest = std::max(largest, count);
        return largest;
    }

private:
    std::map<size_t, size_t> free_;   // offset -> length, never touching
    size_t capacity_ = 0;
    size_t used_ = 0;
};

} // namespace mcpp::gpu
