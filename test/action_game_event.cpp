#include "action/ActionGameplayEventBlock.h"
#include "action/ActionNotifyRegistry.h"
#include "action/game_event/ActionGameEventSink.h"
#include "game_event/GameEvent.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <cstdint>
#include <span>

namespace {

eve::SubjectRef subject(const char* text) {
    auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId id(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

}  // namespace

TEST_CASE("actionGameEvent.validatesAndAppendsToPersistentLog") {
    eve::Value::Object invalidPayload{{"tag", eve::Value("not canonical")}};
    CHECK(!eve::action::ActionGameplayEventBinding::fromPayload(invalidPayload).ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionTimelineEvent event;
    event.kind = eve::action::ActionTimelineEventKind::Notify;
    event.trackId = id("action-track:gameplay");
    event.itemId = id("action-item:event");
    event.type = id("gameplay:event");
    event.payload = {{"tag", eve::Value("Combat.Action.Hit")},
                     {"targetIndex", eve::Value(std::int64_t{0})},
                     {"data", eve::Value(eve::Value::Object{{"power", eve::Value(std::int64_t{7})}})}};

    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(71);
    ecs::EntityHandle sourceHandle{nullptr, typeid(void), 3, 1};
    ecs::EntityHandle targetHandle{nullptr, typeid(void), 5, 2};
    context.source = sourceHandle;
    context.targets.push_back(targetHandle);
    context.tick = eve::SimulationTick(42);
    auto absent = registry.value().dispatch(event, context);
    CHECK(!absent.ok());
    CHECK_EQ(absent.status().code(), eve::StatusCode::NotFound);

    const auto timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1710000000123));
    eve::game_event::GameEventLog log(
        [](std::span<std::uint8_t> bytes) {
            for (std::size_t index = 0; index < bytes.size(); ++index)
                bytes[index] = static_cast<std::uint8_t>(index + 1);
            return true;
        },
        [timestamp] { return timestamp; });
    const auto sourceSubject = subject("018f0f4e-6b3c-7ab1-8def-0123456789ab");
    const auto targetSubject = subject("018f0f4e-6b3c-7ab2-8def-0123456789ab");
    eve::action::game_event_adapter::ActionGameEventSink sink(
        log, [=](ecs::EntityHandle handle) -> eve::Result<eve::SubjectRef> {
            if (handle.id == sourceHandle.id) return eve::Result<eve::SubjectRef>::success(sourceSubject);
            if (handle.id == targetHandle.id) return eve::Result<eve::SubjectRef>::success(targetSubject);
            return eve::Result<eve::SubjectRef>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "entity is stale", "entity"));
        });
    sink.setEnabled(true);
    REQUIRE(registry.value().dispatch(event, context).ok());
    REQUIRE(sink.lastSequence().has_value());
    const auto* stored = log.find(*sink.lastSequence());
    REQUIRE(stored != nullptr);
    CHECK_EQ(stored->type, std::string("Combat.Action.Hit"));
    CHECK_EQ(stored->source, sourceSubject.format());
    CHECK_EQ(stored->subject, targetSubject.format());
    CHECK_EQ(stored->schemaId, id("action:gameplay-event-v1"));
    CHECK_EQ(stored->schemaVersion.value(), 1U);
    CHECK_EQ(stored->tick, eve::SimulationTick(42));
    CHECK_EQ(stored->payload, std::string("{\"power\":7}"));

    sink.setEnabled(false);
    CHECK(!registry.value().dispatch(event, context).ok());
    CHECK_EQ(log.size(), 1);
}
