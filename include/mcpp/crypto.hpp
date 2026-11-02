#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mcpp::crypto {

// Sixteen random bytes. This is the whole session key: Minecraft uses it as
// both the AES key and the initialisation vector.
std::vector<uint8_t> random_secret();

// RSA PKCS#1 v1.5 against the server's key, which arrives as a DER-encoded
// SubjectPublicKeyInfo in the encryption request.
std::vector<uint8_t> rsa_encrypt(std::span<const uint8_t> der_public_key,
                                 std::span<const uint8_t> plain);

// The string Mojang's session server expects. It is a SHA-1 over the server id,
// the shared secret and the public key, printed as a signed two's-complement
// hex number, which is a quirk of the original Java implementation rather than
// anything standard.
std::string server_hash(const std::string& server_id, std::span<const uint8_t> secret,
                        std::span<const uint8_t> der_public_key);

// AES-128 in CFB8, which is a stream cipher: one byte in, one byte out, state
// carried between calls. Both directions use the secret as key and as IV.
class Stream {
public:
    Stream() = default;
    ~Stream();

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&& other) noexcept;
    Stream& operator=(Stream&& other) noexcept;

    void start(std::span<const uint8_t> secret, bool encrypting);
    bool active() const { return context_ != nullptr; }
    void apply(uint8_t* data, size_t size);

private:
    void* context_ = nullptr;
    bool encrypting_ = false;
};

} // namespace mcpp::crypto
