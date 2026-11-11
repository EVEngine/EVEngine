#include "spawn/definitions/SpawnTemplateSource.h"
#include "definitions/Definitions.h"

namespace eve::spawn {
SpawnTemplateSource::SpawnTemplateSource(definitions::DefinitionRegistry& registry,
                                         const IUnitDefinitionResolver&   units)
    : registry_(registry), units_(units) {}

Result<void> SpawnTemplateSource::publish(const Value& source) {
    auto compiled = compileTemplate(source, units_);
    if (!compiled) return Result<void>::failure(compiled.status());
    const auto& revision = compiled.value();
    auto        current  = registry_.resolve("spawn.template", revision.definition().id);
    if (!current) {
        const auto* diagnostic = current.status().primaryDiagnostic();
        if (diagnostic == nullptr || diagnostic->code() != DiagnosticCode::NotFound)
            return Result<void>::failure(current.status());
        auto inserted = registry_.insert("spawn.template", revision.definition().id, 1, revision.canonicalJson());
        if (!inserted) return Result<void>::failure(inserted.status());
        return Result<void>::success();
    }
    auto replaced = registry_.replace("spawn.template", revision.definition().id, 1, revision.canonicalJson());
    if (!replaced) return Result<void>::failure(replaced.status());
    return Result<void>::success();
}

Result<CompiledTemplate> SpawnTemplateSource::resolve(const DefinitionRef& reference) const {
    if (!reference.id().isValid() || reference.id().namespaceName() != "spawn.template")
        return Result<CompiledTemplate>::failure(
            Diagnostic::error(DiagnosticCode::TypeMismatch, "Expected spawn.template definition", reference.format()));
    auto source = registry_.resolve("spawn.template", std::string(reference.id().name()));
    if (!source) return Result<CompiledTemplate>::failure(source.status());
    auto parsed = Value::fromJson(source.value().get().json);
    if (!parsed) return Result<CompiledTemplate>::failure(parsed.status());
    auto compiled = compileTemplate(parsed.value(), units_);
    if (!compiled) return Result<CompiledTemplate>::failure(compiled.status());
    if (compiled.value().definition().id != reference.id().name())
        return Result<CompiledTemplate>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Template id does not match registry key", reference.format()));
    return compiled;
}
}  // namespace eve::spawn
