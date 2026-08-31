#include "pixelworld_editor/PixelWorldCatalogPanel.h"

#include "pixelworld/PixelMaterialCatalogCodec.h"
#include "ui/UIHost.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace eve::pixelworld_editor {
namespace {

using namespace eve::pixelworld;

constexpr const char* kCatalogPanelHostName = "eve_pixelworld_catalog";

template <class T>
eve::Result<T> fail(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "pixelworld.catalog-panel"));
}

std::vector<MaterialDefinition> definitions(const MaterialCatalog& catalog) {
    return {catalog.definitions().begin(), catalog.definitions().end()};
}

std::vector<MaterialReactionRule> reactions(const MaterialCatalog& catalog) {
    return {catalog.reactions().begin(), catalog.reactions().end()};
}

std::vector<MaterialPhaseRule> phases(const MaterialCatalog& catalog) {
    return {catalog.phaseRules().begin(), catalog.phaseRules().end()};
}

std::string stateName(MaterialState state) {
    constexpr std::array names{"Empty", "Solid", "Powder", "Liquid", "Gas", "Energy"};
    const auto index = static_cast<std::size_t>(state);
    return index < names.size() ? names[index] : "Unknown";
}

template <class Integer>
std::optional<Integer> parseInteger(std::string_view text) {
    Integer value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

std::optional<std::uint32_t> parseRgba(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 8) return std::nullopt;
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

std::string rgbaText(std::uint32_t rgba) {
    std::ostringstream output;
    output << '#' << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << rgba;
    return output.str();
}

std::vector<std::string> splitTags(std::string_view value) {
    std::vector<std::string> result;
    while (!value.empty()) {
        const auto comma = value.find(',');
        std::string tag(value.substr(0, comma));
        const auto first = tag.find_first_not_of(" \t");
        const auto last = tag.find_last_not_of(" \t");
        if (first != std::string::npos) result.push_back(tag.substr(first, last - first + 1));
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::string joinTags(const std::vector<std::string>& tags) {
    std::string result;
    for (const auto& tag : tags) {
        if (!result.empty()) result += ", ";
        result += tag;
    }
    return result;
}

template <class Integer, class Setter>
eve::ui::WidgetDesc integerField(std::string label, std::string id, Integer value,
                                 Setter setter) {
    return eve::ui::inputText(std::move(label), std::to_string(value), std::move(id),
                              [setter = std::move(setter)](const std::string& text) mutable {
                                  if (const auto parsed = parseInteger<Integer>(text)) setter(*parsed);
                              });
}

}  // namespace

PixelWorldCatalogPanel::PixelWorldCatalogPanel(MaterialCatalog catalog)
    : draft_(std::move(catalog)) {
    if (!draft_.reactions().empty()) selectedReaction_ = 0;
    if (!draft_.phaseRules().empty()) selectedPhase_ = 0;
}

PixelWorldCatalogPanel::~PixelWorldCatalogPanel() {
    if (auto host = eve::ui::UIHost::resolve(host_)) {
        host->get().setTree(eve::ui::window("", {}));
        host->get().setVisible(false);
    }
}

PixelWorldCatalogPanel::PixelWorldCatalogPanel(PixelWorldCatalogPanel&& other)
    : draft_(std::move(other.draft_)),
      revision_(other.revision_),
      selectedMaterial_(other.selectedMaterial_),
      selectedReaction_(other.selectedReaction_),
      selectedPhase_(other.selectedPhase_),
      statusText_(std::move(other.statusText_)),
      host_(other.host_) {
    other.host_ = {};
    if (host_.table) (void)refresh();
}

PixelWorldCatalogPanel& PixelWorldCatalogPanel::operator=(PixelWorldCatalogPanel&& other) {
    if (this == &other) return *this;
    if (auto host = eve::ui::UIHost::resolve(host_)) {
        host->get().setTree(eve::ui::window("", {}));
        host->get().setVisible(false);
    }
    draft_ = std::move(other.draft_);
    revision_ = other.revision_;
    selectedMaterial_ = other.selectedMaterial_;
    selectedReaction_ = other.selectedReaction_;
    selectedPhase_ = other.selectedPhase_;
    statusText_ = std::move(other.statusText_);
    host_ = other.host_;
    other.host_ = {};
    if (host_.table) (void)refresh();
    return *this;
}

eve::Result<PixelWorldCatalogPanel> PixelWorldCatalogPanel::create(std::string_view catalogJson) {
    auto decoded = decodeMaterialCatalogJson(catalogJson);
    if (!decoded.ok())
        return eve::Result<PixelWorldCatalogPanel>::failure(decoded.status());
    return eve::Result<PixelWorldCatalogPanel>::success(
        PixelWorldCatalogPanel(std::move(decoded).takeValue()));
}

PixelWorldCatalogPanel PixelWorldCatalogPanel::builtIn() {
    return PixelWorldCatalogPanel(MaterialCatalog::builtIn());
}

std::vector<MaterialCard> PixelWorldCatalogPanel::materialCards() const {
    std::vector<MaterialCard> result;
    result.reserve(draft_.definitions().size());
    for (std::size_t index = 0; index < draft_.definitions().size(); ++index) {
        const auto& definition = draft_.definitions()[index];
        result.push_back({definition.id, definition.name, definition.state,
                          definition.displayRgba, index == selectedMaterial_,
                          draft_.canReact(definition.id),
                          draft_.canPhaseTransition(definition.id)});
    }
    return result;
}

eve::Result<void> PixelWorldCatalogPanel::selectMaterial(std::size_t index) {
    if (index >= draft_.definitions().size())
        return fail<void>(eve::DiagnosticCode::InvalidArgument,
                          "material selection is outside the Catalog", "materialIndex");
    selectedMaterial_ = index;
    return eve::Result<void>::success();
}

eve::Result<void> PixelWorldCatalogPanel::selectReaction(std::size_t index) {
    if (index >= draft_.reactions().size())
        return fail<void>(eve::DiagnosticCode::InvalidArgument,
                          "reaction selection is outside the Catalog", "reactionIndex");
    selectedReaction_ = index;
    return eve::Result<void>::success();
}

eve::Result<void> PixelWorldCatalogPanel::selectPhase(std::size_t index) {
    if (index >= draft_.phaseRules().size())
        return fail<void>(eve::DiagnosticCode::InvalidArgument,
                          "phase selection is outside the Catalog", "phaseIndex");
    selectedPhase_ = index;
    return eve::Result<void>::success();
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::commitCandidate(
    std::vector<MaterialDefinition> candidateDefinitions,
    std::vector<MaterialReactionRule> candidateReactions,
    std::vector<MaterialPhaseRule> candidatePhases) {
    auto candidate = MaterialCatalog::create(std::move(candidateDefinitions),
                                             std::move(candidateReactions),
                                             std::move(candidatePhases));
    if (!candidate.ok())
        return eve::Result<CatalogDraftReceipt>::failure(candidate.status());
    CatalogDraftReceipt receipt{revision_, revision_ + 1, candidate.value().fingerprint()};
    draft_ = std::move(candidate).takeValue();
    revision_ = receipt.revisionAfter;
    if (selectedMaterial_ >= draft_.definitions().size()) selectedMaterial_ = 0;
    if (draft_.reactions().empty()) selectedReaction_.reset();
    else if (!selectedReaction_ || *selectedReaction_ >= draft_.reactions().size())
        selectedReaction_ = draft_.reactions().size() - 1;
    if (draft_.phaseRules().empty()) selectedPhase_.reset();
    else if (!selectedPhase_ || *selectedPhase_ >= draft_.phaseRules().size())
        selectedPhase_ = draft_.phaseRules().size() - 1;
    return eve::Result<CatalogDraftReceipt>::success(receipt);
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::replaceMaterial(
    MaterialDefinition definition) {
    const auto index = static_cast<std::size_t>(definition.id);
    if (index >= draft_.definitions().size())
        return fail<CatalogDraftReceipt>(eve::DiagnosticCode::InvalidArgument,
                                         "material id is outside the Catalog", "material.id");
    auto candidateDefinitions = definitions(draft_);
    candidateDefinitions[index] = std::move(definition);
    return commitCandidate(std::move(candidateDefinitions), reactions(draft_), phases(draft_));
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::replaceReaction(
    std::size_t index, MaterialReactionRule rule) {
    if (index >= draft_.reactions().size())
        return fail<CatalogDraftReceipt>(eve::DiagnosticCode::InvalidArgument,
                                         "reaction index is outside the Catalog", "reactionIndex");
    auto candidate = reactions(draft_);
    candidate[index] = std::move(rule);
    return commitCandidate(definitions(draft_), std::move(candidate), phases(draft_));
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::addReaction(
    MaterialReactionRule rule) {
    const std::string selectedId = rule.id;
    auto candidate = reactions(draft_);
    candidate.push_back(std::move(rule));
    auto committed = commitCandidate(definitions(draft_), std::move(candidate), phases(draft_));
    if (committed.ok()) {
        for (std::size_t index = 0; index < draft_.reactions().size(); ++index)
            if (draft_.reactions()[index].id == selectedId) selectedReaction_ = index;
    }
    return committed;
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::removeReaction(std::size_t index) {
    if (index >= draft_.reactions().size())
        return fail<CatalogDraftReceipt>(eve::DiagnosticCode::InvalidArgument,
                                         "reaction index is outside the Catalog", "reactionIndex");
    auto candidate = reactions(draft_);
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
    return commitCandidate(definitions(draft_), std::move(candidate), phases(draft_));
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::replacePhase(
    std::size_t index, MaterialPhaseRule rule) {
    if (index >= draft_.phaseRules().size())
        return fail<CatalogDraftReceipt>(eve::DiagnosticCode::InvalidArgument,
                                         "phase index is outside the Catalog", "phaseIndex");
    auto candidate = phases(draft_);
    candidate[index] = std::move(rule);
    return commitCandidate(definitions(draft_), reactions(draft_), std::move(candidate));
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::addPhase(MaterialPhaseRule rule) {
    const std::string selectedId = rule.id;
    auto candidate = phases(draft_);
    candidate.push_back(std::move(rule));
    auto committed = commitCandidate(definitions(draft_), reactions(draft_), std::move(candidate));
    if (committed.ok()) {
        for (std::size_t index = 0; index < draft_.phaseRules().size(); ++index)
            if (draft_.phaseRules()[index].id == selectedId) selectedPhase_ = index;
    }
    return committed;
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::removePhase(std::size_t index) {
    if (index >= draft_.phaseRules().size())
        return fail<CatalogDraftReceipt>(eve::DiagnosticCode::InvalidArgument,
                                         "phase index is outside the Catalog", "phaseIndex");
    auto candidate = phases(draft_);
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
    return commitCandidate(definitions(draft_), reactions(draft_), std::move(candidate));
}

eve::Result<std::string> PixelWorldCatalogPanel::document() const {
    return encodeMaterialCatalogJson(draft_);
}

eve::Result<CatalogDraftReceipt> PixelWorldCatalogPanel::loadDocument(
    std::string_view catalogJson) {
    auto decoded = decodeMaterialCatalogJson(catalogJson);
    if (!decoded.ok())
        return eve::Result<CatalogDraftReceipt>::failure(decoded.status());
    return commitCandidate(definitions(decoded.value()), reactions(decoded.value()),
                           phases(decoded.value()));
}

eve::Result<PixelCatalogReloadReceipt> PixelWorldCatalogPanel::apply(
    PixelWorldControlService& control, std::uint64_t worldId,
    std::uint64_t expectedFingerprint) const {
    auto encoded = document();
    if (!encoded.ok())
        return eve::Result<PixelCatalogReloadReceipt>::failure(encoded.status());
    return control.reloadMaterialCatalog(worldId, encoded.value(), expectedFingerprint);
}

eve::Result<eve::ui::UIHostHandle> PixelWorldCatalogPanel::open() {
    auto host = eve::ui::UIHost::resolve(host_);
    if (!host) {
        host_ = eve::ui::UIHost::createHost(kCatalogPanelHostName);
        host = eve::ui::UIHost::resolve(host_);
    }
    if (!host) {
        return fail<eve::ui::UIHostHandle>(
            eve::DiagnosticCode::PreconditionViolation,
            "pixelworld catalog panel requires an active UI ECS world", "uiHost");
    }
    host->get().setVisible(true);
    host->get().setLayer(90);
    auto meta = host->get().meta();
    meta->hasPos = true;
    meta->posX = 12.0F;
    meta->posY = 12.0F;
    meta->hasSize = true;
    meta->sizeX = 820.0F;
    meta->sizeY = 540.0F;
    auto refreshed = refresh();
    if (!refreshed) {
        return eve::Result<eve::ui::UIHostHandle>::failure(refreshed.status());
    }
    return eve::Result<eve::ui::UIHostHandle>::success(host_);
}

void PixelWorldCatalogPanel::close() {
    if (auto host = eve::ui::UIHost::resolve(host_)) host->get().setVisible(false);
}

bool PixelWorldCatalogPanel::isOpen() const {
    auto host = eve::ui::UIHost::resolve(host_);
    return host && host->get().meta()->visible;
}

eve::Result<void> PixelWorldCatalogPanel::refresh() {
    auto host = eve::ui::UIHost::resolve(host_);
    if (!host)
        return fail<void>(eve::DiagnosticCode::PreconditionViolation,
                          "pixelworld catalog panel is not mounted", "uiHost");
    host->get().setTree(buildWidgetTree());
    return eve::Result<void>::success();
}

void PixelWorldCatalogPanel::observe(eve::Result<CatalogDraftReceipt> result) {
    if (result.ok()) statusText_ = "Catalog draft validated at revision " +
                                   std::to_string(result.value().revisionAfter);
    else statusText_ = result.status().describe();
    if (isOpen()) (void)refresh();
}

void PixelWorldCatalogPanel::observe(eve::Result<void> result) {
    if (result.ok()) statusText_ = "Selection updated";
    else statusText_ = result.status().describe();
    if (isOpen()) (void)refresh();
}

eve::ui::WidgetDesc PixelWorldCatalogPanel::buildWidgetTree() {
    using namespace eve::ui;
    std::vector<WidgetDesc> materialRows;
    for (std::size_t index = 0; index < draft_.definitions().size(); ++index) {
        const auto& material = draft_.definitions()[index];
        const float red = float((material.displayRgba >> 24U) & 0xFFU) / 255.0f;
        const float green = float((material.displayRgba >> 16U) & 0xFFU) / 255.0f;
        const float blue = float((material.displayRgba >> 8U) & 0xFFU) / 255.0f;
        WidgetDesc swatch = image("material-swatch/" + std::to_string(index), 18.0f, 18.0f)
                                 .withTint(red, green, blue, 1.0f)
                                 .withCornerRadius(3.0f);
        WidgetDesc choose = button(material.name + "  ·  " + stateName(material.state),
                                   "material/" + std::to_string(index), [this, index] {
                                       observe(selectMaterial(index));
                                   });
        choose.checked = index == selectedMaterial_;
        materialRows.push_back(row({std::move(swatch), std::move(choose)},
                                   "material-row/" + std::to_string(index)));
    }

    const MaterialDefinition selected = draft_.definitions()[selectedMaterial_];
    auto mutateMaterial = [this, id = selected.id](auto mutation) {
        MaterialDefinition value = draft_.definition(id);
        mutation(value);
        observe(replaceMaterial(std::move(value)));
    };
    std::vector<std::string> stateOptions{"Empty", "Solid", "Powder", "Liquid", "Gas", "Energy"};
    const float red = float((selected.displayRgba >> 24U) & 0xFFU) / 255.0f;
    const float green = float((selected.displayRgba >> 16U) & 0xFFU) / 255.0f;
    const float blue = float((selected.displayRgba >> 8U) & 0xFFU) / 255.0f;
    const float alpha = float(selected.displayRgba & 0xFFU) / 255.0f;

    std::vector<WidgetDesc> materialInspector{
        sectionHeader("Material · " + selected.name, "material-heading"),
        image("material-preview", 96.0f, 56.0f).withTint(red, green, blue, alpha).withCornerRadius(6.0f),
        inputText("Name", selected.name, "material-name", [mutateMaterial](const std::string& value) mutable {
            mutateMaterial([&](auto& material) { material.name = value; });
        }),
        combo("State", stateOptions, static_cast<int>(selected.state), "material-state",
              [mutateMaterial](float value) mutable {
                  mutateMaterial([&](auto& material) {
                      material.state = static_cast<MaterialState>(std::clamp(int(value), 0, 5));
                  });
              }),
        inputText("RGBA", rgbaText(selected.displayRgba), "material-rgba",
                  [mutateMaterial](const std::string& value) mutable {
                      if (const auto parsed = parseRgba(value))
                          mutateMaterial([&](auto& material) { material.displayRgba = *parsed; });
                  }),
        checkbox("Flammable", selected.flammable, "material-flammable",
                 [mutateMaterial](bool value) mutable {
                     mutateMaterial([&](auto& material) { material.flammable = value; });
                 }),
        inputText("Tags", joinTags(selected.tags), "material-tags",
                  [mutateMaterial](const std::string& value) mutable {
                      mutateMaterial([&](auto& material) { material.tags = splitTags(value); });
                  }),
        integerField("Density", "material-density", selected.density,
                     [mutateMaterial](std::uint16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.density = value; });
                     }),
        integerField("Viscosity", "material-viscosity", selected.viscosity,
                     [mutateMaterial](std::uint8_t value) mutable {
                         mutateMaterial([&](auto& material) { material.viscosity = value; });
                     }),
        integerField("Thermal conductivity", "material-conductivity", selected.thermalConductivity,
                     [mutateMaterial](std::uint8_t value) mutable {
                         mutateMaterial([&](auto& material) { material.thermalConductivity = value; });
                     }),
        integerField("Heat capacity", "material-capacity", selected.heatCapacity,
                     [mutateMaterial](std::uint16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.heatCapacity = value; });
                     }),
        integerField("Ignition temperature", "material-ignition", selected.ignitionTemperature,
                     [mutateMaterial](std::int16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.ignitionTemperature = value; });
                     }),
        integerField("Melting temperature", "material-melting", selected.meltingTemperature,
                     [mutateMaterial](std::int16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.meltingTemperature = value; });
                     }),
        integerField("Boiling temperature", "material-boiling", selected.boilingTemperature,
                     [mutateMaterial](std::int16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.boilingTemperature = value; });
                     }),
        integerField("Default temperature", "material-default-temperature", selected.defaultTemperature,
                     [mutateMaterial](std::int16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.defaultTemperature = value; });
                     }),
        integerField("Default lifetime", "material-lifetime", selected.defaultLifetime,
                     [mutateMaterial](std::uint16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.defaultLifetime = value; });
                     }),
        integerField("Blast resistance", "material-blast", selected.blastResistance,
                     [mutateMaterial](std::uint16_t value) mutable {
                         mutateMaterial([&](auto& material) { material.blastResistance = value; });
                     })};

    std::vector<std::string> materialNames;
    for (const auto& material : draft_.definitions()) materialNames.push_back(material.name);
    std::vector<std::string> reactionNames;
    for (const auto& reaction : draft_.reactions()) reactionNames.push_back(reaction.id);
    std::vector<WidgetDesc> reactionEditor{sectionHeader("Binary reactions", "reaction-heading")};
    if (!reactionNames.empty()) {
        const auto reactionIndex = selectedReaction_.value_or(0);
        const MaterialReactionRule selectedRule = draft_.reactions()[reactionIndex];
        auto mutateReaction = [this, reactionIndex](auto mutation) {
            MaterialReactionRule value = draft_.reactions()[reactionIndex];
            mutation(value);
            observe(replaceReaction(reactionIndex, std::move(value)));
        };
        reactionEditor.push_back(combo("Rule", reactionNames, int(reactionIndex), "reaction-select",
                                       [this](float value) {
                                           observe(selectReaction(std::size_t(std::max(0, int(value)))));
                                       }));
        reactionEditor.push_back(inputText("ID", selectedRule.id, "reaction-id",
                                           [mutateReaction](const std::string& value) mutable {
                                               mutateReaction([&](auto& rule) { rule.id = value; });
                                           }));
        auto materialCombo = [&](std::string label, std::string id, MaterialId value, auto assign) {
            return combo(std::move(label), materialNames, int(value), std::move(id),
                         [mutateReaction, assign](float index) mutable {
                             mutateReaction([&](auto& rule) {
                                 assign(rule, static_cast<MaterialId>(std::max(0, int(index))));
                             });
                         });
        };
        reactionEditor.push_back(materialCombo("First", "reaction-first", selectedRule.first,
                                               [](auto& rule, auto value) { rule.first = value; }));
        reactionEditor.push_back(materialCombo("Second", "reaction-second", selectedRule.second,
                                               [](auto& rule, auto value) { rule.second = value; }));
        reactionEditor.push_back(materialCombo("First result", "reaction-first-result",
                                               selectedRule.firstResult,
                                               [](auto& rule, auto value) { rule.firstResult = value; }));
        reactionEditor.push_back(materialCombo("Second result", "reaction-second-result",
                                               selectedRule.secondResult,
                                               [](auto& rule, auto value) { rule.secondResult = value; }));
        reactionEditor.push_back(integerField("Minimum temperature", "reaction-min-temperature",
                                              selectedRule.minimumTemperature,
                                              [mutateReaction](std::int16_t value) mutable {
                                                  mutateReaction([&](auto& rule) {
                                                      rule.minimumTemperature = value;
                                                  });
                                              }));
        reactionEditor.push_back(integerField("Heat delta", "reaction-heat", selectedRule.heatDelta,
                                              [mutateReaction](std::int16_t value) mutable {
                                                  mutateReaction([&](auto& rule) { rule.heatDelta = value; });
                                              }));
        reactionEditor.push_back(integerField("Priority", "reaction-priority", selectedRule.priority,
                                              [mutateReaction](std::int32_t value) mutable {
                                                  mutateReaction([&](auto& rule) { rule.priority = value; });
                                              }));
        reactionEditor.push_back(button("Delete reaction", "reaction-delete", [this, reactionIndex] {
            observe(removeReaction(reactionIndex));
        }));
    }
    reactionEditor.push_back(button("Add reaction", "reaction-add", [this] {
        MaterialReactionRule rule;
        rule.id = "reaction-" + std::to_string(revision_ + 1);
        observe(addReaction(std::move(rule)));
    }));

    std::vector<std::string> phaseNames;
    for (const auto& phase : draft_.phaseRules()) phaseNames.push_back(phase.id);
    std::vector<WidgetDesc> phaseEditor{sectionHeader("Phase transitions", "phase-heading")};
    if (!phaseNames.empty()) {
        const auto phaseIndex = selectedPhase_.value_or(0);
        const MaterialPhaseRule selectedRule = draft_.phaseRules()[phaseIndex];
        auto mutatePhase = [this, phaseIndex](auto mutation) {
            MaterialPhaseRule value = draft_.phaseRules()[phaseIndex];
            mutation(value);
            observe(replacePhase(phaseIndex, std::move(value)));
        };
        phaseEditor.push_back(combo("Rule", phaseNames, int(phaseIndex), "phase-select",
                                    [this](float value) {
                                        observe(selectPhase(std::size_t(std::max(0, int(value)))));
                                    }));
        phaseEditor.push_back(inputText("ID", selectedRule.id, "phase-id",
                                        [mutatePhase](const std::string& value) mutable {
                                            mutatePhase([&](auto& rule) { rule.id = value; });
                                        }));
        phaseEditor.push_back(combo("Source", materialNames, int(selectedRule.source), "phase-source",
                                    [mutatePhase](float value) mutable {
                                        mutatePhase([&](auto& rule) {
                                            rule.source = static_cast<MaterialId>(std::max(0, int(value)));
                                        });
                                    }));
        phaseEditor.push_back(combo("Result", materialNames, int(selectedRule.result), "phase-result",
                                    [mutatePhase](float value) mutable {
                                        mutatePhase([&](auto& rule) {
                                            rule.result = static_cast<MaterialId>(std::max(0, int(value)));
                                        });
                                    }));
        phaseEditor.push_back(combo("Direction", {"At or below", "At or above"},
                                    int(selectedRule.direction), "phase-direction",
                                    [mutatePhase](float value) mutable {
                                        mutatePhase([&](auto& rule) {
                                            rule.direction = value < 0.5f ? TemperatureDirection::AtOrBelow
                                                                         : TemperatureDirection::AtOrAbove;
                                        });
                                    }));
        phaseEditor.push_back(integerField("Threshold", "phase-threshold", selectedRule.threshold,
                                           [mutatePhase](std::int16_t value) mutable {
                                               mutatePhase([&](auto& rule) { rule.threshold = value; });
                                           }));
        phaseEditor.push_back(integerField("Temperature delta", "phase-temperature-delta",
                                           selectedRule.temperatureDelta,
                                           [mutatePhase](std::int16_t value) mutable {
                                               mutatePhase([&](auto& rule) {
                                                   rule.temperatureDelta = value;
                                               });
                                           }));
        phaseEditor.push_back(integerField("Priority", "phase-priority", selectedRule.priority,
                                           [mutatePhase](std::int32_t value) mutable {
                                               mutatePhase([&](auto& rule) { rule.priority = value; });
                                           }));
        phaseEditor.push_back(button("Delete transition", "phase-delete", [this, phaseIndex] {
            observe(removePhase(phaseIndex));
        }));
    }
    phaseEditor.push_back(button("Add transition", "phase-add", [this] {
        MaterialPhaseRule rule;
        rule.id = "phase-" + std::to_string(revision_ + 1);
        observe(addPhase(std::move(rule)));
    }));

    WidgetDesc browser = sidebar({searchField("Filter materials", {}, "material-filter"),
                                  scrollList("materials", std::move(materialRows), 0.0f, 28.0f)},
                                 "catalog-browser", 260.0f);
    WidgetDesc inspector = child(
        "catalog-inspector",
        {collapsingHeader("Material properties", std::move(materialInspector), "material-card", true),
         collapsingHeader("Reactions", std::move(reactionEditor), "reaction-section", false),
         collapsingHeader("Phase transitions", std::move(phaseEditor), "phase-section", false)});
    WidgetDesc content = splitPane(FlexDirection::Row, std::move(browser), std::move(inspector),
                                   0.28f, "catalog-split");
    return window("PixelWorld Material & Reaction Catalog",
                  {toolbar({badge("Validated", "catalog-valid"), spacer(),
                            text("Fingerprint " + std::to_string(draft_.fingerprint()),
                                 "catalog-fingerprint")},
                           "catalog-toolbar"),
                   std::move(content), statusBar({text(statusText_, "catalog-status")},
                                                 "catalog-statusbar")},
                  "pixelworld-catalog-editor");
}

}  // namespace eve::pixelworld_editor
