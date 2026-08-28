#include "climbing/Climbing.h"

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> registryFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing"));
}

}  // namespace

eve::Result<ClimbingRuntimeHandleRef> Climbing::newRuntime() {
    return Climbing::create()->runtimes_.emplace(std::make_unique<ClimbingRuntime>());
}

eve::script::Borrowed<ClimbingRuntime> Climbing::resolve(ClimbingRuntimeHandleRef reference) noexcept {
    Climbing* module = ModuleManager::getInstance<Climbing>("Climbing");
    return module ? module->runtimes_.resolve(reference) : eve::script::Borrowed<ClimbingRuntime>();
}

eve::Result<void> Climbing::release(ClimbingRuntimeHandleRef reference) {
    Climbing* module = ModuleManager::getInstance<Climbing>("Climbing");
    if (!module)
        return registryFailure<void>(eve::DiagnosticCode::StaleHandle, "Climbing module is no longer loaded",
                                     "runtime");
    return module->runtimes_.erase(reference);
}

bool Climbing::isStale(ClimbingRuntimeHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Climbing* module = ModuleManager::getInstance<Climbing>("Climbing");
    return !module || module->runtimes_.isStale(reference);
}

eve::Result<ClimbingAnchorGraphHandleRef> Climbing::newAnchorGraph(ClimbingAnchorGraphDefinition graph,
                                                                   physics::World3D& world,
                                                                   physics::PhysicsBodyHandle body) {
    auto instance = ClimbingAnchorGraphInstance::bind(std::move(graph), world, body);
    if (!instance) return eve::Result<ClimbingAnchorGraphHandleRef>::failure(instance.status());
    return Climbing::create()->anchorGraphs_.emplace(
        std::make_unique<ClimbingAnchorGraphInstance>(std::move(instance).takeValue()));
}

eve::script::Borrowed<ClimbingAnchorGraphInstance> Climbing::resolveAnchorGraph(
    ClimbingAnchorGraphHandleRef reference) noexcept {
    Climbing* module = ModuleManager::getInstance<Climbing>("Climbing");
    return module ? module->anchorGraphs_.resolve(reference) : eve::script::Borrowed<ClimbingAnchorGraphInstance>();
}

eve::Result<void> Climbing::releaseAnchorGraph(ClimbingAnchorGraphHandleRef reference) {
    Climbing* module = ModuleManager::getInstance<Climbing>("Climbing");
    if (!module)
        return registryFailure<void>(eve::DiagnosticCode::StaleHandle, "Climbing module is no longer loaded",
                                     "anchorGraph");
    return module->anchorGraphs_.erase(reference);
}

bool Climbing::isAnchorGraphStale(ClimbingAnchorGraphHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Climbing* module = ModuleManager::getInstance<Climbing>("Climbing");
    return !module || module->anchorGraphs_.isStale(reference);
}

}  // namespace eve::climbing
