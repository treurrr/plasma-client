#include "mcpp/session.hpp"

namespace mcpp {

void Session::read_tags(ByteBuffer& in) {
    const int32_t registries = in.read_varint();
    for (int32_t r = 0; r < registries; ++r) {
        std::string registry = in.read_string();
        Tags::Set set;
        const int32_t count = in.read_varint();
        for (int32_t t = 0; t < count; ++t) {
            std::string tag = in.read_string();
            const int32_t size = in.read_varint();
            if (size < 0) fail("negative tag size");
            std::vector<int32_t> ids(static_cast<size_t>(size));
            for (auto& id : ids) id = in.read_varint();
            set.emplace(std::move(tag), std::move(ids));
        }
        data_.tags.set(std::move(registry), std::move(set));
    }
}

bool Session::handle_data(PacketConnection::Packet& packet) {
    const auto& cb = ids_->play_cb;
    auto& in = packet.data;

    if (packet.id == cb.change_difficulty) {
        data_.difficulty = in.read_u8();
        data_.difficulty_locked = in.read_bool();
        return true;
    }
    if (packet.id == cb.update_tags) {
        read_tags(in);
        return true;
    }
    return false;
}

} // namespace mcpp
