# Lapilli native (C++)

Infrastructure shared by the LUDIARS services ported from Node.js to C++: JavaScript-compatible
JSON and strings, child processes that kill their whole tree, bounded line / file input, OpenSSL
crypto, `node:util` parseArgs, a WHATWG URL subset, a Beast HTTP server and client, and the
`claude -p` / `codex exec` one-shot launch boundary. Namespace `lapilli`. The full contract
(limits, error kinds, per-repo wrappers) is in `spec/feature/native-common.md`.

Dependencies: C++20, OS APIs, Boost 1.87.0 headers, nlohmann_json 3.11.3, OpenSSL 3, Lorica
`cab4f12` — nothing else.

## Targets

| Target | Headers | Needs |
|---|---|---|
| `Lapilli::core` | `budget json text js_math process lines fs cli ordered url` | Lorica, nlohmann_json |
| `Lapilli::crypto` | `crypto` | OpenSSL |
| `Lapilli::one_shot` | `one_shot` | core |
| `Lapilli::net` | `http/server http/client http/tls http/address_policy` | core, crypto, Boost, OpenSSL |
| `Lapilli::test_main` | `test/check.hpp` | — |

Options: `LAPILLI_NATIVE_CRYPTO`, `LAPILLI_NATIVE_ONE_SHOT`, `LAPILLI_NATIVE_NET` (default ON),
`LAPILLI_NATIVE_TESTS` (default ON only when this directory is the top-level project).

## Build and test

```sh
cmake -S native -B build -DLORICA_SOURCE_DIR=/path/to/lorica \
  -DFETCHCONTENT_SOURCE_DIR_BOOST=/path/to/boost-1.87.0 -DFETCHCONTENT_SOURCE_DIR_JSON=/path/to/json
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Without the `*_SOURCE_DIR` variables the pinned releases / commit are downloaded.

## Use from a service

```cmake
FetchContent_Declare(lapilli GIT_REPOSITORY https://github.com/LUDIARS/Lapilli.git
  GIT_TAG <pinned commit sha> SOURCE_SUBDIR native)
FetchContent_MakeAvailable(lapilli)          # or add_subdirectory(${LAPILLI_SOURCE_DIR}/native ...)
target_link_libraries(my_service PRIVATE Lapilli::core Lapilli::net)
```

Existing `Lorica::boundary` / `nlohmann_json::nlohmann_json` targets are reused; pass
`-DLAPILLI_BOOST_TARGET=<your Boost INTERFACE target>` to reuse your Boost headers.

```cpp
#include "lapilli/json.hpp"
#include "lapilli/process.hpp"
#include "lapilli/http/server.hpp"

auto doc = lapilli::json::parse(text, {.max_bytes = 1 << 20});        // JsonError{too_large|too_deep|invalid}
lapilli::process::Spec spec{.argv = {"git", "status"}};
spec.timeout = std::chrono::seconds(10);
auto result = lapilli::process::run(spec);                              // never throws for the child
lapilli::http::Server server({}, lapilli::http::synchronous([](const auto& request) {
    return lapilli::http::ServerResponse::json(200, R"({"status":"ok"})");
}));
auto port = server.listen();                                            // 127.0.0.1, ephemeral port
server.run();
```

Tests use `lapilli/test/check.hpp` (`LAPILLI_TEST`, `LAPILLI_CHECK`, `LAPILLI_CHECK_EQ`,
`LAPILLI_CHECK_THROWS(_AS)`) linked with `Lapilli::test_main`; consumers may use the same harness.
