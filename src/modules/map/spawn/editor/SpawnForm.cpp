#include "map/spawn/editor/SpawnForm.h"
#include "editor/EditorPropertyModel.h"
#include "editor/EditorTransactionService.h"
#include "map/spawn/editing/SpawnDocument.h"
#include "map/spawn/editing/SpawnTemplateDocument.h"
namespace eve::spawn_editor {
struct SpawnForm::Impl {
    std::unique_ptr<editing::IDomainOperationTarget> document;
    editing::LocalWorldAuthority                     authority;
    editor::LocalTransactionBackend                  backend;
    editor::EditorPropertyModel                      model;
    template <class Document>
    explicit Impl(std::unique_ptr<Document> target)
        : document(std::move(target)),
          authority(document.get()),
          backend(&authority),
          model(properties().schema(selection()), selection(), &properties(), editor::PropertyModelSurface::Developer,
                editor::HostProfile::developer(), &backend) {}
    editing::IPropertyProvider& properties() { return dynamic_cast<editing::IPropertyProvider&>(*document); }
    editing::SelectionSnapshot  selection() const {
        editing::SelectionSnapshot result;
        const auto                 id = document->targetId();
        result.items.push_back(
            {editing::SelectionDomain::Asset, id, editing::StableId(id.value()), document->describe().type});
        return result;
    }
};
SpawnForm::SpawnForm(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnForm::~SpawnForm() = default;
Result<std::unique_ptr<SpawnForm>> SpawnForm::create(std::unique_ptr<spawn_editing::SpawnDocumentTarget> document) {
    using R = Result<std::unique_ptr<SpawnForm>>;
    if (!document) return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing sidecar document"));
    auto impl  = std::make_unique<Impl>(std::move(document));
    auto bound = impl->model.bind();
    if (!bound) return R::failure(bound.status());
    return R::success(std::unique_ptr<SpawnForm>(new SpawnForm(std::move(impl))));
}
Result<std::unique_ptr<SpawnForm>> SpawnForm::create(
    std::unique_ptr<spawn_editing::SpawnTemplateDocumentTarget> document) {
    using R = Result<std::unique_ptr<SpawnForm>>;
    if (!document) return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing template document"));
    auto impl  = std::make_unique<Impl>(std::move(document));
    auto bound = impl->model.bind();
    if (!bound) return R::failure(bound.status());
    return R::success(std::unique_ptr<SpawnForm>(new SpawnForm(std::move(impl))));
}
property_access::IPropertyAccess& SpawnForm::view() noexcept { return impl_->model; }
eve::Revision                     SpawnForm::targetRevision() const noexcept { return impl_->model.targetRevision(); }
eve::Revision SpawnForm::sourceRevision() const noexcept { return eve::Revision(impl_->document->revision()); }
Result<FormSourceSnapshot> SpawnForm::captureSource() const {
    if (history().pending)
        return Result<FormSourceSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Source capture requires a resolved transaction"));
    const auto revision = sourceRevision();
    if (const auto* target = dynamic_cast<const spawn_editing::SpawnDocumentTarget*>(impl_->document.get()))
        return Result<FormSourceSnapshot>::success({revision, target->source()});
    const auto& target = dynamic_cast<const spawn_editing::SpawnTemplateDocumentTarget&>(*impl_->document);
    return Result<FormSourceSnapshot>::success({revision, target.source()});
}
Result<FormMapSnapshot> SpawnForm::captureMap() const {
    const auto* target = dynamic_cast<const spawn_editing::SpawnDocumentTarget*>(impl_->document.get());
    if (!target)
        return Result<FormMapSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "Template forms do not contain a compiled map"));
    return Result<FormMapSnapshot>::success({sourceRevision(), target->shareCompiled()});
}
FormHistory SpawnForm::history() const noexcept {
    return {impl_->backend.canUndo(), impl_->backend.canRedo(), impl_->backend.active()};
}
Result<editing::TransactionReceipt> SpawnForm::undo() { return impl_->model.undo(); }
Result<editing::TransactionReceipt> SpawnForm::redo() { return impl_->model.redo(); }
Result<editing::TransactionReceipt> SpawnForm::retry() { return impl_->model.retryTransaction(); }
Result<void>                        SpawnForm::discard() { return impl_->model.rollbackTransaction(); }
}  // namespace eve::spawn_editor
