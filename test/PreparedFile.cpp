#include "filesystem/PreparedFile.h"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Resource.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "thread/Thread.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("filesystem.prepared snapshots deduplicate and survive cache eviction") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("eve_prepared_file_contract", true));
    REQUIRE(fs->setupWriteDirectory());
    auto* threads = eve::thread::Thread::create();
    REQUIRE(threads != nullptr);
    fs->write("snapshot.bin", "old", 3);
    REQUIRE(eve::filesystem::requestPreparedFile("snapshot.bin").ok());
    auto first  = eve::filesystem::readPreparedFile("snapshot.bin", 3);
    auto second = eve::filesystem::readPreparedFile("snapshot.bin", 3);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value() == second.value());
    REQUIRE(!eve::filesystem::readPreparedFile("snapshot.bin", 2).ok());
    eve::ResourceManager::getInstance().unloadPath("snapshot.bin");
    fs->write("snapshot.bin", "new", 3);
    auto replacement = eve::filesystem::readPreparedFile("snapshot.bin", 3);
    REQUIRE(replacement.ok());
    REQUIRE(replacement.value() != first.value());
    REQUIRE(std::string(static_cast<const char*>(first.value()->getData()), 3) == "old");
    REQUIRE(std::string(static_cast<const char*>(replacement.value()->getData()), 3) == "new");
    REQUIRE(eve::filesystem::requestPreparedFile("missing-snapshot.bin").ok());
    REQUIRE(!eve::filesystem::readPreparedFile("missing-snapshot.bin", 1024).ok());
}

TEST_CASE("thread.resource executor bounds concurrent decoding") {
    auto* threads = eve::thread::Thread::create();
    REQUIRE(threads != nullptr);
    struct State {
        std::atomic<int>   active = 0, maximum = 0, completed = 0;
        std::promise<void> done;
    };
    auto  state    = std::make_shared<State>();
    auto  done     = state->done.get_future();
    auto* executor = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    REQUIRE(executor != nullptr);
    for (int i = 0; i < 12; ++i) {
        REQUIRE(executor
                    ->submit([state] {
                        const int n        = ++state->active;
                        int       previous = state->maximum;
                        while (previous < n && !state->maximum.compare_exchange_weak(previous, n)) {
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        --state->active;
                        if (++state->completed == 12) state->done.set_value();
                    })
                    .ok());
    }
    REQUIRE(done.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(state->maximum.load() <= 8);
    REQUIRE(state->completed.load() == 12);
}

TEST_CASE("filesystem.prepared rejects missing executor without submitting") {
    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs != nullptr);
    auto* executor = eve::cap::query<eve::caps::IAsyncWorkExecutor>();
    struct Restore {
        eve::caps::IAsyncWorkExecutor* executor;
        ~Restore() {
            if (executor) eve::cap::provide<eve::caps::IAsyncWorkExecutor>(executor);
        }
    } restore{executor};
    if (executor) eve::cap::revoke<eve::caps::IAsyncWorkExecutor>(executor);
    REQUIRE(!eve::filesystem::requestPreparedFile("snapshot.bin").ok());
}
