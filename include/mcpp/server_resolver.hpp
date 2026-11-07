#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mcpp {

// `ServerAddress`: a host and a port, the port 25565 when the player gave none.
struct ServerAddress {
    std::string host;
    uint16_t port = 25565;
};

// `ServerRedirectHandler.createDnsSrvRedirectHandler`: the first SRV record in a
// DNS answer. JNDI hands vanilla the attribute's first value rather than the
// lowest priority, so the first record answered is the one that counts.
std::optional<ServerAddress> first_srv_in_answer(const unsigned char* answer, int length);

// `ServerRedirectHandler.createDnsSrvRedirectHandler`: the `_minecraft._tcp` SRV
// record of a host the player gave no port for, if it has one.
std::optional<ServerAddress> lookup_srv_redirect(const ServerAddress& address);

// `ServerNameResolver.resolveAddress`: the SRV target when there is one, else the
// address itself. Vanilla lets the redirect win even when the host resolves on its
// own, so a server whose name points one way and whose SRV record points another
// is reached the way the game reaches it.
ServerAddress resolve_server_address(const ServerAddress& address);

} // namespace mcpp
