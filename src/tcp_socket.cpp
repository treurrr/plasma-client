#include <poll.h>
#include "mcpp/tcp_socket.hpp"

#include "mcpp/byte_buffer.hpp"

#include <cerrno>
#include <cstring>
#include <format>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace mcpp {

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void TcpSocket::connect(const std::string& host, uint16_t port, int timeout_ms) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const std::string service = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
    if (rc != 0) {
        throw ProtocolError(std::format("cannot resolve {}: {}", host, gai_strerror(rc)));
    }

    int last_errno = 0;
    for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        int fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            last_errno = errno;
            continue;
        }
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            fd_ = fd;
            break;
        }
        last_errno = errno;
        ::close(fd);
    }
    freeaddrinfo(result);

    if (fd_ < 0) {
        throw ProtocolError(
            std::format("cannot connect to {}:{}: {}", host, port, std::strerror(last_errno)));
    }

    int one = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

void TcpSocket::set_receive_timeout(int timeout_ms) {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void TcpSocket::send_all(std::span<const uint8_t> data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            throw ProtocolError(std::format("send failed: {}", std::strerror(errno)));
        }
        sent += static_cast<size_t>(n);
    }
}

void TcpSocket::recv_exact(uint8_t* dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::recv(fd_, dst + got, n - got, 0);
        if (r == 0) throw ProtocolError("connection closed by server");
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) throw TimeoutError("recv timed out");
            throw ProtocolError(std::format("recv failed: {}", std::strerror(errno)));
        }
        got += static_cast<size_t>(r);
    }
}

bool TcpSocket::readable() const {
    if (fd_ < 0) return false;
    pollfd entry{fd_, POLLIN, 0};
    return ::poll(&entry, 1, 0) > 0 && (entry.revents & POLLIN) != 0;
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace mcpp
