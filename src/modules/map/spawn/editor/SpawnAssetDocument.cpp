#include "map/spawn/editor/SpawnAssetDocument.h"
#include "editing/EditingProperty.h"
#include "editor/EditorDocumentService.h"
#include "map/spawn/editing/SpawnDocument.h"
#include "map/spawn/editing/SpawnTemplateDocument.h"
#include "map/spawn/editor/SpawnForm.h"
namespace eve::spawn_editor {
struct SpawnAssetDocument::Impl {
    std::shared_ptr<editor::IAtomicDocumentStore> store;
    editor::DocumentService                       service;
    editor::DocumentId                            id;
    std::shared_ptr<SpawnForm>                    form;
    std::optional<eve::Revision>                  saved;
    bool                                          busy = false;
    explicit Impl(std::shared_ptr<editor::IAtomicDocumentStore> owner)
        : store(std::move(owner)), service(store.get()) {}
    template <class Build>
    static Result<std::unique_ptr<Impl>> open(std::shared_ptr<editor::IAtomicDocumentStore> store,
                                              editor::DocumentKey key, std::string uri, const Value& initial,
                                              Build build) {
        using R = Result<std::unique_ptr<Impl>>;
        if (!store) return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing document store"));
        auto result   = std::make_unique<Impl>(std::move(store));
        auto snapshot = result->service.open(key, key.asset.value(), std::move(uri), editing::toEditingValue(initial));
        if (!snapshot) return R::failure(snapshot.status());
        auto content = result->service.content(snapshot.value().id);
        if (!content) return R::failure(content.status());
        auto target = build(editing::TargetId(key.asset.value()), editing::toPresentationValue(content.value()));
        if (!target) return R::failure(target.status());
        auto form = SpawnForm::create(std::move(target).takeValue());
        if (!form) return R::failure(form.status());
        result->form = std::move(form).takeValue();
        result->id   = snapshot.value().id;
        if (snapshot.value().revision.disk != 0) result->saved = result->form->sourceRevision();
        return R::success(std::move(result));
    }
};
SpawnAssetDocument::SpawnAssetDocument(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SpawnAssetDocument::~SpawnAssetDocument() = default;
Result<std::unique_ptr<SpawnAssetDocument>> SpawnAssetDocument::openSidecar(
    std::shared_ptr<editor::IAtomicDocumentStore> store, editor::DocumentKey key, std::string uri,
    const Value& initialSource, std::shared_ptr<const spawn::ITemplateResolver> templates,
    std::shared_ptr<const spawn::IUnitDefinitionResolver> units) {
    auto state = Impl::open(
        std::move(store), std::move(key), std::move(uri), initialSource,
        [templates = std::move(templates), units = std::move(units)](editing::TargetId id, const Value& source) {
            return spawn_editing::SpawnDocumentTarget::create(std::move(id), source, templates, units);
        });
    if (!state) return Result<std::unique_ptr<SpawnAssetDocument>>::failure(state.status());
    return Result<std::unique_ptr<SpawnAssetDocument>>::success(
        std::unique_ptr<SpawnAssetDocument>(new SpawnAssetDocument(std::move(state).takeValue())));
}
Result<std::unique_ptr<SpawnAssetDocument>> SpawnAssetDocument::openTemplate(
    std::shared_ptr<editor::IAtomicDocumentStore> store, editor::DocumentKey key, std::string uri,
    const Value& initialSource, std::shared_ptr<const spawn::IUnitDefinitionResolver> units) {
    auto state = Impl::open(std::move(store), std::move(key), std::move(uri), initialSource,
                            [units = std::move(units)](editing::TargetId id, const Value& source) {
                                return spawn_editing::SpawnTemplateDocumentTarget::create(std::move(id), source, units);
                            });
    if (!state) return Result<std::unique_ptr<SpawnAssetDocument>>::failure(state.status());
    return Result<std::unique_ptr<SpawnAssetDocument>>::success(
        std::unique_ptr<SpawnAssetDocument>(new SpawnAssetDocument(std::move(state).takeValue())));
}
std::shared_ptr<SpawnForm> SpawnAssetDocument::form() const noexcept { return impl_->form; }
bool                       SpawnAssetDocument::dirty() const noexcept {
    return !impl_->saved || *impl_->saved != impl_->form->sourceRevision();
}
Result<void> SpawnAssetDocument::save() {
    if (impl_->busy || impl_->form->history().pending)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Save is active or the form has an unresolved transaction"));
    struct Guard {
        bool& busy;
        explicit Guard(bool& value) : busy(value) { busy = true; }
        ~Guard() { busy = false; }
    } guard(impl_->busy);
    auto source = impl_->form->captureSource();
    if (!source) return Result<void>::failure(source.status());
    const auto revision = source.value().revision;
    auto       edit     = impl_->service.edit(impl_->id, editing::toEditingValue(source.value().source));
    if (!edit) return Result<void>::failure(edit.status());
    auto ticket = impl_->service.requestSave(impl_->id);
    if (!ticket) return Result<void>::failure(ticket.status());
    auto saved = impl_->service.executeSave(ticket.value());
    if (!saved) return Result<void>::failure(saved.status());
    impl_->saved = revision;
    return Result<void>::success();
}
}  // namespace eve::spawn_editor
