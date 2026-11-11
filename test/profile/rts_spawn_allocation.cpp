// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"rts"}
// Process-local allocation interception needs an OBJECT consumer, as in the Crowd allocation contract.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include "attributes/AttributeSet.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "crowd/Crowd.h"
#include "rts/RTS.h"
#include "sensing/Sensing.h"
#include "weapon/Weapon.h"
namespace {
thread_local bool        measuring    = false;
thread_local std::size_t allocations  = 0;
thread_local std::size_t failureIndex = std::numeric_limits<std::size_t>::max();
std::size_t              countUnits(ecs::Table& table) {
    std::size_t count = 0;
    auto        view  = ecs::View<eve::rts::Unit>(table);
    for (auto it = view.begin(); it != view.end(); ++it) ++count;
    return count;
}
std::size_t countWeapons(ecs::Table& table) {
    std::size_t count = 0;
    auto        view  = ecs::View<eve::weapon::WeaponEntity>(table);
    for (auto it = view.begin(); it != view.end(); ++it) ++count;
    return count;
}
std::size_t queryCount() { return eve::cap::detail::listenerCountRaw(eve::IStateQuery::capabilityName); }
std::size_t mutationCount() { return eve::cap::detail::listenerCountRaw(eve::IStateMutation::capabilityName); }
int         checkAttributeMoves() {
    const auto beforeQueries   = queryCount();
    const auto beforeMutations = mutationCount();
    {
        eve::attributes::AttributeSet source("move-source");
        source.setBase("hp", 7);
        eve::attributes::AttributeSet target("move-target");
        target.setBase("hp", 3);
        eve::IStateQuery* targetAdapter = nullptr;
        eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery* query) {
            if (query->attribute("move-target", "hp")) targetAdapter = query;
        });
        if (!targetAdapter) return 10;
        allocations = 0;
        measuring   = true;
        eve::attributes::AttributeSet moved(std::move(source));
        target    = std::move(moved);
        measuring = false;
        if (allocations != 0) return 11;
        if (queryCount() != beforeQueries + 1 || mutationCount() != beforeMutations + 1) return 12;
        auto value = targetAdapter->attribute("move-source", "hp");
        if (!value || value->asDouble() != 7) return 13;
        eve::StateMutation mutation{"move-source", "hp", eve::Value(11.0)};
        auto*              writer = dynamic_cast<eve::IStateMutation*>(targetAdapter);
        if (!writer || !writer->apply(std::span(&mutation, 1), {}).ok()) return 14;
        value = targetAdapter->attribute("move-source", "hp");
        if (!value || value->asDouble() != 11 || queryCount() != beforeQueries + 1) return 15;
        allocations          = 0;
        measuring            = true;
        const auto listeners = eve::cap::listenerCount<eve::IStateQuery>();
        for (std::size_t index = 0; index < listeners; ++index) (void)eve::cap::listenerAt<eve::IStateQuery>(index);
        (void)eve::cap::query<eve::IStateQuery>();
        measuring = false;
        if (allocations != 0) return 16;
    }
    if (queryCount() != beforeQueries || mutationCount() != beforeMutations) return 17;
    {
        std::optional<eve::attributes::AttributeSet> surviving;
        {
            eve::attributes::AttributeSet temporary("outlives-source");
            temporary.setBase("hp", 9);
            allocations = 0;
            measuring   = true;
            surviving.emplace(std::move(temporary));
            measuring = false;
            if (allocations != 0) return 18;
        }
        int matches = 0;
        eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery* query) {
            auto value = query->attribute("outlives-source", "hp");
            if (value && value->asDouble() == 9) ++matches;
        });
        if (matches != 1) return 19;
    }
    if (queryCount() != beforeQueries || mutationCount() != beforeMutations) return 20;
    {
        eve::attributes::AttributeSet source("prepared-source-with-an-owning-subject");
        source.setBase("health-attribute-with-owning-storage", 7);
        eve::attributes::AttributeSet target("prepared-target-with-an-owning-subject");
        target.setBase("health-attribute-with-owning-storage", 1);
        allocations = 0;
        measuring   = true;
        { auto cancelled = source.prepareCopy(); }
        measuring                         = false;
        const auto preparationAllocations = allocations;
        for (std::size_t fail = 0; fail < preparationAllocations; ++fail) {
            allocations  = 0;
            failureIndex = fail;
            measuring    = true;
            bool threw   = false;
            try {
                auto cancelled = source.prepareCopy();
            } catch (const std::bad_alloc&) {
                threw = true;
            }
            measuring    = false;
            failureIndex = std::numeric_limits<std::size_t>::max();
            if (!threw || queryCount() != beforeQueries + 2 || mutationCount() != beforeMutations + 2 ||
                source.getBase("health-attribute-with-owning-storage") != 7 ||
                target.getBase("health-attribute-with-owning-storage") != 1)
                return 21;
        }
        auto candidate = source.prepareCopy();
        candidate.setBase("health-attribute-with-owning-storage", 42);
        allocations  = 0;
        failureIndex = 0;
        measuring    = true;
        target.commitPrepared(std::move(candidate));
        measuring    = false;
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (allocations != 0 || target.getBase("health-attribute-with-owning-storage") != 42 ||
            target.subject() != "prepared-target-with-an-owning-subject" || queryCount() != beforeQueries + 2 ||
            mutationCount() != beforeMutations + 2)
            return 22;
        std::printf("Detached attribute preparation: %zu allocation failpoints; commit allocates zero\n",
                            preparationAllocations);
    }
    if (queryCount() != beforeQueries || mutationCount() != beforeMutations) return 23;
    std::puts(
        "Attribute moves allocate zero; registered adapter follows ownership and survives mutation; teardown restores "
                "listeners");
    return 0;
}
int checkUnitBatch() {
    using namespace eve;
    auto             world = std::make_shared<ecs::Table>();
    ecs::ScopedTable scope(*world);
    crowd::Crowd     crowd;
    crowd.setClampToField(false);
    rts::RTS facade(world);
    facade.setCrowdProvider(&crowd);
    definitions::DefinitionRegistry registry;
    auto                            loaded = rts::RTSContentLoader::load(registry, R"({
        "weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
        "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35}]
    })");
    if (!loaded) return 60;
    auto recipe = rts::PreparedUnitArchetype::prepare(registry, *LogicalId::parse("unit:fighter"));
    if (!recipe) return 61;
    const auto id      = *PersistentId::parse("01900000-0000-7000-8000-000000000699");
    auto       faction = facade.newFaction(SubjectRef::fromPersistentId(id.child("faction")));
    if (!faction) return 62;
    std::vector<rts::UnitBatchRequest> requests;
    requests.push_back({SubjectRef::fromPersistentId(id.child("a")), recipe.value(), 0, 0});
    requests.push_back({SubjectRef::fromPersistentId(id.child("b")), recipe.value(), 0, 0});
    auto prepared = facade.prepareUnitBatch(*faction.value(), requests);
    if (!prepared || facade.unitCount() != 0 || crowd.getAgentCount() != 0 || countWeapons(*world) != 0) return 63;
    auto blocked = prepared.value().commit();
    if (!blocked || blocked.value().state != rts::UnitBatchState::Blocked) return 64;
    if (facade.unitCount() != 0 || crowd.getAgentCount() != 0 || countWeapons(*world) != 0 ||
        !faction.value()->members()->units.empty())
        return 65;
    requests[1].x              = 4;
    auto command               = std::make_shared<rts::CommandSpec>();
    command->kind              = rts::OrderKind::AttackMove;
    command->target            = {80, 90};
    requests[0].initialCommand = command;
    requests[1].initialCommand = command;
    {
        auto view = ecs::View<rts::Unit>(*world);
        if (!world->is_deferred()) return 89;
        if (facade.prepareUnitBatch(*faction.value(), requests).ok()) return 90;
    }
    auto inView = facade.prepareUnitBatch(*faction.value(), requests);
    if (!inView) return 91;
    {
        auto view = ecs::View<rts::Unit>(*world);
        if (inView.value().commit().ok()) return 92;
    }
    if (facade.unitCount() != 0 || crowd.getAgentCount() != 0) return 93;
    command->target.x = std::numeric_limits<float>::quiet_NaN();
    if (facade.prepareUnitBatch(*faction.value(), requests).ok()) return 95;
    command->target = {80, 90};
    auto target     = facade.newUnit(SubjectRef::fromPersistentId(id.child("command-target")));
    if (!target) return 96;
    command->targetEntity = ecs::handle_of(target.value());
    auto staleTarget      = facade.prepareUnitBatch(*faction.value(), requests);
    if (!staleTarget) return 97;
    target.value()->release();
    if (staleTarget.value().commit().ok() || facade.unitCount() != 0 || crowd.getAgentCount() != 0) return 98;
    command->targetEntity = {};
    auto retry            = facade.prepareUnitBatch(*faction.value(), requests);
    if (!retry) return 66;
    command->target = {200, 300};  // Mutating the caller alias must not alter the prepared command.
    auto committed  = retry.value().commit();
    if (!committed || committed.value().state != rts::UnitBatchState::Committed ||
        committed.value().subjects.size() != 2 || facade.unitCount() != 2 || crowd.getAgentCount() != 2 ||
        countWeapons(*world) != 2 || faction.value()->members()->units.size() != 2)
        return 67;
    for (const auto& request : requests) {
        auto* unit = facade.findUnit(request.subject);
        if (!unit || unit->durability()->state.health != 40 || unit->motion()->x != request.x ||
            unit->motion()->y != request.y || unit->crowd()->runtimeBindingKey != request.subject.format())
            return 68;
        auto order = unit->orders()->values.current();
        if (!order || order.value().kind != rts::OrderKind::AttackMove || order.value().target.x != 80 ||
            order.value().target.y != 90 || unit->orders()->values.orderCount() != 1)
            return 94;
        const auto agent = crowd.readAgent({unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner});
        if (!agent || agent.value().x != request.x || agent.value().y != request.y) return 69;
    }
    // A later batch blocked by a committed neighbor cannot move or replace it.
    auto neighborRequest    = requests[0];
    neighborRequest.subject = SubjectRef::fromPersistentId(id.child("blocked-neighbor"));
    auto neighborCandidate  = facade.prepareUnitBatch(*faction.value(), std::span(&neighborRequest, 1));
    if (!neighborCandidate) return 81;
    auto neighborBlocked = neighborCandidate.value().commit();
    if (!neighborBlocked || neighborBlocked.value().state != rts::UnitBatchState::Blocked ||
        neighborBlocked.value().placementStatus.isSuccess() || facade.unitCount() != 2 || crowd.getAgentCount() != 2 ||
        countWeapons(*world) != 2)
        return 82;
    auto* existing  = facade.findUnit(requests[0].subject);
    auto  unchanged = crowd.readAgent({existing->crowd()->runtimeAgent, existing->crowd()->runtimeOwner});
    if (!unchanged || unchanged.value().x != 0 || unchanged.value().y != 0) return 83;
    if (retry.value().commit().ok()) return 70;
    if (facade.prepareUnitBatch(*faction.value(), requests).ok()) return 71;
    std::optional<rts::PreparedUnitBatch> orphan;
    {
        rts::RTS temporary(world);
        temporary.setCrowdProvider(&crowd);
        auto otherFaction = temporary.newFaction(SubjectRef::fromPersistentId(id.child("other-faction")));
        if (!otherFaction) return 72;
        requests[0].subject = SubjectRef::fromPersistentId(id.child("orphan"));
        auto candidate      = temporary.prepareUnitBatch(*otherFaction.value(), std::span(requests.data(), 1));
        if (!candidate) return 73;
        orphan.emplace(std::move(candidate).takeValue());
    }
    if (orphan->commit().ok() || crowd.getAgentCount() != 2) return 74;
    {
        rts::RTS isolatedOwner(world);
        auto     team = isolatedOwner.newFaction(SubjectRef::fromPersistentId(id.child("missing-provider")));
        if (!team) return 84;
        if (isolatedOwner.prepareUnitBatch(*team.value(), std::span(requests.data(), 1)).ok()) return 85;
        isolatedOwner.setCrowdProvider(&crowd);
        auto deadFaction = isolatedOwner.prepareUnitBatch(*team.value(), std::span(requests.data(), 1));
        if (!deadFaction) return 86;
        team.value()->release();
        if (deadFaction.value().commit().ok()) return 87;
        if (isolatedOwner.unitCount() != 0 || crowd.getAgentCount() != 2) return 88;
    }
    const auto attempt = [&](std::size_t fail, bool inject) -> std::size_t {
        auto             isolated = std::make_shared<ecs::Table>();
        ecs::ScopedTable isolatedScope(*isolated);
        crowd::Crowd     backend;
        backend.setClampToField(false);
        rts::RTS domain(isolated);
        domain.setCrowdProvider(&backend);
        auto team = domain.newFaction(SubjectRef::fromPersistentId(id.child("allocation-faction")));
        if (!team) std::exit(75);
        std::vector<rts::UnitBatchRequest> batch;
        batch.push_back({SubjectRef::fromPersistentId(id.child("allocation-a")), recipe.value(), 10, 10});
        batch.push_back({SubjectRef::fromPersistentId(id.child("allocation-b")), recipe.value(), 14, 10});
        batch[0].initialCommand = command;
        batch[1].initialCommand = command;
        auto candidate          = domain.prepareUnitBatch(*team.value(), batch);
        if (!candidate) std::exit(76);
        const auto queries = queryCount(), mutations = mutationCount();
        allocations  = 0;
        failureIndex = fail;
        bool success = false;
        measuring    = true;
        try {
            auto result = candidate.value().commit();
            success     = result && result.value().state == rts::UnitBatchState::Committed;
        } catch (const std::bad_alloc&) {
        }
        measuring           = false;
        const auto measured = allocations;
        failureIndex        = std::numeric_limits<std::size_t>::max();
        if (inject) {
            if (success || domain.unitCount() != 0 || countUnits(*isolated) != 0 || countWeapons(*isolated) != 0 ||
                backend.getAgentCount() != 0 || !team.value()->members()->units.empty() || queryCount() != queries ||
                mutationCount() != mutations) {
                std::fprintf(stderr,
                             "RTS batch allocation %zu: success=%d units=%zu table=%zu weapons=%zu agents=%d "
                             "members=%zu queries=%zu/%zu mutations=%zu/%zu\n",
                             fail, success, domain.unitCount(), countUnits(*isolated), countWeapons(*isolated),
                             backend.getAgentCount(), team.value()->members()->units.size(), queryCount(), queries,
                             mutationCount(), mutations);
                std::exit(77);
            }
            auto retryBatch = domain.prepareUnitBatch(*team.value(), batch);
            if (!retryBatch) std::exit(78);
            auto retryResult = retryBatch.value().commit();
            if (!retryResult || retryResult.value().state != rts::UnitBatchState::Committed) std::exit(79);
        } else if (!success)
            std::exit(80);
        for (const auto& request : batch) {
            auto* unit = domain.findUnit(request.subject);
            if (!unit || unit->orders()->values.orderCount() != 1) std::exit(99);
            auto order = unit->orders()->values.current();
            if (!order || order.value().kind != rts::OrderKind::AttackMove || order.value().target.x != 200 ||
                order.value().target.y != 300)
                std::exit(100);
        }
        return measured;
    };
    // Warm process-wide component/backend metadata before counting per-transaction allocations.
    attempt(std::numeric_limits<std::size_t>::max(), false);
    const auto allocationCount = attempt(std::numeric_limits<std::size_t>::max(), false);
    for (std::size_t fail = 0; fail < allocationCount; ++fail) attempt(fail, true);
    std::printf("RTS two-unit joint commit: %zu allocation failpoints, no residual state\n", allocationCount);
    std::puts("RTS batch publishes two units/weapons/agents atomically; blocked and stale tokens publish nothing");
    return 0;
}
int checkBatchRemoval(bool administrative = false) {
    using namespace eve;
    definitions::DefinitionRegistry registry;
    auto                            loaded = rts::RTSContentLoader::load(registry, R"({
        "weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
        "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35}]
    })");
    if (!loaded) return 101;
    auto recipe = rts::PreparedUnitArchetype::prepare(registry, *LogicalId::parse("unit:fighter"));
    if (!recipe) return 102;
    const auto id      = *PersistentId::parse("01900000-0000-7000-8000-000000000799");
    const auto attempt = [&](std::size_t fail, bool inject) -> std::size_t {
        auto             world = std::make_shared<ecs::Table>();
        ecs::ScopedTable scope(*world);
        crowd::Crowd     backend;
        backend.setClampToField(false);
        sensing::SensingWorld sensing;
        if (!sensing.rebuildIndex(sensing::SpatialIndexKind::XYZ, 8).ok()) std::exit(111);
        rts::RTS facade(world);
        facade.setCrowdProvider(&backend);
        facade.setCombatProviders(&sensing, nullptr);
        auto faction = facade.newFaction(SubjectRef::fromPersistentId(id.child("faction")));
        if (!faction) std::exit(103);
        std::vector<rts::UnitBatchRequest> requests;
        requests.push_back({SubjectRef::fromPersistentId(id.child("a")), recipe.value(), 0, 0});
        requests.push_back({SubjectRef::fromPersistentId(id.child("b")), recipe.value(), 4, 0});
        auto prepared = facade.prepareUnitBatch(*faction.value(), requests);
        if (!prepared) std::exit(104);
        auto born = prepared.value().commit();
        if (!born || born.value().state != rts::UnitBatchState::Committed) std::exit(105);
        for (const auto& request : requests) {
            auto mirrored = sensing.upsert(request.subject.format(), request.x, request.y, "army", "", "");
            if (!mirrored) std::exit(106);
            if (!administrative) facade.findUnit(request.subject)->durability()->alive = false;
        }
        std::vector<SubjectRef> subjects;
        for (const auto& request : requests) subjects.push_back(request.subject);
        {
            auto view = ecs::View<rts::Unit>(*world);
            if (facade.cleanupDestroyed().ok()) std::exit(112);
            if (administrative && facade.prepareUnitRemoval(subjects).ok()) std::exit(114);
        }
        std::optional<rts::PreparedUnitRemoval> candidate;
        if (administrative) {
            auto preparedRemoval = facade.prepareUnitRemoval(subjects);
            if (!preparedRemoval) std::exit(115);
            candidate.emplace(std::move(preparedRemoval).takeValue());
            {
                auto view = ecs::View<rts::Unit>(*world);
                if (candidate->commit().ok()) std::exit(120);
            }
            if (facade.unitCount() != 2 || backend.getAgentCount() != 2) std::exit(121);
            auto outsideView = facade.prepareUnitRemoval(subjects);
            if (!outsideView) std::exit(122);
            candidate.emplace(std::move(outsideView).takeValue());
        }
        const auto componentBuffers = facade.findUnit(requests[0].subject)->getComponentManager().components.size();
        allocations                 = 0;
        failureIndex                = fail;
        bool success                = false;
        measuring                   = true;
        try {
            auto removed = administrative ? candidate->commit() : facade.cleanupDestroyed();
            success      = removed && removed.value() == 2;
        } catch (const std::bad_alloc&) {
        }
        measuring           = false;
        const auto measured = allocations;
        failureIndex        = std::numeric_limits<std::size_t>::max();
        if (inject) {
            if (success || facade.unitCount() != 2 || countUnits(*world) != 2 || countWeapons(*world) != 2 ||
                backend.getAgentCount() != 2 || faction.value()->members()->units.size() != 2 ||
                sensing.subjects().size() != 2) {
                std::fprintf(
                    stderr,
                    "RTS cleanup allocation %zu: success=%d units=%zu weapons=%zu agents=%d members=%zu sensing=%zu\n",
                    fail, success, facade.unitCount(), countWeapons(*world), backend.getAgentCount(),
                    faction.value()->members()->units.size(), sensing.subjects().size());
                std::exit(107);
            }
            if (facade.findUnit(requests[0].subject)->getComponentManager().components.size() != componentBuffers)
                std::exit(113);
            if (administrative) {
                auto retryCandidate = facade.prepareUnitRemoval(subjects);
                if (!retryCandidate) std::exit(116);
                auto retry = retryCandidate.value().commit();
                if (!retry || retry.value() != 2) std::exit(117);
            } else {
                auto retry = facade.cleanupDestroyed();
                if (!retry || retry.value() != 2) std::exit(108);
            }
        } else if (!success)
            std::exit(109);
        if (facade.unitCount() != 0 || countUnits(*world) != 0 || countWeapons(*world) != 0 ||
            backend.getAgentCount() != 0 || !faction.value()->members()->units.empty() || !sensing.subjects().empty())
            std::exit(110);
        if (administrative) {
            auto repeated = facade.prepareUnitRemoval(subjects);
            if (!repeated) std::exit(118);
            auto completed = repeated.value().commit();
            if (!completed || completed.value() != subjects.size()) std::exit(119);
        }
        return measured;
    };
    attempt(std::numeric_limits<std::size_t>::max(), false);
    const auto count = attempt(std::numeric_limits<std::size_t>::max(), false);
    for (std::size_t fail = 0; fail < count; ++fail) attempt(fail, true);
    std::printf(
        "RTS joint cleanup: %zu allocation failpoints preserve units/weapons/agents/members/sensing; retry succeeds\n",
        count);
    if (administrative) {
        auto             world = std::make_shared<ecs::Table>();
        ecs::ScopedTable scope(*world);
        rts::RTS         domain(world);
        const auto       subject  = SubjectRef::fromPersistentId(id.child("incarnation"));
        auto             original = domain.newUnit(subject);
        if (!original) return 123;
        auto oldRemoval = domain.prepareUnitRemoval(std::span(&subject, 1));
        if (!oldRemoval) return 124;
        original.value()->release();
        auto replacement = domain.newUnit(subject);
        if (!replacement) return 125;
        if (oldRemoval.value().commit().ok() || domain.findUnit(subject) != replacement.value()) return 126;
        std::optional<rts::PreparedUnitRemoval> orphan;
        {
            rts::RTS temporary(world);
            auto     candidate = temporary.prepareUnitRemoval(std::span(&subject, 1));
            if (!candidate) return 127;
            orphan.emplace(std::move(candidate).takeValue());
        }
        if (orphan->commit().ok() || domain.unitCount() != 1) return 128;
        auto finalRemoval = domain.prepareUnitRemoval(std::span(&subject, 1));
        if (!finalRemoval || finalRemoval.value().commit().expect("administrative removal") != 1 ||
            domain.unitCount() != 0)
            return 129;
        std::puts("Administrative removal preserves replacement incarnations, rejects dead owners and active Views");
    }
    return 0;
}
int checkCapturedRecipe() {
    using namespace eve;
    ecs::Table                                table;
    ecs::ScopedTable                          scope(table);
    std::optional<rts::PreparedUnitArchetype> original, replacement;
    SnapshotHashProvider                      hash = [](std::string_view text) -> Result<ContentId> {
        ContentId::Bytes bytes{};
        std::uint8_t     value = 41;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            for (const unsigned char byte : text) value = static_cast<std::uint8_t>(value * 33u + byte + index);
            bytes[index] = static_cast<std::uint8_t>(value + index);
        }
        return Result<ContentId>::success(ContentId(bytes));
    };

    const auto logical = *LogicalId::parse("unit:fighter");
    const auto id      = *PersistentId::parse("01900000-0000-7000-8000-000000000599");
    {
        definitions::DefinitionRegistry registry;
        auto                            loaded = rts::RTSContentLoader::load(registry, R"({
            "weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
            "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35}]
        })");
        if (!loaded) return 30;
        auto recipe = rts::PreparedUnitArchetype::prepare(registry, logical);
        if (!recipe) return 31;
        original.emplace(std::move(recipe).value());
        auto liveCreated = weapon::WeaponDefinitionRuntime::create(registry, "club", id.child("live"));
        if (!liveCreated) return 45;
        auto&      live             = liveCreated.value();
        const auto liveHandle       = live.definitionHandle();
        live.state().resource.value = 7;
        auto liveSnapshot           = live.snapshot(Revision(1), SimulationTick(1), hash);
        if (!liveSnapshot) return 46;
        if (countUnits(table) != 0 || countWeapons(table) != 0) return 32;
        loaded = rts::RTSContentLoader::load(registry, R"({
            "weapons":[{"id":"club","damageType":"normal","damage":90,"range":1.2,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
            "units":[{"id":"fighter","weaponType":"club","health":70,"speed":9,"radius":0.35}]
        })");
        if (!loaded) return 33;
        recipe = rts::PreparedUnitArchetype::prepare(registry, logical);
        if (!recipe) return 34;
        replacement.emplace(std::move(recipe).value());
        if (live.reload(definition::ReloadPolicy::RejectWhileActive).ok() || live.definitionHandle() != liveHandle)
            return 47;
        if (!live.reload(definition::ReloadPolicy::RebuildInstance).ok()) return 48;
        auto liveDefinition = live.definition();
        if (!liveDefinition || liveDefinition.value().damage != 90 || live.state().resource.value != 7) return 49;
        if (live.restore(liveSnapshot.value(), hash).ok() || live.state().resource.value != 7) return 50;
        if (original->source()->get().generation == replacement->source()->get().generation) return 35;
        auto future = registry.replace("unit", "fighter", 2, "{}");
        if (!future) return 52;
        auto unsupported = rts::PreparedUnitArchetype::prepare(registry, logical);
        if (unsupported.ok() || unsupported.code() != StatusCode::Unsupported) return 53;
    }
    for (auto* recipe : {&original, &replacement}) {
        const auto unitJson   = recipe->value().source()->get().json;
        const auto weaponJson = recipe->value().weaponSource()->get().json;
        auto       value      = recipe->value().encodeState();
        if (!value) return 110;
        auto wire = value.value().toJson();
        if (!wire) return 111;
        recipe->reset();
        auto parsed = Value::fromJson(wire.value());
        if (!parsed) return 112;
        auto admitted = rts::PreparedUnitArchetype::prepareState(parsed.value());
        if (!admitted) return 113;
        recipe->emplace(std::move(admitted).takeValue());
        if (recipe->value().source()->get().json != unitJson ||
            recipe->value().weaponSource()->get().json != weaponJson)
            return 114;
        if (countUnits(table) != 0 || countWeapons(table) != 0) return 115;
    }
    const rts::CapturedArchetypeWeaponFactory factory = [](const definitions::Definition& source,
                                                           PersistentId subject) -> Result<weapon::WeaponEntity*> {
        auto prepared = weapon::WeaponDefinitionRuntime::prepareCaptured(source, subject);
        if (!prepared) return Result<weapon::WeaponEntity*>::failure(prepared.status());
        auto* entity   = weapon::WeaponEntity::createWeapon();
        auto  rollback = std::unique_ptr<weapon::WeaponEntity, void (*)(weapon::WeaponEntity*)>(
            entity, [](auto* value) { value->release(); });
        auto projected = prepared.value().applyTo(entity);
        if (!projected) return Result<weapon::WeaponEntity*>::failure(projected.status());
        rollback.release();
        return Result<weapon::WeaponEntity*>::success(entity);
    };
    auto* rejected = rts::Unit::createUnit(SubjectRef::fromPersistentId(id.child("rejected")), logical);
    const rts::CapturedArchetypeWeaponFactory missing = [](const definitions::Definition&, PersistentId) {
        return Result<weapon::WeaponEntity*>::success(nullptr);
    };
    if (original->apply(*rejected, missing).ok() || rejected->durability()->state.health != 1 ||
        countWeapons(table) != 0)
        return 51;
    auto* oldUnit = rts::Unit::createUnit(SubjectRef::fromPersistentId(id), logical);
    auto* newUnit = rts::Unit::createUnit(SubjectRef::fromPersistentId(id.child("new")), logical);
    if (!original->apply(*oldUnit, factory).ok() || !replacement->apply(*newUnit, factory).ok()) return 36;
    auto* oldWeapon = dynamic_cast<weapon::WeaponEntity*>(oldUnit->weapon()->link.resolve());
    auto* newWeapon = dynamic_cast<weapon::WeaponEntity*>(newUnit->weapon()->link.resolve());
    if (!oldWeapon || !newWeapon || oldUnit->durability()->state.health != 40 ||
        newUnit->durability()->state.health != 70 || oldUnit->motion()->speed != 3 || newUnit->motion()->speed != 9 ||
        oldWeapon->definition()->def->damage != 10 || newWeapon->definition()->def->damage != 90)
        return 37;
    if (oldWeapon->definitionBinding()->identity.definitionGeneration != original->weaponSource()->get().generation)
        return 38;
    auto* wrong =
        rts::Unit::createUnit(SubjectRef::fromPersistentId(id.child("wrong")), *LogicalId::parse("unit:other"));
    if (original->apply(*wrong, factory).ok() || countWeapons(table) != 2 || wrong->durability()->state.health != 1)
        return 39;
    auto futureWeapon      = original->weaponSource()->get();
    futureWeapon.version   = SchemaVersion(2);
    auto unsupportedWeapon = weapon::WeaponDefinitionRuntime::prepareCaptured(futureWeapon, id.child("future"));
    if (unsupportedWeapon.ok() || unsupportedWeapon.code() != StatusCode::Unsupported) return 54;
    auto captured =
        weapon::WeaponDefinitionRuntime::prepareCaptured(original->weaponSource()->get(), id.child("runtime"));
    if (!captured) return 40;
    auto& runtime = captured.value();
    if (runtime.reload(definition::ReloadPolicy::RebuildInstance).ok()) return 41;
    runtime.state().resource.value = 7;
    auto snapshot                  = runtime.snapshot(Revision(1), SimulationTick(1), hash);
    if (!snapshot) return 42;
    runtime.state().resource.value = 1;
    if (!runtime.restore(snapshot.value(), hash).ok() || runtime.state().resource.value != 7) return 43;
    auto moved = std::move(*original);
    if (original->source() || original->apply(*oldUnit, factory).ok() || !moved.source()) return 44;
    std::puts(
        "Captured unit and weapon revisions survive replacement/registry destruction; typed application and exact "
        "restore succeed");
    return 0;
}
}  // namespace
void* operator new(std::size_t size) {
    if (measuring && allocations++ == failureIndex) {
        throw std::bad_alloc();
    }
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "batch") == 0) return checkUnitBatch();
    if (argc > 1 && std::strcmp(argv[1], "removal") == 0) return checkBatchRemoval();
    if (argc > 1 && std::strcmp(argv[1], "administrative") == 0) return checkBatchRemoval(true);
    if (argc > 1 && std::strcmp(argv[1], "ownership") == 0) {
        auto                      world     = std::make_shared<ecs::Table>();
        std::weak_ptr<ecs::Table> weakWorld = world;
        eve::LifetimeWitness      witness;
        {
            eve::rts::RTS facade(world);
            witness = facade.lifetimeWitness();
            if (!witness.isAlive() || facade.ownedWorld() != world) return 40;
            {
                ecs::ScopedTable scope(*world);
                const auto       id = eve::PersistentId::parse("01900000-0000-7000-8000-000000000501");
                if (!id) return 41;
                auto unit = facade.newUnit(eve::SubjectRef::fromPersistentId(*id));
                if (!unit) return 42;
                if (countUnits(*world) != 1) return 43;
            }
            world.reset();
            if (weakWorld.expired()) return 44;
        }
        if (witness.isAlive() || !weakWorld.expired()) return 45;
        // The other destruction order: the external world survives facade teardown,
        // but no owned root survives in it.
        world = std::make_shared<ecs::Table>();
        {
            eve::rts::RTS    facade(world);
            ecs::ScopedTable scope(*world);
            const auto       id = eve::PersistentId::parse("01900000-0000-7000-8000-000000000502");
            if (!id) return 46;
            auto unit = facade.newUnit(eve::SubjectRef::fromPersistentId(*id));
            if (!unit) return 47;
        }
        if (countUnits(*world) != 0) return 48;
        eve::rts::RTS borrowed;
        if (borrowed.ownedWorld()) return 49;
        std::puts("Owned RTS table and facade witness obey both destruction orders");
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "recipe") == 0) return checkCapturedRecipe();
    if (argc > 1 && std::strcmp(argv[1], "attributes") == 0) return checkAttributeMoves();
    const bool weaponMode  = argc > 1 && std::strcmp(argv[1], "weapon") == 0;
    const bool factionMode = weaponMode || (argc > 1 && std::strcmp(argv[1], "faction") == 0);
    const bool facadeMode  = factionMode || (argc > 1 && std::strcmp(argv[1], "facade") == 0);
    const auto id          = eve::PersistentId::parse("01900000-0000-7000-8000-000000000501");
    if (!id) return 1;
    const auto subject        = eve::SubjectRef::fromPersistentId(*id);
    const auto factionSubject = eve::SubjectRef::fromPersistentId(id->child("faction"));
    const auto setup = [&](std::optional<eve::rts::RTS>& facade, eve::definitions::DefinitionRegistry& registry) {
        if (!facadeMode) return;
        facade.emplace();
        if (factionMode && !facade->newFaction(factionSubject).ok()) std::exit(21);
        if (weaponMode && !facade
                               ->loadContent(registry, R"({
            "weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,
              "cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
            "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35,
              "attackRange":1.2,"sightRange":10,"targetTags":["biological"]}]
        })")
                               .ok())
            std::exit(22);
    };
    const auto create = [&](std::optional<eve::rts::RTS>& facade) -> eve::rts::Unit* {
        if (!facade) return eve::rts::Unit::createUnit(subject);
        auto result =
            factionMode ? facade->newFactionUnit(*facade->findFaction(factionSubject), subject,
                                                 weaponMode ? *eve::LogicalId::parse("unit:fighter") : eve::LogicalId{})
                          : facade->newUnit(subject);
        if (!result) return nullptr;
        auto* unit = result.value();
        if (weaponMode) {
            auto* weapon = dynamic_cast<eve::weapon::WeaponEntity*>(unit->weapon()->link.resolve());
            if (!weapon || !weapon->definition()->def || weapon->definition()->def->damage != 10 ||
                weapon->definitionBinding()->reloadPolicy != eve::definition::ReloadPolicy::RebuildInstance ||
                unit->durability()->state.health != 40 || unit->motion()->speed != 3)
                std::exit(23);
        }
        return unit;
    };
    // Warm process-wide type metadata before measuring per-world creation.
    {
        ecs::Table                           table;
        ecs::ScopedTable                     scope(table);
        eve::definitions::DefinitionRegistry registry;
        std::optional<eve::rts::RTS>         facade;
        setup(facade, registry);
        auto* unit = create(facade);
        if (!unit) return 1;
        ecs::DestroyEntity(unit);
    }
    std::size_t count = 0;
    {
        ecs::Table                           table;
        ecs::ScopedTable                     scope(table);
        eve::definitions::DefinitionRegistry registry;
        std::optional<eve::rts::RTS>         facade;
        setup(facade, registry);
        allocations = 0;
        measuring   = true;
        auto* unit  = create(facade);
        measuring   = false;
        count       = allocations;
        if (!unit || count == 0 || countUnits(table) != 1) return 2;
        ecs::DestroyEntity(unit);
    }
    std::size_t leaks = 0;
    for (std::size_t fail = 0; fail < count; ++fail) {
        ecs::Table                           table;
        ecs::ScopedTable                     scope(table);
        eve::definitions::DefinitionRegistry registry;
        std::optional<eve::rts::RTS>         facade;
        setup(facade, registry);
        failureIndex                    = fail;
        allocations                     = 0;
        bool            threw           = false;
        eve::rts::Unit* unit            = nullptr;
        const auto      beforeQueries   = queryCount();
        const auto      beforeMutations = mutationCount();
        measuring                       = true;
        try {
            unit = create(facade);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        measuring = false;
        if (!threw && unit != nullptr) {
            if (unit) ecs::DestroyEntity(unit);
            std::fprintf(stderr, "allocation %zu did not fail as requested\n", fail);
            return 3;
        }
        if (countUnits(table) != 0 || (facade && (facade->unitCount() != 0 || facade->findUnit(subject) != nullptr))) {
            ++leaks;
            std::fprintf(stderr, "allocation %zu left a published RTS Unit\n", fail);
        }
        if (factionMode && !facade->findFaction(factionSubject)->members()->units.empty()) {
            ++leaks;
            std::fprintf(stderr, "allocation %zu left a faction membership\n", fail);
        }
        if (countWeapons(table) != 0) {
            ++leaks;
            std::fprintf(stderr, "allocation %zu left a weapon entity\n", fail);
        }
        if (queryCount() != beforeQueries || mutationCount() != beforeMutations) {
            ++leaks;
            std::fprintf(
                stderr,
                "allocation %zu left an attribute capability registration: queries %zu -> %zu, mutations %zu -> %zu\n",
                fail, beforeQueries, queryCount(), beforeMutations, mutationCount());
        }
        if (leaks) continue;
        failureIndex = std::numeric_limits<std::size_t>::max();
        auto* retry  = create(facade);
        if (!retry || countUnits(table) != 1) return 4;
        ecs::DestroyEntity(retry);
    }
    std::printf("RTS %s creation: %zu allocation failpoints, %zu residual-state failures\n",
                weaponMode ? "weapon" : (factionMode ? "faction" : (facadeMode ? "facade" : "Unit")), count, leaks);
    return leaks ? 5 : 0;
}
