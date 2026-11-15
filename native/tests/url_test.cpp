// lapilli::url against new URL / encodeURIComponent / URLSearchParams in Node v22.
#include "lapilli/test/check.hpp"
#include "lapilli/url.hpp"

using namespace lapilli::url;

namespace {
/** href | origin | hostname | port | pathname | search | hash, or THROWS (as the Node script printed). */
std::string show(const std::optional<Url>& u) {
    if (!u) return "THROWS";
    return u->href() + " | " + u->origin() + " | " + u->hostname + " | " + u->port + " | " + (u->special ? u->pathname : u->opaque) + " | " +
           u->search + " | " + u->hash;
}
} // namespace

LAPILLI_TEST("new URL(text) parts") {
    const std::pair<const char*, const char*> cases[] = {
        {"HTTPS://User:Pw@Example.COM:443/a/./b/../c?x=1#f", "https://User:Pw@example.com/a/c?x=1#f | https://example.com | example.com |  | /a/c | ?x=1 | #f"},
        {"http://0x7f.1/", "http://127.0.0.1/ | http://127.0.0.1 | 127.0.0.1 |  | / |  | "},
        {"http://2130706433:8080", "http://127.0.0.1:8080/ | http://127.0.0.1:8080 | 127.0.0.1 | 8080 | / |  | "},
        {"http://[0:0::1]/", "http://[::1]/ | http://[::1] | [::1] |  | / |  | "},
        {"http://a b/", "THROWS"},
        {"http://host:99999", "THROWS"},
        {"http://", "THROWS"},
        {"not a url", "THROWS"},
        {"http://ex.com/a b?c d#e f", "http://ex.com/a%20b?c%20d#e%20f | http://ex.com | ex.com |  | /a%20b | ?c%20d | #e%20f"},
        {"http://ex.com/%7e?q=\"x\"", "http://ex.com/%7e?q=%22x%22 | http://ex.com | ex.com |  | /%7e | ?q=%22x%22 | "},
        {"ws://h:80/x", "ws://h/x | ws://h | h |  | /x |  | "},
        {"http://ex.com\\path\\x", "http://ex.com/path/x | http://ex.com | ex.com |  | /path/x |  | "},
    };
    for (const auto& [input, expected] : cases) LAPILLI_CHECK_EQ(show(parse(input)), std::string(expected));
    const auto mail = parse("mailto:a@b.c");
    LAPILLI_CHECK(mail && !mail->special && mail->href() == "mailto:a@b.c" && mail->origin() == "null");
    LAPILLI_CHECK_EQ(parse("https://[::1]:8443/")->bare_hostname(), std::string("::1"));
    LAPILLI_CHECK_EQ(parse("https://x/")->effective_port(), std::uint16_t{443});
}

LAPILLI_TEST("new URL(reference, base)") {
    const auto base = parse("https://u:p@x.example:8443/base/dir/file?q=1#h");
    LAPILLI_CHECK(base.has_value());
    const std::pair<const char*, const char*> cases[] = {
        {"api/tasks", "https://u:p@x.example:8443/base/dir/api/tasks"},
        {"../up", "https://u:p@x.example:8443/base/up"},
        {"/abs?z", "https://u:p@x.example:8443/abs?z"},
        {"//other.example/p", "https://other.example/p"},
        {"?only", "https://u:p@x.example:8443/base/dir/file?only"},
        {"#frag", "https://u:p@x.example:8443/base/dir/file?q=1#frag"},
        {"", "https://u:p@x.example:8443/base/dir/file?q=1"},
        {"./a/../b", "https://u:p@x.example:8443/base/dir/b"},
        {"http://y/", "http://y/"},
        {"..", "https://u:p@x.example:8443/base/"},
        {"a:b", "a:b"},
    };
    for (const auto& [reference, expected] : cases) {
        const auto resolved = base->resolve(reference);
        LAPILLI_CHECK(resolved.has_value());
        LAPILLI_CHECK_EQ(resolved->href(), std::string(expected));
    }
    LAPILLI_CHECK_EQ(parse("https://x.example/base/")->resolve("api/tasks")->href(), std::string("https://x.example/base/api/tasks"));
    LAPILLI_CHECK_EQ(parse("https://x.example/base")->resolve("api/tasks")->href(), std::string("https://x.example/api/tasks"));
}

LAPILLI_TEST("encodeURIComponent / decodeURIComponent") {
    LAPILLI_CHECK_EQ(encode_uri_component("a b&c=d/\xC3\xA9\xF0\x9F\x98\x80~!*()'"), std::string("a%20b%26c%3Dd%2F%C3%A9%F0%9F%98%80~!*()'"));
    LAPILLI_CHECK(!decode_uri_component("%E0%A4%A").has_value());
    LAPILLI_CHECK(!decode_uri_component("%C3%28").has_value());
    LAPILLI_CHECK_EQ(decode_uri_component("%E3%81%82%2F+").value_or("?"), std::string("\xE3\x81\x82/+"));
    LAPILLI_CHECK_EQ(percent_decode("%41%zz%4"), std::string("A%zz%4"));
}

LAPILLI_TEST("URLSearchParams parse and serialize") {
    const auto params = parse_query("?a=1&b=x+y&a=%E3%81%82&&c&=v&d=%zz");
    const QueryList expected{{"a", "1"}, {"b", "x y"}, {"a", "\xE3\x81\x82"}, {"c", ""}, {"", "v"}, {"d", "%zz"}};
    LAPILLI_CHECK(params == expected);
    LAPILLI_CHECK_EQ(serialize_query(params), std::string("a=1&b=x+y&a=%E3%81%82&c=&=v&d=%25zz"));
    LAPILLI_CHECK_EQ(serialize_query({{"k y", "a&b=c/\xC3\xA9*-._~"}}), std::string("k+y=a%26b%3Dc%2F%C3%A9*-._%7E"));
    LAPILLI_CHECK_EQ(query_get(params, "a").value_or("?"), std::string("1"));
    LAPILLI_CHECK_EQ(query_get_all(params, "a").size(), 2u);
    LAPILLI_CHECK(!query_get(params, "zz").has_value());
}

LAPILLI_TEST("IPv4 strict and host forms") {
    LAPILLI_CHECK_EQ(parse_ipv4("192.168.0.1").value_or(0), 0xC0A80001u);
    for (const char* bad : {"1.2.3", "1.2.3.4.5", "01.2.3.4", "256.1.1.1", "1.2.3.4 ", "a.b.c.d", "1..2.3"}) LAPILLI_CHECK(!parse_ipv4(bad).has_value());
    LAPILLI_CHECK_EQ(parse_ipv4_host("0x7f.1").value_or("?"), std::string("127.0.0.1"));
    LAPILLI_CHECK_EQ(parse_ipv6_host("0:0::1").value_or("?"), std::string("[::1]"));
    LAPILLI_CHECK(!parse_ipv6_host("1:::2").has_value());
}
