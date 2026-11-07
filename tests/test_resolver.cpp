#include "mcpp/server_resolver.hpp"

#include <cstdint>
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

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void put_name(std::vector<uint8_t>& out, std::string_view name) {
    size_t at = 0;
    while (at < name.size()) {
        const size_t dot = name.find('.', at);
        const size_t end = dot == std::string_view::npos ? name.size() : dot;
        out.push_back(static_cast<uint8_t>(end - at));
        out.insert(out.end(), name.begin() + at, name.begin() + end);
        at = end + 1;
        if (dot == std::string_view::npos) break;
    }
    out.push_back(0);
}

// A DNS answer the way a resolver hands one back: a header, the question that
// was asked, then one record per target.
struct Srv {
    uint16_t priority;
    uint16_t weight;
    uint16_t port;
    std::string_view target;
};

std::vector<uint8_t> answer_with(const std::vector<Srv>& records, bool srv_type = true) {
    std::vector<uint8_t> out;
    put16(out, 0x1234);
    put16(out, 0x8180);
    put16(out, 1);
    put16(out, static_cast<uint16_t>(records.size()));
    put16(out, 0);
    put16(out, 0);
    put_name(out, "_minecraft._tcp.example.com");
    put16(out, 33);
    put16(out, 1);
    for (const Srv& record : records) {
        put_name(out, "_minecraft._tcp.example.com");
        put16(out, srv_type ? 33 : 1);
        put16(out, 1);
        put16(out, 0);
        put16(out, 300);
        std::vector<uint8_t> rdata;
        put16(rdata, record.priority);
        put16(rdata, record.weight);
        put16(rdata, record.port);
        put_name(rdata, record.target);
        put16(out, static_cast<uint16_t>(rdata.size()));
        out.insert(out.end(), rdata.begin(), rdata.end());
    }
    return out;
}

std::optional<mcpp::ServerAddress> parse(const std::vector<uint8_t>& packet) {
    return mcpp::first_srv_in_answer(packet.data(), static_cast<int>(packet.size()));
}

void test_single_record() {
    const auto found = parse(answer_with({{0, 1, 25434, "nl1-prem06.ferox.host"}}));
    check(found.has_value(), "a single SRV record is found");
    if (found) {
        check(found->host == "nl1-prem06.ferox.host",
              std::format("host is {}", found->host));
        check(found->port == 25434, std::format("port is {}", found->port));
    }
}

// JNDI gives vanilla the attribute's first value, so the first record answered
// wins even when a later one has a lower priority.
void test_first_record_wins() {
    const auto found = parse(answer_with({{20, 1, 1111, "second.example.com"},
                                          {0, 1, 2222, "lowest.example.com"}}));
    check(found.has_value(), "the first of several records is found");
    if (found) {
        check(found->host == "second.example.com",
              std::format("the first record wins, got {}", found->host));
        check(found->port == 1111, std::format("port is {}", found->port));
    }
}

// `ServerAddress.parsePort`: a port that will not parse is 25565.
void test_port_zero_falls_back() {
    const auto found = parse(answer_with({{0, 1, 0, "noport.example.com"}}));
    check(found.has_value() && found->port == 25565, "port 0 falls back to 25565");
}

void test_non_srv_answer_is_ignored() {
    check(!parse(answer_with({{0, 1, 25565, "a.example.com"}}, false)).has_value(),
          "an answer of another record type gives no redirect");
}

void test_malformed_answers() {
    check(!parse({}).has_value(), "an empty answer gives no redirect");
    check(!mcpp::first_srv_in_answer(nullptr, 0).has_value(), "no answer gives no redirect");
    auto truncated = answer_with({{0, 1, 25565, "a.example.com"}});
    truncated.resize(truncated.size() / 2);
    check(!parse(truncated).has_value(), "a truncated answer gives no redirect");
    std::vector<uint8_t> noise(40, 0xFF);
    check(!parse(noise).has_value(), "noise gives no redirect");
}

// `ServerNameResolver.resolveAddress`: a host the player gave a port for is never
// redirected, so our own test server at 127.0.0.1:25601 is reached directly.
void test_explicit_port_is_never_redirected() {
    check(!mcpp::lookup_srv_redirect({"example.com", 25601}).has_value(),
          "a host with an explicit port is not looked up");
}

} // namespace

int main() {
    test_single_record();
    test_first_record_wins();
    test_port_zero_falls_back();
    test_non_srv_answer_is_ignored();
    test_malformed_answers();
    test_explicit_port_is_never_redirected();
    if (failures == 0) std::cout << "resolver ok\n";
    return failures == 0 ? 0 : 1;
}
