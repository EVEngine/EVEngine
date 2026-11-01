#include "production/Production.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace eve::production {
namespace {

/** @brief Script-owned handle proxy; the queue remains owned by Production. */
struct ScriptWorkQueue {
    explicit ScriptWorkQueue(WorkQueueHandleRef value) : reference(value) {}
    WorkQueueHandleRef reference;
};

/** @brief Squirrel-owned task view that re-resolves its queue handle per call. */
struct ScriptWorkTask {
    ScriptWorkTask(WorkQueueHandleRef queue, std::string id) : reference(queue), taskId(std::move(id)) {}
    WorkQueueHandleRef reference;
    std::string        taskId;
};

/** @brief Squirrel-owned event view that re-resolves its queue handle per call. */
struct ScriptWorkEvent {
    ScriptWorkEvent(WorkQueueHandleRef queue, int eventIndex) : reference(queue), index(eventIndex) {}
    WorkQueueHandleRef reference;
    int                index = -1;
};

template <class T>
eve::Result<T> productionBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "production.squirrel"));
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release, const char* errorPath) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status());
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned production proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned production allocation");
        return eve::script::projectStatusResult(vm, status);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    (void)errorPath;
    return result;
}

ssq::Object projectValueObject(HSQUIRRELVM vm, const eve::Value& value) {
    if (!vm) return ssq::Object();
    const SQInteger top    = sq_gettop(vm);
    auto            pushed = eve::script::pushValue(vm, value);
    if (!pushed.ok()) {
        pushed.ignore("failed to project a work-task Value payload");
        sq_settop(vm, top);
        return ssq::Object(vm);
    }
    ssq::Object result(vm);
    if (SQ_SUCCEEDED(sq_getstackobj(vm, -1, &result.getRaw()))) sq_addref(vm, &result.getRaw());
    sq_settop(vm, top);
    return result;
}

}  // namespace

eve::Result<WorkQueueHandleRef> Production::newQueueHandle() {
    Production* module = Production::create();
    return module->queues_.emplace(std::make_unique<WorkQueue>());
}

eve::ResultRef<WorkQueue> Production::resolve(WorkQueueHandleRef reference) {
    Production* module = ModuleManager::getInstance<Production>("Production");
    if (!module)
        return productionBindingFailure<std::reference_wrapper<WorkQueue>>(
            eve::DiagnosticCode::StaleHandle, "Production module is no longer loaded", "queue");
    auto view = module->queues_.resolve(reference);
    if (!view.isBound())
        return productionBindingFailure<std::reference_wrapper<WorkQueue>>(eve::DiagnosticCode::StaleHandle,
                                                                           "work queue handle is stale", "queue");
    return eve::ResultRef<WorkQueue>::success(std::ref(*view));
}

eve::Result<void> Production::release(WorkQueueHandleRef reference) {
    Production* module = ModuleManager::getInstance<Production>("Production");
    if (!module)
        return productionBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Production module is no longer loaded",
                                              "queue");
    return module->queues_.erase(reference);
}

bool Production::isStale(WorkQueueHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Production* module = ModuleManager::getInstance<Production>("Production");
    return !module || module->queues_.isStale(reference);
}

Module_IMPL(Production, new Production());

void Production::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto              task =
        table.addClass<ScriptWorkTask>("WorkTask", std::function<ScriptWorkTask*()>([] { return nullptr; }), true);
    auto resolveTask = [](ScriptWorkTask* proxy) -> eve::OptionalRef<const ProductionTask> {
        if (!proxy) return {};
        auto queue = Production::resolve(proxy->reference);
        if (!queue.ok()) {
            queue.ignore("stale work queue while reading a task proxy");
            return {};
        }
        return queue.value().get().find(proxy->taskId);
    };
    task.addFunc("getId", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().id : std::string{};
    });
    task.addFunc("getOwner", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().owner : std::string{};
    });
    task.addFunc("getKind", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().kind : std::string{};
    });
    task.addFunc("getProduct", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().product : std::string{};
    });
    task.addFunc("getContext", [vm, resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? projectValueObject(vm, value->get().context) : ssq::Object(vm);
    });
    task.addFunc("getDuration", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? float(value->get().duration.seconds()) : 0.0f;
    });
    task.addFunc("getProgress", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? float(value->get().progress.seconds()) : 0.0f;
    });
    task.addFunc("getPriority", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().priority : 0;
    });
    task.addFunc("getState", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? std::string(taskStateName(value->get().state)) : std::string{};
    });
    task.addFunc("getReason", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().reason : std::string{};
    });

    auto event =
        table.addClass<ScriptWorkEvent>("WorkEvent", std::function<ScriptWorkEvent*()>([] { return nullptr; }), true);
    auto resolveEvent = [](ScriptWorkEvent* proxy) -> eve::OptionalRef<ProductionEvent> {
        if (!proxy) return {};
        auto queue = Production::resolve(proxy->reference);
        if (!queue.ok()) {
            queue.ignore("stale work queue while reading an event proxy");
            return {};
        }
        return queue.value().get().eventAt(proxy->index);
    };
    event.addFunc("getSequence", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? int64_t(value->get().sequence) : int64_t{0};
    });
    event.addFunc("getTick", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? int64_t(value->get().tick.value()) : int64_t{0};
    });
    event.addFunc("getKind", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? std::string(eventKindName(value->get().kind)) : std::string{};
    });
    event.addFunc("getTaskId", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().taskId : std::string{};
    });
    event.addFunc("getOwner", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().owner : std::string{};
    });
    event.addFunc("getTaskKind", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().taskKind : std::string{};
    });
    event.addFunc("getProduct", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().product : std::string{};
    });
    event.addFunc("getReason", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().reason : std::string{};
    });

    auto queue =
        table.addClass<ScriptWorkQueue>("WorkQueue", std::function<ScriptWorkQueue*()>([] { return nullptr; }), true);
    queue.addFunc("ownership", [](ScriptWorkQueue*) { return std::string("owned"); });
    queue.addFunc("ownerEpoch", [](ScriptWorkQueue* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    queue.addFunc("handle", [](ScriptWorkQueue* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    queue.addFunc("isStale", [](ScriptWorkQueue* value) { return !value || Production::isStale(value->reference); });
    queue.addFunc("release", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        return eve::script::projectResult(vm, Production::release(value->reference));
    });
    queue.addFunc("enqueue", [vm](ScriptWorkQueue* value, const std::string& owner, const std::string& kind,
                                  const std::string& product, const std::string& contextJson, float duration,
                                  int priority) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                               "work queue proxy must not be null", "queue")
                    .status());
        auto payload = eve::Value::fromJson(contextJson);
        if (!payload.ok()) return eve::script::projectStatusResult(vm, payload.status());
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(
            vm,
            queueView.value().get().enqueue(owner, kind, product, std::move(payload).takeValue(), duration, priority),
            [](std::string id) { return eve::Value(std::move(id)); });
    });
    queue.addFunc("pause", [vm](ScriptWorkQueue* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(vm, queueView.value().get().pause(id));
    });
    queue.addFunc("resume", [vm](ScriptWorkQueue* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(vm, queueView.value().get().resume(id));
    });
    queue.addFunc("cancel", [vm](ScriptWorkQueue* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(vm, queueView.value().get().cancel(id, reason));
    });
    queue.addFunc("fail", [vm](ScriptWorkQueue* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(vm, queueView.value().get().fail(id, reason));
    });
    queue.addFunc("advance", [vm](ScriptWorkQueue* value, std::int64_t tick, float seconds) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto delta = eve::Duration::fromSeconds(seconds);
        if (!delta.ok()) return eve::script::projectStatusResult(vm, delta.status());
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(
            vm, queueView.value().get().advance(
                    {eve::SimulationTick(static_cast<std::uint64_t>(tick)), std::move(delta).takeValue()}));
    });
    queue.addFunc("setSlotCount", [vm](ScriptWorkQueue* value, const std::string& owner, int slots) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(vm, queueView.value().get().setSlotCount(owner, slots));
    });
    queue.addFunc("slotCount", [](ScriptWorkQueue* value, const std::string& owner) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading slot count");
            return 0;
        }
        return queueView.value().get().slotCount(owner);
    });
    queue.addFunc("runningCount", [](ScriptWorkQueue* value, const std::string& owner) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading running count");
            return 0;
        }
        return queueView.value().get().runningCount(owner);
    });
    queue.addFunc("find", [vm](ScriptWorkQueue* value, const std::string& id) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while finding a task");
            return ssq::Object(vm);
        }
        if (!queueView.value().get().find(id)) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkTask>(
            vm, std::make_unique<ScriptWorkTask>(value->reference, id));
        if (!object.ok()) {
            object.ignore("failed to create work task proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("taskCount", [](ScriptWorkQueue* value) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading task count");
            return 0;
        }
        return queueView.value().get().taskCount();
    });
    queue.addFunc("taskAt", [vm](ScriptWorkQueue* value, int index) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok() || !queueView.value().get().taskAt(index)) {
            queueView.ignore("work task lookup did not produce a task proxy");
            return ssq::Object(vm);
        }
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkTask>(
            vm, std::make_unique<ScriptWorkTask>(value->reference, queueView.value().get().taskAt(index)->get().id));
        if (!object.ok()) {
            object.ignore("failed to create work task proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("ownerTaskCount", [](ScriptWorkQueue* value, const std::string& owner) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading owner task count");
            return 0;
        }
        return queueView.value().get().ownerTaskCount(owner);
    });
    queue.addFunc("ownerTaskAt", [vm](ScriptWorkQueue* value, const std::string& owner, int index) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while finding an owner task");
            return ssq::Object(vm);
        }
        auto taskRef = queueView.value().get().ownerTaskAt(owner, index);
        if (!taskRef) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkTask>(
            vm, std::make_unique<ScriptWorkTask>(value->reference, taskRef->get().id));
        if (!object.ok()) {
            object.ignore("failed to create work task proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("eventCount", [](ScriptWorkQueue* value) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading event count");
            return 0;
        }
        return queueView.value().get().eventCount();
    });
    queue.addFunc("eventAt", [vm](ScriptWorkQueue* value, int index) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok() || !queueView.value().get().eventAt(index)) {
            queueView.ignore("work event lookup did not produce an event proxy");
            return ssq::Object(vm);
        }
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkEvent>(
            vm, std::make_unique<ScriptWorkEvent>(value->reference, index));
        if (!object.ok()) {
            object.ignore("failed to create work event proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("clearEvents", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        queueView.value().get().clearEvents();
        return eve::script::projectResult(vm, eve::Result<void>::success());
    });
    queue.addFunc("snapshot", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm,
                productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                      "work queue proxy must not be null", "queue"),
                [](std::string text) { return eve::Value(std::move(text)); });
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok())
            return eve::script::projectResult(
                vm,
                productionBindingFailure<std::string>(queueView.status().code() == eve::StatusCode::NotFound
                                                          ? eve::DiagnosticCode::NotFound
                                                          : eve::DiagnosticCode::StaleHandle,
                                                      "work queue handle is stale", "queue"),
                [](std::string text) { return eve::Value(std::move(text)); });
        return eve::script::projectResult(vm, queueView.value().get().snapshot(),
                                          [](std::string text) { return eve::Value(std::move(text)); });
    });
    queue.addFunc("restore", [vm](ScriptWorkQueue* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        return eve::script::projectResult(vm, queueView.value().get().restore(json));
    });
    queue.addFunc("clear", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status());
        queueView.value().get().clear();
        return eve::script::projectResult(vm, eve::Result<void>::success());
    });
    auto cls = table.addClass(name, Production::create, false);
    expose(cls);
}

void Production::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Production::getName);
    cls.addFunc("newWorkQueue", [vm = cls.getHandle()](Production*) -> ssq::Table {
        return makeOwnedProxy<WorkQueueHandleRef, ScriptWorkQueue>(
            vm, Production::newQueueHandle(), [](WorkQueueHandleRef ref) { return Production::release(ref); }, "queue");
    });
}

}  // namespace eve::production
