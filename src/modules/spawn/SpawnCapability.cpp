#include "common/Capability.h"
#include "spawn/SpawnWorld.h"

namespace eve::spawn {
Result<std::shared_ptr<SpawnProviders>> bindSpawnProviders() {
    auto* source = cap::query<ISpawnProviderSource>();
    if (source == nullptr)
        return Result<std::shared_ptr<SpawnProviders>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Spawn provider source is absent", "spawn.providers"));
    try {
        auto session = source->acquire();
        if (!session) return Result<std::shared_ptr<SpawnProviders>>::failure(session.status());
        if (!session.value() || !session.value()->placement || !session.value()->factory)
            return Result<std::shared_ptr<SpawnProviders>>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "Spawn provider session is incomplete", "spawn.providers"));
        return session;
    } catch (const std::exception& error) {
        return Result<std::shared_ptr<SpawnProviders>>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, error.what(), "spawn.providers"));
    } catch (...) {
        return Result<std::shared_ptr<SpawnProviders>>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, "Provider source threw", "spawn.providers"));
    }
}
}  // namespace eve::spawn
