// clang-format off
// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"network","default":"concurrency"}
// clang-format on
#include "data/ByteData.h"
#include "network/HttpRequest.h"
#include "network/Network.h"
#include "network/UdpSocket.h"
#include "platform_event/PlatformEvent.h"
#include "zeroerr/unittest.h"

#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/SecureServerSocket.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/StreamCopier.h>
#include <Poco/TemporaryFile.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

namespace {
using namespace std::chrono_literals;
using eve::network::HttpRequest;
using eve::network::Network;

void ensure(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

struct Gate {
    std::mutex              mutex;
    std::condition_variable cv;
    bool                    entered  = false;
    bool                    released = false;
    void                    release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
    void waitEntered() {
        std::unique_lock lock(mutex);
        ensure(cv.wait_for(lock, 3s, [&] { return entered; }), "HTTPS handler did not start");
    }
};

class Handler final : public Poco::Net::HTTPRequestHandler {
public:
    explicit Handler(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}
    void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override {
        if (request.getURI() == "/blocked") {
            std::unique_lock lock(gate_->mutex);
            gate_->entered = true;
            gate_->cv.notify_all();
            gate_->cv.wait(lock, [&] { return gate_->released; });
        }
        if (request.getURI() == "/slow") std::this_thread::sleep_for(250ms);
        std::ostringstream received;
        Poco::StreamCopier::copyStream(request.stream(), received);
        const auto body =
            request.getMethod() == "POST" ? request.get("X-Probe", "") + "|" + received.str() : std::string("tls-ok");
        if (request.getURI() == "/status") response.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        response.setContentLength(static_cast<std::streamsize>(body.size()));
        if (request.getURI() == "/truncated") {
            response.setContentLength(static_cast<std::streamsize>(body.size() + 5));
            response.setKeepAlive(false);
        }
        response.send().write(body.data(), static_cast<std::streamsize>(body.size()));
    }

private:
    std::shared_ptr<Gate> gate_;
};

class Factory final : public Poco::Net::HTTPRequestHandlerFactory {
public:
    explicit Factory(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}
    Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest&) override {
        return new Handler(gate_);
    }

private:
    std::shared_ptr<Gate> gate_;
};

class TestServer {
public:
    explicit TestServer(int protocol = 0, bool expired = false, bool secure = true) : secure_(secure) {
        using Context = Poco::Net::Context;
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"),
                                                                EVP_PKEY_free);
        std::unique_ptr<X509, decltype(&X509_free)>         cert(X509_new(), X509_free);
        ensure(key && cert, "certificate allocation");
        ensure(X509_set_version(cert.get(), 2) == 1, "certificate version");
        ensure(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) == 1, "certificate serial");
        ensure(X509_gmtime_adj(X509_getm_notBefore(cert.get()), expired ? -7200 : -60) != nullptr, "notBefore");
        ensure(X509_gmtime_adj(X509_getm_notAfter(cert.get()), expired ? -3600 : 3600) != nullptr, "notAfter");
        ensure(X509_set_pubkey(cert.get(), key.get()) == 1, "public key");
        auto* subject = X509_get_subject_name(cert.get());
        ensure(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                          reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0) == 1,
               "subject");
        ensure(X509_set_issuer_name(cert.get(), subject) == 1, "issuer");
        for (const auto& [nid, text] :
             {std::pair{NID_subject_alt_name, "DNS:localhost"}, std::pair{NID_basic_constraints, "critical,CA:TRUE"}}) {
            std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
                X509V3_EXT_conf_nid(nullptr, nullptr, nid, const_cast<char*>(text)), X509_EXTENSION_free);
            ensure(extension && X509_add_ext(cert.get(), extension.get(), -1) == 1, "certificate extension");
        }
        ensure(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0, "certificate signature");
        std::unique_ptr<BIO, decltype(&BIO_free)> file(BIO_new_file(ca_.path().c_str(), "w"), BIO_free);
        ensure(file && PEM_write_bio_X509(file.get(), cert.get()) == 1, "write certificate");
        file.reset();

        Poco::Net::ServerSocket socket;
        if (secure) {
            context_  = new Context(Context::SERVER_USE, "", "", "", Context::VERIFY_NONE);
            auto* ssl = context_->sslContext();
            ensure(SSL_CTX_use_certificate(ssl, cert.get()) == 1, "server certificate");
            ensure(SSL_CTX_use_PrivateKey(ssl, key.get()) == 1, "server key");
            if (protocol) {
                ensure(SSL_CTX_set_min_proto_version(ssl, protocol) == 1, "minimum TLS");
                ensure(SSL_CTX_set_max_proto_version(ssl, protocol) == 1, "maximum TLS");
            }
            socket = Poco::Net::SecureServerSocket(Poco::Net::SocketAddress("127.0.0.1", 0), 8, context_);
        } else {
            socket = Poco::Net::ServerSocket(Poco::Net::SocketAddress("127.0.0.1", 0));
        }
        port_   = socket.address().port();
        server_ = std::make_unique<Poco::Net::HTTPServer>(new Factory(gate), socket, new Poco::Net::HTTPServerParams);
        server_->start();
    }
    ~TestServer() {
        gate->release();
        server_->stopAll();
    }
    std::string url(const std::string& path = "/", const std::string& host = "localhost") const {
        return std::string(secure_ ? "https://" : "http://") + host + ":" + std::to_string(port_) + path;
    }
    std::string           ca() const { return ca_.path(); }
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();

private:
    Poco::Crypto::OpenSSLInitializer       ssl_;
    Poco::TemporaryFile                    ca_;
    Poco::Net::Context::Ptr                context_;
    std::unique_ptr<Poco::Net::HTTPServer> server_;
    unsigned short                         port_ = 0;
    bool                                   secure_;
};

class Events {
public:
    Events() : events_(eve::platform_event::PlatformEvent::create()) { events_->clear(); }
    std::unique_ptr<eve::platform_event::Message> wait(Network& network, void* origin) {
        const auto deadline = std::chrono::steady_clock::now() + 4s;
        while (std::chrono::steady_clock::now() < deadline) {
            network.pump();
            while (auto message = events_->pollOwned())
                if (!message->args.empty()) pending_[message->args[0].p] = std::move(message);
            auto found = pending_.find(origin);
            if (found != pending_.end()) {
                auto result = std::move(found->second);
                pending_.erase(found);
                return result;
            }
            std::this_thread::sleep_for(2ms);
        }
        throw std::runtime_error("HTTP completion timed out");
    }
    static std::string body(const eve::platform_event::Message& message) {
        auto* data = static_cast<eve::data::ByteData*>(message.args.at(2).p);
        return data ? std::string(static_cast<const char*>(data->getData()), data->getSize()) : std::string{};
    }

private:
    eve::platform_event::PlatformEvent*                                      events_;
    std::unordered_map<void*, std::unique_ptr<eve::platform_event::Message>> pending_;
};
}  // namespace

TEST_CASE("network.Http.httpsProtocolsAndBody") {
    for (int protocol : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        TestServer server(protocol);
        Network    network;
        network.setCaLocation(server.ca());
        int         status = 0;
        std::string body;
        REQUIRE(network.httpRequest("GET", server.url(), "", 2000, status, body));
        CHECK(status == 200);
        CHECK(body == "tls-ok");
        Events events;
        auto   request = network.makeHttp("POST", server.url());
        request->setHeader("X-Probe", "header");
        request->setBodyString(std::string("a\0b", 3));
        REQUIRE(request->submit());
        auto response = events.wait(network, request.get());
        REQUIRE(response->name == "httpresp");
        CHECK(response->args[1].i == 200);
        CHECK(Events::body(*response) == std::string("header|a\0b", 10));
        REQUIRE(network.httpRequest("GET", server.url("/status"), "", 2000, status, body));
        CHECK(status == 404);
    }
}

TEST_CASE("network.Http.rejectsUntrustedHostnameAndExpired") {
    Events     events;
    TestServer valid;
    Network    network;
    auto       untrusted = network.makeHttp("GET", valid.url());
    REQUIRE(untrusted->submit());
    auto failure = events.wait(network, untrusted.get());
    REQUIRE(failure->name == "neterr");
    CHECK(failure->args[1].s == "tls");

    network.setCaLocation(valid.ca());
    auto wrongHost = network.makeHttp("GET", valid.url("/", "127.0.0.1"));
    REQUIRE(wrongHost->submit());
    failure = events.wait(network, wrongHost.get());
    REQUIRE(failure->name == "neterr");
    CHECK(failure->args[1].s == "tls");

    TestServer expired(0, true);
    network.setCaLocation(expired.ca());
    auto stale = network.makeHttp("GET", expired.url());
    REQUIRE(stale->submit());
    failure = events.wait(network, stale.get());
    REQUIRE(failure->name == "neterr");
    CHECK(failure->args[1].s == "tls");

    auto invalidCa = network.makeHttp("GET", valid.url());
    invalidCa->setCaLocation(expired.ca() + ".missing");
    REQUIRE(invalidCa->submit());
    failure = events.wait(network, invalidCa.get());
    REQUIRE(failure->name == "neterr");
    CHECK(failure->args[1].s == "tls");
}

TEST_CASE("network.Http.requestPolicyIsolationAndSnapshot") {
    TestServer server;
    Network    network;
    Events     events;
    auto       insecure = network.makeHttp("GET", server.url("/", "127.0.0.1"));
    insecure->setVerifySsl(false);
    REQUIRE(insecure->submit());
    auto strict = network.makeHttp("GET", server.url());
    REQUIRE(strict->submit());
    REQUIRE(events.wait(network, insecure.get())->name == "httpresp");
    auto rejected = events.wait(network, strict.get());
    REQUIRE(rejected->name == "neterr");
    CHECK(rejected->args[1].s == "tls");
    CHECK(network.getVerifySsl());

    network.setCaLocation(server.ca());
    auto snapshot = network.makeHttp("GET", server.url());
    REQUIRE(snapshot->submit());
    network.setCaLocation(server.ca() + ".missing");
    REQUIRE(events.wait(network, snapshot.get())->name == "httpresp");
    auto overrideCa = network.makeHttp("GET", server.url());
    overrideCa->setCaLocation(server.ca());
    REQUIRE(overrideCa->submit());
    REQUIRE(events.wait(network, overrideCa.get())->name == "httpresp");

    HttpRequest copy(*overrideCa);
    overrideCa.reset();
    REQUIRE(copy.submit());
    REQUIRE(events.wait(network, &copy)->name == "httpresp");
}

TEST_CASE("network.Http.plainHttpAndTimeout") {
    TestServer  server(0, false, false);
    Network     network;
    int         status = 0;
    std::string body;
    REQUIRE(network.httpRequest("GET", server.url(), "", 2000, status, body));
    CHECK(status == 200);
    CHECK(body == "tls-ok");
    CHECK(!network.httpRequest("GET", server.url("/truncated"), "", 2000, status, body));
    Events events;
    auto   slow = network.makeHttp("GET", server.url("/slow"));
    slow->setTimeout(40);
    REQUIRE(slow->submit());
    auto response = events.wait(network, slow.get());
    REQUIRE(response->name == "neterr");
    CHECK(response->args[1].s == "timeout");
}

TEST_CASE("network.Http.destroyAndReuseOrigin") {
    TestServer server;
    Network    network;
    network.setTimeout(1500);
    network.setCaLocation(server.ca());
    Events                         events;
    alignas(HttpRequest) std::byte storage[sizeof(HttpRequest)];
    auto* old = std::construct_at(reinterpret_cast<HttpRequest*>(storage), &network, "GET", server.url("/blocked"));
    REQUIRE(old->submit());
    server.gate->waitEntered();
    std::destroy_at(old);
    auto* next = std::construct_at(reinterpret_cast<HttpRequest*>(storage), &network, "GET", server.url());
    REQUIRE(next->submit());
    server.gate->release();
    auto response = events.wait(network, next);
    REQUIRE(response->name == "httpresp");
    CHECK(Events::body(*response) == "tls-ok");
    const auto deadline = std::chrono::steady_clock::now() + 250ms;
    while (std::chrono::steady_clock::now() < deadline) {
        network.pump();
        std::this_thread::sleep_for(2ms);
    }
    CHECK(network.telemetrySnapshot().completions == 1);
    std::destroy_at(next);
}

TEST_CASE("network.Http.moduleLifetimeAndProviderReplacement") {
    TestServer server;
    auto       first  = std::make_unique<Network>();
    auto       second = std::make_unique<Network>();
    first.reset();
    CHECK(eve::cap::query<eve::service::INetwork>() == second.get());
    second->setCaLocation(server.ca());
    auto request = second->makeHttp("GET", server.url("/blocked"));
    request->setTimeout(1500);
    REQUIRE(request->submit());
    server.gate->waitEntered();
    server.gate->release();
    second.reset();
    CHECK(eve::cap::query<eve::service::INetwork>() == nullptr);
    CHECK(request->network() == nullptr);
    CHECK(!request->submit());
}

TEST_CASE("network.Http.syncPreservesOtherCompletions") {
    TestServer server;
    Network    network;
    network.setCaLocation(server.ca());
    Events events;
    auto   async = network.makeHttp("GET", server.url());
    REQUIRE(async->submit());
    int         status = 0;
    std::string body;
    REQUIRE(network.httpRequest("GET", server.url(), "", 2000, status, body));
    REQUIRE(events.wait(network, async.get())->name == "httpresp");
}

TEST_CASE("network.Http.copyAssignmentRetiresOldOrigin") {
    TestServer oldServer;
    TestServer newServer;
    auto       oldNetwork = std::make_unique<Network>();
    Network    newNetwork;
    oldNetwork->setCaLocation(oldServer.ca());
    newNetwork.setCaLocation(newServer.ca());
    Events      events;
    HttpRequest request(oldNetwork.get(), "POST", oldServer.url("/blocked"));
    request.setTimeout(1500);
    request.setBodyString("old");
    REQUIRE(request.submit());
    oldServer.gate->waitEntered();
    HttpRequest configuration(&newNetwork, "POST", newServer.url());
    configuration.setBodyString("new");
    request = configuration;
    oldServer.gate->release();
    oldNetwork.reset();
    CHECK(request.network() == &newNetwork);
    REQUIRE(request.submit());
    auto response = events.wait(newNetwork, &request);
    REQUIRE(response->name == "httpresp");
    CHECK(Events::body(*response) == "|new");
}

TEST_CASE("network.Http.reentrantPumpCancellationDropsCompletion") {
    TestServer server;
    Network    network;
    network.setCaLocation(server.ca());
    auto* events = eve::platform_event::PlatformEvent::create();
    events->clear();
    Poco::Net::DatagramSocket exclusive(Poco::Net::SocketAddress::IPv4);
#ifdef _WIN32
    exclusive.setOption(SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#endif
    exclusive.bind(Poco::Net::SocketAddress("0.0.0.0", 0), false);
    auto udp     = network.makeUdp();
    auto request = network.makeHttp("GET", server.url());
    REQUIRE(request->submit());
    // Let the HTTP result become ready without pumping its event yet.
    std::this_thread::sleep_for(150ms);
    // Bind failure posts a UDP completion synchronously, so it is definitely
    // queued before pump, without relying on worker/telemetry timing.
    REQUIRE(!udp->bind(exclusive.address().port()));

    struct Cancellation {
        eve::platform_event::PlatformEvent* events;
        std::unique_ptr<HttpRequest>*       request;
        std::thread::id                     owner;
    } cancellation{events, &request, std::this_thread::get_id()};
    struct FilterScope {
        SDL_EventFilter previous    = nullptr;
        void*           data        = nullptr;
        bool            hadPrevious = SDL_GetEventFilter(&previous, &data) == SDL_TRUE;
        ~FilterScope() { SDL_SetEventFilter(hadPrevious ? previous : nullptr, data); }
    } filter;
    SDL_SetEventFilter(
        [](void* context, SDL_Event* event) -> int {
            auto& state = *static_cast<Cancellation*>(context);
            if (event->type >= SDL_USEREVENT && event->user.data1 == state.events &&
                std::this_thread::get_id() == state.owner)
                state.request->reset();
            return 1;
        },
        &cancellation);
    // Queuing the UDP error invokes SDL's filter synchronously. Cancellation
    // must retire the HTTP result even if it was already staged for this pump.
    network.pump();
    CHECK(!request);
    auto message = events->pollOwned();
    REQUIRE(message != nullptr);
    CHECK(message->name == "neterr");
    CHECK(message->args[0].p == udp.get());
    CHECK(events->pollOwned() == nullptr);
    CHECK(network.telemetrySnapshot().completions == 1);
}
