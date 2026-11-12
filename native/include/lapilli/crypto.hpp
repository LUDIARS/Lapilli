// Thin wrappers over OpenSSL 3 (no primitive is implemented here; randomness only from RAND_bytes).
// Binary values travel as std::string of raw bytes.
#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lapilli::crypto {

class CryptoError : public std::runtime_error {
public:
    explicit CryptoError(const std::string& what) : std::runtime_error(what) {}
};

/** SHA-256 digest: 32 raw bytes / 64 lower-case hex characters. Throws CryptoError. */
std::string sha256_raw(std::string_view data);
std::string sha256_hex(std::string_view data);
/** HMAC-SHA-256: 32 raw bytes / hex. Throws CryptoError. */
std::string hmac_sha256(std::string_view key, std::string_view data);
std::string hmac_sha256_hex(std::string_view key, std::string_view data);
/** `count` bytes from OpenSSL's CSPRNG. Throws CryptoError (never falls back to another source). */
std::string random_bytes(std::size_t count);
/** `count` random bytes as hex (2 * count characters). */
std::string random_hex(std::size_t count);
/** Constant-time comparison (CRYPTO_memcmp); different lengths compare unequal at once. */
bool constant_time_equal(std::string_view a, std::string_view b) noexcept;
/** Overwrites the buffer (OPENSSL_cleanse). */
void cleanse(std::string& secret) noexcept;

/** Lower-case hex; from_hex accepts either case, even length only. */
std::string to_hex(std::string_view raw);
std::optional<std::string> from_hex(std::string_view text);
/** RFC 4648 base64 with padding; the decoder is strict (alphabet only, canonical padding, no whitespace). */
std::string base64_encode(std::string_view raw);
std::optional<std::string> base64_decode(std::string_view text);
/** base64url without padding; the decoder is strict (no padding, canonical trailing bits). */
std::string base64url_encode(std::string_view raw);
std::optional<std::string> base64url_decode(std::string_view text);

} // namespace lapilli::crypto
