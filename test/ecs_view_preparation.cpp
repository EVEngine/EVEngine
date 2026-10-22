#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <ECS.hpp>

#include <functional>
#include <stdexcept>

namespace {
struct PreparedValue {
    inline static int                   constructed = 0;
    inline static int                   throwAt     = -1;
    inline static std::function<void()> onConstruct;
    int                                 value = 7;

    PreparedValue() {
        if (constructed == throwAt) throw std::runtime_error("injected construction failure");
        ++constructed;
        if (onConstruct) onConstruct();
    }
};

class PreparedEntity : public ecs::Entity {
public:
    ENTITY(PreparedEntity, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
class PreparedDerived : public PreparedEntity {
public:
    ENTITY(PreparedDerived, PreparedEntity)
};

struct ConstructionState {
    ConstructionState() {
        PreparedValue::constructed = 0;
        PreparedValue::throwAt     = -1;
        PreparedValue::onConstruct = {};
    }
    ~ConstructionState() {
        PreparedValue::throwAt     = -1;
        PreparedValue::onConstruct = {};
    }
};

struct ExplicitValue {
    explicit ExplicitValue(int input) : value(input) {}
    int value;
};
}  // namespace

TEST_CASE("ecs.viewPreparationPreservesWordAndPrefixBoundaries") {
    ConstructionState state;
    ecs::Table        table;
    auto*             buffer = table.getOrCreateManager<PreparedEntity>().getOrCreateComponentBuffer<PreparedValue>();
    buffer->ensure_space(193);
    for (const auto count : {0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 193u}) {
        buffer->engageUpTo(count);
        REQUIRE_EQ(PreparedValue::constructed, static_cast<int>(count));
        buffer->engageUpTo(count);
        REQUIRE_EQ(PreparedValue::constructed, static_cast<int>(count));
    }
    buffer->at(62).value = 91;
    buffer->reset_slot(63);
    buffer->reset_slot(64);
    buffer->reset_slot(128);
    buffer->engageUpTo(65);
    REQUIRE_EQ(PreparedValue::constructed, 195);
    REQUIRE(!buffer->has(128));
    REQUIRE_EQ(buffer->at(62).value, 91);
    buffer->engageUpTo(193);
    REQUIRE_EQ(PreparedValue::constructed, 196);
    REQUIRE(buffer->has(128));
}

TEST_CASE("ecs.viewPreparationRetriesThrowingConstruction") {
    ConstructionState state;
    ecs::Table        table;
    auto*             buffer = table.getOrCreateManager<PreparedEntity>().getOrCreateComponentBuffer<PreparedValue>();
    PreparedValue::throwAt   = 64;
    bool threw               = false;
    try {
        buffer->engageUpTo(130);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE_EQ(PreparedValue::constructed, 64);
    REQUIRE(!buffer->has(64));
    PreparedValue::throwAt = -1;
    buffer->engageUpTo(130);
    REQUIRE_EQ(PreparedValue::constructed, 130);
    REQUIRE(buffer->has(129));
}

TEST_CASE("ecs.viewPreparationObservesConstructorChangesWithinWord") {
    ConstructionState state;
    ecs::Table        table;
    auto*             buffer = table.getOrCreateManager<PreparedEntity>().getOrCreateComponentBuffer<PreparedValue>();
    buffer->engageUpTo(128);
    buffer->reset_slot(64);
    bool reset                 = false;
    PreparedValue::onConstruct = [&] {
        if (!reset) {
            reset = true;
            buffer->reset_slot(65);
        }
    };
    buffer->engageUpTo(128);
    REQUIRE_EQ(PreparedValue::constructed, 130);
    REQUIRE(buffer->has(64));
    REQUIRE(buffer->has(65));
}

TEST_CASE("ecs.viewPreparationKeepsInheritanceDeferAndTableIsolation") {
    ConstructionState state;
    ecs::Table        table;
    ecs::Table        other;
    ecs::EntityHandle first;
    for (int i = 0; i < 130; ++i) {
        auto* entity = PreparedDerived::create(table);
        if (i == 0) first = ecs::handle_of(entity);
    }
    for (int pass = 0; pass < 3; ++pass) {
        int count = 0;
        for (auto [value] : ecs::View<PreparedEntity, PreparedValue>(table)) {
            REQUIRE_EQ(value->value, 7);
            ++count;
        }
        REQUIRE_EQ(count, 130);
    }
    REQUIRE_EQ(PreparedValue::constructed, 130);
    {
        auto empty = ecs::View<PreparedEntity, PreparedValue>(other);
        REQUIRE(empty.begin() == empty.end());
    }
    ecs::EntityHandle staged;
    {
        ecs::ScopedDefer defer(table);
        staged = ecs::handle_of(PreparedDerived::create(table));
        ecs::DestroyEntity(ecs::try_get(first));
        int visible = 0;
        for (auto [value] : ecs::View<PreparedEntity, PreparedValue>(table)) {
            (void)value;
            ++visible;
        }
        REQUIRE_EQ(visible, 129);
    }
    REQUIRE(ecs::try_get(first) == nullptr);
    REQUIRE(ecs::try_get(staged) != nullptr);
    int published = 0;
    for (auto [value] : ecs::View<PreparedEntity, PreparedValue>(table)) {
        REQUIRE_EQ(value->value, 7);
        ++published;
    }
    REQUIRE_EQ(published, 130);
}

TEST_CASE("ecs.viewPreparationDoesNotRequireDefaultConstruction") {
    ecs::Table table;
    for (int i = 0; i < 130; ++i) {
        auto* entity = PreparedDerived::create(table);
        REQUIRE(entity != nullptr);
    }
    auto* buffer = table.getOrCreateManager<PreparedDerived>().getOrCreateComponentBuffer<ExplicitValue>();
    REQUIRE(buffer->emplace(129, 42));
    int count = 0;
    for (auto [value] : ecs::View<PreparedEntity, ExplicitValue>(table)) {
        REQUIRE_EQ(value->value, 42);
        ++count;
    }
    REQUIRE_EQ(count, 1);
}

TEST_CASE("ecs.idleCommitKeepsNestedAndExplicitPublication") {
    ecs::Table        table;
    ecs::EntityHandle first;
    ecs::EntityHandle second;
    {
        ecs::ScopedDefer outer(table);
        first = ecs::handle_of(PreparedDerived::create(table));
        REQUIRE(!ecs::is_entity_visible(ecs::try_get(first)));
        table.commit();
        REQUIRE(table.is_deferred());
        REQUIRE(ecs::is_entity_visible(ecs::try_get(first)));
        {
            ecs::ScopedDefer inner(table);
            second = ecs::handle_of(PreparedDerived::create(table));
        }
        REQUIRE(!ecs::is_entity_visible(ecs::try_get(second)));
    }
    REQUIRE(ecs::is_entity_visible(ecs::try_get(second)));
    table.commit();
    REQUIRE(ecs::is_entity_visible(ecs::try_get(first)));
    REQUIRE(ecs::is_entity_visible(ecs::try_get(second)));
    {
        ecs::ScopedDefer scope(table);
        auto*            immediate = ecs::CreateEntity<PreparedDerived>(table, true);
        REQUIRE(ecs::is_entity_visible(immediate));
        ecs::DestroyEntity(immediate);
        ecs::DestroyEntity(ecs::try_get(first));
    }
    REQUIRE(ecs::try_get(first) == nullptr);
    REQUIRE(ecs::is_entity_visible(ecs::try_get(second)));
}

TEST_CASE("ecs.idleCommitCancelsStagedAndRearmsAfterReuse") {
    ecs::Table        table;
    ecs::EntityHandle cancelled;
    {
        ecs::ScopedDefer scope(table);
        cancelled = ecs::handle_of(PreparedDerived::create(table));
        ecs::DestroyEntity(ecs::try_get(cancelled));
        table.commit();
        REQUIRE(ecs::try_get(cancelled) == nullptr);
    }
    ecs::EntityHandle replacement;
    {
        ecs::ScopedDefer scope(table);
        replacement = ecs::handle_of(PreparedDerived::create(table));
        REQUIRE_EQ(replacement.id, cancelled.id);
        REQUIRE(!ecs::is_entity_visible(ecs::try_get(replacement)));
    }
    REQUIRE(ecs::is_entity_visible(ecs::try_get(replacement)));
    REQUIRE(ecs::try_get(cancelled) == nullptr);
}

TEST_CASE("ecs.failedViewPreparationReleasesItsDeferScope") {
    ConstructionState state;
    ecs::Table        table;
    for (int i = 0; i < 130; ++i) {
        auto* entity = PreparedDerived::create(table);
        REQUIRE(entity != nullptr);
    }
    PreparedValue::throwAt = 64;
    bool threw             = false;
    try {
        auto view = ecs::View<PreparedEntity, PreparedValue>(table);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(!table.is_deferred());
    REQUIRE_EQ(PreparedValue::constructed, 64);
    PreparedValue::throwAt = -1;
    auto* fresh            = PreparedDerived::create(table);
    REQUIRE(ecs::is_entity_visible(fresh));
    int count = 0;
    for (auto [value] : ecs::View<PreparedEntity, PreparedValue>(table)) {
        REQUIRE_EQ(value->value, 7);
        ++count;
    }
    REQUIRE_EQ(count, 131);
    REQUIRE_EQ(PreparedValue::constructed, 131);
}

TEST_CASE("ecs.failedNestedViewPreservesTheOuterDeferScope") {
    ConstructionState state;
    ecs::Table        table;
    ecs::EntityHandle pending;
    {
        ecs::ScopedDefer outer(table);
        pending                = ecs::handle_of(PreparedDerived::create(table));
        PreparedValue::throwAt = 0;
        bool threw             = false;
        try {
            auto view = ecs::View<PreparedEntity, PreparedValue>(table);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        REQUIRE(threw);
        REQUIRE(table.is_deferred());
        REQUIRE(!ecs::is_entity_visible(ecs::try_get(pending)));
        PreparedValue::throwAt = -1;
    }
    REQUIRE(!table.is_deferred());
    REQUIRE(ecs::is_entity_visible(ecs::try_get(pending)));
}

TEST_CASE("ecs.entityOnlyViewKeepsDeferredIterationAndEmptyComparison") {
    ecs::Table table;
    auto*      base    = PreparedEntity::create(table);
    auto*      derived = PreparedDerived::create(table);
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);
    int visited = 0;
    {
        auto view = ecs::View<PreparedEntity>(table);
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto* staged = PreparedDerived::create(table);
            REQUIRE(!ecs::is_entity_visible(staged));
            ecs::DestroyEntity(it.entity());
            ++visited;
        }
    }
    REQUIRE_EQ(visited, 2);
    {
        auto view      = ecs::View<PreparedEntity>(table);
        int  published = 0;
        for (auto it = view.begin(); it != view.end(); ++it) {
            ecs::DestroyEntity(it.entity());
            ++published;
        }
        REQUIRE_EQ(published, 2);
    }
    auto empty = ecs::View<PreparedEntity>(table);
    REQUIRE(empty.begin() == empty.end());
}

TEST_CASE("ecs.viewPreparationSkipsDeadSlotsAndInitializesReuse") {
    ConstructionState              state;
    ecs::Table                     table;
    std::vector<ecs::EntityHandle> handles;
    for (int i = 0; i < 130; ++i) handles.push_back(ecs::handle_of(PreparedDerived::create(table)));
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE_EQ(PreparedValue::constructed, 130);
    for (unsigned i = 1; i < handles.size(); ++i) ecs::DestroyEntity(ecs::try_get(handles[i]));
    for (int pass = 0; pass < 2; ++pass) {
        int visible = 0;
        for (auto [value] : ecs::View<PreparedEntity, PreparedValue>(table)) {
            REQUIRE_EQ(value->value, 7);
            ++visible;
        }
        REQUIRE_EQ(visible, 1);
        REQUIRE_EQ(PreparedValue::constructed, 130);
    }
    auto*      reused      = PreparedDerived::create(table);
    const auto replacement = ecs::handle_of(reused);
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE_EQ(PreparedValue::constructed, 131);
    REQUIRE(ecs::try_get(replacement) != nullptr);
    for (unsigned i = 1; i < handles.size(); ++i) REQUIRE(ecs::try_get(handles[i]) == nullptr);
    ecs::DestroyEntity(reused);
    ecs::DestroyEntity(ecs::try_get(handles.front()));
    {
        auto view = ecs::View<PreparedEntity, PreparedValue>(table);
        REQUIRE(view.begin() == view.end());
    }
    REQUIRE_EQ(PreparedValue::constructed, 131);
}

TEST_CASE("ecs.viewPreparationKeepsStagingAndSkipsCancelledEntities") {
    ConstructionState state;
    ecs::Table        table;
    ecs::EntityHandle staged;
    {
        ecs::ScopedDefer defer(table);
        staged          = ecs::handle_of(PreparedDerived::create(table));
        auto* cancelled = PreparedDerived::create(table);
        ecs::DestroyEntity(cancelled);
        {
            auto view = ecs::View<PreparedEntity, PreparedValue>(table);
            REQUIRE(view.begin() == view.end());
        }
        REQUIRE_EQ(PreparedValue::constructed, 1);
    }
    REQUIRE(ecs::try_get(staged) != nullptr);
    {
        auto view = ecs::View<PreparedEntity, PreparedValue>(table);
        REQUIRE(view.begin() != view.end());
    }
    REQUIRE_EQ(PreparedValue::constructed, 1);
}

TEST_CASE("ecs.viewPreparationObservesDestructionDuringConstruction") {
    ConstructionState state;
    ecs::Table        table;
    PreparedDerived::create(table);
    auto* later                = PreparedDerived::create(table);
    PreparedValue::onConstruct = [&] { ecs::DestroyEntity(later); };
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE_EQ(PreparedValue::constructed, 1);
}

TEST_CASE("ecs.viewPreparationInvalidatesCachedSparsePass") {
    ConstructionState state;
    ecs::Table        table;
    auto*             first = PreparedDerived::create(table);
    auto*             dead  = PreparedDerived::create(table);
    ecs::DestroyEntity(dead);
    for (int pass = 0; pass < 3; ++pass) {
        auto view = ecs::View<PreparedEntity, PreparedValue>(table);
    }
    auto* buffer = first->getComponentManager().getOrCreateComponentBuffer<PreparedValue>();
    buffer->reset_slot(first->id);
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE_EQ(PreparedValue::constructed, 2);
    for (int pass = 0; pass < 2; ++pass) {
        auto view = ecs::View<PreparedEntity, PreparedValue>(table);
    }
    auto* replacement = PreparedDerived::create(table);
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE_EQ(PreparedValue::constructed, 3);
    REQUIRE(buffer->has(replacement->id));
}

TEST_CASE("ecs.viewPreparationDoesNotCacheConstructorSideEffects") {
    ConstructionState state;
    ecs::Table        table;
    auto*             first = PreparedDerived::create(table);
    PreparedDerived::create(table);
    auto* buffer               = first->getComponentManager().getOrCreateComponentBuffer<PreparedValue>();
    PreparedValue::onConstruct = [&] {
        if (PreparedValue::constructed == 2) buffer->reset_slot(first->id);
    };
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE(!buffer->has(first->id));
    PreparedValue::onConstruct = {};
    { auto view = ecs::View<PreparedEntity, PreparedValue>(table); }
    REQUIRE(buffer->has(first->id));
    REQUIRE_EQ(PreparedValue::constructed, 3);
}
