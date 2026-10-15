#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/Diagnostic.h"
#include "common/StringMapInstances.h"
#include "common/Subscription.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// common_diagnostic_vector_instances.cpp


TEST_CASE("common.diagnosticVectorInstances.independentContainersOnThreads") {
    std::atomic<unsigned> failures{0};
    auto                  run = [&] {
        for (unsigned iteration = 0; iteration < 32; ++iteration) {
            std::vector<eve::Diagnostic> values;
            values.push_back(eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::string(2048, 'a')));
            auto copied  = values;
            copied.at(0) = eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "changed");
            copied.reserve(128);
            if (values.at(0).message().size() != 2048 || copied.at(0).message() != "changed") ++failures;
        }
    };
    std::thread a(run), b(run), c(run), d(run);
    a.join();
    b.join();
    c.join();
    d.join();
    CHECK(failures.load() == 0u);
}

// common_string_map_instances.cpp
namespace {
using AttributeMap = std::unordered_map<std::string, std::string>;
static_assert(std::is_move_constructible_v<AttributeMap>);
static_assert(std::is_copy_constructible_v<AttributeMap>);
}  // namespace


TEST_CASE("common.stringMap.independentContainersOnThreads") {
    std::atomic<int>         failures{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([worker, &failures] {
            for (int iteration = 0; iteration < 32; ++iteration) {
                const std::string expected(2048, static_cast<char>('a' + worker));
                AttributeMap      original{{"value", expected}};
                AttributeMap      copy(original);
                copy["iteration"] = std::to_string(iteration);
                copy.rehash(64);
                if (copy.at("value") != expected || original.size() != 1) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(failures.load() == 0);
}

// common_subscription.cpp
