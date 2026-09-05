#include "voxel_editor/VoxelCatalogEditor.h"

#include "editor/EditorProtocol.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::voxel_editor {
namespace {

template <class T = void>
voxel_editing::EditorResult<T> editorError(voxel_editing::EditorStatus status, std::string rule, std::string message) {
    return eve::editing::failed<T>(status, voxel_editing::RuleId(std::move(rule)), std::move(message));
}

}  // namespace

VoxelCatalogEditor::VoxelCatalogEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {
    seedProject();
    auto previewed = refreshPreview();
    if (!previewed.ok())
        previewed.ignore("voxel editor keeps an empty projection when the seeded preview is rejected");
}

void VoxelCatalogEditor::seedProject() {
    auto applySeed = [this](voxel_editing::EditorResult<voxel_editing::DomainOperation> operation, const char* why) {
        if (!operation.ok()) {
            operation.ignore(why);
            return;
        }
        auto applied = target_.applyDomainOperation(std::move(operation).takeValue());
        if (!applied.ok()) applied.ignore(why);
    };

    voxel_editing::VoxelModelValue cube;
    cube.id    = voxel_editing::ObjectId("cube");
    cube.name  = "cube";
    cube.sizeX = 8;
    cube.sizeY = 8;
    cube.sizeZ = 8;
    for (int z = 2; z < 6; ++z)
        for (int y = 2; y < 6; ++y)
            for (int x = 2; x < 6; ++x) cube.voxels.push_back({x, y, z});
    cube.sockets[0] = {"wood-edge", voxel_editing::VoxelSocketKind::Symmetric};
    cube.sockets[1] = {"wood-edge", voxel_editing::VoxelSocketKind::Symmetric};

    voxel_editing::VoxelModelValue bed;
    bed.id    = voxel_editing::ObjectId("bed");
    bed.name  = "bed";
    bed.sizeX = 8;
    bed.sizeY = 4;
    bed.sizeZ = 6;
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 8; ++x) {
            bed.voxels.push_back({x, 0, z});
            bed.voxels.push_back({x, 1, z});
        }
    for (int y = 2; y < 4; ++y)
        for (int x = 0; x < 8; ++x) bed.voxels.push_back({x, y, 0});
    bed.sockets[0] = {"bed-rail", voxel_editing::VoxelSocketKind::Male};
    bed.sockets[1] = {"bed-rail", voxel_editing::VoxelSocketKind::Female};

    applySeed(target_.makeCreateModel(cube), "voxel editor seed cube");
    applySeed(target_.makeCreateModel(bed), "voxel editor seed bed");
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"voxel.models", "Models", "left", "list", 100},
        {"voxel.viewport", "Sculpt", "center", "preview", 100},
        {"voxel.tools", "Tools", "right", "inspector", 100},
        {"voxel.inspector", "Model", "bottom", "inspector", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "voxel.sculpt") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.workspace-conflict",
                               "Could not install the voxel sculpt workspace");
    }
    if (!candidate.activatePanel("voxel.viewport"))
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.workspace-activate",
                           "Could not activate the voxel sculpt viewport");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

editor::SelectionSnapshot VoxelCatalogEditor::selection() const {
    editor::SelectionSnapshot snapshot;
    snapshot.channel = "voxel-catalog";
    editor::SelectionItem item;
    item.domain = editor::SelectionDomain::Asset;
    item.target = editor::TargetId(target_.targetId());
    item.item   = editor::StableId(selectedId_);
    item.type   = "voxel.model";
    snapshot.items.push_back(item);
    snapshot.primary = item;
    return snapshot;
}

const voxel_editing::VoxelModelValue* VoxelCatalogEditor::selectedModel() const {
    return target_.findModel(voxel_editing::ObjectId(selectedId_));
}

const char* VoxelCatalogEditor::fillName(voxel_editing::VoxelCellFill fill) {
    switch (fill) {
        case voxel_editing::VoxelCellFill::Filled:
            return "filled";
        case voxel_editing::VoxelCellFill::Partial:
            return "partial";
        case voxel_editing::VoxelCellFill::Empty:
        default:
            return "empty";
    }
}

std::string VoxelCatalogEditor::toolName() const { return tool_ == VoxelSculptTool::Erase ? "erase" : "attach"; }

voxel_editing::EditorResult<void> VoxelCatalogEditor::commit(
    voxel_editing::EditorResult<voxel_editing::DomainOperation> operation, std::string label) {
    if (!operation.ok()) return voxel_editing::EditorResult<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("voxel.sculpt.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok())
        return editorError(begun.code(), "editor.voxel.begin", "Could not begin the voxel transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending voxel transaction already inactive");
        return voxel_editing::EditorResult<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) return voxel_editing::EditorResult<void>::failure(committed.status());
    return refreshPreview();
}

void VoxelCatalogEditor::cameraAxes(float& fx, float& fy, float& fz, float& rx, float& ry, float& rz, float& ux,
                                    float& uy, float& uz) const {
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);
    fx             = sy * cp;
    fy             = sp;
    fz             = cy * cp;
    rx             = cy;
    ry             = 0.0f;
    rz             = -sy;
    ux             = -sy * sp;
    uy             = cp;
    uz             = -cy * sp;
}

void VoxelCatalogEditor::rebuildScreen() {
    screen_.clear();
    const auto* model = selectedModel();
    if (!model) return;
    float fx, fy, fz, rx, ry, rz, ux, uy, uz;
    cameraAxes(fx, fy, fz, rx, ry, rz, ux, uy, uz);
    const float cx    = static_cast<float>(model->sizeX) * 0.5f;
    const float cy    = static_cast<float>(model->sizeY) * 0.5f;
    const float cz    = static_cast<float>(model->sizeZ) * 0.5f;
    const float extent =
        std::max(1.0f, static_cast<float>(std::max(model->sizeX, std::max(model->sizeY, model->sizeZ))));
    const float scale = std::min(viewportW_, viewportH_) / (extent * 1.8f);
    for (const auto& voxel : model->voxels) {
        const float px = static_cast<float>(voxel.x) + 0.5f - cx;
        const float py = static_cast<float>(voxel.y) + 0.5f - cy;
        const float pz = static_cast<float>(voxel.z) + 0.5f - cz;
        ScreenVoxel box;
        box.depth = px * fx + py * fy + pz * fz;
        box.w     = scale * 0.9f;
        box.h     = scale * 0.9f;
        box.x     = viewportW_ * 0.5f + (px * rx + py * ry + pz * rz) * scale - box.w * 0.5f;
        box.y     = viewportH_ * 0.5f - (px * ux + py * uy + pz * uz) * scale - box.h * 0.5f;
        screen_.push_back(box);
    }
    std::sort(screen_.begin(), screen_.end(), [](const ScreenVoxel& a, const ScreenVoxel& b) { return a.depth > b.depth; });
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::refreshPreview() {
    if (!selectedModel())
        return editorError(voxel_editing::EditorStatus::NotFound, "editor.voxel.selection",
                           "Voxel model was not found");
    rebuildScreen();
    joinPartners_    = target_.hullJoinPartners(voxel_editing::ObjectId(selectedId_), selectedFace_);
    previewRevision_ = target_.revision();
    return eve::editing::applied<void>();
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::selectModel(std::string id) {
    if (!target_.findModel(voxel_editing::ObjectId(id)))
        return editorError(voxel_editing::EditorStatus::NotFound, "editor.voxel.model", "Voxel model was not found");
    selectedId_ = std::move(id);
    return refreshPreview();
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::setTool(std::string tool) {
    if (tool == "attach")
        tool_ = VoxelSculptTool::Attach;
    else if (tool == "erase")
        tool_ = VoxelSculptTool::Erase;
    else
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.tool",
                           "Voxel tool must be attach or erase");
    return eve::editing::applied<void>();
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::setViewport(float width, float height) {
    if (!(width > 1.0f) || !(height > 1.0f) || !std::isfinite(width) || !std::isfinite(height))
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.viewport",
                           "Viewport size must be finite and greater than 1");
    viewportW_ = width;
    viewportH_ = height;
    rebuildScreen();
    return eve::editing::applied<void>();
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::orbit(float yawDelta, float pitchDelta) {
    if (!std::isfinite(yawDelta) || !std::isfinite(pitchDelta))
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.orbit",
                           "Orbit deltas must be finite");
    yaw_ += yawDelta;
    pitch_ = std::clamp(pitch_ + pitchDelta, -1.2f, 1.2f);
    rebuildScreen();
    return eve::editing::applied<void>();
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::applyPick(const voxel_editing::VoxelPick& pick) {
    const auto* model = selectedModel();
    if (!model)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.selection",
                           "A model must be selected to sculpt");
    if (tool_ == VoxelSculptTool::Erase) {
        if (!pick.hit)
            return editorError(voxel_editing::EditorStatus::NotFound, "editor.voxel.pick",
                               "No voxel under the pointer");
        return commit(target_.makeSetVoxel(model->id, pick.hitX, pick.hitY, pick.hitZ, false), "Erase voxel");
    }
    if (!pick.canAttach)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.attach",
                           "Attach needs an empty cell inside the model bounds");
    return commit(target_.makeSetVoxel(model->id, pick.prevX, pick.prevY, pick.prevZ, true), "Attach voxel");
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::pointerWorldRay(float ox, float oy, float oz, float dx, float dy,
                                                                      float dz) {
    const auto* model = selectedModel();
    if (!model)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.selection",
                           "A model must be selected to sculpt");
    if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.ray", "Ray values must be finite");
    return applyPick(voxel_editing::pickVoxelModel(*model, ox, oy, oz, dx, dy, dz, 256.0f));
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::pointerDown(float x, float y) {
    const auto* model = selectedModel();
    if (!model)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.selection",
                           "A model must be selected to sculpt");
    if (!std::isfinite(x) || !std::isfinite(y))
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.pointer",
                           "Pointer coordinates must be finite");
    float fx, fy, fz, rx, ry, rz, ux, uy, uz;
    cameraAxes(fx, fy, fz, rx, ry, rz, ux, uy, uz);
    const float cx     = static_cast<float>(model->sizeX) * 0.5f;
    const float cy     = static_cast<float>(model->sizeY) * 0.5f;
    const float cz     = static_cast<float>(model->sizeZ) * 0.5f;
    const float extent =
        std::max(1.0f, static_cast<float>(std::max(model->sizeX, std::max(model->sizeY, model->sizeZ))));
    const float scale = std::min(viewportW_, viewportH_) / (extent * 1.8f);
    const float nx    = (x - viewportW_ * 0.5f) / scale;
    const float ny    = (viewportH_ * 0.5f - y) / scale;
    const float ox    = cx - fx * distance_ + rx * nx + ux * ny;
    const float oy    = cy - fy * distance_ + ry * nx + uy * ny;
    const float oz    = cz - fz * distance_ + rz * nx + uz * ny;
    const auto  pick  = voxel_editing::pickVoxelModel(*model, ox, oy, oz, fx, fy, fz, distance_ * 4.0f);
    return applyPick(pick);
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::setVoxel(int x, int y, int z, bool occupied) {
    const auto* model = selectedModel();
    if (!model)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.selection",
                           "A model must be selected to sculpt");
    return commit(target_.makeSetVoxel(model->id, x, y, z, occupied), occupied ? "Attach voxel" : "Erase voxel");
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::setSelectedSocket(std::string tag, std::string kind) {
    auto tagSet =
        commit(target_.makeSet(selection(),
                               voxel_editing::PropertyPath("model.socket." + std::to_string(selectedFace_) + ".tag"),
                               voxel_editing::EditorValue(std::move(tag)), voxel_editing::PropertySetMode::Absolute),
               "Set socket tag");
    if (!tagSet.ok()) return tagSet;
    return commit(target_.makeSet(selection(),
                                  voxel_editing::PropertyPath("model.socket." + std::to_string(selectedFace_) + ".kind"),
                                  voxel_editing::EditorValue(std::move(kind)),
                                  voxel_editing::PropertySetMode::Absolute),
                  "Set socket kind");
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::selectFace(int face) {
    if (face < 0 || face > 5)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.face", "Face index must be 0..5");
    selectedFace_ = face;
    return refreshPreview();
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::createModel(std::string id, std::string name, int sizeX,
                                                                  int sizeY, int sizeZ) {
    voxel_editing::VoxelModelValue model;
    model.id    = voxel_editing::ObjectId(id);
    model.name  = std::move(name);
    model.sizeX = sizeX;
    model.sizeY = sizeY;
    model.sizeZ = sizeZ;
    auto created = commit(target_.makeCreateModel(model), "Create model");
    if (!created.ok()) return created;
    return selectModel(std::move(id));
}

voxel_editing::EditorResult<void> VoxelCatalogEditor::deleteSelectedModel() {
    const auto* model = selectedModel();
    if (!model)
        return editorError(voxel_editing::EditorStatus::Rejected, "editor.voxel.selection",
                           "A model must be selected to delete");
    auto deleted = commit(target_.makeDeleteModel(model->id), "Delete model");
    if (!deleted.ok()) return deleted;
    if (!target_.models().empty()) return selectModel(target_.models().front().id.value());
    selectedId_.clear();
    screen_.clear();
    return eve::editing::applied<void>();
}

voxel_editing::EditorResult<editor::TransactionReceipt> VoxelCatalogEditor::undo() {
    auto undone = transactions_.undo();
    if (!undone.ok()) return undone;
    auto previewed = refreshPreview();
    if (!previewed.ok()) return voxel_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return undone;
}

voxel_editing::EditorResult<editor::TransactionReceipt> VoxelCatalogEditor::redo() {
    auto redone = transactions_.redo();
    if (!redone.ok()) return redone;
    auto previewed = refreshPreview();
    if (!previewed.ok()) return voxel_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return redone;
}

std::string VoxelCatalogEditor::modelId(int index) const {
    if (index < 0 || index >= modelCount()) return {};
    return target_.models()[static_cast<std::size_t>(index)].id.value();
}

std::string VoxelCatalogEditor::modelName(int index) const {
    if (index < 0 || index >= modelCount()) return {};
    return target_.models()[static_cast<std::size_t>(index)].name;
}

std::string VoxelCatalogEditor::modelFill(int index) const {
    if (index < 0 || index >= modelCount()) return {};
    return fillName(voxel_editing::voxelClassifyModelFill(target_.models()[static_cast<std::size_t>(index)]));
}

bool VoxelCatalogEditor::isModelSelected(int index) const {
    return index >= 0 && index < modelCount() && modelId(index) == selectedId_;
}

int VoxelCatalogEditor::voxelCount() const {
    const auto* model = selectedModel();
    return model ? static_cast<int>(model->voxels.size()) : 0;
}

int VoxelCatalogEditor::voxelX(int index) const {
    const auto* model = selectedModel();
    if (!model || index < 0 || index >= static_cast<int>(model->voxels.size())) return 0;
    return model->voxels[static_cast<std::size_t>(index)].x;
}

int VoxelCatalogEditor::voxelY(int index) const {
    const auto* model = selectedModel();
    if (!model || index < 0 || index >= static_cast<int>(model->voxels.size())) return 0;
    return model->voxels[static_cast<std::size_t>(index)].y;
}

int VoxelCatalogEditor::voxelZ(int index) const {
    const auto* model = selectedModel();
    if (!model || index < 0 || index >= static_cast<int>(model->voxels.size())) return 0;
    return model->voxels[static_cast<std::size_t>(index)].z;
}

int VoxelCatalogEditor::modelSizeX() const {
    const auto* model = selectedModel();
    return model ? model->sizeX : 0;
}

int VoxelCatalogEditor::modelSizeY() const {
    const auto* model = selectedModel();
    return model ? model->sizeY : 0;
}

int VoxelCatalogEditor::modelSizeZ() const {
    const auto* model = selectedModel();
    return model ? model->sizeZ : 0;
}

float VoxelCatalogEditor::screenVoxelX(int index) const {
    if (index < 0 || index >= screenVoxelCount()) return 0;
    return screen_[static_cast<std::size_t>(index)].x;
}

float VoxelCatalogEditor::screenVoxelY(int index) const {
    if (index < 0 || index >= screenVoxelCount()) return 0;
    return screen_[static_cast<std::size_t>(index)].y;
}

float VoxelCatalogEditor::screenVoxelW(int index) const {
    if (index < 0 || index >= screenVoxelCount()) return 0;
    return screen_[static_cast<std::size_t>(index)].w;
}

float VoxelCatalogEditor::screenVoxelH(int index) const {
    if (index < 0 || index >= screenVoxelCount()) return 0;
    return screen_[static_cast<std::size_t>(index)].h;
}

std::string VoxelCatalogEditor::selectedSocketTag() const {
    const auto* model = selectedModel();
    if (!model) return {};
    return model->sockets[static_cast<std::size_t>(selectedFace_)].tag;
}

std::string VoxelCatalogEditor::selectedSocketKind() const {
    const auto* model = selectedModel();
    if (!model) return {};
    return voxel_editing::voxelSocketKindName(model->sockets[static_cast<std::size_t>(selectedFace_)].kind);
}

std::string VoxelCatalogEditor::joinPartnerId(int index) const {
    if (index < 0 || index >= joinPartnerCount()) return {};
    return joinPartners_[static_cast<std::size_t>(index)].value();
}

}  // namespace eve::voxel_editor
