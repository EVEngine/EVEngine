#pragma once
#include "common/Export.h"

#include "editing/EditingCommandRegistry.h"
#include "editing/EditingProperty.h"
#include "editing/EditingResult.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve::editing {

/** @brief UI-neutral panel descriptor contributed by a domain editor package. */
struct EditorPanelSpec {
    std::string id;
    std::string title;
    std::string region = "center";
    std::string capability;
    std::string context;
    int         order           = 0;
    bool        primaryViewport = false;
};

/** @brief Target type advertised for automation create, with optional createSchema. */
struct EditorTargetTypeSpec {
    std::string    type;
    PropertySchema createSchema;
};

/** @brief Declarative cross-domain link hint (executed by EditorHost routers). */
struct EditorLinkSpec {
    std::string when;
    std::string then;
};

/** @brief Undo participation policy for a mounted package. */
enum class EditorUndoPolicy { Share, Private };

/**
 * @brief Single domain-editor deliverable for Host discover/autoMount.
 *
 * Domains advertise one package; Host installs panels/commands/schemas.
 * Incomplete packages must set incomplete=true and are rejected by default.
 */
struct EVENGINE_API_PLATFORM EditorPackage {
    std::string                           ownerModule;
    std::vector<EditorPanelSpec>          panels;
    std::vector<EditingCommandDescriptor> commands;
    std::vector<EditorTargetTypeSpec>     targetTypes;
    std::string                           selectionDomain;
    std::string                           previewKind;
    std::vector<EditorLinkSpec>           links;
    EditorUndoPolicy                      undoPolicy = EditorUndoPolicy::Share;
    bool                                  incomplete = false;
};

/**
 * @brief Capability-discovered package advertisement from a domain editor module.
 *
 * @ownership Provider lifetime is owned by the domain module; Host never retains
 *            the provider pointer across frames without a generation lease.
 */
class IEditorPackageProvider {
public:
    static constexpr const char* capabilityName = "IEditorPackageProvider";

    /** @brief Releases IEditorPackageProvider resources. */
    virtual ~IEditorPackageProvider() = default;

    /** @brief Stable owner module identity (matches command ownerModule). */
    [[nodiscard]] virtual std::string_view ownerModule() const = 0;

    /**
     * @brief Return the advertised package snapshot.
     * @lifetime Borrowed until the provider is destroyed or next mutation.
     */
    [[nodiscard]] virtual const EditorPackage& package() const = 0;
};

/**
 * @brief Validate package completeness for Host mount.
 * @param allowIncomplete When false, incomplete packages fail.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> validateEditorPackage(const EditorPackage& package,
                                                                       bool                 allowIncomplete = false);

}  // namespace eve::editing
