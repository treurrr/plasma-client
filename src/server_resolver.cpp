#include "mcpp/server_resolver.hpp"

#include <arpa/nameser.h>
#include <netdb.h>
#include <resolv.h>

namespace mcpp {
namespace {

// `ServerAddress.parsePort`: a port that will not parse falls back to 25565.
uint16_t port_or_default(unsigned value) {
    return value == 0 || value > 65535 ? 25565 : static_cast<uint16_t>(value);
}

} // namespace

// `ServerRedirectHandler.createDnsSrvRedirectHandler`: only a host without a port
// is looked up, the query is `_minecraft._tcp.<host>` for SRV, and the first
// record answered is the one taken - JNDI hands vanilla the attribute's first
// value, not the lowest priority. Any failure means no redirect, silently.
std::optional<ServerAddress> lookup_srv_redirect(const ServerAddress& address) {
    if (address.port != 25565) return std::nullopt;

    const std::string query = "_minecraft._tcp." + address.host;
    unsigned char answer[NS_PACKETSZ];
    const int length = res_search(query.c_str(), ns_c_in, ns_t_srv, answer, sizeof(answer));
    if (length < 0) return std::nullopt;

    ns_msg message;
    if (ns_initparse(answer, length, &message) < 0) return std::nullopt;

    const int count = ns_msg_count(message, ns_s_an);
    for (int index = 0; index < count; ++index) {
        ns_rr record;
        if (ns_parserr(&message, ns_s_an, index, &record) < 0) continue;
        if (ns_rr_type(record) != ns_t_srv || ns_rr_rdlen(record) < 7) continue;

        const unsigned char* data = ns_rr_rdata(record);
        const uint16_t port = port_or_default(ns_get16(data + 4));
        char target[NS_MAXDNAME];
        if (ns_name_uncompress(ns_msg_base(message), ns_msg_end(message), data + 6, target,
                               sizeof(target)) < 0) {
            continue;
        }
        if (target[0] == '\0') continue;
        return ServerAddress{target, port};
    }
    return std::nullopt;
}

// `ServerNameResolver.resolveAddress`: the redirect is looked up whenever the
// address itself is allowed, and replaces it when it exists.
ServerAddress resolve_server_address(const ServerAddress& address) {
    if (auto redirect = lookup_srv_redirect(address)) return *redirect;
    return address;
}

} // namespace mcpp
