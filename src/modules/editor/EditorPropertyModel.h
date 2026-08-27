#pragma once

#include "editor/EditorHostProfile.h"
#include "editor/EditorPropertyPresenter.h"
#include "property_access/PropertyAccess.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace eve::editor {

class IEditorTransactionBackend;
struct EditorDryRunReport;

/** @brief Property visibility/permission surface exposed by an editor model. */
enum class PropertyModelSurface { Developer, Runtime };

/**
 * @brief Adapts command-backed editor properties to the shared MVVM contract.
 *
 * The adapter never mutates a target directly. When a transaction backend is
 * injected, a UI write is converted into a PropertyEditIntent and a
 * DomainOperation, then staged and committed through that backend. The legacy
 * sink remains a one-way compatibility facade and is used only when no
 * transaction backend is configured.
 */
class EditorPropertyModel final : public property_access::IPropertyAccess {
public:
    using EditSink = std::function<EditorResult<void>(const PropertyEditIntent &)>;

    EditorPropertyModel(PropertySchema schema, SelectionSnapshot selection,
                        const IPropertyProvider *provider,
                        PropertyModelSurface surface = PropertyModelSurface::Developer,
                        HostProfile profile = HostProfile::developer(),
                        IEditorTransactionBackend *transactionBackend = nullptr);
    ~EditorPropertyModel() override;

    const property_access::PropertySchema &schema() const override { return presentationSchema_; }
    std::optional<eve::Value> read(const std::string &path) const override;
    [[nodiscard]] property_access::WriteResult write(const std::string &path,
                                                  const eve::Value &value) override;
    std::uint64_t revision() const override { return revision_; }
    property_access::Subscription subscribe(ChangeCallback callback) override;

    /** @brief Return the authoritative provider revision captured by this model. */
    [[nodiscard]] eve::Revision targetRevision() const noexcept { return targetRevision_; }

    /**
     * @brief Bind to the provider's current revision and values.
     * @return Applied after an atomic refresh, or a structured failure.
     */
    [[nodiscard]] EditorResult<void> bind();

    /**
     * @brief Set the borrowed canonical transaction boundary.
     * @param backend Backend that outlives this model, or null to detach it.
     * @return Applied when changed; rejected while that backend has pending work.
     */
    [[nodiscard]] EditorResult<void> setTransactionBackend(IEditorTransactionBackend *backend);

    /**
     * @brief Begin an explicit property transaction.
     * @param label Human-readable history label.
     * @return The stable transaction identity, or a structured failure.
     */
    [[nodiscard]] EditorResult<TransactionId> beginTransaction(std::string label = "Edit property");
    /** @brief Validate pending property writes without changing the target. */
    [[nodiscard]] EditorResult<EditorDryRunReport> previewTransaction();
    /** @brief Commit pending property writes and refresh observers after success. */
    [[nodiscard]] EditorResult<TransactionReceipt> commitTransaction();
    /** @brief Discard pending property writes without publishing them. */
    [[nodiscard]] EditorResult<void> rollbackTransaction();
    /** @brief Retry a retained failed commit. */
    [[nodiscard]] EditorResult<TransactionReceipt> retryTransaction();
    /** @brief Undo the latest committed property transaction. */
    [[nodiscard]] EditorResult<TransactionReceipt> undo();
    /** @brief Redo the latest undone property transaction. */
    [[nodiscard]] EditorResult<TransactionReceipt> redo();

    /** @brief Install the legacy command sink; canonical code should inject a backend. */
    void setEditSink(EditSink sink) { sink_ = std::move(sink); }
    /**
     * @brief Re-read values and adopt the provider's current revision.
     * @return Applied after an atomic refresh; Conflict leaves the model
     *         unchanged when the provider changes during the read.
     */
    [[nodiscard]] EditorResult<void> refresh();

    /** @brief Explicit spelling for refreshing and rebasing this model. */
    [[nodiscard]] EditorResult<void> rebase();

private:
    struct ObserverState;
    void rebuildSchema();
    void emit(const std::string &path, const eve::Value &value);
    [[nodiscard]] EditorResult<eve::Revision> readProviderRevision() const;
    [[nodiscard]] EditorResult<void> ensureCurrentRevision() const;

    PropertySchema editorSchema_;
    SelectionSnapshot selection_;
    const IPropertyProvider *provider_ = nullptr;
    PropertyModelSurface surface_ = PropertyModelSurface::Developer;
    HostProfile profile_;
    property_access::PropertySchema presentationSchema_;
    std::map<std::string, eve::Value> cachedValues_;
    EditSink sink_;
    IEditorTransactionBackend *transactionBackend_ = nullptr;
    std::set<std::string>       pendingPaths_;
    eve::Revision               targetRevision_;
    bool                        bound_ = false;
    std::uint64_t revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
};

}  // namespace eve::editor
