#pragma once

#include "common/Result.h"
#include "pixelworld/PixelMaterial.h"
#include "pixelworld/PixelWorldControl.h"
#include "ui/Widget.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::pixelworld_editor {

/** @brief One renderer-neutral material card projected by the Catalog panel. */
struct MaterialCard {
    eve::pixelworld::MaterialId id = eve::pixelworld::MaterialId::Air;
    std::string name;
    eve::pixelworld::MaterialState state = eve::pixelworld::MaterialState::Empty;
    std::uint32_t displayRgba = 0;
    bool selected = false;
    bool reacts = false;
    bool transitions = false;
};

/** @brief Draft mutation receipt used for UI reconciliation and optimistic publication. */
struct CatalogDraftReceipt {
    std::uint64_t revisionBefore = 0;
    std::uint64_t revisionAfter = 0;
    std::uint64_t fingerprint = 0;
};

/**
 * @brief Dedicated visual material/reaction Catalog editor with transactional draft admission.
 *
 * The panel owns an immutable validated draft and never owns or retains a PixelWorld pointer.
 * Every edit reconstructs a complete candidate Catalog and publishes it only after validation.
 * `apply()` borrows the control service synchronously and reaches the runtime authority through
 * its generation-aware world registry. All methods are owner-thread-only and invoke no unknown
 * callbacks except callbacks explicitly embedded in a returned WidgetDesc tree. The panel must
 * outlive any mounted tree because those callbacks borrow `this`.
 */
class PixelWorldCatalogPanel {
public:
    ~PixelWorldCatalogPanel();
    PixelWorldCatalogPanel(const PixelWorldCatalogPanel&) = delete;
    PixelWorldCatalogPanel& operator=(const PixelWorldCatalogPanel&) = delete;
    PixelWorldCatalogPanel(PixelWorldCatalogPanel&& other);
    PixelWorldCatalogPanel& operator=(PixelWorldCatalogPanel&& other);

    /** @brief Decode and validate a versioned Catalog document into a fresh panel draft. */
    [[nodiscard]] static eve::Result<PixelWorldCatalogPanel> create(std::string_view catalogJson);
    /** @brief Create a panel over the canonical built-in Catalog. */
    static PixelWorldCatalogPanel builtIn();

    /** @brief Current validated immutable draft. */
    const eve::pixelworld::MaterialCatalog& draft() const noexcept { return draft_; }
    /** @brief Monotonic local draft revision, advanced only by accepted mutations. */
    std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Selected material index, always valid for the current nonempty draft. */
    std::size_t selectedMaterialIndex() const noexcept { return selectedMaterial_; }
    /** @brief Selected reaction index when at least one binary rule exists. */
    std::optional<std::size_t> selectedReactionIndex() const noexcept { return selectedReaction_; }
    /** @brief Selected phase-rule index when at least one transition exists. */
    std::optional<std::size_t> selectedPhaseIndex() const noexcept { return selectedPhase_; }

    /** @brief Project deterministic cards for the material browser. */
    std::vector<MaterialCard> materialCards() const;
    /** @brief Select a material by canonical index without mutating the Catalog. */
    [[nodiscard]] eve::Result<void> selectMaterial(std::size_t index);
    /** @brief Select a binary reaction rule by canonical index. */
    [[nodiscard]] eve::Result<void> selectReaction(std::size_t index);
    /** @brief Select a phase-transition rule by canonical index. */
    [[nodiscard]] eve::Result<void> selectPhase(std::size_t index);

    /** @brief Replace one material definition after validating the entire reconstructed Catalog. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> replaceMaterial(
        eve::pixelworld::MaterialDefinition definition);
    /** @brief Replace one binary rule after validating the entire reconstructed Catalog. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> replaceReaction(
        std::size_t index, eve::pixelworld::MaterialReactionRule rule);
    /** @brief Add a binary rule and select its canonical post-validation position. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> addReaction(
        eve::pixelworld::MaterialReactionRule rule);
    /** @brief Remove one binary rule transactionally. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> removeReaction(std::size_t index);
    /** @brief Replace one phase rule after validating the entire reconstructed Catalog. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> replacePhase(
        std::size_t index, eve::pixelworld::MaterialPhaseRule rule);
    /** @brief Add a phase rule and select its canonical post-validation position. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> addPhase(
        eve::pixelworld::MaterialPhaseRule rule);
    /** @brief Remove one phase rule transactionally. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> removePhase(std::size_t index);

    /** @brief Encode the current validated draft using the canonical versioned JSON codec. */
    [[nodiscard]] eve::Result<std::string> document() const;
    /** @brief Replace the draft from a validated document; failure leaves all panel state intact. */
    [[nodiscard]] eve::Result<CatalogDraftReceipt> loadDocument(std::string_view catalogJson);
    /** @brief Apply the draft through the canonical paused-world authority path. */
    [[nodiscard]] eve::Result<eve::pixelworld::PixelCatalogReloadReceipt> apply(
        eve::pixelworld::PixelWorldControlService& control, std::uint64_t worldId,
        std::uint64_t expectedFingerprint) const;

    /** @brief Mount or reveal the panel in the UI ECS world and publish its current tree. */
    [[nodiscard]] eve::Result<eve::ui::UIHostHandle> open();
    /** @brief Hide the mounted panel without discarding its validated draft. */
    void close();
    /** @brief True while the panel has a live, visible UI host. */
    bool isOpen() const;
    /** @brief Reconcile the mounted host with the latest draft and selection state. */
    [[nodiscard]] eve::Result<void> refresh();
    /** @brief Mounted UI host handle, or an empty handle before open(). */
    eve::ui::UIHostHandle host() const noexcept { return host_; }

    /** @brief Build a dedicated material browser, property inspector and rule editor visual tree. */
    eve::ui::WidgetDesc buildWidgetTree();
    /** @brief Last callback diagnostic rendered in the panel status bar. */
    const std::string& statusText() const noexcept { return statusText_; }

private:
    explicit PixelWorldCatalogPanel(eve::pixelworld::MaterialCatalog catalog);
    [[nodiscard]] eve::Result<CatalogDraftReceipt> commitCandidate(
        std::vector<eve::pixelworld::MaterialDefinition> definitions,
        std::vector<eve::pixelworld::MaterialReactionRule> reactions,
        std::vector<eve::pixelworld::MaterialPhaseRule> phases);
    void observe(eve::Result<CatalogDraftReceipt> result);
    void observe(eve::Result<void> result);

    eve::pixelworld::MaterialCatalog draft_;
    std::uint64_t revision_ = 1;
    std::size_t selectedMaterial_ = 0;
    std::optional<std::size_t> selectedReaction_;
    std::optional<std::size_t> selectedPhase_;
    std::string statusText_ = "Catalog draft is valid";
    eve::ui::UIHostHandle host_{};
};

}  // namespace eve::pixelworld_editor
