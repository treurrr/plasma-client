#include "mcpp/session.hpp"

namespace mcpp {

// Nothing yet: the sound packets are ignored until there is a mixer to give them to.
bool Session::handle_sound(PacketConnection::Packet&) {
    return false;
}

} // namespace mcpp
