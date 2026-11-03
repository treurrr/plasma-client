#pragma once
#include "mcpp/json.hpp"

#include <cstdint>
#include <string>

// The status ping: what a server says about itself before anyone logs in, which
// is everything the multiplayer list shows. It needs no account and no
// version of its own, so it lives apart from Session.
namespace mcpp::status {

struct Request {
    std::string host;
    uint16_t port = 25565;
    // -1 = unknown: ViaVersion mirrors back whatever we claim, which would
    // hide the server's real version.
    int32_t protocol = -1;
    int timeout_ms = 5000;
    // The ping after the status costs one more round trip.
    bool measure_latency = true;
};

struct Reply {
    std::string raw;   // the status JSON as the server sent it
    json::Value root;
    std::string version = "?";
    int32_t protocol = -1;
    int32_t online = -1;
    int32_t max = -1;
    // The description flattened to plain text. It often has two lines.
    std::string motd;
    // -1 when it was not measured or the server did not answer the ping.
    double latency_ms = -1.0;
};

// Throws on a connection that fails or a server that answers something else.
Reply query(const Request& request);

} // namespace mcpp::status
