#include "common/VersionedRegistry.h"

namespace eve::detail {
namespace {

/** @brief ParseError carrying one registry validation message. */
eve::Result<void> registryParseError(const char* message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError, message));
}

}  // namespace

eve::Result<void> validateRegistryNextSequence(EventSequence nextEventSequence) {
    if (nextEventSequence.isZero()) return registryParseError("registry next event sequence must be positive");
    return eve::Result<void>::success();
}

eve::Result<Generation> nextRegistryGeneration(Generation current, bool present) {
    if (!present) return eve::Result<Generation>::success(Generation(1));
    const auto next = current.incremented();
    if (!next || next->isZero())
        return eve::Result<Generation>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "registry generation exhausted"));
    return eve::Result<Generation>::success(*next);
}

eve::Result<EventSequence> nextRegistryEventSequence(EventSequence current) {
    const auto next = current.incremented();
    if (!next)
        return eve::Result<EventSequence>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "registry event sequence exhausted"));
    return eve::Result<EventSequence>::success(*next);
}

eve::Result<void> EventLogValidator::accept(EventSequence sequence, Generation generation, bool remove,
                                            bool tombstone) {
    if (sequence.isZero() || (!previous_.isZero() && sequence <= previous_))
        return registryParseError("registry event sequence is not increasing");
    if (generation.isZero()) return registryParseError("registry event generation must be positive");
    if (remove && !tombstone) return registryParseError("remove event must describe a tombstone");
    if (!remove && tombstone) return registryParseError("only remove events may describe tombstones");
    previous_ = sequence;
    return eve::Result<void>::success();
}

eve::Result<void> EventLogValidator::finish(EventSequence nextEventSequence) const {
    if (!previous_.isZero() && nextEventSequence <= previous_)
        return registryParseError("next event sequence must exceed retained events");
    return eve::Result<void>::success();
}

}  // namespace eve::detail
