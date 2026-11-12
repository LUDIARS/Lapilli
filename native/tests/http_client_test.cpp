// lapilli::http::fetch against local Beast servers on 127.0.0.1 only (no external service): the
// request as built (method, path, headers, body, multipart), redirects (manual / error / follow with
// method and header rules, hop limit), the SSRF policy (loopback refused under public_only, pinned
// addresses), the body limit exactly and one over (Content-Length and chunked), the head limit,
// timeouts and cancellation, streaming, TLS refusal of a plain server, the address policy table.
#include "lapilli/http/address_policy.hpp"
#include "lapilli/http/client.hpp"
#include "lapilli/http/server.hpp"
#include "lapilli/http/tls.hpp"
#include "lapilli/test/check.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

using namespace lapilli::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace bhttp = beast::http;
using tcp = asio::ip::tcp;

namespace {

/** Serves one connection per request with `answer` (raw socket access); records what arrived. */
class LocalServer {
public:
    using Answer = std::function<void(const bhttp::request<bhttp::string_body>&, tcp::socket&)>;
    explicit LocalServer(Answer answer) : acceptor_(io_, {asio::ip::make_address("127.0.0.1"), 0}), answer_(std::move(answer)) {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::thread([this] { serve(); });
    }
    ~LocalServer() {
        stop_ = true;
        beast::error_code ignored;
        tcp::socket poke(io_);
        poke.connect({asio::ip::make_address("127.0.0.1"), port_}, ignored);
        if (thread_.joinable()) thread_.join();
    }
    [[nodiscard]] std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    [[nodiscard]] std::uint16_t port() const { return port_; }
    std::vector<bhttp::request<bhttp::string_body>> received() {
        std::lock_guard lock(mutex_);
        return received_;
    }

private:
    void serve() {
        while (!stop_) {
            tcp::socket socket(io_);
            beast::error_code error;
            acceptor_.accept(socket, error);
            if (error || stop_) return;
            beast::flat_buffer buffer;
            bhttp::request<bhttp::string_body> request;
            bhttp::read(socket, buffer, request, error);
            if (error) continue;
            {
                std::lock_guard lock(mutex_);
                received_.push_back(request);
            }
            answer_(request, socket);
        }
    }
    asio::io_context io_;
    tcp::acceptor acceptor_;
    Answer answer_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<bhttp::request<bhttp::string_body>> received_;
    std::thread thread_;
};

void respond(tcp::socket& socket, bhttp::status status, const std::string& body, const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    bhttp::response<bhttp::string_body> response{status, 11};
    response.body() = body;
    for (const auto& [name, value] : headers) response.set(name, value);
    response.prepare_payload();
    beast::error_code ignored;
    bhttp::write(socket, response, ignored);
    socket.shutdown(tcp::socket::shutdown_both, ignored);
}

FetchErrorKind kind_of(const ClientRequest& request, std::string* message = nullptr) {
    FetchFailure failure;
    LAPILLI_CHECK(!try_fetch(request, &failure).has_value());
    if (message != nullptr) *message = failure.message;
    return failure.kind;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

LAPILLI_TEST("a request goes out as built and the answer comes back with status, headers and body") {
    LocalServer server([](const auto&, tcp::socket& socket) { respond(socket, bhttp::status::created, R"({"task":{"id":"t1"}})", {{"X-Audio-Rate", "48000"}}); });
    ClientRequest request;
    request.method = "POST";
    request.url = server.url("/api/tasks?x=1");
    request.headers = {{"content-type", "application/json"}, {"authorization", "Bearer token"}};
    request.body = R"({"a":1})";
    const auto response = fetch(request);
    LAPILLI_CHECK_EQ(response.status, 201);
    LAPILLI_CHECK(response.ok());
    LAPILLI_CHECK_EQ(response.header("X-Audio-Rate").value_or(""), std::string("48000"));
    LAPILLI_CHECK_EQ(response.json()["task"]["id"], lapilli::Json("t1"));
    LAPILLI_CHECK_EQ(response.url, server.url("/api/tasks?x=1"));
    const auto received = server.received().at(0);
    LAPILLI_CHECK_EQ(std::string(received.method_string()), std::string("POST"));
    LAPILLI_CHECK_EQ(std::string(received.target()), std::string("/api/tasks?x=1"));
    LAPILLI_CHECK_EQ(std::string(received["authorization"]), std::string("Bearer token"));
    LAPILLI_CHECK_EQ(std::string(received["user-agent"]), std::string("lapilli"));
    LAPILLI_CHECK_EQ(received.body(), std::string(R"({"a":1})"));
}

LAPILLI_TEST("a multipart body carries its random boundary in the content type") {
    LocalServer server([](const auto&, tcp::socket& socket) { respond(socket, bhttp::status::accepted, "{}"); });
    ClientRequest request;
    request.method = "POST";
    request.url = server.url("/upload");
    request.form = std::vector<FormPart>{{"review", "review.json", "application/json", "{}"}, {"video", "r.mp4", "video/mp4", std::string("\x00\x01", 2)}};
    LAPILLI_CHECK_EQ(fetch(request).status, 202);
    const auto received = server.received().at(0);
    const std::string type(received["content-type"]);
    LAPILLI_CHECK(type.rfind("multipart/form-data; boundary=----lapilli", 0) == 0);
    LAPILLI_CHECK_EQ(received.body(), encode_form(*request.form, type.substr(type.find('=') + 1)));
    LAPILLI_CHECK(random_boundary() != random_boundary());
}

LAPILLI_TEST("redirects: manual returns the 3xx, error refuses it, follow goes on with the fetch rules") {
    LocalServer target([](const auto&, tcp::socket& socket) { respond(socket, bhttp::status::ok, "landed"); });
    const std::string elsewhere = "http://localhost:" + std::to_string(target.port()) + "/landed";
    LocalServer server([&](const bhttp::request<bhttp::string_body>& request, tcp::socket& socket) {
        if (request.target() == "/loop") return respond(socket, bhttp::status::found, "", {{"Location", "/loop"}});
        if (request.target() == "/same") return respond(socket, bhttp::status::see_other, "", {{"Location", "/final"}});
        if (request.target() == "/final") return respond(socket, bhttp::status::ok, "final:" + std::string(request.method_string()) + ":" + request.body());
        respond(socket, bhttp::status::found, "", {{"Location", elsewhere}});
    });
    ClientRequest request;
    request.url = server.url("/x");
    LAPILLI_CHECK_EQ(fetch(request).status, 302);
    request.redirect = RedirectMode::error;
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::redirect);
    request.redirect = RedirectMode::follow;
    request.method = "POST";
    request.body = "data";
    request.headers = {{"Authorization", "Bearer secret"}, {"Content-Type", "text/plain"}};
    const auto followed = fetch(request);
    LAPILLI_CHECK_EQ(followed.body, std::string("landed"));
    LAPILLI_CHECK(followed.redirected);
    LAPILLI_CHECK_EQ(followed.url, elsewhere);
    const auto landed = target.received().at(0);
    LAPILLI_CHECK_EQ(std::string(landed.method_string()), std::string("GET")); // 302 after POST -> GET
    LAPILLI_CHECK(landed.find("authorization") == landed.end());             // origin changed
    LAPILLI_CHECK(landed.find("content-type") == landed.end());
    request.url = server.url("/same");
    request.method = "PUT";
    LAPILLI_CHECK_EQ(fetch(request).body, std::string("final:GET:")); // 303 -> GET without body
    request.url = server.url("/loop");
    request.max_redirects = 3;
    std::string message;
    LAPILLI_CHECK(kind_of(request, &message) == FetchErrorKind::redirect);
    LAPILLI_CHECK(contains(message, "too many redirects"));
}

LAPILLI_TEST("SSRF: public_only refuses loopback (also behind a redirect and via pinned addresses)") {
    LocalServer server([](const auto&, tcp::socket& socket) { respond(socket, bhttp::status::ok, "reached"); });
    ClientRequest request;
    request.url = server.url("/");
    request.address_policy = AddressPolicy::public_only;
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::blocked_address);
    request.url = "http://public.example:" + std::to_string(server.port()) + "/";
    request.pinned_addresses = std::vector<std::string>{"127.0.0.1"};
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::blocked_address);
    // Pinned addresses under `allow` skip DNS: the name is never looked up.
    request.address_policy = AddressPolicy::allow;
    LAPILLI_CHECK_EQ(fetch(request).body, std::string("reached"));
    LAPILLI_CHECK_EQ(std::string(server.received().back()["host"]), "public.example:" + std::to_string(server.port()));
    LAPILLI_CHECK(server.received().size() == 1u);
}

LAPILLI_TEST("boundary: the body limit holds exactly and one over (Content-Length and chunked); the head limit too") {
    LocalServer server([](const bhttp::request<bhttp::string_body>& request, tcp::socket& socket) {
        if (request.target() == "/chunked") {
            const std::string text = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3e8\r\n" + std::string(1000, 'c') + "\r\n0\r\n\r\n";
            asio::write(socket, asio::buffer(text));
            beast::error_code ignored;
            socket.shutdown(tcp::socket::shutdown_both, ignored);
            return;
        }
        if (request.target() == "/head") return respond(socket, bhttp::status::ok, "", {{"X-Big", std::string(4096, 'h')}});
        respond(socket, bhttp::status::ok, std::string(1000, 'a'));
    });
    ClientRequest request;
    request.url = server.url("/");
    request.max_body = 1000;
    LAPILLI_CHECK_EQ(fetch(request).body.size(), 1000u);
    request.url = server.url("/chunked");
    LAPILLI_CHECK_EQ(fetch(request).body.size(), 1000u);
    request.max_body = 999;
    std::string message;
    LAPILLI_CHECK(kind_of(request, &message) == FetchErrorKind::too_large);
    LAPILLI_CHECK(contains(message, "larger than 999 bytes"));
    request.url = server.url("/");
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::too_large);
    request.url = server.url("/head");
    request.header_limit = 1024;
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::header_too_large);
}

LAPILLI_TEST("timeouts, refused connections, bad URLs and cancellation; errors never carry header values") {
    LocalServer server([](const auto&, tcp::socket&) { std::this_thread::sleep_for(std::chrono::milliseconds(600)); });
    ClientRequest request;
    request.url = server.url("/slow");
    request.timeout = std::chrono::milliseconds(200);
    request.headers = {{"authorization", "Bearer secret-token"}};
    std::string message;
    LAPILLI_CHECK(kind_of(request, &message) == FetchErrorKind::timeout);
    LAPILLI_CHECK(contains(message, "timed out"));
    LAPILLI_CHECK(!contains(message, "secret-token"));
    std::atomic<bool> cancel{false};
    std::thread trigger([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancel = true;
    });
    request.timeout = std::chrono::milliseconds(5000);
    request.cancel = &cancel;
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::cancelled);
    trigger.join();
    ClientRequest refused;
    refused.url = "http://127.0.0.1:1/";
    LAPILLI_CHECK(kind_of(refused, &message) == FetchErrorKind::connect);
    LAPILLI_CHECK(contains(message, "could not connect"));
    ClientRequest credentials;
    credentials.url = "http://user:pw@127.0.0.1:1/";
    LAPILLI_CHECK(kind_of(credentials, &message) == FetchErrorKind::credentials);
    LAPILLI_CHECK(!contains(message, "pw"));
    ClientRequest other;
    other.url = "ftp://127.0.0.1/";
    LAPILLI_CHECK(kind_of(other) == FetchErrorKind::unsupported_scheme);
    other.url = "not a url";
    LAPILLI_CHECK(kind_of(other) == FetchErrorKind::invalid_url);
    LAPILLI_CHECK_THROWS_AS(fetch(other), FetchError);
}

LAPILLI_TEST("https verifies the peer: a plain-text server fails the TLS handshake") {
    LocalServer server([](const auto&, tcp::socket& socket) { respond(socket, bhttp::status::ok, "plain"); });
    ClientRequest request;
    request.url = "https://127.0.0.1:" + std::to_string(server.port()) + "/";
    request.timeout = std::chrono::milliseconds(3000);
    LAPILLI_CHECK(kind_of(request) == FetchErrorKind::tls);
    LAPILLI_CHECK(is_ip_literal("::1") && is_ip_literal("10.0.0.1") && !is_ip_literal("example.com"));
}

LAPILLI_TEST("a stream is read as it arrives and ends when cancelled or when the callback says stop") {
    std::atomic<bool> release{false};
    LocalServer server([&release](const auto&, tcp::socket& socket) {
        const std::string head = "HTTP/1.1 200 OK\r\nX-Audio-Rate: 48000\r\nTransfer-Encoding: chunked\r\n\r\n";
        asio::write(socket, asio::buffer(head));
        for (int i = 0; i < 3; ++i) asio::write(socket, asio::buffer(std::string("3\r\nabc\r\n")));
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        beast::error_code ignored;
        socket.close(ignored);
    });
    std::atomic<bool> stop{false};
    std::string body;
    std::atomic<std::size_t> received{0};
    int status = 0;
    ClientRequest request;
    request.url = server.url("/stream");
    request.timeout = std::chrono::milliseconds(0);
    request.max_body = lapilli::unlimited;
    request.cancel = &stop;
    request.on_head = [&](const ClientResponse& head) { status = head.status; };
    request.on_chunk = [&](std::string_view chunk) {
        body.append(chunk);
        received = body.size();
        return true;
    };
    std::thread reader([&] { fetch(request); });
    for (int i = 0; i < 2000 && received < 9; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop = true;
    reader.join();
    release = true;
    LAPILLI_CHECK_EQ(status, 200);
    LAPILLI_CHECK_EQ(body, std::string("abcabcabc"));
}

LAPILLI_TEST("the lapilli server and client talk to each other") {
    Server server(ServerOptions{}, synchronous([](const ServerRequest& request) { return ServerResponse::json(200, "{\"echo\":\"" + request.body + "\"}"); }));
    const auto port = server.listen();
    server.start();
    ClientRequest request;
    request.method = "POST";
    request.url = "http://127.0.0.1:" + std::to_string(port) + "/echo";
    request.body = "hi";
    const auto response = fetch(request);
    LAPILLI_CHECK_EQ(response.json()["echo"], lapilli::Json("hi"));
    LAPILLI_CHECK_EQ(response.header("content-type").value_or(""), std::string("application/json; charset=utf-8"));
}

LAPILLI_TEST("the address policy table") {
    for (const char* blocked : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.1.1", "169.254.169.254", "100.64.0.1", "0.0.0.0", "255.255.255.255",
                                "224.0.0.1", "198.18.0.1", "::1", "::", "fc00::1", "fe80::1", "ff02::1", "::ffff:127.0.0.1", "64:ff9b::a00:1",
                                "2002:7f00:1::", "2001:db8::1", "[::1]", "not-an-ip", "1.2.3"})
        LAPILLI_CHECK(!is_public_address(blocked));
    for (const char* allowed : {"8.8.8.8", "1.1.1.1", "2606:4700:4700::1111", "::ffff:8.8.8.8", "64:ff9b::808:808", "2002:808:808::"})
        LAPILLI_CHECK(is_public_address(allowed));
}
