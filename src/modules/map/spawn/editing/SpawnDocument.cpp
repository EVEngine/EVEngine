#include "map/spawn/editing/SpawnDocument.h"
#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include "SpawnDocumentInternal.inl"
#include "SpawnFormInternal.inl"
#include "spawn/SpawnMap.h"
namespace eve::spawn_editing {
using detail::failure;
namespace {
constexpr std::string_view operationType = "spawn.document.replace.v1";
}
struct SpawnDocumentTarget::Impl : detail::DocumentData<spawn::CompiledMap> {};
SpawnDocumentTarget::SpawnDocumentTarget(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnDocumentTarget::~SpawnDocumentTarget() = default;
Result<std::unique_ptr<SpawnDocumentTarget>> SpawnDocumentTarget::create(
    editing::TargetId id, const Value& input, std::shared_ptr<const spawn::ITemplateResolver> templates,
    std::shared_ptr<const spawn::IUnitDefinitionResolver> units) {
    using R = Result<std::unique_ptr<SpawnDocumentTarget>>;
    if (id.empty() || !templates || !units)
        return failure<std::unique_ptr<SpawnDocumentTarget>>(DiagnosticCode::InvalidArgument,
                                                             "Document id and actual resolver owners required");
    auto source   = input;
    auto compiler = [templates, units](const Value& tree) {
        return detail::compileSafely<spawn::CompiledMap>([&] { return spawn::compileMap(tree, *templates, *units); });
    };
    auto content = compiler(source);
    if (!content) return R::failure(content.status());
    auto impl     = std::make_unique<Impl>();
    impl->id      = std::move(id);
    impl->compile = std::move(compiler);
    impl->state   = std::make_shared<const Impl::State>(std::move(source), std::move(content).takeValue());
    return R::success(std::unique_ptr<SpawnDocumentTarget>(new SpawnDocumentTarget(std::move(impl))));
}
editing::TargetId         SpawnDocumentTarget::targetId() const { return impl_->id; }
editing::TargetDescriptor SpawnDocumentTarget::describe() const {
    return {impl_->id,
            "spawn-document",
            revision(),
            false,
            {ISpawnDocumentEditTarget::editingCapabilityId(), editing::IPropertyProvider::editingCapabilityId()}};
}
void* SpawnDocumentTarget::queryCapability(const editing::CapabilityId& capability) {
    if (capability == editing::IPropertyProvider::editingCapabilityId())
        return static_cast<editing::IPropertyProvider*>(this);
    return capability == ISpawnDocumentEditTarget::editingCapabilityId() ? static_cast<ISpawnDocumentEditTarget*>(this)
                                                                         : nullptr;
}
Value                     SpawnDocumentTarget::source() const { return impl_->state->source; }
const spawn::CompiledMap& SpawnDocumentTarget::compiled() const noexcept { return impl_->state->compiled; }
std::shared_ptr<const spawn::CompiledMap> SpawnDocumentTarget::shareCompiled() const noexcept {
    return {impl_->state, &impl_->state->compiled};
}
Result<editing::DomainOperation> SpawnDocumentTarget::makeReplace(const Value& input) const {
    return detail::planReplace(*impl_, input, operationType);
}
Result<void> SpawnDocumentTarget::applyDomainOperation(const editing::DomainOperation& input) {
    return detail::applyReplace(*impl_, input, operationType, revision(), [&] { markDirty(); });
}
std::unique_ptr<editing::IDomainOperationTarget> SpawnDocumentTarget::cloneDomainState() const {
    auto impl      = std::make_unique<Impl>(*impl_);
    impl->busy     = false;
    impl->stagedAt = revision();
    auto result    = std::unique_ptr<SpawnDocumentTarget>(new SpawnDocumentTarget(std::move(impl)));
    result->setRevision(revision());
    result->setDirtyRegion(dirtyRegion());
    return result;
}
Result<void> SpawnDocumentTarget::commitDomainState(std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    if (impl_->busy) return failure<void>(DiagnosticCode::PreconditionViolation, "Document operation reentry");
    auto* staged = dynamic_cast<SpawnDocumentTarget*>(candidate.get());
    if (!staged || staged == this || staged->impl_->id != impl_->id || staged->impl_->identity != impl_->identity ||
        staged->impl_->stagedAt != revision() || staged->impl_->busy)
        return failure<void>(DiagnosticCode::Conflict, "Foreign or stale staged spawn document");
    impl_->state = std::move(staged->impl_->state);
    setRevision(staged->revision());
    setDirtyRegion(staged->dirtyRegion());
    return editing::applied<void>();
}

Result<eve::Revision> SpawnDocumentTarget::currentRevision(const editing::SelectionSnapshot& selection) const {
    if (!detail::matchesSelection(*this, selection, "spawn-document"))
        return failure<eve::Revision>(DiagnosticCode::InvalidArgument, "Spawn form selection mismatch");
    return Result<eve::Revision>::success(eve::Revision(revision()));
}
editing::PropertySchema SpawnDocumentTarget::schema(const editing::SelectionSnapshot& selection) const {
    return detail::formSchema(*this, selection, "spawn-document");
}
editing::PropertyReadResult SpawnDocumentTarget::read(const editing::SelectionSnapshot& selection,
                                                      const editing::PropertyPath&      path) const {
    return detail::readField(*this, selection, path, "spawn-document");
}
Result<editing::DomainOperation> SpawnDocumentTarget::makeSet(const editing::SelectionSnapshot& selection,
                                                              const editing::PropertyPath&      path,
                                                              const editing::Value&             value,
                                                              editing::PropertySetMode          mode) const {
    return detail::planField(*this, selection, path, value, mode, "spawn-document");
}
Result<editing::DomainOperation> SpawnDocumentTarget::makeReset(const editing::SelectionSnapshot&,
                                                                const editing::PropertyPath&) const {
    return failure<editing::DomainOperation>(DiagnosticCode::Unsupported, "Spawn fields require explicit values");
}
}  // namespace eve::spawn_editing
