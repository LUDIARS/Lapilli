// lapilli::http::Server end to end on an ephemeral loopback port, driven by a raw socket client:
// routing, HEAD, keep-alive and its cap, body at the limit / one over / huge Content-Length,
// oversized header (431), broken request (400), connections over the cap (closed), asynchronous
// answers from another thread, handler timeout (503), a dropped responder or a throwing handler
// (500), and resolve_port.
#include "lapilli/http/server.hpp"
#include "lapilli/test/check.hpp"

#include <boost/asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace lapilli::http;

namespace {

ServerResponse health(const ServerRequest& request) {
    if (request.path != "/health") return ServerResponse::json(404, R"({"error":"not_found"})");
    if (request.method != "GET" && request.method != "HEAD") return ServerResponse::json(405, R"({"error":"method_not_allowed"})");
    return ServerResponse::json(200, R"({"status":"ok"})");
}

struct Running {
    std::unique_ptr<Server> server;
    std::uint16_t port = 0;
};

Running start(Handler handler, ServerLimits limits = {}) {
    ServerOptions options;
    options.limits = limits;
    Running running;
    running.server = std::make_unique<Server>(options, std::move(handler));
    running.port = running.server->listen();
    running.server->start();
    return running;
}

std::string talk(std::uint16_t port, const std::string& request) {
    asio::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
    boost::system::error_code error;
    asio::write(socket, asio::buffer(request), error);
    std::string response;
    std::array<char, 4096> chunk{};
    for (;;) {
        const auto n = socket.read_some(asio::buffer(chunk), error);
        response.append(chunk.data(), n);
        if (error) break;
    }
    return response;
}

bool starts(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }

} // namespace

LAPILLI_TEST("GET answers with status, headers and body; unknown paths 404; other methods 405; HEAD has no body") {
    auto running = start(synchronous(health));
    const auto ok = talk(running.port, "GET /health?x=1 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const auto head = talk(running.port, "HEAD /health HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const auto missing = talk(running.port, "GET /nope HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const auto post = talk(running.port, "POST /health HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    LAPILLI_CHECK(starts(ok, "HTTP/1.1 200"));
    LAPILLI_CHECK(ok.find("Content-Type: application/json; charset=utf-8") != std::string::npos);
    LAPILLI_CHECK(ok.find(R"({"status":"ok"})") != std::string::npos);
    LAPILLI_CHECK(starts(head, "HTTP/1.1 200"));
    LAPILLI_CHECK(head.find("Content-Length: 15") != std::string::npos);
    LAPILLI_CHECK(head.find("\"status\"") == std::string::npos);
    LAPILLI_CHECK(starts(missing, "HTTP/1.1 404"));
    LAPILLI_CHECK(starts(post, "HTTP/1.1 405"));
}

LAPILLI_TEST("the request carries method, path, query, headers, body and the peer address") {
    ServerRequest seen;
    auto running = start(synchronous([&seen](const ServerRequest& request) {
        seen = request;
        return ServerResponse::text(201, "made");
    }));
    const auto response = talk(running.port, "PUT /a/b?x=1&y=2 HTTP/1.1\r\nHost: x\r\nX-Token: t\r\nContent-Length: 3\r\nConnection: close\r\n\r\nabc");
    running.server.reset();
    LAPILLI_CHECK(starts(response, "HTTP/1.1 201"));
    LAPILLI_CHECK_EQ(seen.method, std::string("PUT"));
    LAPILLI_CHECK_EQ(seen.path, std::string("/a/b"));
    LAPILLI_CHECK_EQ(seen.query, std::string("x=1&y=2"));
    LAPILLI_CHECK_EQ(seen.header("x-token").value_or(""), std::string("t"));
    LAPILLI_CHECK_EQ(seen.body, std::string("abc"));
    LAPILLI_CHECK_EQ(seen.remote_address, std::string("127.0.0.1"));
}

LAPILLI_TEST("boundary: a body at the limit is read, one byte over and a huge Content-Length are 413") {
    ServerLimits limits;
    limits.body_bytes = 16;
    auto running = start(synchronous(health), limits);
    const auto at = talk(running.port, "POST /health HTTP/1.1\r\nHost: x\r\nContent-Length: 16\r\nConnection: close\r\n\r\n" + std::string(16, 'a'));
    const auto over = talk(running.port, "POST /health HTTP/1.1\r\nHost: x\r\nContent-Length: 17\r\nConnection: close\r\n\r\n" + std::string(17, 'a'));
    const auto huge = talk(running.port, "POST /health HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999999\r\nConnection: close\r\n\r\n");
    const auto chunked = talk(running.port, "POST /health HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n20\r\n" + std::string(32, 'b') + "\r\n0\r\n\r\n");
    LAPILLI_CHECK(starts(at, "HTTP/1.1 405"));
    LAPILLI_CHECK(starts(over, "HTTP/1.1 413"));
    LAPILLI_CHECK(over.find("payload_too_large") != std::string::npos);
    LAPILLI_CHECK(starts(huge, "HTTP/1.1 413"));
    LAPILLI_CHECK(starts(chunked, "HTTP/1.1 413"));
}

LAPILLI_TEST("boundary: an oversized header is 431, a broken request line is 400; neither reaches the handler") {
    ServerLimits limits;
    limits.header_bytes = 256;
    bool reached = false;
    auto running = start(synchronous([&reached](const ServerRequest& r) {
                             reached = true;
                             return health(r);
                         }),
                         limits);
    const auto big = talk(running.port, "GET /health HTTP/1.1\r\nHost: x\r\nX-Pad: " + std::string(1024, 'p') + "\r\n\r\n");
    const auto broken = talk(running.port, "NOT HTTP\r\n\r\n");
    running.server.reset();
    LAPILLI_CHECK(starts(big, "HTTP/1.1 431"));
    LAPILLI_CHECK(starts(broken, "HTTP/1.1 400"));
    LAPILLI_CHECK(!reached);
}

LAPILLI_TEST("keep-alive serves several requests until requests_per_connection, then closes") {
    ServerLimits limits;
    limits.requests_per_connection = 2;
    auto running = start(synchronous(health), limits);
    const std::string one = "GET /health HTTP/1.1\r\nHost: x\r\n\r\n";
    const auto response = talk(running.port, one + one + one);
    std::size_t count = 0;
    for (std::size_t at = response.find("HTTP/1.1 200"); at != std::string::npos; at = response.find("HTTP/1.1 200", at + 1)) ++count;
    LAPILLI_CHECK_EQ(count, 2u);
    LAPILLI_CHECK(response.find("Connection: close") != std::string::npos);
}

LAPILLI_TEST("connections over the cap are closed without an answer") {
    ServerLimits limits;
    limits.max_connections = 1;
    auto running = start(synchronous(health), limits);
    asio::io_context io;
    tcp::socket holder(io);
    holder.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), running.port));
    for (int i = 0; i < 100 && running.server->open_connections() < 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto refused = talk(running.port, "GET /health HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    holder.close();
    for (int i = 0; i < 100 && running.server->open_connections() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto served = talk(running.port, "GET /health HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    LAPILLI_CHECK(refused.empty());
    LAPILLI_CHECK(starts(served, "HTTP/1.1 200"));
}

LAPILLI_TEST("an answer may come later from another thread") {
    std::vector<std::thread> workers;
    std::atomic<bool> second_accepted{true};
    auto running = start([&workers, &second_accepted](ServerRequest&& request, Responder responder) {
        workers.emplace_back([responder, body = request.body, &second_accepted] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            responder.send(ServerResponse::text(200, "later:" + body));
            second_accepted = responder.send(ServerResponse::text(500, "twice"));
        });
    });
    const auto response = talk(running.port, "POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\nConnection: close\r\n\r\nhi");
    for (auto& worker : workers) worker.join();
    LAPILLI_CHECK(!second_accepted);
    LAPILLI_CHECK(starts(response, "HTTP/1.1 200"));
    LAPILLI_CHECK(response.find("later:hi") != std::string::npos);
}

LAPILLI_TEST("a handler that never answers gets 503 at handler_timeout; a dropped responder or a throw gets 500") {
    ServerLimits limits;
    limits.handler_timeout = std::chrono::milliseconds(100);
    std::vector<Responder> parked;
    auto slow = start([&parked](ServerRequest&&, Responder responder) { parked.push_back(responder); }, limits);
    const auto timed_out = talk(slow.port, "GET /x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    LAPILLI_CHECK(starts(timed_out, "HTTP/1.1 503"));
    LAPILLI_CHECK(parked.at(0).sent());
    auto dropped = start([](ServerRequest&&, Responder) {});
    LAPILLI_CHECK(starts(talk(dropped.port, "GET /x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"), "HTTP/1.1 500"));
    auto throwing = start([](ServerRequest&&, Responder) { throw std::runtime_error("boom"); });
    const auto thrown = talk(throwing.port, "GET /x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    LAPILLI_CHECK(starts(thrown, "HTTP/1.1 500"));
    LAPILLI_CHECK(thrown.find("boom") == std::string::npos);
}

LAPILLI_TEST("run() blocks until stop(); listen errors are reported; a responder may outlive its server") {
    ServerOptions options;
    std::optional<Responder> kept;
    std::atomic<bool> arrived{false};
    auto server = std::make_unique<Server>(options, [&kept, &arrived](ServerRequest&&, Responder responder) {
        kept = responder;
        arrived = true;
    });
    const auto port = server->listen();
    std::thread runner([&] { server->run(); });
    asio::io_context io;
    tcp::socket client(io);
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
    asio::write(client, asio::buffer(std::string("GET / HTTP/1.1\r\nHost: x\r\n\r\n")));
    for (int i = 0; i < 200 && !arrived; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    server->stop();
    runner.join();
    server.reset();
    LAPILLI_CHECK(kept.has_value());
    LAPILLI_CHECK(!kept->send(ServerResponse::text(200, "too late")));
    kept.reset();
    ServerOptions taken;
    taken.port = port;
    Server first(ServerOptions{}, synchronous(health));
    taken.port = first.listen();
    Server second(taken, synchronous(health));
    LAPILLI_CHECK_THROWS_AS(second.listen(), ServerError);
    ServerOptions bad;
    bad.host = "not-an-ip";
    Server third(bad, synchronous(health));
    LAPILLI_CHECK_THROWS_AS(third.listen(), ServerError);
}

LAPILLI_TEST("resolve_port: default, valid range, and the message naming the variable") {
    LAPILLI_CHECK_EQ(resolve_port(std::nullopt, 4430, "INGENIUM_PORT"), 4430);
    LAPILLI_CHECK_EQ(resolve_port(std::string(""), 4430, "INGENIUM_PORT"), 4430);
    LAPILLI_CHECK_EQ(resolve_port(std::string(" 5000 "), 4430, "INGENIUM_PORT"), 5000);
    LAPILLI_CHECK_EQ(resolve_port(std::string("1"), 4430, "X_PORT"), 1);
    LAPILLI_CHECK_EQ(resolve_port(std::string("65535"), 4430, "X_PORT"), 65535);
    for (const char* value : {"0", "70000", "abc", "12.5", "-1", "65536", "99999999999999999999"}) {
        std::string message;
        try {
            resolve_port(std::string(value), 4430, "INGENIUM_PORT");
        } catch (const std::invalid_argument& error) {
            message = error.what();
        }
        LAPILLI_CHECK_EQ(message, "INGENIUM_PORT must be an integer between 1 and 65535 (got '" + std::string(value) + "')");
    }
}
