#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/VersionedRegistry.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

struct Value {
    int             payload;
    eve::Generation generation;

    Value() = delete;
    explicit Value(int value) : payload(value) {}
};

struct EventData {
    eve::SchemaVersion version;
};

static_assert(!std::is_default_constructible_v<Value>);

struct ThrowingValue {
    inline static bool throwOnCopy = false;
    inline static bool throwOnMoveAssignment = false;

    int             payload;
    eve::Generation generation;

    ThrowingValue() = delete;
    explicit ThrowingValue(int value) : payload(value) {}

    ThrowingValue(const ThrowingValue& other) {
        if (throwOnCopy) throw std::runtime_error("injected value copy failure");
        payload = other.payload;
        generation = other.generation;
    }

    ThrowingValue& operator=(const ThrowingValue& other) {
        if (throwOnCopy) throw std::runtime_error("injected value assignment failure");
        payload = other.payload;
        generation = other.generation;
        return *this;
    }

    ThrowingValue(ThrowingValue&&) noexcept = default;

    ThrowingValue& operator=(ThrowingValue&& other) {
        if (throwOnMoveAssignment) throw std::runtime_error("injected value move assignment failure");
        payload = other.payload;
        generation = other.generation;
        return *this;
    }
};

struct ThrowingEventData {
    inline static bool throwOnCopy = false;
    inline static int  movesBeforeThrow = -1;

    eve::SchemaVersion version;

    ThrowingEventData() = default;
    explicit ThrowingEventData(eve::SchemaVersion value) : version(value) {}

    ThrowingEventData(const ThrowingEventData& other) {
        if (throwOnCopy) throw std::runtime_error("injected event copy failure");
        version = other.version;
    }

    ThrowingEventData& operator=(const ThrowingEventData& other) {
        if (throwOnCopy) throw std::runtime_error("injected event assignment failure");
        version = other.version;
        return *this;
    }

    ThrowingEventData(ThrowingEventData&& other) {
        if (movesBeforeThrow == 0) throw std::runtime_error("injected event move failure");
        if (movesBeforeThrow > 0) --movesBeforeThrow;
        version = other.version;
    }
    ThrowingEventData& operator=(ThrowingEventData&&) noexcept = default;
};

using Registry = eve::VersionedRegistry<std::string, Value, EventData>;
using ThrowingValueRegistry = eve::VersionedRegistry<std::string, ThrowingValue, EventData>;
using ThrowingEventRegistry = eve::VersionedRegistry<std::string, Value, ThrowingEventData>;

Registry makeRegistry() {
    return Registry([](Value& value, eve::Generation generation) { value.generation = generation; });
}

}  // namespace

TEST_CASE("versionedRegistry.lifecycleUsesGenerationAndTombstone") {
    auto registry = makeRegistry();

    auto inserted = registry.insert("unit", Value{1}, EventData{eve::SchemaVersion(7)});
    REQUIRE(inserted.ok());
    const auto first = std::move(inserted).takeValue();
    CHECK_EQ(first.generation.value(), uint64_t{1});
    REQUIRE(registry.resolve(first).ok());
    CHECK_EQ(registry.resolve(first).value().get().generation.value(), uint64_t{1});

    auto duplicate = registry.insert("unit", Value{2}, EventData{eve::SchemaVersion(8)});
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    REQUIRE(duplicate.error() != nullptr);
    CHECK_EQ(duplicate.error()->code(), eve::DiagnosticCode::AlreadyExists);

    auto replaced = registry.replace("unit", Value{3}, EventData{eve::SchemaVersion(9)});
    REQUIRE(replaced.ok());
    const auto second = std::move(replaced).takeValue();
    CHECK_EQ(second.generation.value(), uint64_t{2});
    CHECK(registry.isStale(first));
    CHECK(!registry.isStale(second));

    auto removed = registry.remove(std::string("unit"), EventData{eve::SchemaVersion(9)});
    REQUIRE(removed.ok());
    const auto tombstone = std::move(removed).takeValue();
    CHECK_EQ(tombstone.generation.value(), uint64_t{3});
    CHECK(registry.isTombstone("unit"));
    CHECK(registry.isStale(tombstone));
    auto stale = registry.resolve(tombstone);
    CHECK(!stale.ok());
    CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);

    auto generation = registry.generationOf("unit");
    REQUIRE(generation.ok());
    CHECK_EQ(generation.value().value(), uint64_t{3});

    auto revived = registry.insert("unit", Value{4}, EventData{eve::SchemaVersion(10)});
    REQUIRE(revived.ok());
    const auto fourth = std::move(revived).takeValue();
    CHECK_EQ(fourth.generation.value(), uint64_t{4});
    CHECK(registry.isStale(tombstone));
    CHECK_EQ(registry.size(), std::size_t{1});
    CHECK_EQ(registry.tombstoneCount(), std::size_t{0});
}

TEST_CASE("versionedRegistry.eventsAndStateIncludeTombstones") {
    auto registry = makeRegistry();
    REQUIRE(registry.insert("b", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    REQUIRE(registry.insert("a", Value{2}, EventData{eve::SchemaVersion(2)}).ok());
    auto removed = registry.remove(std::string("a"), EventData{eve::SchemaVersion(2)});
    REQUIRE(removed.ok());

    CHECK_EQ(registry.eventCount(), std::size_t{3});
    REQUIRE(registry.eventAt(0) != nullptr);
    CHECK_EQ(registry.eventAt(0)->operation, eve::RegistryOperation::Insert);
    CHECK_EQ(registry.eventAt(0)->sequence.value(), uint64_t{1});
    REQUIRE(registry.eventAt(2) != nullptr);
    CHECK(registry.eventAt(2)->isTombstone());
    CHECK_EQ(registry.eventAt(2)->operation, eve::RegistryOperation::Remove);
    CHECK_EQ(registry.eventAt(2)->generation.value(), uint64_t{2});

    const auto image = registry.snapshotState();
    auto restored = makeRegistry();
    auto restoreResult = restored.restoreState(image);
    REQUIRE(restoreResult.ok());
    CHECK_EQ(restored.size(), registry.size());
    CHECK_EQ(restored.tombstoneCount(), registry.tombstoneCount());
    CHECK(restored.isTombstone("a"));
    CHECK_EQ(restored.eventCount(), registry.eventCount());
    auto restoredValue = restored.resolve("b");
    REQUIRE(restoredValue.ok());
    CHECK_EQ(restoredValue.value().get().generation.value(), uint64_t{1});
    auto restoredGeneration = restored.generationOf("a");
    REQUIRE(restoredGeneration.ok());
    CHECK_EQ(restoredGeneration.value().value(), uint64_t{2});
}

TEST_CASE("versionedRegistry.subscriptionIsCanonicalEventSource") {
    auto registry = makeRegistry();
    std::vector<eve::RegistryOperation> operations;
    auto subscription = registry.subscribe([&](const Registry::Event& event) {
        operations.push_back(event.operation);
    });
    REQUIRE(registry.insert("unit", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    REQUIRE(registry.replace("unit", Value{2}, EventData{eve::SchemaVersion(2)}).ok());
    auto removed = registry.remove(std::string("unit"), EventData{eve::SchemaVersion(2)});
    REQUIRE(removed.ok());
    CHECK_EQ(operations, std::vector<eve::RegistryOperation>({eve::RegistryOperation::Insert,
                                                               eve::RegistryOperation::Replace,
                                                               eve::RegistryOperation::Remove}));
    subscription.dispose();
    REQUIRE(registry.insert("other", Value{3}, EventData{eve::SchemaVersion(3)}).ok());
    CHECK_EQ(operations.size(), std::size_t{3});
}

TEST_CASE("versionedRegistry.preparationFailureLeavesCanonicalStateUntouched") {
    ThrowingValue::throwOnCopy = false;
    ThrowingValueRegistry registry;
    auto inserted = registry.insert("unit", ThrowingValue{1}, EventData{eve::SchemaVersion(1)});
    REQUIRE(inserted.ok());
    const auto original = std::move(inserted).takeValue();

    ThrowingValue::throwOnCopy = true;
    auto failed = registry.replace("unit", ThrowingValue{2}, EventData{eve::SchemaVersion(2)});
    ThrowingValue::throwOnCopy = false;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{1});
    CHECK(!registry.isStale(original));
    auto value = registry.resolve(original);
    REQUIRE(value.ok());
    CHECK_EQ(value.value().get().payload, 1);

    ThrowingValue::throwOnMoveAssignment = true;
    auto moveAssignmentFailure = registry.replace("unit", ThrowingValue{3},
                                                   EventData{eve::SchemaVersion(3)});
    ThrowingValue::throwOnMoveAssignment = false;
    CHECK(!moveAssignmentFailure.ok());
    CHECK_EQ(moveAssignmentFailure.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{1});
    auto unchanged = registry.resolve(original);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().get().payload, 1);
}

TEST_CASE("versionedRegistry.eventPreparationFailureLeavesStateUntouched") {
    ThrowingEventRegistry registry;
    ThrowingEventData::throwOnCopy = true;
    auto failed = registry.insert("unit", Value{1}, ThrowingEventData{eve::SchemaVersion(1)});
    ThrowingEventData::throwOnCopy = false;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{0});
    CHECK_EQ(registry.size(), std::size_t{0});
}

TEST_CASE("versionedRegistry.eventMoveFailureLeavesStateUntouched") {
    ThrowingEventRegistry registry;
    ThrowingEventData data{eve::SchemaVersion(1)};
    // One move constructs insert's parameter and one constructs mutate's
    // parameter; the third move constructs the candidate event.
    ThrowingEventData::movesBeforeThrow = 2;
    auto failed = registry.insert("unit", Value{1}, std::move(data));
    ThrowingEventData::movesBeforeThrow = -1;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{0});
    CHECK_EQ(registry.size(), std::size_t{0});
}

TEST_CASE("versionedRegistry.callbackFailureReportsAppliedWithoutRollback") {
    auto registry = makeRegistry();
    std::vector<eve::EventSequence> observed;
    auto observer = registry.subscribe([&](const Registry::Event& event) {
        observed.push_back(event.sequence);
    });
    auto throwingObserver = registry.subscribe([](const Registry::Event&) {
        throw std::runtime_error("injected observer failure");
    });

    auto inserted = registry.insert("unit", Value{1}, EventData{eve::SchemaVersion(1)});
    REQUIRE(inserted.ok());
    CHECK_EQ(inserted.code(), eve::StatusCode::Applied);
    REQUIRE_EQ(inserted.diagnostics().size(), std::size_t{1});
    CHECK_EQ(inserted.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
    CHECK_EQ(observed.size(), std::size_t{1});
    CHECK(registry.contains("unit"));
    CHECK_EQ(registry.eventCount(), std::size_t{1});

    observer.dispose();
    throwingObserver.dispose();
}

TEST_CASE("versionedRegistry.restoreProjectorFailureIsTransactional") {
    auto source = makeRegistry();
    REQUIRE(source.insert("source", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    const auto image = source.snapshotState();

    bool failProjection = false;
    Registry target([&](Value& value, eve::Generation generation) {
        value.generation = generation;
        if (failProjection) throw std::runtime_error("injected generation projection failure");
    });
    REQUIRE(target.insert("old", Value{9}, EventData{eve::SchemaVersion(1)}).ok());
    const auto oldState = target.snapshotState();

    failProjection = true;
    auto failed = target.restoreState(image);
    failProjection = false;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    auto oldValue = target.resolve("old");
    REQUIRE(oldValue.ok());
    CHECK_EQ(oldValue.value().get().payload, 9);
    CHECK(!target.contains("source"));
    CHECK_EQ(target.eventCount(), oldState.events.size());
}
