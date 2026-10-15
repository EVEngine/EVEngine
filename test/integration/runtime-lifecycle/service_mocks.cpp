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
