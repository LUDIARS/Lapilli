// A WHATWG URL subset: what `new URL(...)` does for the http(s) / ws(s) / file URLs the services
// handle (scheme, credentials, host with IPv4 / IPv6 canonicalisation, port, dot segments, percent
// encoding of path / query / fragment), relative resolution, origin, and the URLSearchParams /
// encodeURIComponent helpers. Non-ASCII (IDNA) host names are rejected instead of being converted to
// punycode. Non-special schemes keep everything after the scheme opaque.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lapilli::url {

struct Url {
    /** With the colon: "https:". */
    std::string protocol;
    std::string username;
    std::string password;
    /** Lower case; IPv4 canonical ("0x7f.1" -> "127.0.0.1"); IPv6 compressed in brackets ("[::1]"). */
    std::string hostname;
    /** Empty when absent or the scheme's default. */
    std::string port;
    /** "/" at least for special schemes. */
    std::string pathname;
    /** "" or "?...". */
    std::string search;
    /** "" or "#...". */
    std::string hash;
    bool special = false;
    /** Non-special URLs: everything after the scheme. */
    std::string opaque;

    [[nodiscard]] std::string host() const { return port.empty() ? hostname : hostname + ":" + port; }
    /** URL#origin ("null" for file: and non-special schemes). */
    [[nodiscard]] std::string origin() const;
    [[nodiscard]] std::string href() const;
    /** Host for DNS / connecting (IPv6 without brackets). */
    [[nodiscard]] std::string bare_hostname() const;
    /** The port number, or the scheme's default (0 when none). */
    [[nodiscard]] std::uint16_t effective_port() const;
    /** new URL(reference, this). */
    [[nodiscard]] std::optional<Url> resolve(std::string_view reference) const;
};

/** new URL(text), or nullopt where it throws. */
std::optional<Url> parse(std::string_view text);
/** new URL(reference, base): absolute URLs, "//host/x", "/abs", "rel/x", "../x", "?q", "#f". */
std::optional<Url> resolve(std::string_view reference, const Url& base);
bool is_special_scheme(std::string_view protocol) noexcept;
/** "80" / "443" / "21" / "" for the protocol (with its colon). */
std::string default_port(std::string_view protocol);

/** Percent-decoding of every valid %XX (invalid escapes are kept as they are; bytes are not validated). */
std::string percent_decode(std::string_view text);
/** encodeURIComponent (UTF-8; lone surrogates cannot occur in valid UTF-8, invalid input is sanitized first). */
std::string encode_uri_component(std::string_view text);
/** decodeURIComponent, or nullopt where it throws URIError (bad escape or invalid UTF-8). */
std::optional<std::string> decode_uri_component(std::string_view text);

using QueryList = std::vector<std::pair<std::string, std::string>>;
/** new URLSearchParams(query) (a leading '?' is skipped; '+' is a space; invalid UTF-8 becomes U+FFFD). */
QueryList parse_query(std::string_view query);
/** URLSearchParams#toString (application/x-www-form-urlencoded; no leading '?'). */
std::string serialize_query(const QueryList& params);
/** URLSearchParams#get: the first value of `name`. */
std::optional<std::string> query_get(const QueryList& params, std::string_view name);
/** URLSearchParams#getAll. */
std::vector<std::string> query_get_all(const QueryList& params, std::string_view name);

/** A strict dotted-decimal IPv4 address ("1.2.3.4", no leading zeros / shorthand), as 32 bits. */
std::optional<std::uint32_t> parse_ipv4(std::string_view text);
/** The WHATWG host parser's IPv4 forms ("0x7f.1", "2130706433"), canonical dotted text. */
std::optional<std::string> parse_ipv4_host(std::string_view host);
/** An IPv6 address (without brackets), canonical compressed text with brackets. */
std::optional<std::string> parse_ipv6_host(std::string_view address);

} // namespace lapilli::url
