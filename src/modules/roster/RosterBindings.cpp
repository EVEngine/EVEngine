#include "roster/Roster.h"

#include "common/SquirrelBinding.h"
#include "common/Value.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>

namespace eve::roster {
namespace {

eve::Value vacancyValue(const Vacancy& vacancy) {
    return eve::Value(eve::Value::Object{{"kind", eve::Value(vacancy.kind)},
                                         {"targetId", eve::Value(vacancy.targetId)},
                                         {"role", eve::Value(vacancy.role)},
                                         {"lostSubject", eve::Value(vacancy.lostSubject)},
                                         {"tick", eve::Value(static_cast<std::int64_t>(vacancy.tick))}});
}

eve::Value spawnRequestValue(const SpawnRequest& request) {
    return eve::Value(eve::Value::Object{{"requestId", eve::Value(request.requestId)},
                                         {"policyId", eve::Value(request.policyId)},
                                         {"preferredDef", eve::Value(request.preferredDef)},
                                         {"postId", eve::Value(request.postId)},
                                         {"squadId", eve::Value(request.squadId)},
                                         {"role", eve::Value(request.role)},
                                         {"readyAtTick", eve::Value(static_cast<std::int64_t>(request.readyAtTick))}});
}

}  // namespace

void Roster::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Roster::create, false);
    expose(cls);
}

void Roster::expose(ssq::Class& cls) {
    const HSQUIRRELVM vm = cls.getHandle();
    cls.addFunc("getName", &Roster::getName);
    cls.addFunc("clear", &Roster::clear);

    cls.addFunc("registerPost", [vm](Roster* self, const std::string& postId, int capacity, const std::string& role) {
        return script::projectResult(vm, self->registerPost(postId, capacity, role));
    });
    cls.addFunc("removePost", [vm](Roster* self, const std::string& postId, int tick) {
        return script::projectResult(vm, self->removePost(postId, static_cast<std::uint64_t>(tick < 0 ? 0 : tick)));
    });
    cls.addFunc("assignPost",
                [vm](Roster* self, const std::string& postId, const std::string& subject, const std::string& role) {
                    return script::projectResult(vm, self->assignPost(postId, subject, role));
                });
    cls.addFunc("releasePost", [vm](Roster* self, const std::string& postId, const std::string& subject, int tick) {
        return script::projectResult(
            vm, self->releasePost(postId, subject, static_cast<std::uint64_t>(tick < 0 ? 0 : tick)));
    });
    cls.addFunc("postOccupancy", [vm](Roster* self, const std::string& postId) {
        return script::projectResult(vm, self->postOccupancy(postId), [](int value) { return Value(value); });
    });
    cls.addFunc("postCapacity", [vm](Roster* self, const std::string& postId) {
        return script::projectResult(vm, self->postCapacity(postId), [](int value) { return Value(value); });
    });
    cls.addFunc("postAssigneeAt", [vm](Roster* self, const std::string& postId, int index) {
        return script::projectResult(vm, self->postAssigneeAt(postId, index),
                                     [](const std::string& value) { return Value(value); });
    });
    cls.addFunc("postRole", [vm](Roster* self, const std::string& postId, const std::string& subject) {
        return script::projectResult(vm, self->postRole(postId, subject),
                                     [](const std::string& value) { return Value(value); });
    });

    cls.addFunc("createSquad", [vm](Roster* self, const std::string& squadId, int capacity, const std::string& intent) {
        return script::projectResult(vm, self->createSquad(squadId, capacity, intent));
    });
    cls.addFunc("destroySquad", [vm](Roster* self, const std::string& squadId, int tick) {
        return script::projectResult(vm, self->destroySquad(squadId, static_cast<std::uint64_t>(tick < 0 ? 0 : tick)));
    });
    cls.addFunc("addMember",
                [vm](Roster* self, const std::string& squadId, const std::string& subject, const std::string& role) {
                    return script::projectResult(vm, self->addMember(squadId, subject, role));
                });
    cls.addFunc("removeMember", [vm](Roster* self, const std::string& squadId, const std::string& subject, int tick) {
        return script::projectResult(
            vm, self->removeMember(squadId, subject, static_cast<std::uint64_t>(tick < 0 ? 0 : tick)));
    });
    cls.addFunc("setIntent", [vm](Roster* self, const std::string& squadId, const std::string& intent) {
        return script::projectResult(vm, self->setIntent(squadId, intent));
    });
    cls.addFunc("intent", [vm](Roster* self, const std::string& squadId) {
        return script::projectResult(vm, self->intent(squadId), [](const std::string& value) { return Value(value); });
    });
    cls.addFunc("memberCount", [vm](Roster* self, const std::string& squadId) {
        return script::projectResult(vm, self->memberCount(squadId), [](int value) { return Value(value); });
    });
    cls.addFunc("memberAt", [vm](Roster* self, const std::string& squadId, int index) {
        return script::projectResult(vm, self->memberAt(squadId, index),
                                     [](const std::string& value) { return Value(value); });
    });
    cls.addFunc("memberRole", [vm](Roster* self, const std::string& squadId, const std::string& subject) {
        return script::projectResult(vm, self->memberRole(squadId, subject),
                                     [](const std::string& value) { return Value(value); });
    });

    cls.addFunc("releaseSubject", [vm](Roster* self, const std::string& subject, int tick) {
        return script::projectResult(vm, self->releaseSubject(subject, static_cast<std::uint64_t>(tick < 0 ? 0 : tick)),
                                     [](int value) { return Value(value); });
    });
    cls.addFunc("vacancyCount", &Roster::vacancyCount);
    cls.addFunc("vacancyAt", [vm](Roster* self, int index) {
        return script::projectResult(vm, self->vacancyAt(index), vacancyValue);
    });
    cls.addFunc("clearVacancies", &Roster::clearVacancies);

    cls.addFunc("setPolicy",
                [vm](Roster* self, const std::string& policyId, const std::string& preferredDef, int delayTicks,
                     int budget, const std::string& postId, const std::string& squadId, const std::string& role) {
                    return script::projectResult(
                        vm, self->setPolicy(policyId, preferredDef, delayTicks, budget, postId, squadId, role));
                });
    cls.addFunc("setFallback", [vm](Roster* self, const std::string& policyId, const std::string& preferredDef,
                                    const std::string& fallbackDef) {
        return script::projectResult(vm, self->setFallback(policyId, preferredDef, fallbackDef));
    });
    cls.addFunc("clearPolicy", [vm](Roster* self, const std::string& policyId) {
        return script::projectResult(vm, self->clearPolicy(policyId));
    });
    cls.addFunc("step", [vm](Roster* self, int tick) {
        return script::projectResult(vm, self->step(static_cast<std::uint64_t>(tick < 0 ? 0 : tick)),
                                     [](int value) { return Value(value); });
    });
    cls.addFunc("spawnRequestCount", &Roster::spawnRequestCount);
    cls.addFunc("spawnRequestAt", [vm](Roster* self, int index) {
        return script::projectResult(vm, self->spawnRequestAt(index), spawnRequestValue);
    });
    cls.addFunc("ackSpawnRequest", [vm](Roster* self, const std::string& requestId) {
        return script::projectResult(vm, self->ackSpawnRequest(requestId));
    });
    cls.addFunc("failSpawnRequest", [vm](Roster* self, const std::string& requestId, int tick) {
        return script::projectResult(
            vm, self->failSpawnRequest(requestId, static_cast<std::uint64_t>(tick < 0 ? 0 : tick)));
    });
}

}  // namespace eve::roster
