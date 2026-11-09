// lapilli::crypto against values recorded from Node's crypto / Buffer.
#include "lapilli/crypto.hpp"
#include "lapilli/test/check.hpp"

using namespace lapilli::crypto;

LAPILLI_TEST("sha256 and hmac-sha256 match Node") {
    LAPILLI_CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    LAPILLI_CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    LAPILLI_CHECK_EQ(sha256_raw("abc").size(), 32u);
    LAPILLI_CHECK_EQ(hmac_sha256_hex("key", "The quick brown fox jumps over the lazy dog"),
                     std::string("f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"));
    LAPILLI_CHECK_EQ(hmac_sha256_hex("", ""), std::string("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"));
}

LAPILLI_TEST("base64 / base64url / hex round trip and the decoders are strict") {
    const std::string raw("\xff\xfe\x00" "ab", 5);
    LAPILLI_CHECK_EQ(base64_encode(raw), std::string("//4AYWI="));
    LAPILLI_CHECK_EQ(base64url_encode(raw), std::string("__4AYWI"));
    LAPILLI_CHECK_EQ(base64_encode("f"), std::string("Zg=="));
    LAPILLI_CHECK_EQ(base64_encode("fo"), std::string("Zm8="));
    LAPILLI_CHECK_EQ(base64_decode("//4AYWI=").value_or("?"), raw);
    LAPILLI_CHECK_EQ(base64url_decode("__4AYWI").value_or("?"), raw);
    for (const char* bad : {"Zg=", "Zg", "Zh==", "Z g=", "Zg==="}) LAPILLI_CHECK(!base64_decode(bad).has_value());
    for (const char* bad : {"__4AYWI=", "Zh", "a"}) LAPILLI_CHECK(!base64url_decode(bad).has_value());
    LAPILLI_CHECK_EQ(to_hex(raw), std::string("fffe006162"));
    LAPILLI_CHECK_EQ(from_hex("FFfe006162").value_or("?"), raw);
    LAPILLI_CHECK(!from_hex("abc").has_value() && !from_hex("zz").has_value());
}

LAPILLI_TEST("random bytes come from OpenSSL; comparison is constant time; cleanse wipes") {
    const auto a = random_bytes(32);
    const auto b = random_bytes(32);
    LAPILLI_CHECK_EQ(a.size(), 32u);
    LAPILLI_CHECK(a != b);
    LAPILLI_CHECK_EQ(random_hex(12).size(), 24u);
    LAPILLI_CHECK(random_bytes(0).empty());
    LAPILLI_CHECK(constant_time_equal("abc", "abc"));
    LAPILLI_CHECK(!constant_time_equal("abc", "abd"));
    LAPILLI_CHECK(!constant_time_equal("abc", "ab"));
    std::string secret = "token";
    cleanse(secret);
    LAPILLI_CHECK_EQ(secret, std::string(5, '\0'));
}
