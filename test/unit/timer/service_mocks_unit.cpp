#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "timer/Timer.h"  // Test-domain owner for the timer service contract.
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {
class MemoryFiles final : public eve::service::IFileSystem {
public:
    bool                                        rejectRead  = false;
    bool                                        rejectWrite = false;
    std::map<std::string, std::vector<uint8_t>> files;

    bool readFile(const std::string& path, std::vector<uint8_t>& out) override {
        if (rejectRead) return false;
        auto found = files.find(path);
        if (found == files.end()) return false;
        out = found->second;
        return true;
    }

    bool writeFile(const std::string& path, const void* data, size_t size) override {
        if (rejectWrite) return false;
        const auto* bytes = static_cast<const uint8_t*>(data);
        files[path]       = std::vector<uint8_t>(bytes, bytes + size);
        return true;
    }

    bool fileExists(const std::string& path) override { return files.contains(path); }
};

class FixedNetwork final : public eve::service::INetwork {
public:
    bool        connected  = true;
    int         nextStatus = 200;
    std::string nextBody   = "{}";

    bool httpRequest(const std::string&, const std::string&, const std::string&, int, int& status,
                     std::string& body) override {
        if (!connected) return false;
        status = nextStatus;
        body   = nextBody;
        return true;
    }
};

class FixedTimer final : public eve::service::ITimer {
public:
    explicit FixedTimer(double now) : now(now) {}
    double elapsedSeconds() override { return now; }
    double now;
};
}  // namespace

TEST_CASE("service.capability.filesystem_absent_then_registered_with_owned_bytes") {
    eve::cap::detail::clearAllRaw();
    CHECK(eve::cap::query<eve::service::IFileSystem>() == nullptr);
    MemoryFiles files;
    eve::cap::provide<eve::service::IFileSystem>(&files);
    auto* service = eve::cap::query<eve::service::IFileSystem>();
    REQUIRE(service == &files);

    std::vector<uint8_t> output{9};
    CHECK(!service->readFile("missing", output));
    CHECK_EQ(output.size(), 1U);
    const std::vector<uint8_t> input{0, 128, 255};
    REQUIRE(service->writeFile("binary", input.data(), input.size()));
    CHECK(service->fileExists("binary"));
    REQUIRE(service->readFile("binary", output));
    CHECK_EQ(output, input);

    files.rejectRead = true;
    output           = {7};
    CHECK(!service->readFile("binary", output));
    CHECK_EQ(output, std::vector<uint8_t>{7});
    files.rejectWrite = true;
    CHECK(!service->writeFile("rejected", input.data(), input.size()));
    CHECK(!service->fileExists("rejected"));
    eve::cap::revoke<eve::service::IFileSystem>(&files);
    CHECK(eve::cap::query<eve::service::IFileSystem>() == nullptr);
}

TEST_CASE("service.capability.network_distinguishes_http_status_from_transport_failure") {
    eve::cap::detail::clearAllRaw();
    FixedNetwork network;
    eve::cap::provide<eve::service::INetwork>(&network);
    auto* service = eve::cap::query<eve::service::INetwork>();
    REQUIRE(service == &network);

    int         status = -1;
    std::string body   = "old";
    REQUIRE(service->httpRequest("GET", "https://example.test/ok", "", 1000, status, body));
    CHECK_EQ(status, 200);
    CHECK_EQ(body, std::string("{}"));

    network.nextStatus = 404;
    network.nextBody   = "missing";
    REQUIRE(service->httpRequest("GET", "https://example.test/missing", "", 1000, status, body));
    CHECK_EQ(status, 404);
    CHECK_EQ(body, std::string("missing"));

    network.connected = false;
    status            = -1;
    body              = "unchanged";
    CHECK(!service->httpRequest("GET", "https://example.test/down", "", 1000, status, body));
    CHECK_EQ(status, -1);
    CHECK_EQ(body, std::string("unchanged"));
    eve::cap::revoke<eve::service::INetwork>(&network);
}

TEST_CASE("service.capability.timer_provider_ref_is_a_snapshot_of_registration") {
    eve::cap::detail::clearAllRaw();
    CHECK(!static_cast<bool>(eve::cap::ProviderRef<eve::service::ITimer>::bind()));
    FixedTimer first(1.5);
    FixedTimer second(2.75);
    eve::cap::provide<eve::service::ITimer>(&first);
    auto firstBound = eve::cap::ProviderRef<eve::service::ITimer>::bind();
    REQUIRE(static_cast<bool>(firstBound));
    CHECK_EQ(firstBound->elapsedSeconds(), 1.5);
    eve::cap::provide<eve::service::ITimer>(&second);
    CHECK(eve::cap::query<eve::service::ITimer>() == &second);
    CHECK_EQ(firstBound->elapsedSeconds(), 1.5);
    eve::cap::revoke<eve::service::ITimer>(&first);
    CHECK(eve::cap::query<eve::service::ITimer>() == &second);
    eve::cap::revoke<eve::service::ITimer>(&second);
    CHECK(eve::cap::query<eve::service::ITimer>() == nullptr);
}
