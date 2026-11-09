#include "lapilli/crypto.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace lapilli::crypto {

namespace {

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kB64Url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encode(std::string_view raw, const char* alphabet, bool pad) {
    std::string out;
    out.reserve((raw.size() + 2) / 3 * 4);
    const auto byte = [&](std::size_t at) { return static_cast<std::uint32_t>(static_cast<unsigned char>(raw[at])); };
    std::size_t i = 0;
    for (; i + 3 <= raw.size(); i += 3) {
        const std::uint32_t n = (byte(i) << 16) | (byte(i + 1) << 8) | byte(i + 2);
        out.push_back(alphabet[(n >> 18) & 63]);
        out.push_back(alphabet[(n >> 12) & 63]);
        out.push_back(alphabet[(n >> 6) & 63]);
        out.push_back(alphabet[n & 63]);
    }
    const std::size_t rest = raw.size() - i;
    if (rest == 1) {
        const std::uint32_t n = byte(i) << 16;
        out.push_back(alphabet[(n >> 18) & 63]);
        out.push_back(alphabet[(n >> 12) & 63]);
        if (pad) out += "==";
    } else if (rest == 2) {
        const std::uint32_t n = (byte(i) << 16) | (byte(i + 1) << 8);
        out.push_back(alphabet[(n >> 18) & 63]);
        out.push_back(alphabet[(n >> 12) & 63]);
        out.push_back(alphabet[(n >> 6) & 63]);
        if (pad) out.push_back('=');
    }
    return out;
}

std::optional<std::string> decode(std::string_view text, const char* alphabet) {
    std::array<int, 256> map{};
    map.fill(-1);
    for (int i = 0; i < 64; ++i) map[static_cast<unsigned char>(alphabet[i])] = i;
    if (text.size() % 4 == 1) return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3 + 2);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        const int v = map[static_cast<unsigned char>(c)];
        if (v < 0) return std::nullopt;
        buffer = ((buffer << 6) | static_cast<std::uint32_t>(v)) & 0xFFFFFFu;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

std::string sha256_raw(std::string_view data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1) throw CryptoError("sha256 failed");
    return std::string(reinterpret_cast<const char*>(digest.data()), length);
}

std::string sha256_hex(std::string_view data) { return to_hex(sha256_raw(data)); }

std::string hmac_sha256(std::string_view key, std::string_view data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> mac{};
    std::size_t length = 0;
    // An empty key is valid HMAC input; OpenSSL wants a non-null pointer for it.
    static const unsigned char empty = 0;
    const auto* key_bytes = key.empty() ? &empty : reinterpret_cast<const unsigned char*>(key.data());
    if (EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr, key_bytes, key.size(), reinterpret_cast<const unsigned char*>(data.data()),
                  data.size(), mac.data(), mac.size(), &length) == nullptr) {
        throw CryptoError("hmac-sha256 failed");
    }
    return std::string(reinterpret_cast<const char*>(mac.data()), length);
}

std::string hmac_sha256_hex(std::string_view key, std::string_view data) { return to_hex(hmac_sha256(key, data)); }

std::string random_bytes(std::size_t count) {
    std::string out(count, '\0');
    std::size_t done = 0;
    while (done < count) {
        const std::size_t chunk = std::min<std::size_t>(count - done, static_cast<std::size_t>(INT_MAX));
        if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data() + done), static_cast<int>(chunk)) != 1) {
            cleanse(out);
            throw CryptoError("the random generator failed");
        }
        done += chunk;
    }
    return out;
}

std::string random_hex(std::size_t count) { return to_hex(random_bytes(count)); }

bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

void cleanse(std::string& secret) noexcept {
    if (!secret.empty()) OPENSSL_cleanse(secret.data(), secret.size());
}

std::string to_hex(std::string_view raw) {
    static constexpr const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0xF]);
    }
    return out;
}

std::optional<std::string> from_hex(std::string_view text) {
    if (text.size() % 2 != 0) return std::nullopt;
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = value(text[i]);
        const int lo = value(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
    return out;
}

std::string base64_encode(std::string_view raw) { return encode(raw, kB64, true); }
std::string base64url_encode(std::string_view raw) { return encode(raw, kB64Url, false); }

std::optional<std::string> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::string_view body = text;
    std::size_t padding = 0;
    while (!body.empty() && body.back() == '=' && padding < 2) {
        body.remove_suffix(1);
        ++padding;
    }
    auto decoded = decode(body, kB64);
    if (!decoded || base64_encode(*decoded) != text) return std::nullopt;
    return decoded;
}

std::optional<std::string> base64url_decode(std::string_view text) {
    auto decoded = decode(text, kB64Url);
    if (!decoded || base64url_encode(*decoded) != text) return std::nullopt;
    return decoded;
}

} // namespace lapilli::crypto
