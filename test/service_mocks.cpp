#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "timer/Timer.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Deterministic test doubles: no PhysFS, no sockets, no wall clock.
// ---------------------------------------------------------------------------

class MockFileSystem : public eve::service::IFileSystem {
public:
    bool failReads = false;
    bool failWrites = false;
    std::map<std::string, std::vector<uint8_t>> files;

    bool readFile(const std::string &path, std::vector<uint8_t> &out) override {
        if (failReads) return false;
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }

    bool writeFile(const std::string &path, const void *data, size_t size) override {
        if (failWrites) return false;
        const auto *bytes = static_cast<const uint8_t *>(data);
        files[path] = std::vector<uint8_t>(bytes, bytes + size);
        return true;
    }

    bool fileExists(const std::string &path) override {
        return files.find(path) != files.end();
    }
};

class MockNetwork : public eve::service::INetwork {
public:
    bool transportFails = false;
    int cannedStatus = 200;
    std::string cannedBody = "{}";

    bool httpRequest(const std::string &, const std::string &, const std::string &, int, int &status,
                     std::string &responseBody) override {
        if (transportFails) return false;
        status = cannedStatus;
        responseBody = cannedBody;
        return true;
    }
};

class MockTimer : public eve::service::ITimer {
public:
    double value = 0.0;
    double elapsedSeconds() override { return value; }
};

}  // namespace

TEST_CASE("service.providers.timerRegistersReal") {
    eve::cap::detail::clearAllRaw();
    auto *timer = eve::timer::Timer::create();
    REQUIRE(timer != nullptr);
    auto *svc = eve::cap::query<eve::service::ITimer>();
    REQUIRE(svc != nullptr);
    CHECK(svc->elapsedSeconds() >= 0.0);
    eve::cap::detail::clearAllRaw();
}

TEST_CASE("service.mocks.filesystemErrorPaths") {
    eve::cap::detail::clearAllRaw();
    MockFileSystem fs;
    eve::cap::provide<eve::service::IFileSystem>(&fs);
    auto *svc = eve::cap::query<eve::service::IFileSystem>();
    REQUIRE(svc != nullptr);

    std::vector<uint8_t> out;
    CHECK(!svc->readFile("missing.bin", out));  // missing → false, no throw

    const char payload[] = "hello";
    CHECK(svc->writeFile("a.bin", payload, sizeof(payload) - 1));
    CHECK(svc->fileExists("a.bin"));
    CHECK(svc->readFile("a.bin", out));
    REQUIRE(out.size() == sizeof(payload) - 1);
    CHECK(std::memcmp(out.data(), payload, out.size()) == 0);

    fs.failReads = true;
    CHECK(!svc->readFile("a.bin", out));  // simulated read failure
    fs.failReads = false;
    fs.failWrites = true;
    CHECK(!svc->writeFile("b.bin", payload, sizeof(payload) - 1));  // simulated full disk
    eve::cap::detail::clearAllRaw();
}

TEST_CASE("service.mocks.networkErrorPaths") {
    eve::cap::detail::clearAllRaw();
    MockNetwork net;
    eve::cap::provide<eve::service::INetwork>(&net);
    auto *svc = eve::cap::query<eve::service::INetwork>();
    REQUIRE(svc != nullptr);

    int status = 0;
    std::string body;
    CHECK(svc->httpRequest("GET", "https://example.test/x", "", 1000, status, body));
    CHECK_EQ(status, 200);
    CHECK_EQ(body, "{}");

    net.cannedStatus = 404;
    CHECK(svc->httpRequest("GET", "https://example.test/missing", "", 1000, status, body));
    CHECK_EQ(status, 404);

    net.transportFails = true;
    CHECK(!svc->httpRequest("GET", "https://example.test/down", "", 1000, status, body));
    eve::cap::detail::clearAllRaw();
}

TEST_CASE("service.mocks.timerDeterministic") {
    eve::cap::detail::clearAllRaw();
    MockTimer t;
    t.value = 1.5;
    eve::cap::provide<eve::service::ITimer>(&t);
    auto *svc = eve::cap::query<eve::service::ITimer>();
    REQUIRE(svc != nullptr);
    CHECK_EQ(svc->elapsedSeconds(), 1.5);
    eve::cap::detail::clearAllRaw();
}
