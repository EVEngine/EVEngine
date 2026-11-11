// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"role":"compile-contract","default":"module-composition","module":"rpg_spawn"}
// OBJECT-only ordinary allocation interception around the real RPG factory and its actual two-actor batch.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "unit/rpg_spawn/RPGSpawnFixture.h"
namespace {
thread_local bool        nothrowAllocation = false;
thread_local bool        injected          = false;
thread_local bool        injectedNothrow   = false;
thread_local bool        measuring         = false;
thread_local std::size_t allocations       = 0;
thread_local std::size_t failureIndex      = std::numeric_limits<std::size_t>::max();
using Token                                = std::unique_ptr<eve::spawn::IPreparedSpawn>;
Token prepare(test_rpg_spawn::Fixture& fixture) {
    auto outcome = fixture.factory->prepare(fixture.request, fixture.positions).expect("factory preparation");
    return std::move(std::get<Token>(outcome));
}
bool published(test_rpg_spawn::Fixture& fixture, Token& token) {
    if (!token->isPublished() || fixture.count() != fixture.request.units.size()) return false;
    for (const auto& unit : fixture.request.units) {
        auto actor = fixture.factory->inspect(unit.subject);
        if (!actor || actor.value().subject != unit.subject || actor.value().origin != fixture.request.source ||
            actor.value().hp != 20 || actor.value().attack != 7)
            return false;
    }
    return true;
}
}  // namespace
void* operator new(std::size_t size) {
    if (measuring && allocations++ == failureIndex) {
        injected        = true;
        injectedNothrow = nothrowAllocation;
        throw std::bad_alloc();
    }
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    const auto previous = nothrowAllocation;
    nothrowAllocation   = true;
    try {
        auto* result      = ::operator new(size);
        nothrowAllocation = previous;
        return result;
    } catch (...) {
        nothrowAllocation = previous;
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void  operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void  operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }
void  operator delete(void* value) noexcept { std::free(value); }
void  operator delete[](void* value) noexcept { std::free(value); }
void  operator delete(void* value, std::size_t) noexcept { std::free(value); }
void  operator delete[](void* value, std::size_t) noexcept { std::free(value); }
int   main(int argc, char** argv) {
    const bool commitMode    = argc > 1 && std::strcmp(argv[1], "commit") == 0;
    const auto idleQueries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto idleMutations = eve::cap::listenerCount<eve::IStateMutation>();
    {
        test_rpg_spawn::Fixture warm;
        auto                    token = prepare(warm);
        token->commit().expect("warm canonical publication");
        if (!published(warm, token)) return 1;
    }
    if (eve::cap::listenerCount<eve::IStateQuery>() != idleQueries ||
        eve::cap::listenerCount<eve::IStateMutation>() != idleMutations)
        return 15;
    std::size_t count     = 0;
    std::size_t recovered = 0;
    {
        test_rpg_spawn::Fixture fixture;
        if (fixture.count() != 0) return 1;
        Token token;
        if (commitMode) token = prepare(fixture);
        const auto queries = eve::cap::listenerCount<eve::IStateQuery>();
        allocations        = 0;
        measuring          = true;
        if (commitMode)
            token->commit().expect("baseline publication");
        else
            token = prepare(fixture);
        measuring = false;
        count     = allocations;
        if (count == 0) return 2;
        if (!commitMode &&
            (token->isPublished() || fixture.count() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries))
            return 3;
        if (!commitMode) token->commit().expect("baseline publication");
        if (!published(fixture, token)) return 4;
    }
    for (std::size_t fail = 0; fail < count; ++fail) {
        if (eve::cap::listenerCount<eve::IStateQuery>() != idleQueries ||
            eve::cap::listenerCount<eve::IStateMutation>() != idleMutations)
            return 16;
        test_rpg_spawn::Fixture fixture;
        if (fixture.count() != 0) return 5;
        Token token;
        if (commitMode) token = prepare(fixture);
        const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
        const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
        allocations          = 0;
        failureIndex         = fail;
        injected             = false;
        injectedNothrow      = false;
        measuring            = true;
        bool rejected        = false;
        try {
            if (commitMode) {
                auto result = token->commit();
                measuring   = false;
                rejected    = !result.ok();
                if (result.ok() && result.value() != eve::spawn::CommitOutcome::Committed) return 21;
            } else {
                auto result = fixture.factory->prepare(fixture.request, fixture.positions);
                measuring   = false;
                rejected    = !result.ok();
                if (result.ok()) token = std::move(std::get<Token>(result.value()));
            }
        } catch (const std::bad_alloc&) {
            measuring = false;
            rejected  = true;
        }
        failureIndex = std::numeric_limits<std::size_t>::max();
        if (!injected) return 17;
        if (!rejected && commitMode && injectedNothrow) {
            // The standard stable sort can finish after a non-throwing scratch allocation fails.
            if (!published(fixture, token) ||
                eve::cap::listenerCount<eve::IStateQuery>() != queries + fixture.request.units.size() ||
                eve::cap::listenerCount<eve::IStateMutation>() != mutations + fixture.request.units.size() ||
                fixture.sourceWorld->inspect(fixture.request.source).expect("unchanged core ledger").alive != 0)
                return 18;
            token->commit().expect("recovered replay");
            auto replay = prepare(fixture);
            if (!published(fixture, token) || !replay->isPublished()) return 19;
            ++recovered;
            continue;
        }
        if (!rejected || fixture.count() != 0 || eve::cap::listenerCount<eve::IStateQuery>() != queries ||
            eve::cap::listenerCount<eve::IStateMutation>() != mutations ||
            fixture.sourceWorld->inspect(fixture.request.source).expect("unchanged core ledger").alive != 0) {
            std::fprintf(
                stderr,
                "RPG factory %s failpoint %zu: rejected=%d roots=%zu queries=%zu/%zu mutations=%zu/%zu published=%d\n",
                commitMode ? "commit" : "prepare", fail, int(rejected), fixture.count(),
                eve::cap::listenerCount<eve::IStateQuery>(), queries, eve::cap::listenerCount<eve::IStateMutation>(),
                mutations, token ? int(token->isPublished()) : -1);
            return 6;
        }
        for (const auto& unit : fixture.request.units)
            if (fixture.factory->inspect(unit.subject).ok()) return 7;
        auto proof = prepare(fixture);
        if (proof->isPublished() || (token && token->isPublished())) return 8;
        if (!commitMode) token = std::move(proof);
        if (token->commit().expect("same factory/token retry") != eve::spawn::CommitOutcome::Committed ||
            !published(fixture, token))
            return 9;
        token->commit().expect("replay");
        if (!published(fixture, token)) return 10;
        auto replay = prepare(fixture);
        if (!replay->isPublished()) return 11;
    }
    if (eve::cap::listenerCount<eve::IStateQuery>() != idleQueries ||
        eve::cap::listenerCount<eve::IStateMutation>() != idleMutations)
        return 20;
    std::printf(
        "Real RPG factory %s: %zu ordinary allocation failpoints (%zu complete nothrow recoveries); "
          "roots/index/receipt/capability/core-ledger atomic; retry and replay succeed\n",
        commitMode ? "commit" : "prepare", count, recovered);
    return 0;
}
