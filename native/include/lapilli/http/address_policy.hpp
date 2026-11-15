// The SSRF guard of the HTTP client: which addresses are public (globally routable unicast).
// Refused: unspecified, loopback, private (RFC 1918), CGNAT, link-local, documentation / test nets,
// benchmarking, multicast, reserved, broadcast; IPv6 ULA, link-local, site-local, multicast,
// discard, documentation, and the IPv4 embedded in IPv4-mapped / -compatible, NAT64 (64:ff9b::/96)
// and 6to4 (2002::/16) addresses is checked as IPv4. Anything unparseable is not public.
#pragma once

#include <string_view>

namespace lapilli::http {

bool is_public_address(std::string_view address);

} // namespace lapilli::http
