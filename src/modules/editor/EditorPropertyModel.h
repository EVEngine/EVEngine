#pragma once

#include "editor/EditorHostProfile.h"
#include "editor/EditorPropertyPresenter.h"
#include "presentation/PropertyModel.h"

#include <functional>
#include <memory>

namespace eve::editor {

/** @brief Convert an editor protocol value to the shared presentation value. */
presentation::Value toPresentationValue(const EditorValue &value);
/** @brief Convert a shared presentation value to the editor protocol value. */
EditorValue toEditorValue(const presentation::Value &value);

/** @brief Property visibility/permission surface exposed by an editor model. */
enum class PropertyModelSurface { Developer, Runtime };

/**
 * @brief Adapts command-backed editor properties to the shared MVVM contract.
 *
 * The adapter never mutates a target directly. A UI write is converted into
 * the same PropertyEditIntent used by other editor hosts and handed to the
 * configured sink, which normally plans/executes it through EditorSession.
 */
class EditorPropertyModel final : public presentation::IPropertyModel {
public:
    using EditSink = std::function<EditorResult<void>(const PropertyEditIntent &)>;

    EditorPropertyModel(PropertySchema schema, SelectionSnapshot selection,
                        const IPropertyProvider *provider,
                        PropertyModelSurface surface = PropertyModelSurface::Developer,
                        HostProfile profile = HostProfile::developer());
    ~EditorPropertyModel() override;

    const presentation::PropertySchema &schema() const override { return presentationSchema_; }
    std::optional<presentation::Value> read(const std::string &path) const override;
    presentation::WriteResult write(const std::string &path,
                                    const presentation::Value &value) override;
    std::uint64_t revision() const override { return revision_; }
    presentation::Subscription subscribe(ChangeCallback callback) override;

    /** @brief Install the command execution boundary used for UI writes. */
    void setEditSink(EditSink sink) { sink_ = std::move(sink); }
    /** @brief Re-read values and notify observers of properties that changed. */
    void refresh();

private:
    struct ObserverState;
    void rebuildSchema();
    void emit(const std::string &path, const presentation::Value &value);

    PropertySchema editorSchema_;
    SelectionSnapshot selection_;
    const IPropertyProvider *provider_ = nullptr;
    PropertyModelSurface surface_ = PropertyModelSurface::Developer;
    HostProfile profile_;
    presentation::PropertySchema presentationSchema_;
    std::map<std::string, presentation::Value> cachedValues_;
    EditSink sink_;
    std::uint64_t revision_ = 0;
    std::shared_ptr<ObserverState> observers_;
};

}  // namespace eve::editor
