#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace mcpp {

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    void connect(const std::string& host, uint16_t port, int timeout_ms = 5000);
    void set_receive_timeout(int timeout_ms);
    void send_all(std::span<const uint8_t> data);
    void recv_exact(uint8_t* dst, size_t n);
    // True when a read would not block, so a render loop can drain the
    // socket without paying a timeout for an empty one.
    bool readable() const;
    void close();
    bool valid() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

} // namespace mcpp
