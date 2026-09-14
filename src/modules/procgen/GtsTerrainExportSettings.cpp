#include "procgen/GtsTerrainExportSettings.h"

#include "common/Value.h"
#include "image/ImageData.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace eve::procgen {
namespace {
template <class T> Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, message, "procgen.gtsTerrainExportSettings"));
}
bool enumRange(int value, int last) { return value >= 0 && value <= last; }
bool textureResolutionValid(int value) {
    for (int candidate = 32; candidate <= 8192; candidate *= 2)
        if (value == candidate) return true;
    return false;
}
Result<void> validateList(const std::vector<GtsTerrainExportLodSettings>& levels) {
    if (levels.size() > 32) return invalid<void>("GTS terrain export supports at most 32 LOD records");
    float previous = 1.0f;
    for (const auto& level : levels) {
        auto valid = level.validate();
        if (!valid) return valid;
        if (level.screenRelativeTransitionHeight >= previous)
            return invalid<void>("GTS terrain export transition heights must descend");
        previous = level.screenRelativeTransitionHeight;
    }
    return Result<void>::success();
}
Value encodeOptions(const GtsMeshSimplificationOptions& value) {
    return Value::Object{{"aggressiveness", value.aggressiveness},
                         {"enableSmartLink", value.enableSmartLink},
                         {"maxIterationCount", value.maxIterationCount},
                         {"preserveBorderEdges", value.preserveBorderEdges},
                         {"preserveSurfaceCurvature", value.preserveSurfaceCurvature},
                         {"preserveUvFoldoverEdges", value.preserveUvFoldoverEdges},
                         {"preserveUvSeamEdges", value.preserveUvSeamEdges},
                         {"vertexLinkDistance", value.vertexLinkDistance}};
}
Value encodeLod(const GtsTerrainExportLodSettings& value) {
    return Value::Object{{"alphaChannel", int(value.alphaChannel)},
                         {"bakeLayerMask", int64_t(value.bakeLayerMask)},
                         {"bakeLighting", int(value.bakeLighting)},
                         {"bakeVertexColors", value.bakeVertexColors},
                         {"captureBaseMapTextures", value.captureBaseMapTextures},
                         {"createMaterials", value.createMaterials},
                         {"exportNormalMaps", value.exportNormalMaps},
                         {"exportSplatmaps", value.exportSplatmaps},
                         {"exportTextures", value.exportTextures},
                         {"materialShader", int(value.materialShader)},
                         {"mode", int(value.mode)},
                         {"namePrefix", value.namePrefix},
                         {"normalEdgeMode", int(value.normalEdgeMode)},
                         {"saveResolution", int(value.saveResolution)},
                         {"screenRelativeTransitionHeight", value.screenRelativeTransitionHeight},
                         {"simplification", encodeOptions(value.simplification)},
                         {"simplifyQuality", value.simplifyQuality},
                         {"textureExportMethod", int(value.textureExportMethod)},
                         {"textureExportResolution", value.textureExportResolution},
                         {"vertexColorSmoothing", value.vertexColorSmoothing}};
}
Value encodeWorkflow(const GtsTerrainExportWorkflow& value) {
    return Value::Object{{"action",int(value.action)},{"addMeshColliderToImpostor",value.addMeshColliderToImpostor},
        {"addObjectColliders",value.addObjectColliders},{"addTerrainCollider",value.addTerrainCollider},
        {"addTreeColliders",value.addTreeColliders},{"bakeCombinedCollisionMesh",value.bakeCombinedCollisionMesh},
        {"colliderResolution",int(value.colliderResolution)},{"colliderSimplifyQuality",value.colliderSimplifyQuality},
        {"colliderType",int(value.colliderType)},{"convertSourceTerrains",value.convertSourceTerrains},
        {"convertTreesToObjects",value.convertTreesToObjects},{"copyPcgObjects",value.copyPcgObjects},
        {"copyPcgObjectsToImpostor",value.copyPcgObjectsToImpostor},{"createColliderScenes",value.createColliderScenes},
        {"createImpostorScenes",value.createImpostorScenes},{"impostorRange",value.impostorRange},
        {"invertExportMask",value.invertExportMask},{"objFaceMode",int(value.objFaceMode)},{"selection",int(value.selection)},
        {"sourceTreatment",int(value.sourceTreatment)}};
}
bool exact(const Value::Object& object, std::initializer_list<const char*> names) {
    if (object.size() != names.size()) return false;
    for (auto* name : names)
        if (!object.contains(name)) return false;
    return true;
}
const double* number(const Value& value) { return value.getIf<double>(); }
bool decodeOptions(const Value& encoded, GtsMeshSimplificationOptions& output) {
    auto* object = encoded.getIf<Value::Object>();
    if (!object || !exact(*object, {"aggressiveness", "enableSmartLink", "maxIterationCount",
                                    "preserveBorderEdges", "preserveSurfaceCurvature",
                                    "preserveUvFoldoverEdges", "preserveUvSeamEdges", "vertexLinkDistance"}))
        return false;
    auto* aggressiveness = number(object->at("aggressiveness"));
    auto* distance = number(object->at("vertexLinkDistance"));
    auto* iterations = object->at("maxIterationCount").getIf<int64_t>();
    auto* smart = object->at("enableSmartLink").getIf<bool>();
    auto* border = object->at("preserveBorderEdges").getIf<bool>();
    auto* curvature = object->at("preserveSurfaceCurvature").getIf<bool>();
    auto* foldover = object->at("preserveUvFoldoverEdges").getIf<bool>();
    auto* seam = object->at("preserveUvSeamEdges").getIf<bool>();
    if (!aggressiveness || !distance || !iterations || !smart || !border || !curvature || !foldover || !seam)
        return false;
    output.aggressiveness = *aggressiveness;
    output.vertexLinkDistance = *distance;
    output.maxIterationCount = int(*iterations);
    output.enableSmartLink = *smart;
    output.preserveBorderEdges = *border;
    output.preserveSurfaceCurvature = *curvature;
    output.preserveUvFoldoverEdges = *foldover;
    output.preserveUvSeamEdges = *seam;
    return true;
}
bool decodeLod(const Value& encoded, GtsTerrainExportLodSettings& output) {
    auto* object = encoded.getIf<Value::Object>();
    if (!object || !exact(*object, {"alphaChannel", "bakeLayerMask", "bakeLighting", "bakeVertexColors",
                                    "captureBaseMapTextures", "createMaterials", "exportNormalMaps",
                                    "exportSplatmaps", "exportTextures", "materialShader", "mode", "namePrefix",
                                    "normalEdgeMode", "saveResolution", "screenRelativeTransitionHeight",
                                    "simplification", "simplifyQuality", "textureExportMethod",
                                    "textureExportResolution", "vertexColorSmoothing"}))
        return false;
    auto integer = [&](const char* name) { return object->at(name).getIf<int64_t>(); };
    auto boolean = [&](const char* name) { return object->at(name).getIf<bool>(); };
    auto* alpha = integer("alphaChannel"); auto* mask = integer("bakeLayerMask");
    auto* lighting = integer("bakeLighting"); auto* shader = integer("materialShader");
    auto* mode = integer("mode"); auto* edge = integer("normalEdgeMode");
    auto* resolution = integer("saveResolution"); auto* textureMethod = integer("textureExportMethod");
    auto* textureResolution = integer("textureExportResolution"); auto* smoothing = integer("vertexColorSmoothing");
    auto* transition = number(object->at("screenRelativeTransitionHeight"));
    auto* quality = number(object->at("simplifyQuality")); auto* prefix = object->at("namePrefix").getIf<std::string>();
    auto* vertexColors = boolean("bakeVertexColors"); auto* capture = boolean("captureBaseMapTextures");
    auto* materials = boolean("createMaterials"); auto* normals = boolean("exportNormalMaps");
    auto* splats = boolean("exportSplatmaps"); auto* textures = boolean("exportTextures");
    if (!alpha || *alpha<0 || *alpha>1 || !mask || *mask < 0 || *mask > std::numeric_limits<std::uint32_t>::max() ||
        !lighting || *lighting<0 || *lighting>1 || !shader || *shader<0 || *shader>1 ||
        !mode || *mode<0 || *mode>2 || !edge || *edge<0 || *edge>1 || !resolution || *resolution<0 || *resolution>4 ||
        !textureMethod || *textureMethod<0 || *textureMethod>1 || !textureResolution || *textureResolution<32 ||
        *textureResolution>8192 || !smoothing || *smoothing<0 || *smoothing>128 || !transition ||
        !quality || !prefix || prefix->size() > 256 || !vertexColors || !capture || !materials || !normals ||
        !splats || !textures || !decodeOptions(object->at("simplification"), output.simplification)) return false;
    output.alphaChannel = static_cast<GtsTerrainAlphaChannel>(*alpha);
    output.bakeLayerMask = std::uint32_t(*mask); output.bakeLighting = static_cast<GtsTerrainBakeLighting>(*lighting);
    output.materialShader = static_cast<GtsTerrainExportShader>(*shader); output.mode = static_cast<GtsTerrainLodMode>(*mode);
    output.normalEdgeMode = static_cast<GtsTerrainNormalEdgeMode>(*edge);
    output.saveResolution = static_cast<GtsTerrainSaveResolution>(*resolution);
    output.textureExportMethod = static_cast<GtsTerrainTextureExportMethod>(*textureMethod);
    output.textureExportResolution = int(*textureResolution); output.vertexColorSmoothing = int(*smoothing);
    output.screenRelativeTransitionHeight = float(*transition); output.simplifyQuality = float(*quality);
    output.namePrefix = *prefix; output.bakeVertexColors = *vertexColors; output.captureBaseMapTextures = *capture;
    output.createMaterials = *materials; output.exportNormalMaps = *normals; output.exportSplatmaps = *splats;
    output.exportTextures = *textures;
    return output.validate().ok();
}
bool decodeWorkflow(const Value& encoded,GtsTerrainExportWorkflow& output,bool legacyV2=false) {
    auto* object=encoded.getIf<Value::Object>();
    if(!object||!exact(*object,{"action","addMeshColliderToImpostor","addObjectColliders","addTerrainCollider",
        "addTreeColliders","bakeCombinedCollisionMesh","colliderResolution","colliderSimplifyQuality","colliderType",
        "convertSourceTerrains","convertTreesToObjects","copyPcgObjects","copyPcgObjectsToImpostor",
        "createColliderScenes","createImpostorScenes","impostorRange","invertExportMask","objFaceMode","selection","sourceTreatment"})&&
       !(legacyV2&&exact(*object,{"action","addMeshColliderToImpostor","addObjectColliders","addTerrainCollider",
        "addTreeColliders","bakeCombinedCollisionMesh","colliderResolution","colliderSimplifyQuality","colliderType",
        "convertSourceTerrains","convertTreesToObjects","copyPcgObjects","copyPcgObjectsToImpostor",
        "createColliderScenes","createImpostorScenes","impostorRange","invertExportMask","selection","sourceTreatment"})))return false;
    auto integer=[&](const char* name){return object->at(name).getIf<int64_t>();};
    auto boolean=[&](const char* name){return object->at(name).getIf<bool>();};
    auto* action=integer("action"),*resolution=integer("colliderResolution"),*type=integer("colliderType"),
        *selection=integer("selection"),*treatment=integer("sourceTreatment");
    const int64_t defaultFace=0;auto* face=legacyV2?&defaultFace:integer("objFaceMode");
    auto* quality=number(object->at("colliderSimplifyQuality"));auto* range=number(object->at("impostorRange"));
    auto* addImpostor=boolean("addMeshColliderToImpostor"),*addObjects=boolean("addObjectColliders"),
        *addTerrain=boolean("addTerrainCollider"),*addTrees=boolean("addTreeColliders"),
        *combined=boolean("bakeCombinedCollisionMesh"),*convert=boolean("convertSourceTerrains"),
        *convertTrees=boolean("convertTreesToObjects"),*copy=boolean("copyPcgObjects"),
        *copyImpostor=boolean("copyPcgObjectsToImpostor"),*colliderScenes=boolean("createColliderScenes"),
        *impostorScenes=boolean("createImpostorScenes"),*invert=boolean("invertExportMask");
    if(!action||*action<0||*action>2||!resolution||*resolution<0||*resolution>4||!type||*type<0||*type>1||
       !selection||*selection<0||*selection>1||!treatment||*treatment<0||*treatment>3||!face||*face<0||*face>1||!quality||!range||
       !std::isfinite(*quality)||*quality<0||*quality>1||!std::isfinite(*range)||*range<0||
       !addImpostor||!addObjects||!addTerrain||!addTrees||!combined||!convert||!convertTrees||!copy||!copyImpostor||
       !colliderScenes||!impostorScenes||!invert)return false;
    output.action=static_cast<GtsTerrainConversionAction>(*action);output.colliderResolution=static_cast<GtsTerrainSaveResolution>(*resolution);
    output.colliderType=static_cast<GtsTerrainColliderType>(*type);output.selection=static_cast<GtsTerrainExportSelection>(*selection);
    output.objFaceMode=static_cast<GtsTerrainObjFaceMode>(*face);
    output.sourceTreatment=static_cast<GtsSourceTerrainTreatment>(*treatment);output.colliderSimplifyQuality=float(*quality);output.impostorRange=*range;
    output.addMeshColliderToImpostor=*addImpostor;output.addObjectColliders=*addObjects;output.addTerrainCollider=*addTerrain;
    output.addTreeColliders=*addTrees;output.bakeCombinedCollisionMesh=*combined;output.convertSourceTerrains=*convert;
    output.convertTreesToObjects=*convertTrees;output.copyPcgObjects=*copy;output.copyPcgObjectsToImpostor=*copyImpostor;
    output.createColliderScenes=*colliderScenes;output.createImpostorScenes=*impostorScenes;output.invertExportMask=*invert;
    return true;
}
}

Result<void> GtsTerrainExportLodSettings::validate() const {
    if (!enumRange(int(saveResolution), 4) || !enumRange(int(normalEdgeMode), 1) || !enumRange(int(mode), 2) ||
        !enumRange(int(materialShader), 1) || !enumRange(int(textureExportMethod), 1) ||
        !enumRange(int(alphaChannel), 1) || !enumRange(int(bakeLighting), 1))
        return invalid<void>("GTS terrain export enum value is invalid");
    if (!std::isfinite(simplifyQuality) || simplifyQuality < 0.0f || simplifyQuality > 1.0f ||
        !std::isfinite(screenRelativeTransitionHeight) || screenRelativeTransitionHeight < 0.0f ||
        screenRelativeTransitionHeight >= 1.0f || vertexColorSmoothing < 0 || vertexColorSmoothing > 128 ||
        !textureResolutionValid(textureExportResolution) || namePrefix.size() > 256 ||
        !std::isfinite(simplification.vertexLinkDistance) || simplification.vertexLinkDistance < 0.0 ||
        !std::isfinite(simplification.aggressiveness) || simplification.aggressiveness <= 0.0 ||
        simplification.maxIterationCount <= 0 || simplification.maxIterationCount > 100000)
        return invalid<void>("GTS terrain export LOD value is outside its supported range");
    return Result<void>::success();
}
Result<GtsTerrainLodLevelSettings> GtsTerrainExportLodSettings::compileLevel() const {
    auto valid = validate(); if (!valid) return Result<GtsTerrainLodLevelSettings>::failure(valid.status());
    return Result<GtsTerrainLodLevelSettings>::success(
        {simplifyQuality, screenRelativeTransitionHeight, simplification});
}
Result<GtsTerrainExportLodSettings> makeGtsTerrainImpostorLod(int level) {
    if (level < 0) return invalid<GtsTerrainExportLodSettings>("GTS impostor LOD index must be non-negative");
    GtsTerrainExportLodSettings value; value.namePrefix = "LOD" + std::to_string(level) + "_";
    value.mode = GtsTerrainLodMode::Impostor; value.normalEdgeMode = GtsTerrainNormalEdgeMode::Smooth;
    value.textureExportMethod = GtsTerrainTextureExportMethod::OrthographicBake;
    if (level == 0) { value.saveResolution = GtsTerrainSaveResolution::Half; value.textureExportResolution = 2048; }
    else if (level == 1) { value.saveResolution = GtsTerrainSaveResolution::Quarter; value.textureExportResolution = 1024; }
    else if (level == 2) { value.saveResolution = GtsTerrainSaveResolution::Eighth; value.textureExportResolution = 512; }
    else { value.saveResolution = GtsTerrainSaveResolution::Sixteenth; value.textureExportResolution = 256; }
    value.bakeVertexColors = false; value.alphaChannel = GtsTerrainAlphaChannel::None;
    value.exportSplatmaps = false; value.materialShader = GtsTerrainExportShader::Standard;
    return Result<GtsTerrainExportLodSettings>::success(std::move(value));
}
Result<GtsTerrainExportLodSettings> makeGtsTerrainLowPolyLod(int level) {
    if (level < 0) return invalid<GtsTerrainExportLodSettings>("GTS low-poly LOD index must be non-negative");
    GtsTerrainExportLodSettings value; value.namePrefix = "LOD" + std::to_string(level) + "_";
    value.mode = GtsTerrainLodMode::LowPoly; value.saveResolution = level == 0 ? GtsTerrainSaveResolution::Eighth : GtsTerrainSaveResolution::Sixteenth;
    value.normalEdgeMode = GtsTerrainNormalEdgeMode::Sharp;
    value.textureExportMethod = GtsTerrainTextureExportMethod::BaseMapExport;
    value.bakeVertexColors = true; value.vertexColorSmoothing = 3; value.alphaChannel = GtsTerrainAlphaChannel::None;
    value.exportNormalMaps = false; value.exportSplatmaps = false; value.materialShader = GtsTerrainExportShader::VertexColor;
    return Result<GtsTerrainExportLodSettings>::success(std::move(value));
}
namespace {
Result<GtsTerrainExportLodSettings> preset(int mode,int level,float quality,float transition) {
    Result<GtsTerrainExportLodSettings> made = mode == int(GtsTerrainLodMode::Impostor)
        ? makeGtsTerrainImpostorLod(level)
        : mode == int(GtsTerrainLodMode::LowPoly) ? makeGtsTerrainLowPolyLod(level)
        : invalid<GtsTerrainExportLodSettings>("GTS terrain preset mode must be Impostor or LowPoly");
    if (!made) return made;
    made.value().simplifyQuality=quality;made.value().screenRelativeTransitionHeight=transition;
    auto valid=made.value().validate();if(!valid)return Result<GtsTerrainExportLodSettings>::failure(valid.status());
    return made;
}
}
Result<void> GtsTerrainExportSettings::appendSourcePreset(int mode,int level,float quality,float transitionHeight) {
    auto made=preset(mode,level,quality,transitionHeight);if(!made)return Result<void>::failure(made.status());
    auto candidate=sourceLods_;candidate.push_back(std::move(made).takeValue());return setSourceLods(std::move(candidate));
}
Result<void> GtsTerrainExportSettings::appendImpostorPreset(int mode,int level,float quality,float transitionHeight) {
    auto made=preset(mode,level,quality,transitionHeight);if(!made)return Result<void>::failure(made.status());
    auto candidate=impostorLods_;candidate.push_back(std::move(made).takeValue());return setImpostorLods(std::move(candidate));
}
Result<void> GtsTerrainExportSettings::setSourceLods(std::vector<GtsTerrainExportLodSettings> levels) {
    auto valid = validateList(levels); if (!valid) return valid; sourceLods_ = std::move(levels); return Result<void>::success();
}
Result<void> GtsTerrainExportSettings::configureWorkflow(const GtsTerrainExportWorkflow& workflow) {
    if(!enumRange(int(workflow.action),2)||!enumRange(int(workflow.sourceTreatment),3)||
       !enumRange(int(workflow.selection),1)||!enumRange(int(workflow.colliderType),1)||
       !enumRange(int(workflow.objFaceMode),1)||
       !enumRange(int(workflow.colliderResolution),4)||!std::isfinite(workflow.colliderSimplifyQuality)||
       workflow.colliderSimplifyQuality<0.f||workflow.colliderSimplifyQuality>1.f||
       !std::isfinite(workflow.impostorRange)||workflow.impostorRange<0.0)
        return invalid<void>("GTS terrain export workflow value is invalid");
    workflow_=workflow;return Result<void>::success();
}
GtsTerrainExportWorkflow GtsTerrainExportSettings::getWorkflow()const noexcept{return workflow_;}
Result<void> GtsTerrainExportSettings::setImpostorLods(std::vector<GtsTerrainExportLodSettings> levels) {
    auto valid = validateList(levels); if (!valid) return valid; impostorLods_ = std::move(levels); return Result<void>::success();
}
int GtsTerrainExportSettings::getSourceLodCount() const noexcept { return int(sourceLods_.size()); }
int GtsTerrainExportSettings::getImpostorLodCount() const noexcept { return int(impostorLods_.size()); }
const GtsTerrainExportLodSettings* GtsTerrainExportSettings::sourceLodAt(int index) const noexcept {
    return index >= 0 && index < int(sourceLods_.size()) ? &sourceLods_[std::size_t(index)] : nullptr;
}
const GtsTerrainExportLodSettings* GtsTerrainExportSettings::impostorLodAt(int index) const noexcept {
    return index >= 0 && index < int(impostorLods_.size()) ? &impostorLods_[std::size_t(index)] : nullptr;
}
Result<std::vector<GtsTerrainLodLevelSettings>> GtsTerrainExportSettings::compileSourceLevels() const {
    auto valid = validateList(sourceLods_); if (!valid) return Result<std::vector<GtsTerrainLodLevelSettings>>::failure(valid.status());
    if (sourceLods_.empty()) return invalid<std::vector<GtsTerrainLodLevelSettings>>("GTS source export requires at least one LOD");
    std::vector<GtsTerrainLodLevelSettings> output; output.reserve(sourceLods_.size());
    for (const auto& source : sourceLods_) { auto level = source.compileLevel(); if (!level) return Result<std::vector<GtsTerrainLodLevelSettings>>::failure(level.status()); output.push_back(std::move(level).takeValue()); }
    return Result<std::vector<GtsTerrainLodLevelSettings>>::success(std::move(output));
}
Result<std::string> GtsTerrainExportSettings::snapshotJson() const {
    auto sourceValid = validateList(sourceLods_); if (!sourceValid) return Result<std::string>::failure(sourceValid.status());
    auto impostorValid = validateList(impostorLods_); if (!impostorValid) return Result<std::string>::failure(impostorValid.status());
    Value::Array source, impostor; for (const auto& level : sourceLods_) source.emplace_back(encodeLod(level));
    for (const auto& level : impostorLods_) impostor.emplace_back(encodeLod(level));
    return Value(Value::Object{{"impostorLods", std::move(impostor)}, {"schema", "eve.procgen.gts-terrain-export-settings"},
                               {"sourceLods", std::move(source)}, {"version", 3},{"workflow",encodeWorkflow(workflow_)}}).toJson();
}
Result<void> GtsTerrainExportSettings::restoreJson(const std::string& json) {
    if (json.size() > 1024U * 1024U) return invalid<void>("GTS terrain export settings JSON exceeds size limit");
    auto parsed = Value::fromJson(json); if (!parsed) return Result<void>::failure(parsed.status());
    auto* root = parsed.value().getIf<Value::Object>();
    if (!root||!root->contains("schema")||!root->contains("version")||!root->contains("sourceLods")||!root->contains("impostorLods"))
        return invalid<void>("GTS terrain export settings fields are invalid");
    auto* schema = root->at("schema").getIf<std::string>(); auto* version = root->at("version").getIf<int64_t>();
    if(!version||(*version<1||*version>3)||(*version==1&&!exact(*root,{"impostorLods","schema","sourceLods","version"}))||
       (*version>=2&&!exact(*root,{"impostorLods","schema","sourceLods","version","workflow"})))
        return invalid<void>("GTS terrain export settings fields or version are invalid");
    auto* source = root->at("sourceLods").getIf<Value::Array>(); auto* impostor = root->at("impostorLods").getIf<Value::Array>();
    if (!schema || *schema != "eve.procgen.gts-terrain-export-settings" || !source || !impostor || source->size() > 32 || impostor->size() > 32)
        return invalid<void>("GTS terrain export settings schema or counts are invalid");
    GtsTerrainExportWorkflow decodedWorkflow;if(*version>=2&&!decodeWorkflow(root->at("workflow"),decodedWorkflow,*version==2))
        return invalid<void>("GTS terrain export workflow is invalid");
    std::vector<GtsTerrainExportLodSettings> decodedSource, decodedImpostor;
    for (const auto& encoded : *source) { GtsTerrainExportLodSettings value; if (!decodeLod(encoded, value)) return invalid<void>("GTS source export LOD is invalid"); decodedSource.push_back(std::move(value)); }
    for (const auto& encoded : *impostor) { GtsTerrainExportLodSettings value; if (!decodeLod(encoded, value)) return invalid<void>("GTS impostor export LOD is invalid"); decodedImpostor.push_back(std::move(value)); }
    auto sourceValid = validateList(decodedSource); if (!sourceValid) return sourceValid;
    auto impostorValid = validateList(decodedImpostor); if (!impostorValid) return impostorValid;
    auto workflowValid=configureWorkflow(decodedWorkflow);if(!workflowValid)return workflowValid;
    sourceLods_ = std::move(decodedSource); impostorLods_ = std::move(decodedImpostor);
    return Result<void>::success();
}


Result<void> buildGtsTerrainExportLodsFromHeightmapInto(GtsTerrainLodSet& output,const Heightmap& heightmap,
    const GtsTerrainExportSettings& settings,float sizeX,float sizeY,float sizeZ,int subTiles,GtsMeshPivot pivot) {
    auto levels=settings.compileSourceLevels();if(!levels)return Result<void>::failure(levels.status());
    const auto* first=settings.sourceLodAt(0);if(!first)return invalid<void>("GTS source export requires at least one LOD");
    MeshBuild base;auto built=buildGtsTerrainBaseMesh(base,heightmap,first->saveResolution,sizeX,sizeY,sizeZ);
    if(!built)return built;
    auto lods=buildGtsTerrainLods(base,subTiles,subTiles,pivot,levels.value());
    if(!lods)return Result<void>::failure(lods.status());output=std::move(lods).takeValue();return Result<void>::success();
}
Result<void> buildGtsTerrainColliderMeshFromHeightmapInto(MeshBuild& output,const Heightmap& heightmap,
    const GtsTerrainExportWorkflow& workflow,float sizeX,float sizeY,float sizeZ) {
    GtsTerrainExportSettings validator;auto valid=validator.configureWorkflow(workflow);if(!valid)return valid;
    if(!workflow.addTerrainCollider||workflow.colliderType!=GtsTerrainColliderType::Mesh)
        return invalid<void>("GTS workflow does not request a terrain MeshCollider");
    MeshBuild base;auto built=buildGtsTerrainBaseMesh(base,heightmap,workflow.colliderResolution,sizeX,sizeY,sizeZ);
    if(!built)return built;
    MeshBuild simplified;auto reduced=simplifyGtsMesh(simplified,base,workflow.colliderSimplifyQuality);
    if(!reduced)return Result<void>::failure(reduced.status());output=std::move(simplified);return Result<void>::success();
}

Result<std::string> encodeGtsTerrainObj(const Heightmap& heightmap,GtsTerrainSaveResolution resolution,
    float sizeX,float sizeY,float sizeZ,GtsTerrainObjFaceMode faceMode) {
    if(!enumRange(int(faceMode),1))return invalid<std::string>("GTS terrain OBJ face mode is invalid");
    MeshBuild validated;auto built=buildGtsTerrainBaseMesh(validated,heightmap,resolution,sizeX,sizeY,sizeZ);
    if(!built)return Result<std::string>::failure(built.status());
    const int stride=1<<int(resolution),width=(heightmap.getWidth()-1)/stride+1,height=(heightmap.getHeight()-1)/stride+1;
    std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::setprecision(std::numeric_limits<float>::max_digits10);
    stream<<"# Unity terrain OBJ File\n";
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const float u=float(x*stride)/float(heightmap.getWidth()-1),v=float(y*stride)/float(heightmap.getHeight()-1);
        stream<<"v "<<(-v*sizeZ)<<' '<<(heightmap.height(x*stride,y*stride)*sizeY)<<' '<<(u*sizeX)<<'\n';}
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const float u=float(x*stride)/float(heightmap.getWidth()-1),v=float(y*stride)/float(heightmap.getHeight()-1);stream<<"vt "<<v<<' '<<u<<'\n';}
    for(int y=0;y<height-1;++y)for(int x=0;x<width-1;++x){const int a=y*width+x+1,b=(y+1)*width+x+1,c=(y+1)*width+x+2,d=y*width+x+2;
        if(faceMode==GtsTerrainObjFaceMode::Triangles)stream<<"f "<<a<<'/'<<a<<' '<<b<<'/'<<b<<' '<<d<<'/'<<d<<"\nf "<<b<<'/'<<b<<' '<<c<<'/'<<c<<' '<<d<<'/'<<d<<'\n';
        else stream<<"f "<<a<<'/'<<a<<' '<<b<<'/'<<b<<' '<<c<<'/'<<c<<' '<<d<<'/'<<d<<'\n';}
    auto text=stream.str();if(text.size()>512U*1024U*1024U)return invalid<std::string>("GTS terrain OBJ exceeds size limit");
    return Result<std::string>::success(std::move(text));
}

Result<std::string> encodeGtsMaskedTerrainObj(const Heightmap& heightmap,const Heightmap& maskmap,
    GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,GtsTerrainObjFaceMode faceMode,
    float threshold,bool invert) {
    if(!enumRange(int(faceMode),1)||!std::isfinite(threshold)||threshold<0.f||threshold>1.f)
        return invalid<std::string>("GTS masked terrain OBJ settings are invalid");
    MeshBuild validated;auto built=buildGtsTerrainBaseMesh(validated,heightmap,resolution,sizeX,sizeY,sizeZ);
    if(!built)return Result<std::string>::failure(built.status());
    if(maskmap.getWidth()<1||maskmap.getHeight()<1)return invalid<std::string>("GTS masked terrain OBJ mask is empty");
    const int terrainResolution=heightmap.getWidth(),stride=1<<int(resolution);
    std::vector<int> vertexIndices(std::size_t(terrainResolution)*std::size_t(terrainResolution),-1);
    struct Vertex { float x,y,z,u,v; };
    std::vector<Vertex> vertices;std::vector<int> indices;
    auto sampleNormalized=[](const Heightmap& map,float x,float z){
        return map.sampleBilinear(x*float(map.getWidth()),z*float(map.getHeight()));
    };
    auto vertexIndex=[&](int x,int z,float height){
        auto& index=vertexIndices[std::size_t(z)*std::size_t(terrainResolution)+std::size_t(x)];
        if(index<0){index=int(vertices.size());vertices.push_back({-float(x)*sizeX/float(terrainResolution-1),
            height*sizeY,float(z)*sizeZ/float(terrainResolution-1),float(x)/float(terrainResolution),
            float(z)/float(terrainResolution)});}return index;
    };
    auto addTriangle=[&](int a,int b,int c){indices.push_back(c);indices.push_back(b);indices.push_back(a);};
    auto addQuad=[&](int a,int b,int c,int d){indices.push_back(a);indices.push_back(c);indices.push_back(d);indices.push_back(b);};
    for(int z=0;z<terrainResolution-stride;z+=stride){
        const float z0=float(z)/float(terrainResolution),z1=float(z+stride)/float(terrainResolution);
        for(int x=0;x<terrainResolution-stride;x+=stride){
            const float x0=float(x)/float(terrainResolution),x1=float(x+stride)/float(terrainResolution);
            const bool outside=sampleNormalized(maskmap,x0,z0)<threshold;if(outside!=invert)continue;
            const int v1=vertexIndex(x,z+stride,sampleNormalized(heightmap,z1,x0));
            const int v2=vertexIndex(x+stride,z+stride,sampleNormalized(heightmap,z1,x1));
            const int v3=vertexIndex(x,z,sampleNormalized(heightmap,z0,x0));
            const int v4=vertexIndex(x+stride,z,sampleNormalized(heightmap,z0,x1));
            if(faceMode==GtsTerrainObjFaceMode::Triangles){addTriangle(v3,v1,v2);addTriangle(v3,v2,v4);}
            else addQuad(v1,v2,v3,v4);
        }
    }
    std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::setprecision(std::numeric_limits<float>::max_digits10);
    stream<<"# Unity terrain OBJ File\n";for(const auto& vertex:vertices)stream<<"v "<<vertex.x<<' '<<vertex.y<<' '<<vertex.z<<'\n';
    for(const auto& vertex:vertices)stream<<"vt "<<vertex.u<<' '<<vertex.v<<'\n';
    const int faceSize=faceMode==GtsTerrainObjFaceMode::Triangles?3:4;
    for(std::size_t i=0;i<indices.size();i+=std::size_t(faceSize)){stream<<"f";for(int j=0;j<faceSize;++j){const int index=indices[i+std::size_t(j)]+1;stream<<' '<<index<<'/'<<index;}stream<<'\n';}
    auto text=stream.str();if(text.size()>512U*1024U*1024U)return invalid<std::string>("GTS masked terrain OBJ exceeds size limit");
    return Result<std::string>::success(std::move(text));
}

Result<int> bakeGtsTerrainVertexColorsInto(MeshBuild& output,const MeshBuild& source,
    const image::ImageData& bakedTexture,GtsTerrainNormalEdgeMode edgeMode,int smoothingIterations,
    float terrainSizeX,float terrainSizeZ,bool linearize) {
    if(!enumRange(int(edgeMode),1)||smoothingIterations<0||smoothingIterations>128||source.empty()||
       source.getIndexCount()%3||!std::isfinite(terrainSizeX)||terrainSizeX<=0.f||
       !std::isfinite(terrainSizeZ)||terrainSizeZ<=0.f||bakedTexture.getWidth()<1||bakedTexture.getHeight()<1||
       !bakedTexture.getData()||!bakedTexture.getPixelGetFunction())
        return invalid<int>("GTS terrain vertex-color bake input is invalid");
    const std::size_t pixelSize=bakedTexture.getPixelSize();
    if(!pixelSize||std::size_t(bakedTexture.getWidth())*std::size_t(bakedTexture.getHeight())>
       bakedTexture.getSize()/pixelSize)return invalid<int>("GTS terrain vertex-color texture storage is invalid");
    using Color=image::ImageData::Colorf;const int width=bakedTexture.getWidth(),height=bakedTexture.getHeight();
    std::vector<Color> colors;colors.reserve(std::size_t(width)*std::size_t(height));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)colors.push_back(bakedTexture.getPixel(x,y));
    auto safe=[&](const std::vector<Color>& values,int x,int y){x=std::clamp(x,0,width-1);y=std::clamp(y,0,height-1);return values[std::size_t(y)*std::size_t(width)+std::size_t(x)];};
    for(int iteration=0;iteration<smoothingIterations;++iteration){auto next=colors;for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const auto a=safe(colors,x,y-1),b=safe(colors,x,y+1),c=safe(colors,x-1,y),d=safe(colors,x+1,y);
        next[std::size_t(y)*std::size_t(width)+std::size_t(x)]={(a.r+b.r+c.r+d.r)*.25f,(a.g+b.g+c.g+d.g)*.25f,
            (a.b+b.b+c.b+d.b)*.25f,(a.a+b.a+c.a+d.a)*.25f};}colors=std::move(next);}
    auto linear=[](float value){return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);};
    auto sample=[&](float x,float z){const int tx=std::max(0,int(std::round(x/terrainSizeX*float(width)))-1);
        const int tz=std::max(0,int(std::round(z/terrainSizeZ*float(width)))-1);Color color=safe(colors,tx,tz);
        if(linearize){color.r=linear(color.r);color.g=linear(color.g);color.b=linear(color.b);}return color;};
    MeshBuild candidate;std::vector<float> vertexColors;
    if(edgeMode==GtsTerrainNormalEdgeMode::Smooth){candidate=source;vertexColors.reserve(std::size_t(source.getVertexCount())*4u);
        for(int vertex=0;vertex<source.getVertexCount();++vertex){const auto color=sample(source.getPositionX(vertex),source.getPositionZ(vertex));
            vertexColors.insert(vertexColors.end(),{color.r,color.g,color.b,color.a});}}
    else {candidate.reserve(source.getIndexCount(),source.getIndexCount());vertexColors.reserve(std::size_t(source.getIndexCount())*4u);
        for(int i=0;i<source.getIndexCount();i+=3){const int group=source.getTriangleGroup(i/3);candidate.setActiveGroup(group>=0?source.getGroupName(group):"");
            Color triangle[3];for(int corner=0;corner<3;++corner){const int src=source.getIndex(i+corner);triangle[corner]=sample(source.getPositionX(src),source.getPositionZ(src));
                candidate.addVertex(source.getPositionX(src),source.getPositionY(src),source.getPositionZ(src),0,0,0,source.getUvU(src),source.getUvV(src));}
            const Color average{(triangle[0].r+triangle[1].r+triangle[2].r)/3.f,(triangle[0].g+triangle[1].g+triangle[2].g)/3.f,
                (triangle[0].b+triangle[1].b+triangle[2].b)/3.f,(triangle[0].a+triangle[1].a+triangle[2].a)/3.f};
            for(int corner=0;corner<3;++corner)vertexColors.insert(vertexColors.end(),{average.r,average.g,average.b,average.a});
            const std::uint32_t base=std::uint32_t(i);candidate.addTriangle(base,base+1,base+2);}
        auto& normals=candidate.normals();const auto& positions=candidate.positions();
        for(int i=0;i<candidate.getIndexCount();i+=3){const auto a=candidate.indices()[std::size_t(i)],b=candidate.indices()[std::size_t(i+1)],c=candidate.indices()[std::size_t(i+2)];
            const float abx=positions[b*3]-positions[a*3],aby=positions[b*3+1]-positions[a*3+1],abz=positions[b*3+2]-positions[a*3+2];
            const float acx=positions[c*3]-positions[a*3],acy=positions[c*3+1]-positions[a*3+1],acz=positions[c*3+2]-positions[a*3+2];
            float nx=aby*acz-abz*acy,ny=abz*acx-abx*acz,nz=abx*acy-aby*acx;const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(!(length>0.f)||!std::isfinite(length))return invalid<int>("GTS sharp terrain mesh contains a degenerate triangle");nx/=length;ny/=length;nz/=length;
            for(auto vertex:{a,b,c}){normals[vertex*3]=nx;normals[vertex*3+1]=ny;normals[vertex*3+2]=nz;}}
        for(const auto& [key,value]:source.metadata())candidate.setMeta(key,value);}
    auto applied=candidate.setVertexColors(std::move(vertexColors));if(!applied)return Result<int>::failure(applied.status());
    candidate.setMeta("gts.vertexColors",edgeMode==GtsTerrainNormalEdgeMode::Sharp?"sharp":"smooth");
    output=std::move(candidate);return Result<int>::success(output.getVertexCount());
}
}  // namespace eve::procgen
