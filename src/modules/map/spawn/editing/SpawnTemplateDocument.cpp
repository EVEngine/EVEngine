#include "map/spawn/editing/SpawnTemplateDocument.h"
#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include "SpawnDocumentInternal.inl"
#include "SpawnFormInternal.inl"
#include "spawn/SpawnDefinition.h"
namespace eve::spawn_editing {
using detail::failure;
namespace {
constexpr std::string_view operationType = "spawn.template-document.replace.v1";
}
struct SpawnTemplateDocumentTarget::Impl : detail::DocumentData<spawn::CompiledTemplate> {};
SpawnTemplateDocumentTarget::SpawnTemplateDocumentTarget(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnTemplateDocumentTarget::~SpawnTemplateDocumentTarget() = default;
Result<std::unique_ptr<SpawnTemplateDocumentTarget>> SpawnTemplateDocumentTarget::create(
    editing::TargetId id, const Value& input, std::shared_ptr<const spawn::IUnitDefinitionResolver> units) {
    using R = Result<std::unique_ptr<SpawnTemplateDocumentTarget>>;
    if (id.empty() || !units)
        return failure<std::unique_ptr<SpawnTemplateDocumentTarget>>(DiagnosticCode::InvalidArgument,
                                                                     "Document id and actual resolver owners required");
    auto source   = input;
    auto compiler = [units](const Value& tree) {
        return detail::compileSafely<spawn::CompiledTemplate>([&] { return spawn::compileTemplate(tree, *units); });
    };
    auto content = compiler(source);
    if (!content) return R::failure(content.status());
    auto impl     = std::make_unique<Impl>();
    impl->id      = std::move(id);
    impl->compile = std::move(compiler);
    impl->state   = std::make_shared<const Impl::State>(std::move(source), std::move(content).takeValue());
    return R::success(std::unique_ptr<SpawnTemplateDocumentTarget>(new SpawnTemplateDocumentTarget(std::move(impl))));
}
editing::TargetId         SpawnTemplateDocumentTarget::targetId() const { return impl_->id; }
editing::TargetDescriptor SpawnTemplateDocumentTarget::describe() const {
    return {
        impl_->id,
        "spawn-template-document",
        revision(),
        false,
        {ISpawnTemplateDocumentEditTarget::editingCapabilityId(), editing::IPropertyProvider::editingCapabilityId()}};
}
void* SpawnTemplateDocumentTarget::queryCapability(const editing::CapabilityId& capability) {
    if (capability == editing::IPropertyProvider::editingCapabilityId())
        return static_cast<editing::IPropertyProvider*>(this);
    return capability == ISpawnTemplateDocumentEditTarget::editingCapabilityId()
               ? static_cast<ISpawnTemplateDocumentEditTarget*>(this)
               : nullptr;
}
Value                          SpawnTemplateDocumentTarget::source() const { return impl_->state->source; }
const spawn::CompiledTemplate& SpawnTemplateDocumentTarget::compiled() const noexcept { return impl_->state->compiled; }
Result<editing::DomainOperation> SpawnTemplateDocumentTarget::makeReplace(const Value& input) const {
    return detail::planReplace(*impl_, input, operationType);
}
Result<void> SpawnTemplateDocumentTarget::applyDomainOperation(const editing::DomainOperation& input) {
    return detail::applyReplace(*impl_, input, operationType, revision(), [&] { markDirty(); });
}
std::unique_ptr<editing::IDomainOperationTarget> SpawnTemplateDocumentTarget::cloneDomainState() const {
    auto impl      = std::make_unique<Impl>(*impl_);
    impl->busy     = false;
    impl->stagedAt = revision();
    auto result    = std::unique_ptr<SpawnTemplateDocumentTarget>(new SpawnTemplateDocumentTarget(std::move(impl)));
    result->setRevision(revision());
    result->setDirtyRegion(dirtyRegion());
    return result;
}
Result<void> SpawnTemplateDocumentTarget::commitDomainState(
    std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    if (impl_->busy) return failure<void>(DiagnosticCode::PreconditionViolation, "Document operation reentry");
    auto* staged = dynamic_cast<SpawnTemplateDocumentTarget*>(candidate.get());
    if (!staged || staged == this || staged->impl_->id != impl_->id || staged->impl_->identity != impl_->identity ||
        staged->impl_->stagedAt != revision() || staged->impl_->busy)
        return failure<void>(DiagnosticCode::Conflict, "Foreign or stale staged spawn document");
    impl_->state = std::move(staged->impl_->state);
    setRevision(staged->revision());
    setDirtyRegion(staged->dirtyRegion());
    return editing::applied<void>();
}

Result<eve::Revision> SpawnTemplateDocumentTarget::currentRevision(const editing::SelectionSnapshot& selection) const {
    if (!detail::matchesSelection(*this, selection, "spawn-template-document"))
        return failure<eve::Revision>(DiagnosticCode::InvalidArgument, "Spawn form selection mismatch");
    return Result<eve::Revision>::success(eve::Revision(revision()));
}
editing::PropertySchema SpawnTemplateDocumentTarget::schema(const editing::SelectionSnapshot& selection) const {
    return detail::formSchema(*this, selection, "spawn-template-document");
}
editing::PropertyReadResult SpawnTemplateDocumentTarget::read(const editing::SelectionSnapshot& selection,
                                                              const editing::PropertyPath&      path) const {
    return detail::readField(*this, selection, path, "spawn-template-document");
}
Result<editing::DomainOperation> SpawnTemplateDocumentTarget::makeSet(const editing::SelectionSnapshot& selection,
                                                                      const editing::PropertyPath&      path,
                                                                      const editing::Value&             value,
                                                                      editing::PropertySetMode          mode) const {
    return detail::planField(*this, selection, path, value, mode, "spawn-template-document");
}
Result<editing::DomainOperation> SpawnTemplateDocumentTarget::makeReset(const editing::SelectionSnapshot&,
                                                                        const editing::PropertyPath&) const {
    return failure<editing::DomainOperation>(DiagnosticCode::Unsupported, "Spawn fields require explicit values");
}
}  // namespace eve::spawn_editing
