#include "lapilli/http/address_policy.hpp"

#include <boost/asio/ip/address.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace lapilli::http {

namespace {

bool public_v4(std::uint32_t ip) {
    struct Range {
        std::uint32_t net;
        int bits;
    };
    static constexpr std::array<Range, 15> blocked = {{
        {0x00000000u, 8},  // "this" network
        {0x0A000000u, 8},  // private
        {0x64400000u, 10}, // CGNAT
        {0x7F000000u, 8},  // loopback
        {0xA9FE0000u, 16}, // link-local
        {0xAC100000u, 12}, // private
        {0xC0000000u, 24}, // IETF protocol assignments
        {0xC0000200u, 24}, // TEST-NET-1
        {0xC0586300u, 24}, // 6to4 relay anycast (deprecated)
        {0xC0A80000u, 16}, // private
        {0xC6120000u, 15}, // benchmarking
        {0xC6336400u, 24}, // TEST-NET-2
        {0xCB007100u, 24}, // TEST-NET-3
        {0xE0000000u, 4},  // multicast
        {0xF0000000u, 4},  // reserved, broadcast
    }};
    for (const auto& range : blocked) {
        const std::uint32_t mask = 0xFFFFFFFFu << (32 - range.bits);
        if ((ip & mask) == (range.net & mask)) return false;
    }
    return true;
}

std::uint32_t v4_of(const std::array<unsigned char, 16>& b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8) | static_cast<std::uint32_t>(b[at + 3]);
}

bool public_v6(const std::array<unsigned char, 16>& b) {
    bool first_ten_zero = true;
    for (std::size_t i = 0; i < 10; ++i) first_ten_zero = first_ten_zero && b[i] == 0;
    if (first_ten_zero && b[10] == 0xFF && b[11] == 0xFF) return public_v4(v4_of(b, 12)); // ::ffff:a.b.c.d
    if (first_ten_zero && b[10] == 0 && b[11] == 0) return false; // ::, ::1, ::a.b.c.d (deprecated)
    if ((b[0] & 0xFE) == 0xFC) return false;                       // fc00::/7 ULA
    if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return false;       // fe80::/10 link-local
    if (b[0] == 0xFE && (b[1] & 0xC0) == 0xC0) return false;       // fec0::/10 site-local
    if (b[0] == 0xFF) return false;                                // ff00::/8 multicast
    if (b[0] == 0x01 && b[1] == 0x00 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 0) return false; // 100::/64 discard
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0D && b[3] == 0xB8) return false; // 2001:db8::/32 documentation
    if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xFF && b[3] == 0x9B) {
        bool zero = true;
        for (std::size_t i = 4; i < 12; ++i) zero = zero && b[i] == 0;
        if (zero) return public_v4(v4_of(b, 12)); // 64:ff9b::/96 NAT64
        return false;                            // 64:ff9b:1::/48 local-use NAT64
    }
    if (b[0] == 0x20 && b[1] == 0x02) return public_v4(v4_of(b, 2)); // 2002::/16 6to4
    return true;
}

} // namespace

bool is_public_address(std::string_view address) {
    std::string text(address);
    if (text.size() > 1 && text.front() == '[' && text.back() == ']') text = text.substr(1, text.size() - 2);
    boost::system::error_code error;
    const auto parsed = boost::asio::ip::make_address(text, error);
    if (error) return false;
    if (parsed.is_v4()) return public_v4(parsed.to_v4().to_uint());
    const auto v6 = parsed.to_v6();
    if (v6.scope_id() != 0) return false; // zone ids only exist on link-local scopes
    return public_v6(v6.to_bytes());
}

} // namespace lapilli::http
