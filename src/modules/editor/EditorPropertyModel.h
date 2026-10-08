#pragma once
#include "common/Export.h"


#include "editor/EditorHostProfile.h"
#include "editor/EditorPropertyPresenter.h"
#include "property_access/PropertyAccess.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
 *
 * This model is owner-thread only. Observer callbacks run synchronously after
 * a complete snapshot is published; reentrant notifications are queued until
 * the current notification batch completes.
 */
class EVENGINE_API_ORCHESTRATION EditorPropertyModel final : public property_access::IPropertyAccess {
public:
    using EditSink = std::function<Result<void>(const PropertyEditIntent &)>;

    /** @brief Editor property model. */
    EditorPropertyModel(PropertySchema schema, SelectionSnapshot selection, const IPropertyProvider *provider,
                        PropertyModelSurface       surface            = PropertyModelSurface::Developer,
                        HostProfile                profile            = HostProfile::developer(),
                        IEditorTransactionBackend *transactionBackend = nullptr);
    /** @brief Editor property model. */
    ~EditorPropertyModel() override;

    /** @brief Schema. */
    const property_access::PropertySchema     &schema() const override { return presentationSchema_; }
    /** @brief Reads . */
    std::optional<eve::Value>                  read(const std::string &path) const override;
    /** @brief Writes . */
    [[nodiscard]] property_access::WriteResult write(const std::string &path, const eve::Value &value) override;
    /** @brief Revision. */
    std::uint64_t revision() const override { return revision_; }
    /** @brief Subscribe. */
    property_access::Subscription              subscribe(ChangeCallback callback) override;

    /** @brief Return the authoritative provider revision captured by this model. */
    [[nodiscard]] eve::Revision targetRevision() const noexcept { return targetRevision_; }

    /**
     * @brief Bind to the provider's current revision and values.
     * @return Applied after an atomic refresh, or a structured failure.
     */
    [[nodiscard]] Result<void> bind();

    /**
     * @brief Set the borrowed canonical transaction boundary.
     * @param backend Backend that outlives this model, or null to detach it.
     * @return Applied when changed; rejected while that backend has pending work.
     */
    [[nodiscard]] Result<void> setTransactionBackend(IEditorTransactionBackend *backend);

    /**
     * @brief Begin an explicit property transaction.
     * @param label Human-readable history label.
     * @return The stable transaction identity, or a structured failure.
     */
    [[nodiscard]] Result<TransactionId> beginTransaction(std::string label = "Edit property");
    /** @brief Validate pending property writes without changing the target. */
    [[nodiscard]] Result<EditorDryRunReport> previewTransaction();
    /** @brief Commit pending property writes and refresh observers after success. */
    [[nodiscard]] Result<TransactionReceipt> commitTransaction();
    /** @brief Discard pending property writes without publishing them. */
    [[nodiscard]] Result<void> rollbackTransaction();
    /** @brief Retry a retained failed commit. */
    [[nodiscard]] Result<TransactionReceipt> retryTransaction();
    /** @brief Undo the latest committed property transaction. */
    [[nodiscard]] Result<TransactionReceipt> undo();
    /** @brief Redo the latest undone property transaction. */
    [[nodiscard]] Result<TransactionReceipt> redo();

    /** @brief Install the legacy command sink; canonical code should inject a backend. */
    void setEditSink(EditSink sink) { sink_ = std::move(sink); }
    /**
     * @brief Re-read values and adopt the provider's current revision.
     * @return Applied after an atomic refresh; Conflict leaves the model
     *         unchanged when the provider changes during the read.
     */
    [[nodiscard]] Result<void> refresh();

    /** @brief Explicit spelling for refreshing and rebasing this model. */
    [[nodiscard]] Result<void> rebase();

private:
    struct CachedProperty {
        property_access::PropertyChangeState state = property_access::PropertyChangeState::Missing;
        std::optional<eve::Value>             value;

        /** @brief Operator ==. */
        bool operator==(const CachedProperty &) const = default;
    };

    struct ObserverState;
    void rebuildSchema();
    void                                      dispatch(std::vector<property_access::PropertyChange> changes);
    [[nodiscard]] Result<eve::Revision> readProviderRevision() const;
    [[nodiscard]] Result<void>          ensureCurrentRevision() const;

    PropertySchema editorSchema_;
    SelectionSnapshot selection_;
    const IPropertyProvider *provider_ = nullptr;
    PropertyModelSurface surface_ = PropertyModelSurface::Developer;
    HostProfile profile_;
    property_access::PropertySchema   presentationSchema_;
    std::map<std::string, CachedProperty> cachedProperties_;
    EditSink sink_;
    IEditorTransactionBackend        *transactionBackend_ = nullptr;
    std::set<std::string>             pendingPaths_;
    eve::Revision                     targetRevision_;
    bool                              bound_    = false;
    std::uint64_t revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
    std::vector<std::vector<property_access::PropertyChange>> notificationQueue_;
    bool dispatchingNotifications_ = false;
};

}  // namespace eve::editor
