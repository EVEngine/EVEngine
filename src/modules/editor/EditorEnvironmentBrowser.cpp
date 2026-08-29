#include "editor/EditorEnvironmentBrowser.h"

#include <algorithm>
#include <set>

namespace eve::editor {
namespace {
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
}

EditorResult<EnvironmentAssetCard> EnvironmentAssetBrowser::card(const AssetRecord& record) const {
    EnvironmentAssetCard result;
    result.asset = record.guid; result.logicalUri = record.logicalUri; result.status = record.status;
    result.diagnostics = record.diagnostics;
    if (!record.artifacts.empty()) result.previewArtifact = record.artifacts.front();
    const auto* layoutEntry = field(record.metadata, "layout");
    const auto* widthEntry = field(record.metadata, "width");
    const auto* heightEntry = field(record.metadata, "height");
    const auto* facesEntry = field(record.metadata, "faceCount");
    const auto* layout = layoutEntry ? layoutEntry->getIf<std::string>() : nullptr;
    const auto* width = widthEntry ? widthEntry->getIf<int64_t>() : nullptr;
    const auto* height = heightEntry ? heightEntry->getIf<int64_t>() : nullptr;
    const auto* faces = facesEntry ? facesEntry->getIf<int64_t>() : nullptr;
    static const std::set<std::string> layouts{"cubemap", "horizontal-cross", "vertical-cross", "equirectangular"};
    if (!layout || !layouts.contains(*layout) || !width || !height || *width <= 0 || *height <= 0) {
        result.diagnostics.push_back({RuleId("editor.environment.invalid-map-metadata"),
            DiagnosticSeverity::Error, "Environment map requires supported layout and positive dimensions"});
    } else {
        result.layout = *layout; result.width = static_cast<int>(*width); result.height = static_cast<int>(*height);
        result.faceCount = faces ? static_cast<int>(*faces) : (*layout == "cubemap" ? 6 : 1);
        if (*layout == "cubemap" && result.faceCount != 6)
            result.diagnostics.push_back({RuleId("editor.environment.cubemap-face-count"),
                DiagnosticSeverity::Error, "Cubemap assets require exactly six faces"});
        if (*layout == "equirectangular" && *width != *height * 2)
            result.diagnostics.push_back({RuleId("editor.environment.equirectangular-aspect"),
                DiagnosticSeverity::Warning, "Equirectangular maps normally use a 2:1 aspect ratio"});
    }
    if (record.artifacts.empty()) result.diagnostics.push_back({RuleId("editor.environment.missing-artifact"),
        DiagnosticSeverity::Error, "Environment asset has no imported artifact"});
    if (record.status != AssetStatus::Ready && record.status != AssetStatus::Warning)
        result.diagnostics.push_back({RuleId("editor.environment.asset-not-ready"),
            DiagnosticSeverity::Error, "Environment asset is not ready for preview or assignment"});
    const bool error = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [](const EditorDiagnostic& diagnostic) { return diagnostic.severity == DiagnosticSeverity::Error; });
    if (error) {
        EditorResult<EnvironmentAssetCard> failed; failed.status = EditorStatus::Rejected;
        failed.value = std::move(result); failed.diagnostics = failed.value->diagnostics; return failed;
    }
    return EditorResult<EnvironmentAssetCard>::applied(std::move(result));
}

EditorResult<EnvironmentAssetPage> EnvironmentAssetBrowser::query(
    std::string text, std::size_t offset, std::size_t limit,
    std::optional<std::uint64_t> generation) const {
    if (!database_ || limit == 0)
        return EditorResult<EnvironmentAssetPage>::error(EditorStatus::Rejected,
            RuleId("editor.environment.invalid-browser-query"), "Environment browser requires AssetDB and positive page size");
    AssetQuery filter; filter.typeIds = {"cubemap", "image.cubemap", "environment-map"};
    filter.text = std::move(text);
    auto page = database_->query(filter, offset, limit, generation);
    if (!page.value) {
        EditorResult<EnvironmentAssetPage> failed; failed.status = page.status;
        failed.diagnostics = std::move(page.diagnostics); return failed;
    }
    EnvironmentAssetPage result; result.nextOffset = page.value->nextOffset;
    result.hasMore = page.value->hasMore; result.generation = page.value->generation;
    for (const auto& record : page.value->values) {
        auto value = card(record);
        if (value.value) result.values.push_back(std::move(*value.value));
    }
    return EditorResult<EnvironmentAssetPage>::applied(std::move(result));
}

EditorResult<EnvironmentAssetCard> EnvironmentAssetBrowser::select(const AssetGuid& asset) const {
    if (!database_ || asset.empty()) return EditorResult<EnvironmentAssetCard>::error(EditorStatus::Rejected,
        RuleId("editor.environment.invalid-selection"), "Environment selection requires AssetDB and asset id");
    auto found = database_->find(asset);
    if (!found.value) {
        EditorResult<EnvironmentAssetCard> failed; failed.status = found.status;
        failed.diagnostics = std::move(found.diagnostics); return failed;
    }
    return card(*found.value);
}

}  // namespace eve::editor
