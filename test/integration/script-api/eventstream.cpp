#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "GameEventTestSupport.h"
#include "common/Module.h"
#include "game_event/GameEvent.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::game_event;

namespace {

[[maybe_unused]] std::uint64_t appendSuccess(GameEventLog& stream, std::uint64_t serial, std::string type, std::string source,
                            std::string subject, std::string causation, std::string correlation, std::uint64_t tick,
                            std::uint32_t flags, std::string payload = "null") {
    auto result =
        stream.append(test::envelope(serial, std::move(type), std::move(source), std::move(subject),
                                     std::move(causation), std::move(correlation), tick, flags, std::move(payload)));
    REQUIRE(result.ok());
    return result.value().value();
}

}  // namespace


TEST_CASE("game_event.script.emitQueryCursorAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.GameEvent();
        local stream = module.newLog();
        local seq = stream.append("00000000-0000-4000-8000-000000000001", "promoted", "administration", "general-4", "", "career-4", 12, 1, "{\"rank\":4}");
        local cursor = stream.newConsumer(1);
        if (seq.ok && seq.value == 1 && stream.querySubject("general-4") == 1 && cursor.read(8) == 1) {
            local copy = module.newLog();
            if (copy.restore(stream.snapshotJson()).ok && copy.find(1).getType() == "promoted")
                result = cursor.batchAt(0).getCorrelation();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("career-4"));
}
