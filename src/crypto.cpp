#include "mcpp/crypto.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace mcpp::crypto {

namespace {

[[noreturn]] void fail(const char* what) {
    throw std::runtime_error(std::format("{} failed: {}", what,
                                         ERR_reason_error_string(ERR_get_error())
                                             ? ERR_reason_error_string(ERR_get_error())
                                             : "unknown"));
}

} // namespace

std::vector<uint8_t> random_secret() {
    std::vector<uint8_t> secret(16);
    if (RAND_bytes(secret.data(), static_cast<int>(secret.size())) != 1) fail("RAND_bytes");
    return secret;
}

std::vector<uint8_t> rsa_encrypt(std::span<const uint8_t> der_public_key,
                                 std::span<const uint8_t> plain) {
    const unsigned char* pointer = der_public_key.data();
    EVP_PKEY* key = d2i_PUBKEY(nullptr, &pointer, static_cast<long>(der_public_key.size()));
    if (key == nullptr) fail("d2i_PUBKEY");

    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new(key, nullptr);
    if (context == nullptr) {
        EVP_PKEY_free(key);
        fail("EVP_PKEY_CTX_new");
    }

    std::vector<uint8_t> out;
    size_t size = 0;
    if (EVP_PKEY_encrypt_init(context) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(context, RSA_PKCS1_PADDING) <= 0 ||
        EVP_PKEY_encrypt(context, nullptr, &size, plain.data(), plain.size()) <= 0) {
        EVP_PKEY_CTX_free(context);
        EVP_PKEY_free(key);
        fail("EVP_PKEY_encrypt");
    }
    out.resize(size);
    if (EVP_PKEY_encrypt(context, out.data(), &size, plain.data(), plain.size()) <= 0) {
        EVP_PKEY_CTX_free(context);
        EVP_PKEY_free(key);
        fail("EVP_PKEY_encrypt");
    }
    out.resize(size);

    EVP_PKEY_CTX_free(context);
    EVP_PKEY_free(key);
    return out;
}

std::string server_hash(const std::string& server_id, std::span<const uint8_t> secret,
                        std::span<const uint8_t> der_public_key) {
    uint8_t digest[SHA_DIGEST_LENGTH];
    unsigned int digest_size = 0;
    EVP_MD_CTX* sha = EVP_MD_CTX_new();
    if (sha == nullptr) fail("EVP_MD_CTX_new");
    if (EVP_DigestInit_ex(sha, EVP_sha1(), nullptr) != 1 ||
        EVP_DigestUpdate(sha, server_id.data(), server_id.size()) != 1 ||
        EVP_DigestUpdate(sha, secret.data(), secret.size()) != 1 ||
        EVP_DigestUpdate(sha, der_public_key.data(), der_public_key.size()) != 1 ||
        EVP_DigestFinal_ex(sha, digest, &digest_size) != 1) {
        EVP_MD_CTX_free(sha);
        fail("EVP_Digest");
    }
    EVP_MD_CTX_free(sha);

    const bool negative = (digest[0] & 0x80) != 0;
    if (negative) {
        // Two's complement, by hand, because that is what the Java original
        // produces and the session server compares against.
        int carry = 1;
        for (int i = SHA_DIGEST_LENGTH - 1; i >= 0; --i) {
            const int value = (~digest[i] & 0xff) + carry;
            digest[i] = static_cast<uint8_t>(value & 0xff);
            carry = value >> 8;
        }
    }

    std::string hex;
    hex.reserve(SHA_DIGEST_LENGTH * 2 + 1);
    for (const uint8_t byte : digest) hex += std::format("{:02x}", byte);

    const size_t first = hex.find_first_not_of('0');
    hex = first == std::string::npos ? "0" : hex.substr(first);
    return negative ? "-" + hex : hex;
}

Stream::Stream(Stream&& other) noexcept
    : context_(other.context_), encrypting_(other.encrypting_) {
    other.context_ = nullptr;
}

Stream& Stream::operator=(Stream&& other) noexcept {
    if (this != &other) {
        if (context_ != nullptr) EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(context_));
        context_ = other.context_;
        encrypting_ = other.encrypting_;
        other.context_ = nullptr;
    }
    return *this;
}

Stream::~Stream() {
    if (context_ != nullptr) EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(context_));
}

void Stream::start(std::span<const uint8_t> secret, bool encrypting) {
    auto* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) fail("EVP_CIPHER_CTX_new");
    const int ok = encrypting
                       ? EVP_EncryptInit_ex(context, EVP_aes_128_cfb8(), nullptr, secret.data(),
                                            secret.data())
                       : EVP_DecryptInit_ex(context, EVP_aes_128_cfb8(), nullptr, secret.data(),
                                            secret.data());
    if (ok != 1) {
        EVP_CIPHER_CTX_free(context);
        fail("EVP_CipherInit");
    }
    EVP_CIPHER_CTX_set_padding(context, 0);
    context_ = context;
    encrypting_ = encrypting;
}

void Stream::apply(uint8_t* data, size_t size) {
    if (context_ == nullptr || size == 0) return;
    auto* context = static_cast<EVP_CIPHER_CTX*>(context_);
    int produced = 0;
    const int ok = encrypting_ ? EVP_EncryptUpdate(context, data, &produced, data,
                                                   static_cast<int>(size))
                               : EVP_DecryptUpdate(context, data, &produced, data,
                                                   static_cast<int>(size));
    if (ok != 1) fail("EVP_CipherUpdate");
}

} // namespace mcpp::crypto
