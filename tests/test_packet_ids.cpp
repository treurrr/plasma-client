#include "mcpp/proto/packet_ids.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

template <typename Group>
std::vector<int32_t> ids_of(const Group& group) {
    static_assert(sizeof(Group) % sizeof(int32_t) == 0);
    std::vector<int32_t> ids(sizeof(Group) / sizeof(int32_t));
    std::memcpy(ids.data(), &group, sizeof(Group));
    return ids;
}

// The ids a version has in one state and direction are 0 to n-1, each once.
// Anything else means two fields were given the same packet, or one was lost.
size_t check_dense(const std::vector<int32_t>& all, std::string_view what) {
    std::vector<int32_t> present;
    for (const int32_t id : all) {
        if (id >= 0) present.push_back(id);
    }
    std::ranges::sort(present);
    bool dense = true;
    for (size_t i = 0; i < present.size(); ++i) dense = dense && present[i] == static_cast<int32_t>(i);
    check(dense, std::format("{}: ids run 0 to {} without a gap", what, present.size()));
    return present.size();
}

size_t present_packets(const mcpp::proto::PacketIds& table) {
    const auto name = [&](std::string_view group) {
        return std::format("{} {}", table.protocol, group);
    };
    return check_dense(ids_of(table.handshake_sb), name("handshake_sb")) +
           check_dense(ids_of(table.status_cb), name("status_cb")) +
           check_dense(ids_of(table.status_sb), name("status_sb")) +
           check_dense(ids_of(table.login_cb), name("login_cb")) +
           check_dense(ids_of(table.login_sb), name("login_sb")) +
           check_dense(ids_of(table.config_cb), name("config_cb")) +
           check_dense(ids_of(table.config_sb), name("config_sb")) +
           check_dense(ids_of(table.play_cb), name("play_cb")) +
           check_dense(ids_of(table.play_sb), name("play_sb"));
}

} // namespace

int main() {
    const auto* v774 = mcpp::proto::ids_for(774);
    const auto* v776 = mcpp::proto::ids_for(776);
    check(v774 != nullptr && v776 != nullptr, "both tables are present");
    if (v774 == nullptr || v776 == nullptr) return 1;

    // The counts in the reports: every packet of every state, not a chosen few.
    check(present_packets(*v774) == 251, "774 has all 251 packets of its report");
    check(present_packets(*v776) == 256, "776 has all 256 packets of its report");

    check(v774->handshake_sb.intention == 0 && v774->status_sb.status_request == 0,
          "handshake and status are in the table too");
    check(v774->play_cb.debug_block_value >= 0, "a debug/ packet gets a field with no slash");
    check(v774->play_sb.attack == -1 && v776->play_sb.attack >= 0,
          "a packet only 776 has is -1 in 774");

    if (failures == 0) {
        std::cout << "all packet id tests passed\n";
        return 0;
    }
    std::cerr << std::format("{} check(s) failed\n", failures);
    return 1;
}
