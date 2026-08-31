#include "editor/Editor.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/ActionTimelineScriptBindings.h"
#include "editor/Brush.h"
#include "editor/EditorAutomationProvider.h"
#include "editor/EditorDock.h"
#include "editor/EditorHistory.h"
#include "editor/EditorInspector.h"
#include "editor/EditorSession.h"
#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorVolumeTarget.h"
#include "editor/EditorWorkspace.h"
#include "editor/FieldBrushTool.h"
#include "editor/FieldTargets.h"
#include "editor/GizmoManager.h"
#include "editor/ReflectionProbeVisualizer.h"
#include "editor/ScriptEditorTool.h"
#include "editor/TileBuffer.h"
#include "editor/TransformGizmo.h"
#include "editor/VolumeBrushTool.h"
#include "material_editing/MaterialEditingCommands.h"
#include "scene_editing/SceneEditingCommands.h"

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/ReflectionProbeCapture.h"
#ifdef EVENGINE_HAS_PROCGEN
#include "procgen/heightmap/Heightmap.h"
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace eve::editor {

Module_IMPL(Editor, new Editor());

namespace {

const char* statusName(EditorStatus status) {
    switch (status) {
        case EditorStatus::Applied: return "applied";
        case EditorStatus::Pending: return "pending";
        case EditorStatus::NoOp: return "no-op";
        case EditorStatus::Rejected: return "rejected";
        case EditorStatus::Conflict: return "conflict";
        case EditorStatus::NotFound: return "not-found";
        case EditorStatus::Unsupported: return "unsupported";
        case EditorStatus::Cancelled: return "cancelled";
        case EditorStatus::Failed: return "failed";
    }
    return "failed";
}

const char* transactionStateName(TransactionState state) {
    switch (state) {
        case TransactionState::Planning: return "planning";
        case TransactionState::Previewing: return "previewing";
        case TransactionState::PendingAuthority: return "pending-authority";
        case TransactionState::Committed: return "committed";
        case TransactionState::RolledBack: return "rolled-back";
        case TransactionState::Rejected: return "rejected";
        case TransactionState::Conflicted: return "conflicted";
        case TransactionState::Failed: return "failed";
    }
    return "failed";
}

bool squirrelToEditorValue(HSQUIRRELVM vm, SQInteger index, EditorValue& out, size_t depth = 0) {
    if (!vm || depth > 32) return false;
    const SQInteger absolute = index > 0 ? index : sq_gettop(vm) + index + 1;
    switch (sq_gettype(vm, absolute)) {
        case OT_NULL: out = EditorValue{}; return true;
        case OT_BOOL: {
            SQBool value = SQFalse;
            if (SQ_FAILED(sq_getbool(vm, absolute, &value))) return false;
            out = EditorValue(value != SQFalse);
            return true;
        }
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_FAILED(sq_getinteger(vm, absolute, &value))) return false;
            out = EditorValue(static_cast<int64_t>(value));
            return true;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_FAILED(sq_getfloat(vm, absolute, &value))) return false;
            out = EditorValue(static_cast<double>(value));
            return true;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_FAILED(sq_getstring(vm, absolute, &value))) return false;
            out = EditorValue(value ? value : "");
            return true;
        }
        case OT_ARRAY: {
            EditorValue::Array values;
            const SQInteger    count = sq_getsize(vm, absolute);
            values.reserve(static_cast<size_t>(count));
            for (SQInteger i = 0; i < count; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_FAILED(sq_get(vm, absolute))) return false;
                EditorValue value;
                const bool  ok = squirrelToEditorValue(vm, -1, value, depth + 1);
                sq_pop(vm, 1);
                if (!ok) return false;
                values.push_back(std::move(value));
            }
            out = EditorValue(std::move(values));
            return true;
        }
        case OT_TABLE: {
            EditorValue::Object values;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, absolute))) {
                const SQChar* key = nullptr;
                const bool keyOk  = sq_gettype(vm, -2) == OT_STRING && SQ_SUCCEEDED(sq_getstring(vm, -2, &key)) && key;
                const std::string stableKey = keyOk ? key : "";
                EditorValue       value;
                const bool        valueOk = keyOk && squirrelToEditorValue(vm, -1, value, depth + 1);
                sq_pop(vm, 2);
                if (!valueOk) {
                    sq_pop(vm, 1);
                    return false;
                }
                values[stableKey] = std::move(value);
            }
            sq_pop(vm, 1);
            out = EditorValue(std::move(values));
            return true;
        }
        default: return false;
    }
}

bool objectToEditorValue(const ssq::Object& object, EditorValue& out) {
    HSQUIRRELVM vm = object.getHandle();
    if (!vm) return false;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, object.getRaw());
    const bool ok = squirrelToEditorValue(vm, -1, out);
    sq_settop(vm, top);
    return ok;
}

void pushEditorValue(HSQUIRRELVM vm, const EditorValue& value) {
    std::visit(
        [&](const auto& current) {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                sq_pushnull(vm);
            } else if constexpr (std::is_same_v<T, bool>) {
                sq_pushbool(vm, current ? SQTrue : SQFalse);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                sq_pushinteger(vm, static_cast<SQInteger>(current));
            } else if constexpr (std::is_same_v<T, double>) {
                sq_pushfloat(vm, static_cast<SQFloat>(current));
            } else if constexpr (std::is_same_v<T, std::string>) {
                sq_pushstring(vm, current.c_str(), static_cast<SQInteger>(current.size()));
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                sq_newarray(vm, 0);
                for (const EditorValue& entry : current) {
                    pushEditorValue(vm, entry);
                    sq_arrayappend(vm, -2);
                }
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                sq_newtable(vm);
                for (const auto& [key, entry] : current) {
                    sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
                    pushEditorValue(vm, entry);
                    sq_newslot(vm, -3, SQFalse);
                }
            }
        },
        value.storage());
}

void setValue(ssq::Table& table, const char* name, const EditorValue& value) {
    HSQUIRRELVM vm = table.getHandle();
    sq_pushobject(vm, table.getRaw());
    sq_pushstring(vm, name, -1);
    pushEditorValue(vm, value);
    sq_newslot(vm, -3, SQFalse);
    sq_pop(vm, 1);
}

ssq::Array diagnosticArray(HSQUIRRELVM vm, const std::vector<EditorDiagnostic>& diagnostics) {
    ssq::Array out(vm);
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        ssq::Table item(vm);
        item.set("rule", diagnostic.rule.value());
        item.set("message", diagnostic.message);
        item.set("severity", static_cast<int>(diagnostic.severity));
        out.push(item);
    }
    return out;
}

template <class T>
ssq::Table resultTable(HSQUIRRELVM vm, const EditorResult<T>& result) {
    ssq::Table out(vm);
    out.set("status", std::string(statusName(result.status)));
    out.set("accepted", result.isAccepted());
    out.set("diagnostics", diagnosticArray(vm, result.diagnostics));
    return out;
}

EditorResult<EditorValue> invalidScriptPayload() {
    return EditorResult<EditorValue>::error(
        EditorStatus::Rejected, RuleId("editor.script.invalid-payload"),
        "Script payload must contain only null, bool, number, string, array, or table values");
}

bool invokeScriptCommand(const ssq::Object& callback, const EditorValue& payload) {
    const SQObjectType type = callback.getRaw()._type;
    if (type != OT_CLOSURE && type != OT_NATIVECLOSURE) return false;
    HSQUIRRELVM vm  = callback.getHandle();
    SQInteger   top = sq_gettop(vm);
    sq_pushobject(vm, callback.getRaw());
    sq_pushroottable(vm);
    pushEditorValue(vm, payload);
    bool accepted = false;
    if (SQ_SUCCEEDED(sq_call(vm, 2, SQTrue, SQTrue))) {
        SQBool result = SQFalse;
        accepted      = SQ_SUCCEEDED(sq_getbool(vm, -1, &result)) && result != SQFalse;
    }
    sq_settop(vm, top);
    return accepted;
}

bool registerScriptCommand(Editor* editor, const std::string& id, const std::string& displayName,
                           const std::string& category, ssq::Object callback) {
    if (!editor || id.empty()) return false;
    const SQObjectType callbackType = callback.getRaw()._type;
    if (callbackType != OT_CLOSURE && callbackType != OT_NATIVECLOSURE) return false;

    CommandDescriptor descriptor;
    descriptor.id                = CommandId(id);
    descriptor.ownerModule       = "script:" + id;
    descriptor.displayName       = displayName;
    descriptor.category          = category;
    descriptor.automationAllowed = false;
    return editor->commandService()
        .registerPlannedCommand(
            descriptor,
            [id](const CommandRequest& request) {
                CommandPlan plan;
                plan.summary = EditorValue::Object{{"command", EditorValue(id)}, {"payload", request.payload}};
                DomainOperation operation;
                operation.type    = id;
                operation.payload = request.payload;
                plan.operations.push_back(std::move(operation));
                return EditorResult<CommandPlan>::applied(std::move(plan));
            },
            [callback = std::move(callback)](const CommandRequest& request, const CommandPlan& plan) {
                if (!invokeScriptCommand(callback, request.payload))
                    return EditorResult<TransactionReceipt>::error(
                        EditorStatus::Rejected, RuleId("editor.script.command-rejected"),
                        "Script command callback rejected the planned payload");
                TransactionReceipt receipt;
                receipt.id               = TransactionId(plan.id.value());
                receipt.state            = TransactionState::Committed;
                receipt.beforeRevision   = plan.baseRevision;
                receipt.afterRevision    = plan.baseRevision + 1;
                receipt.authorityReceipt = "script:local";
                return EditorResult<TransactionReceipt>::applied(std::move(receipt));
            },
            true)
        .isAccepted();
}

}  // namespace

#ifdef EVENGINE_HAS_PROCGEN
namespace {

#ifdef EVENGINE_HAS_PROCGEN
struct HeightmapArrays {
    std::vector<float>    pos;
    std::vector<float>    nrm;
    std::vector<float>    uv;
    std::vector<uint32_t> idx;
};

/** Terrain mesh: two triangles per cell, 6 vertices per quad. When
 *  smoothNormals is set, vertex normals come from the height-field gradient
 *  (continuous bowls); otherwise each triangle is flat-shaded. */
void buildHeightmapArrays(const eve::procgen::Heightmap& hm, float cell, float hScale, HeightmapArrays& out,
                          bool smoothNormals) {
    out.pos.clear();
    out.nrm.clear();
    out.uv.clear();
    out.idx.clear();
    const int w = hm.getWidth();
    const int h = hm.getHeight();
    if (w < 2 || h < 2) return;
    const float uw = float(w - 1);
    const float uh = float(h - 1);

    // Per-grid-vertex normals from central differences of the height field.
    std::vector<float> smoothNrm;
    if (smoothNormals) {
        smoothNrm.resize(size_t(w) * size_t(h) * 3);
        auto hs = [&](int x, int z) {
            x = std::clamp(x, 0, w - 1);
            z = std::clamp(z, 0, h - 1);
            return hm.height(x, z);
        };
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                const float dhdx = (hs(x + 1, z) - hs(x - 1, z)) * 0.5f * hScale / cell;
                const float dhdz = (hs(x, z + 1) - hs(x, z - 1)) * 0.5f * hScale / cell;
                float       nx   = -dhdx;
                float       ny   = 1.f;
                float       nz   = -dhdz;
                const float len  = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                float* n = &smoothNrm[(size_t(z) * w + x) * 3];
                n[0]     = nx;
                n[1]     = ny;
                n[2]     = nz;
            }
        }

        // Smooth terrain can share the heightmap's grid vertices. Positions,
        // normals and UVs change while sculpting; the indexed topology does not.
        out.pos.reserve(size_t(w) * size_t(h) * 3u);
        out.nrm.reserve(size_t(w) * size_t(h) * 3u);
        out.uv.reserve(size_t(w) * size_t(h) * 2u);
        out.idx.reserve(size_t(w - 1) * size_t(h - 1) * 6u);
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                out.pos.insert(out.pos.end(), {float(x) * cell, hm.height(x, z) * hScale, float(z) * cell});
                const float* n = &smoothNrm[(size_t(z) * size_t(w) + size_t(x)) * 3u];
                out.nrm.insert(out.nrm.end(), {n[0], n[1], n[2]});
                out.uv.insert(out.uv.end(), {float(x) / uw, float(z) / uh});
            }
        }
        for (int z = 0; z < h - 1; ++z) {
            for (int x = 0; x < w - 1; ++x) {
                const uint32_t i00 = uint32_t(z * w + x);
                const uint32_t i10 = i00 + 1u;
                const uint32_t i01 = i00 + uint32_t(w);
                const uint32_t i11 = i01 + 1u;
                out.idx.insert(out.idx.end(), {i00, i01, i10, i10, i01, i11});
            }
        }
        return;
    }

    auto addTri = [&](float ax, float az, float ay, float bx, float bz, float by, float cx, float cz, float cy) {
        const float    p0x = ax * cell, p0y = ay * hScale, p0z = az * cell;
        const float    p1x = bx * cell, p1y = by * hScale, p1z = bz * cell;
        const float    p2x = cx * cell, p2y = cy * hScale, p2z = cz * cell;
        const uint32_t base = uint32_t(out.pos.size() / 3);
        out.pos.insert(out.pos.end(), {p0x, p0y, p0z, p1x, p1y, p1z, p2x, p2y, p2z});
        if (smoothNormals) {
            const float* n0 = &smoothNrm[(size_t(int(az)) * w + int(ax)) * 3];
            const float* n1 = &smoothNrm[(size_t(int(bz)) * w + int(bx)) * 3];
            const float* n2 = &smoothNrm[(size_t(int(cz)) * w + int(cx)) * 3];
            out.nrm.insert(out.nrm.end(), {n0[0], n0[1], n0[2], n1[0], n1[1], n1[2], n2[0], n2[1], n2[2]});
        } else {
            const float e1x = p1x - p0x, e1y = p1y - p0y, e1z = p1z - p0z;
            const float e2x = p2x - p0x, e2y = p2y - p0y, e2z = p2z - p0z;
            float       nx  = e1y * e2z - e1z * e2y;
            float       ny  = e1z * e2x - e1x * e2z;
            float       nz  = e1x * e2y - e1y * e2x;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            out.nrm.insert(out.nrm.end(), {nx, ny, nz, nx, ny, nz, nx, ny, nz});
        }
        out.uv.insert(out.uv.end(), {ax / uw, az / uh, bx / uw, bz / uh, cx / uw, cz / uh});
        out.idx.insert(out.idx.end(), {base, base + 1, base + 2});
    };

    for (int y = 0; y < h - 1; ++y) {
        for (int x = 0; x < w - 1; ++x) {
            const float h00 = hm.height(x, y);
            const float h10 = hm.height(x + 1, y);
            const float h01 = hm.height(x, y + 1);
            const float h11 = hm.height(x + 1, y + 1);
            // Grid is XZ; y in the function is the XZ "row" axis.
            // Counter-clockwise when viewed from above (+Y).  Besides matching
            // the renderer's front face, this keeps the flat-shaded cross
            // product pointing upward instead of into the terrain.
            addTri(float(x), float(y), h00, float(x), float(y + 1), h01, float(x + 1), float(y), h10);
            addTri(float(x + 1), float(y), h10, float(x), float(y + 1), h01, float(x + 1), float(y + 1), h11);
        }
    }
}
#endif

}  // namespace
#endif

Editor::Editor()
    : targets_(std::make_unique<EditorTargetCoordinator>(commandService_)),
      automation_(std::make_unique<EditorAutomationProvider>(commandService_, *targets_)) {
    const auto sceneCommands    = eve::scene_editing::registerEditingCommands(*targets_);
    const auto materialCommands = eve::material_editing::registerEditingCommands(*targets_);
    if (!sceneCommands.isAccepted() || !materialCommands.isAccepted())
        throw std::runtime_error("Failed to register built-in domain editing commands");
    eve::cap::provide<eve::IEditorAutomation>(automation_.get());
}

Editor::~Editor() { eve::cap::revoke<eve::IEditorAutomation>(automation_.get()); }

EditorResult<void> Editor::registerEditingTarget(IEditableTarget& target) { return targets_->registerTarget(target); }

EditorResult<void> Editor::unregisterEditingTarget(const TargetId& target) {
    auto result = targets_->unregisterTarget(target);
    if (result.status == EditorStatus::Applied) automation_->targetUnregistered(target);
    return result;
}

TransformGizmo* Editor::newGizmo() { return new TransformGizmo(); }

GizmoManager* Editor::newGizmoManager() { return new GizmoManager(); }

ReflectionProbeVisualizer* Editor::newReflectionProbeVisualizer(
    graphics::ReflectionProbeCapture* probe) {
    return new ReflectionProbeVisualizer(probe);
}

TileBuffer* Editor::newTileBuffer(int width, int height) { return new TileBuffer(width, height); }

Brush* Editor::newBrush() { return new Brush(); }

EditorToolbar* Editor::newToolbar() { return new EditorToolbar(); }

EditorInspector* Editor::newInspector() { return new EditorInspector(); }

EditorDock* Editor::newDock() { return new EditorDock(); }

EditorHistory* Editor::newHistory() { return new EditorHistory(); }

EditorSession* Editor::newSession() {
    auto* session = new EditorSession();
    session->setCommandService(&commandService_);
    return session;
}

EditorWorkspace* Editor::newWorkspace(const std::string& id, const std::string& title) {
    if (id.empty()) return nullptr;
    return new EditorWorkspace(id, title.empty() ? id : title);
}

TileBufferTarget* Editor::newTileBufferTarget(const std::string& id, TileBuffer* buffer) {
    return new TileBufferTarget(id, buffer);
}

#ifdef EVENGINE_HAS_MAP
TileLayerTarget* Editor::newTileLayerTarget(const std::string& id, map::TileLayer* layer) {
    return new TileLayerTarget(id, layer);
}
#endif

ScriptEditorTool* Editor::newScriptTool(const std::string& id, const std::string& label) {
    return new ScriptEditorTool(id, label);
}

ConstantBrushFalloff*    Editor::newConstantBrushFalloff() { return new ConstantBrushFalloff(); }
LinearBrushFalloff*      Editor::newLinearBrushFalloff() { return new LinearBrushFalloff(); }
SmoothBrushFalloff*      Editor::newSmoothBrushFalloff() { return new SmoothBrushFalloff(); }
CircleBrushKernel*       Editor::newCircleBrushKernel() { return new CircleBrushKernel(); }
BoxBrushKernel*          Editor::newBoxBrushKernel() { return new BoxBrushKernel(); }
PaintIntFieldOperation*  Editor::newPaintIntFieldOperation(int value) { return new PaintIntFieldOperation(value); }
AddScalarFieldOperation* Editor::newAddScalarFieldOperation() { return new AddScalarFieldOperation(); }
FieldBrushTool*          Editor::newFieldBrushTool(const std::string& id, const std::string& label) {
    return new FieldBrushTool(id, label, nullptr, nullptr);
}

SphereVolumeBrushKernel* Editor::newSphereVolumeBrushKernel() { return new SphereVolumeBrushKernel(); }
BoxVolumeBrushKernel*    Editor::newBoxVolumeBrushKernel() { return new BoxVolumeBrushKernel(); }
PaintIntVolumeOperation* Editor::newPaintIntVolumeOperation(int value) { return new PaintIntVolumeOperation(value); }
VolumeBrushTool*         Editor::newVolumeBrushTool(const std::string& id, const std::string& label) {
    return new VolumeBrushTool(id, label);
}

#ifdef EVENGINE_HAS_VOXEL
VoxelWorldTarget* Editor::newVoxelWorldTarget(const std::string& id, voxel::VoxelWorld* world) {
    return new VoxelWorldTarget(id, world);
}
#endif

#ifdef EVENGINE_HAS_PROCGEN
HeightmapTarget* Editor::newHeightmapTarget(const std::string& id, procgen::Heightmap* heightmap) {
    return new HeightmapTarget(id, heightmap);
}

int Editor::applyHeightmapBrush(procgen::Heightmap* hm, float centerX, float centerY, float radius, float strength) {
    if (!hm || radius < 0.f || strength == 0.f) return 0;
    const int   minX    = std::max(0, int(std::floor(centerX - radius)));
    const int   maxX    = std::min(hm->getWidth() - 1, int(std::ceil(centerX + radius)));
    const int   minY    = std::max(0, int(std::floor(centerY - radius)));
    const int   maxY    = std::min(hm->getHeight() - 1, int(std::ceil(centerY + radius)));
    const float edge    = radius + 0.5f;
    int         changed = 0;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx       = float(x) - centerX;
            const float dy       = float(y) - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) continue;
            const float falloff   = 1.f - distance / edge;
            const float oldHeight = hm->height(x, y);
            const float newHeight = std::clamp(oldHeight + strength * falloff, 0.f, 1.f);
            if (newHeight == oldHeight) continue;
            hm->setHeight(x, y, newHeight);
            ++changed;
        }
    }
    return changed;
}

graphics::Mesh* Editor::newHeightmapMesh(procgen::Heightmap* hm, float cellSize, float heightScale) {
    auto* gfx = eve::ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx || !hm) return nullptr;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, false);
    if (a.idx.empty()) return nullptr;
    return gfx->newMeshFromArrays(a.pos.data(), a.nrm.data(), a.uv.data(), int(a.pos.size() / 3), a.idx.data(),
                                  int(a.idx.size()));
}

bool Editor::updateHeightmapMesh(graphics::Mesh* mesh, graphics::Graphics* gfx, procgen::Heightmap* hm, float cellSize,
                                 float heightScale) {
    if (!mesh || !gfx || !hm) return false;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, false);
    if (a.idx.empty()) return false;
    return gfx->updateMeshVertices(mesh, a.pos.data(), a.nrm.data(), a.uv.data(), int(a.pos.size() / 3), nullptr, 0);
}

graphics::Mesh* Editor::newHeightmapMeshSmooth(procgen::Heightmap* hm, float cellSize, float heightScale) {
    auto* gfx = eve::ModuleManager::getInstance<graphics::Graphics>("Graphics");
    if (!gfx || !hm) return nullptr;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, true);
    if (a.idx.empty()) return nullptr;
    return gfx->newMeshFromArrays(a.pos.data(), a.nrm.data(), a.uv.data(), int(a.pos.size() / 3), a.idx.data(),
                                  int(a.idx.size()));
}

bool Editor::updateHeightmapMeshSmooth(graphics::Mesh* mesh, graphics::Graphics* gfx, procgen::Heightmap* hm,
                                       float cellSize, float heightScale) {
    if (!mesh || !gfx || !hm) return false;
    HeightmapArrays a;
    buildHeightmapArrays(*hm, cellSize, heightScale, a, true);
    if (a.idx.empty()) return false;
    return gfx->updateMeshVertices(mesh, a.pos.data(), a.nrm.data(), a.uv.data(), int(a.pos.size() / 3), nullptr, 0);
}
#endif

void Editor::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Editor::create, false);
    expose(cls);
    exposeActionTimelineScriptBindings(table, cls);

    auto gizmo = table.addClass<TransformGizmo>(
        "TransformGizmo", std::function<TransformGizmo*()>([]() -> TransformGizmo* { return nullptr; }), true);
    gizmo.addFunc("setMode", &TransformGizmo::setMode);
    gizmo.addFunc("getMode", &TransformGizmo::getMode);
    gizmo.addFunc("setSpace", &TransformGizmo::setSpace);
    gizmo.addFunc("getSpace", &TransformGizmo::getSpace);
    gizmo.addFunc("setSize", &TransformGizmo::setSize);
    gizmo.addFunc("getSize", &TransformGizmo::getSize);
    gizmo.addFunc("setPosition", &TransformGizmo::setPosition);
    gizmo.addFunc("getPositionX", &TransformGizmo::getPositionX);
    gizmo.addFunc("getPositionY", &TransformGizmo::getPositionY);
    gizmo.addFunc("getPositionZ", &TransformGizmo::getPositionZ);
    gizmo.addFunc("setRotationEuler", &TransformGizmo::setRotationEuler);
    gizmo.addFunc("getRotationX", &TransformGizmo::getRotationX);
    gizmo.addFunc("getRotationY", &TransformGizmo::getRotationY);
    gizmo.addFunc("getRotationZ", &TransformGizmo::getRotationZ);
    gizmo.addFunc("setScale", &TransformGizmo::setScale);
    gizmo.addFunc("getScaleX", &TransformGizmo::getScaleX);
    gizmo.addFunc("getScaleY", &TransformGizmo::getScaleY);
    gizmo.addFunc("getScaleZ", &TransformGizmo::getScaleZ);
    gizmo.addFunc("setBounds", &TransformGizmo::setBounds);
    gizmo.addFunc("getBoundsMinX", &TransformGizmo::getBoundsMinX);
    gizmo.addFunc("getBoundsMinY", &TransformGizmo::getBoundsMinY);
    gizmo.addFunc("getBoundsMinZ", &TransformGizmo::getBoundsMinZ);
    gizmo.addFunc("getBoundsMaxX", &TransformGizmo::getBoundsMaxX);
    gizmo.addFunc("getBoundsMaxY", &TransformGizmo::getBoundsMaxY);
    gizmo.addFunc("getBoundsMaxZ", &TransformGizmo::getBoundsMaxZ);
    gizmo.addFunc("setSnapTranslate", &TransformGizmo::setSnapTranslate);
    gizmo.addFunc("setSnapRotate", &TransformGizmo::setSnapRotate);
    gizmo.addFunc("setSnapScale", &TransformGizmo::setSnapScale);
    gizmo.addFunc("getSnapTranslateX", &TransformGizmo::getSnapTranslateX);
    gizmo.addFunc("getSnapTranslateY", &TransformGizmo::getSnapTranslateY);
    gizmo.addFunc("getSnapTranslateZ", &TransformGizmo::getSnapTranslateZ);
    gizmo.addFunc("getSnapRotate", &TransformGizmo::getSnapRotate);
    gizmo.addFunc("getSnapScale", &TransformGizmo::getSnapScale);
    gizmo.addFunc("getMatrix", &TransformGizmo::getMatrix);
    gizmo.addFunc("pick", &TransformGizmo::pick);
    gizmo.addFunc("beginDrag", &TransformGizmo::beginDrag);
    gizmo.addFunc("updateDrag", &TransformGizmo::updateDrag);
    gizmo.addFunc("endDrag", &TransformGizmo::endDrag);
    gizmo.addFunc("isDragging", &TransformGizmo::isDragging);
    gizmo.addFunc("isHovered", &TransformGizmo::isHovered);
    gizmo.addFunc("getActiveAxis", &TransformGizmo::getActiveAxis);
    gizmo.addFunc("getHoverAxis", &TransformGizmo::getHoverAxis);
    gizmo.addFunc("rebuildParts", &TransformGizmo::rebuildParts);
    gizmo.addFunc("getPartCount", &TransformGizmo::getPartCount);
    gizmo.addFunc("getPartKind", &TransformGizmo::getPartKind);
    gizmo.addFunc("getPartAxis", &TransformGizmo::getPartAxis);
    gizmo.addFunc("getPartColorR", &TransformGizmo::getPartColorR);
    gizmo.addFunc("getPartColorG", &TransformGizmo::getPartColorG);
    gizmo.addFunc("getPartColorB", &TransformGizmo::getPartColorB);
    gizmo.addFunc("getPartColorA", &TransformGizmo::getPartColorA);
    gizmo.addFunc("getPartOriginX", &TransformGizmo::getPartOriginX);
    gizmo.addFunc("getPartOriginY", &TransformGizmo::getPartOriginY);
    gizmo.addFunc("getPartOriginZ", &TransformGizmo::getPartOriginZ);
    gizmo.addFunc("getPartDirX", &TransformGizmo::getPartDirX);
    gizmo.addFunc("getPartDirY", &TransformGizmo::getPartDirY);
    gizmo.addFunc("getPartDirZ", &TransformGizmo::getPartDirZ);
    gizmo.addFunc("getPartLength", &TransformGizmo::getPartLength);
    gizmo.addFunc("getPartRadius", &TransformGizmo::getPartRadius);

    auto probeVisualizer = table.addClass<ReflectionProbeVisualizer>(
        "ReflectionProbeVisualizer",
        std::function<ReflectionProbeVisualizer*()>([]() -> ReflectionProbeVisualizer* {
            return nullptr;
        }),
        true);
    probeVisualizer.addFunc("setExtents", &ReflectionProbeVisualizer::setExtents);
    probeVisualizer.addFunc("getLineCount", &ReflectionProbeVisualizer::getLineCount);
    probeVisualizer.addFunc("getLineStart", &ReflectionProbeVisualizer::getLineStart);
    probeVisualizer.addFunc("getLineEnd", &ReflectionProbeVisualizer::getLineEnd);
    probeVisualizer.addFunc("getColorR", &ReflectionProbeVisualizer::getColorR);
    probeVisualizer.addFunc("getColorG", &ReflectionProbeVisualizer::getColorG);
    probeVisualizer.addFunc("getColorB", &ReflectionProbeVisualizer::getColorB);
    probeVisualizer.addFunc("getCenterX", &ReflectionProbeVisualizer::getCenterX);
    probeVisualizer.addFunc("getCenterY", &ReflectionProbeVisualizer::getCenterY);
    probeVisualizer.addFunc("getCenterZ", &ReflectionProbeVisualizer::getCenterZ);
    probeVisualizer.addFunc("getStatusLabel", &ReflectionProbeVisualizer::getStatusLabel);

    auto mgr = table.addClass<GizmoManager>(
        "GizmoManager", std::function<GizmoManager*()>([]() -> GizmoManager* { return nullptr; }), true);
    mgr.addFunc("getGizmo", &GizmoManager::getGizmo);
    mgr.addFunc("setPositionEnabled", &GizmoManager::setPositionEnabled);
    mgr.addFunc("setRotationEnabled", &GizmoManager::setRotationEnabled);
    mgr.addFunc("setScaleEnabled", &GizmoManager::setScaleEnabled);
    mgr.addFunc("setBoundEnabled", &GizmoManager::setBoundEnabled);
    mgr.addFunc("getPositionEnabled", &GizmoManager::getPositionEnabled);
    mgr.addFunc("getRotationEnabled", &GizmoManager::getRotationEnabled);
    mgr.addFunc("getScaleEnabled", &GizmoManager::getScaleEnabled);
    mgr.addFunc("getBoundEnabled", &GizmoManager::getBoundEnabled);
    mgr.addFunc("attach", &GizmoManager::attach);
    mgr.addFunc("detach", &GizmoManager::detach);
    mgr.addFunc("isAttached", &GizmoManager::isAttached);
    mgr.addFunc("pick", &GizmoManager::pick);
    mgr.addFunc("beginDrag", &GizmoManager::beginDrag);
    mgr.addFunc("updateDrag", &GizmoManager::updateDrag);
    mgr.addFunc("endDrag", &GizmoManager::endDrag);
    mgr.addFunc("isDragging", &GizmoManager::isDragging);
    mgr.addFunc("isHovered", &GizmoManager::isHovered);

    auto buf = table.addClass<TileBuffer>("TileBuffer",
                                          std::function<TileBuffer*()>([]() -> TileBuffer* { return nullptr; }), true);
    buf.addFunc("getWidth", &TileBuffer::getWidth);
    buf.addFunc("getHeight", &TileBuffer::getHeight);
    buf.addFunc("resize", &TileBuffer::resize);
    buf.addFunc("clear", &TileBuffer::clear);
    buf.addFunc("fill", &TileBuffer::fill);
    buf.addFunc("setGid", &TileBuffer::setGid);
    buf.addFunc("getGid", &TileBuffer::getGid);
    buf.addFunc("inBounds", &TileBuffer::inBounds);

    auto brush = table.addClass<Brush>("Brush", std::function<Brush*()>([]() -> Brush* { return nullptr; }), true);
    brush.addFunc("setTool", &Brush::setTool);
    brush.addFunc("getTool", &Brush::getTool);
    brush.addFunc("setSize", &Brush::setSize);
    brush.addFunc("getSize", &Brush::getSize);
    brush.addFunc("setShape", &Brush::setShape);
    brush.addFunc("getShape", &Brush::getShape);
    brush.addFunc("setTile", &Brush::setTile);
    brush.addFunc("getTile", &Brush::getTile);
    brush.addFunc("setEraseTile", &Brush::setEraseTile);
    brush.addFunc("getEraseTile", &Brush::getEraseTile);
    brush.addFunc("setStampSize", &Brush::setStampSize);
    brush.addFunc("getStampWidth", &Brush::getStampWidth);
    brush.addFunc("getStampHeight", &Brush::getStampHeight);
    brush.addFunc("setStampTile", &Brush::setStampTile);
    brush.addFunc("getStampTile", &Brush::getStampTile);
    brush.addFunc("clearStamp", &Brush::clearStamp);
    brush.addFunc("paintAt", &Brush::paintAt);
    brush.addFunc("eraseAt", &Brush::eraseAt);
    brush.addFunc("floodFill", &Brush::floodFill);
    brush.addFunc("paintLine", &Brush::paintLine);
    brush.addFunc("paintRect", &Brush::paintRect);
    brush.addFunc("previewAt", &Brush::previewAt);
    brush.addFunc("previewLine", &Brush::previewLine);
    brush.addFunc("previewRect", &Brush::previewRect);
    brush.addFunc("getPreviewCount", &Brush::getPreviewCount);
    brush.addFunc("getPreviewX", &Brush::getPreviewX);
    brush.addFunc("getPreviewY", &Brush::getPreviewY);
    brush.addFunc("getPreviewGid", &Brush::getPreviewGid);
    brush.addFunc("getChangeCount", &Brush::getChangeCount);
    brush.addFunc("getChangeX", &Brush::getChangeX);
    brush.addFunc("getChangeY", &Brush::getChangeY);
    brush.addFunc("getChangeOldGid", &Brush::getChangeOldGid);
    brush.addFunc("getChangeNewGid", &Brush::getChangeNewGid);

    auto tb = table.addClass<EditorToolbar>(
        "EditorToolbar", std::function<EditorToolbar*()>([]() -> EditorToolbar* { return nullptr; }), true);
    tb.addFunc("clear", &EditorToolbar::clear);
    tb.addFunc("addTool", &EditorToolbar::addTool);
    tb.addFunc("setShortcut", &EditorToolbar::setShortcut);
    tb.addFunc("setActive", &EditorToolbar::setActive);
    tb.addFunc("getActive", &EditorToolbar::getActive);
    tb.addFunc("matchShortcut", &EditorToolbar::matchShortcut);
    tb.addFunc("getToolCount", &EditorToolbar::getToolCount);
    tb.addFunc("getToolId", &EditorToolbar::getToolId);
    tb.addFunc("getToolLabel", &EditorToolbar::getToolLabel);
    tb.addFunc("getToolShortcut", &EditorToolbar::getToolShortcut);

    auto insp = table.addClass<EditorInspector>(
        "EditorInspector", std::function<EditorInspector*()>([]() -> EditorInspector* { return nullptr; }), true);
    insp.addFunc("clear", &EditorInspector::clear);
    insp.addFunc("addFloat", &EditorInspector::addFloat);
    insp.addFunc("addFloat3", &EditorInspector::addFloat3);
    insp.addFunc("addBool", &EditorInspector::addBool);
    insp.addFunc("addString", &EditorInspector::addString);
    insp.addFunc("addChoice", &EditorInspector::addChoice);
    insp.addFunc("getFieldCount", &EditorInspector::getFieldCount);
    insp.addFunc("getFieldKind", &EditorInspector::getFieldKind);
    insp.addFunc("getFieldId", &EditorInspector::getFieldId);
    insp.addFunc("getFieldLabel", &EditorInspector::getFieldLabel);
    insp.addFunc("getFloat", &EditorInspector::getFloat);
    insp.addFunc("setFloat", &EditorInspector::setFloat);
    insp.addFunc("getFloatMin", &EditorInspector::getFloatMin);
    insp.addFunc("getFloatMax", &EditorInspector::getFloatMax);
    insp.addFunc("getFloatStep", &EditorInspector::getFloatStep);
    insp.addFunc("getFloat3X", &EditorInspector::getFloat3X);
    insp.addFunc("getFloat3Y", &EditorInspector::getFloat3Y);
    insp.addFunc("getFloat3Z", &EditorInspector::getFloat3Z);
    insp.addFunc("setFloat3", &EditorInspector::setFloat3);
    insp.addFunc("getBool", &EditorInspector::getBool);
    insp.addFunc("setBool", &EditorInspector::setBool);
    insp.addFunc("getString", &EditorInspector::getString);
    insp.addFunc("setString", &EditorInspector::setString);
    insp.addFunc("getChoice", &EditorInspector::getChoice);
    insp.addFunc("setChoice", &EditorInspector::setChoice);
    insp.addFunc("getChoicesCsv", &EditorInspector::getChoicesCsv);
    insp.addFunc("isDirty", &EditorInspector::isDirty);
    insp.addFunc("clearDirty", &EditorInspector::clearDirty);
    insp.addFunc("clearAllDirty", &EditorInspector::clearAllDirty);
    insp.addFunc("pollChangedId", &EditorInspector::pollChangedId);

    auto dock = table.addClass<EditorDock>("EditorDock",
                                           std::function<EditorDock*()>([]() -> EditorDock* { return nullptr; }), true);
    dock.addFunc("setRegionSize", &EditorDock::setRegionSize);
    dock.addFunc("getRegionSize", &EditorDock::getRegionSize);
    dock.addFunc("layout", &EditorDock::layout);
    dock.addFunc("getRegionX", &EditorDock::getRegionX);
    dock.addFunc("getRegionY", &EditorDock::getRegionY);
    dock.addFunc("getRegionW", &EditorDock::getRegionW);
    dock.addFunc("getRegionH", &EditorDock::getRegionH);

    auto hist = table.addClass<EditorHistory>(
        "EditorHistory", std::function<EditorHistory*()>([]() -> EditorHistory* { return nullptr; }), true);
    hist.addFunc("clear", &EditorHistory::clear);
    hist.addFunc("push", &EditorHistory::push);
    hist.addFunc("beginGroup", &EditorHistory::beginGroup);
    hist.addFunc("recordTile", &EditorHistory::recordTile);
    hist.addFunc("endGroup", &EditorHistory::endGroup);
    hist.addFunc("isGrouping", &EditorHistory::isGrouping);
    hist.addFunc("canUndo", &EditorHistory::canUndo);
    hist.addFunc("canRedo", &EditorHistory::canRedo);
    hist.addFunc("getUndoCount", &EditorHistory::getUndoCount);
    hist.addFunc("getRedoCount", &EditorHistory::getRedoCount);
    hist.addFunc("undo", &EditorHistory::undo);
    hist.addFunc("redo", &EditorHistory::redo);
    hist.addFunc("applyLastToBuffer", &EditorHistory::applyLastToBuffer);
    hist.addFunc("getLastActionName", &EditorHistory::getLastActionName);
    hist.addFunc("getLastActionKind", &EditorHistory::getLastActionKind);
    hist.addFunc("getLastPayload", &EditorHistory::getLastPayload);
    hist.addFunc("getLastTileCount", &EditorHistory::getLastTileCount);
    hist.addFunc("getLastTileX", &EditorHistory::getLastTileX);
    hist.addFunc("getLastTileY", &EditorHistory::getLastTileY);
    hist.addFunc("getLastTileOldGid", &EditorHistory::getLastTileOldGid);
    hist.addFunc("getLastTileNewGid", &EditorHistory::getLastTileNewGid);

    auto constantFalloff = table.addClass<ConstantBrushFalloff>(
        "ConstantBrushFalloff",
        std::function<ConstantBrushFalloff*()>([]() -> ConstantBrushFalloff* { return nullptr; }), true);
    auto linearFalloff = table.addClass<LinearBrushFalloff>(
        "LinearBrushFalloff", std::function<LinearBrushFalloff*()>([]() -> LinearBrushFalloff* { return nullptr; }),
        true);
    auto smoothFalloff = table.addClass<SmoothBrushFalloff>(
        "SmoothBrushFalloff", std::function<SmoothBrushFalloff*()>([]() -> SmoothBrushFalloff* { return nullptr; }),
        true);
    (void)constantFalloff;
    (void)linearFalloff;
    (void)smoothFalloff;

    auto circleKernel = table.addClass<CircleBrushKernel>(
        "CircleBrushKernel", std::function<CircleBrushKernel*()>([]() -> CircleBrushKernel* { return nullptr; }), true);
    circleKernel.addFunc("setConstantFalloff", [](CircleBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    circleKernel.addFunc("setLinearFalloff", [](CircleBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    circleKernel.addFunc("setSmoothFalloff", [](CircleBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto boxKernel = table.addClass<BoxBrushKernel>(
        "BoxBrushKernel", std::function<BoxBrushKernel*()>([]() -> BoxBrushKernel* { return nullptr; }), true);
    boxKernel.addFunc("setConstantFalloff", [](BoxBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxKernel.addFunc("setLinearFalloff", [](BoxBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxKernel.addFunc("setSmoothFalloff", [](BoxBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto paintOperation = table.addClass<PaintIntFieldOperation>(
        "PaintIntFieldOperation",
        std::function<PaintIntFieldOperation*()>([]() -> PaintIntFieldOperation* { return nullptr; }), true);
    paintOperation.addFunc("setValue", &PaintIntFieldOperation::setValue);
    paintOperation.addFunc("getValue", &PaintIntFieldOperation::value);
    auto addScalarOperation = table.addClass<AddScalarFieldOperation>(
        "AddScalarFieldOperation",
        std::function<AddScalarFieldOperation*()>([]() -> AddScalarFieldOperation* { return nullptr; }), true);
    (void)addScalarOperation;

    auto fieldTool = table.addClass<FieldBrushTool>(
        "FieldBrushTool", std::function<FieldBrushTool*()>([]() -> FieldBrushTool* { return nullptr; }), true);
    fieldTool.addFunc("setRadius", &FieldBrushTool::setRadius);
    fieldTool.addFunc("setStrength", &FieldBrushTool::setStrength);
    fieldTool.addFunc("getRadius", &FieldBrushTool::radius);
    fieldTool.addFunc("getStrength", &FieldBrushTool::strength);
    fieldTool.addFunc("setCircleKernel", [](FieldBrushTool* self, CircleBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    fieldTool.addFunc("setBoxKernel", [](FieldBrushTool* self, BoxBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    fieldTool.addFunc("setPaintIntOperation", [](FieldBrushTool* self, PaintIntFieldOperation* operation) {
        if (self) self->setOperation(operation);
    });
    fieldTool.addFunc("setAddScalarOperation", [](FieldBrushTool* self, AddScalarFieldOperation* operation) {
        if (self) self->setOperation(operation);
    });

    auto sphereVolumeKernel = table.addClass<SphereVolumeBrushKernel>(
        "SphereVolumeBrushKernel",
        std::function<SphereVolumeBrushKernel*()>([]() -> SphereVolumeBrushKernel* { return nullptr; }), true);
    sphereVolumeKernel.addFunc("setConstantFalloff", [](SphereVolumeBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    sphereVolumeKernel.addFunc("setLinearFalloff", [](SphereVolumeBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    sphereVolumeKernel.addFunc("setSmoothFalloff", [](SphereVolumeBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto boxVolumeKernel = table.addClass<BoxVolumeBrushKernel>(
        "BoxVolumeBrushKernel",
        std::function<BoxVolumeBrushKernel*()>([]() -> BoxVolumeBrushKernel* { return nullptr; }), true);
    boxVolumeKernel.addFunc("setConstantFalloff", [](BoxVolumeBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxVolumeKernel.addFunc("setLinearFalloff", [](BoxVolumeBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxVolumeKernel.addFunc("setSmoothFalloff", [](BoxVolumeBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto paintVolumeOperation = table.addClass<PaintIntVolumeOperation>(
        "PaintIntVolumeOperation",
        std::function<PaintIntVolumeOperation*()>([]() -> PaintIntVolumeOperation* { return nullptr; }), true);
    paintVolumeOperation.addFunc("setValue", &PaintIntVolumeOperation::setValue);
    paintVolumeOperation.addFunc("getValue", &PaintIntVolumeOperation::value);

    auto volumeTool = table.addClass<VolumeBrushTool>(
        "VolumeBrushTool", std::function<VolumeBrushTool*()>([]() -> VolumeBrushTool* { return nullptr; }), true);
    volumeTool.addFunc("setRadius", &VolumeBrushTool::setRadius);
    volumeTool.addFunc("getRadius", &VolumeBrushTool::radius);
    volumeTool.addFunc("setSphereKernel", [](VolumeBrushTool* self, SphereVolumeBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    volumeTool.addFunc("setBoxKernel", [](VolumeBrushTool* self, BoxVolumeBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    volumeTool.addFunc("setPaintIntOperation", [](VolumeBrushTool* self, PaintIntVolumeOperation* operation) {
        if (self) self->setOperation(operation);
    });

    auto tileTarget = table.addClass<TileBufferTarget>(
        "TileBufferTarget", std::function<TileBufferTarget*()>([]() -> TileBufferTarget* { return nullptr; }), true);
    tileTarget.addFunc("getTargetId", [](TileBufferTarget* self) { return self ? self->targetId() : std::string{}; });
    tileTarget.addFunc("getRevision", [](TileBufferTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    tileTarget.addFunc("getWidth", &TileBufferTarget::width);
    tileTarget.addFunc("getHeight", &TileBufferTarget::height);
    tileTarget.addFunc("readInt", &TileBufferTarget::readInt);
    tileTarget.addFunc("writeInt", &TileBufferTarget::writeInt);
    tileTarget.addFunc("clearDirtyRegion", &TileBufferTarget::clearDirtyRegion);

#ifdef EVENGINE_HAS_MAP
    auto tileLayerTarget = table.addClass<TileLayerTarget>(
        "TileLayerTarget", std::function<TileLayerTarget*()>([]() -> TileLayerTarget* { return nullptr; }), true);
    tileLayerTarget.addFunc("getTargetId",
                            [](TileLayerTarget* self) { return self ? self->targetId() : std::string{}; });
    tileLayerTarget.addFunc("getRevision", [](TileLayerTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    tileLayerTarget.addFunc("getWidth", &TileLayerTarget::width);
    tileLayerTarget.addFunc("getHeight", &TileLayerTarget::height);
    tileLayerTarget.addFunc("readInt", &TileLayerTarget::readInt);
    tileLayerTarget.addFunc("writeInt", &TileLayerTarget::writeInt);
    tileLayerTarget.addFunc("clearDirtyRegion", &TileLayerTarget::clearDirtyRegion);
#endif

#ifdef EVENGINE_HAS_PROCGEN
    auto heightmapTarget = table.addClass<HeightmapTarget>(
        "HeightmapTarget", std::function<HeightmapTarget*()>([]() -> HeightmapTarget* { return nullptr; }), true);
    heightmapTarget.addFunc("getTargetId",
                            [](HeightmapTarget* self) { return self ? self->targetId() : std::string{}; });
    heightmapTarget.addFunc("getRevision", [](HeightmapTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    heightmapTarget.addFunc("getWidth", &HeightmapTarget::width);
    heightmapTarget.addFunc("getHeight", &HeightmapTarget::height);
    heightmapTarget.addFunc("readScalar", &HeightmapTarget::readScalar);
    heightmapTarget.addFunc("writeScalar", &HeightmapTarget::writeScalar);
    heightmapTarget.addFunc("sampleScalar", &HeightmapTarget::sampleScalar);
    heightmapTarget.addFunc("clearDirtyRegion", &HeightmapTarget::clearDirtyRegion);
#endif

#ifdef EVENGINE_HAS_VOXEL
    auto voxelTarget = table.addClass<VoxelWorldTarget>(
        "VoxelWorldTarget", std::function<VoxelWorldTarget*()>([]() -> VoxelWorldTarget* { return nullptr; }), true);
    voxelTarget.addFunc("getTargetId", [](VoxelWorldTarget* self) { return self ? self->targetId() : std::string{}; });
    voxelTarget.addFunc("getRevision", [](VoxelWorldTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    voxelTarget.addFunc("readInt3", &VoxelWorldTarget::readInt3);
    voxelTarget.addFunc("writeInt3", &VoxelWorldTarget::writeInt3);
    voxelTarget.addFunc("clearDirtyVolume", &VoxelWorldTarget::clearDirtyVolume);
    voxelTarget.addFunc("getDirtyMinX", [](VoxelWorldTarget* self) { return self ? self->dirtyVolume().minX : 0; });
    voxelTarget.addFunc("getDirtyMinY", [](VoxelWorldTarget* self) { return self ? self->dirtyVolume().minY : 0; });
    voxelTarget.addFunc("getDirtyMinZ", [](VoxelWorldTarget* self) { return self ? self->dirtyVolume().minZ : 0; });
    voxelTarget.addFunc("getDirtyMaxX", [](VoxelWorldTarget* self) { return self ? self->dirtyVolume().maxX : -1; });
    voxelTarget.addFunc("getDirtyMaxY", [](VoxelWorldTarget* self) { return self ? self->dirtyVolume().maxY : -1; });
    voxelTarget.addFunc("getDirtyMaxZ", [](VoxelWorldTarget* self) { return self ? self->dirtyVolume().maxZ : -1; });
#endif

    auto session = table.addClass<EditorSession>(
        "EditorSession", std::function<EditorSession*()>([]() -> EditorSession* { return nullptr; }), true);
    session.addFunc("addTool", std::function<bool(EditorSession*, ScriptEditorTool*)>(
                                   [](EditorSession* self, ScriptEditorTool* tool) { return self->addTool(tool); }));
    session.addFunc("addFieldTool",
                    [](EditorSession* self, FieldBrushTool* tool) { return self && self->addTool(tool); });
    session.addFunc("addVolumeTool",
                    [](EditorSession* self, VolumeBrushTool* tool) { return self && self->addTool(tool); });
    session.addFunc("bindTileBufferTarget", [](EditorSession* self, TileBufferTarget* target) {
        if (self) self->bindTarget(target);
    });
#ifdef EVENGINE_HAS_MAP
    session.addFunc("bindTileLayerTarget", [](EditorSession* self, TileLayerTarget* target) {
        if (self) self->bindTarget(target);
    });
#endif
#ifdef EVENGINE_HAS_VOXEL
    session.addFunc("bindVoxelWorldTarget", [](EditorSession* self, VoxelWorldTarget* target) {
        if (self) self->bindTarget(target);
    });
#endif
#ifdef EVENGINE_HAS_PROCGEN
    session.addFunc("bindHeightmapTarget", [](EditorSession* self, HeightmapTarget* target) {
        if (self) self->bindTarget(target);
    });
#endif
    session.addFunc("clearTarget", [](EditorSession* self) {
        if (self) self->bindTarget(nullptr);
    });
    session.addFunc("removeTool", &EditorSession::removeTool);
    session.addFunc("clearTools", &EditorSession::clearTools);
    session.addFunc("activateTool", &EditorSession::activateTool);
    session.addFunc("getToolCount", &EditorSession::getToolCount);
    session.addFunc("getActiveToolId", &EditorSession::activeToolId);
    session.addFunc("hasPointerCapture", &EditorSession::hasPointerCapture);
    session.addFunc("update", &EditorSession::update);
    session.addFunc("cancelActiveTool", &EditorSession::cancelActiveTool);
    session.addFunc(
        "undo", std::function<bool(EditorSession*)>([](EditorSession* self) { return self->transactions().undo(); }));
    session.addFunc(
        "redo", std::function<bool(EditorSession*)>([](EditorSession* self) { return self->transactions().redo(); }));
    session.addFunc("canUndo", [](EditorSession* self) { return self && self->transactions().canUndo(); });
    session.addFunc("canRedo", [](EditorSession* self) { return self && self->transactions().canRedo(); });
    session.addFunc("clearHistory", [](EditorSession* self) {
        if (self) self->transactions().clear();
    });
    session.addFunc("getCommandCount",
                    [](EditorSession* self) { return self ? static_cast<int>(self->availableCommands().size()) : 0; });
    session.addFunc("getCommandId", [](EditorSession* self, int index) {
        if (!self) return std::string{};
        const auto commands = self->availableCommands();
        return index >= 0 && index < static_cast<int>(commands.size()) ? commands[static_cast<size_t>(index)].id.value()
                                                                       : std::string{};
    });
    session.addFunc("getCommandName", [](EditorSession* self, int index) {
        if (!self) return std::string{};
        const auto commands = self->availableCommands();
        return index >= 0 && index < static_cast<int>(commands.size())
                   ? commands[static_cast<size_t>(index)].displayName
                   : std::string{};
    });
    session.addFunc("getCommandCategory", [](EditorSession* self, int index) {
        if (!self) return std::string{};
        const auto commands = self->availableCommands();
        return index >= 0 && index < static_cast<int>(commands.size()) ? commands[static_cast<size_t>(index)].category
                                                                       : std::string{};
    });
    session.addFunc("planCommand", [](EditorSession* self, const std::string& id, ssq::Object payload) -> ssq::Object {
        HSQUIRRELVM vm = payload.getHandle();
        EditorValue value;
        if (!self || !objectToEditorValue(payload, value)) return resultTable(vm, invalidScriptPayload());
        const EditorResult<PlanId> planned = self->retainPlan(CommandId(id), value, CommandSource::Script);
        ssq::Table                 out     = resultTable(vm, planned);
        if (planned.value) out.set("planId", planned.value->value());
        return out;
    });
    session.addFunc(
        "executePlan", [](EditorSession* self, const std::string& planId, ssq::Object scriptContext) -> ssq::Object {
            HSQUIRRELVM vm = scriptContext.getHandle();
            if (!self)
                return resultTable(vm, EditorResult<TransactionReceipt>::error(EditorStatus::Failed,
                                                                               RuleId("editor.script.missing-session"),
                                                                               "Editor session is not available"));
            const EditorResult<TransactionReceipt> executed =
                self->executeRetainedPlan(PlanId(planId), CommandSource::Script);
            ssq::Table out = resultTable(vm, executed);
            if (executed.value) {
                out.set("transactionId", executed.value->id.value());
                out.set("transactionState", std::string(transactionStateName(executed.value->state)));
                out.set("beforeRevision", static_cast<int64_t>(executed.value->beforeRevision));
                out.set("afterRevision", static_cast<int64_t>(executed.value->afterRevision));
                out.set("authorityReceipt", executed.value->authorityReceipt);
            }
            return out;
        });
    session.addFunc("executeCommand",
                    [](EditorSession* self, const std::string& id, ssq::Object payload) -> ssq::Object {
                        HSQUIRRELVM vm = payload.getHandle();
                        EditorValue value;
                        if (!self || !objectToEditorValue(payload, value))
                            return resultTable(vm, invalidScriptPayload());
                        const EditorResult<EditorValue> executed =
                            self->executeCommand(CommandId(id), value, CommandSource::Script);
                        ssq::Table out = resultTable(vm, executed);
                        if (executed.value) setValue(out, "value", *executed.value);
                        return out;
                    });
    session.addFunc("dispatchPointer",
                    std::function<int(EditorSession*, int, int, int, float, float, float, float, float)>(
                        [](EditorSession* self, int phase, int pointerId, int button, float x, float y, float dx,
                           float dy, float pressure) {
                            EditorPointerEvent event;
                            event.phase                 = static_cast<EditorPointerEvent::Phase>(phase);
                            event.pointerId             = pointerId;
                            event.button                = button;
                            event.x                     = x;
                            event.y                     = y;
                            event.deltaX                = dx;
                            event.deltaY                = dy;
                            event.pressure              = pressure;
                            const ToolResponse response = self->dispatchPointer(event);
                            return (response.handled ? 1 : 0) | (response.capturePointer ? 2 : 0) |
                                   (response.releasePointer ? 4 : 0);
                        }));
    session.addFunc("dispatchPointer3D",
                    std::function<int(EditorSession*, int, int, int, float, float, float, float, float, float, float)>(
                        [](EditorSession* self, int phase, int pointerId, int button, float x, float y, float z,
                           float dx, float dy, float dz, float pressure) {
                            EditorPointerEvent event;
                            event.phase                 = static_cast<EditorPointerEvent::Phase>(phase);
                            event.pointerId             = pointerId;
                            event.button                = button;
                            event.x                     = x;
                            event.y                     = y;
                            event.z                     = z;
                            event.deltaX                = dx;
                            event.deltaY                = dy;
                            event.deltaZ                = dz;
                            event.pressure              = pressure;
                            const ToolResponse response = self->dispatchPointer(event);
                            return (response.handled ? 1 : 0) | (response.capturePointer ? 2 : 0) |
                                   (response.releasePointer ? 4 : 0);
                        }));

    auto workspace = table.addClass<EditorWorkspace>(
        "EditorWorkspace", std::function<EditorWorkspace*()>([]() -> EditorWorkspace* { return nullptr; }), true);
    workspace.addFunc("getId", &EditorWorkspace::getId);
    workspace.addFunc("getTitle", &EditorWorkspace::getTitle);
    workspace.addFunc("setTitle", &EditorWorkspace::setTitle);
    workspace.addFunc("registerPanel", &EditorWorkspace::registerPanel);
    workspace.addFunc("removePanel", &EditorWorkspace::removePanel);
    workspace.addFunc("clearPanels", &EditorWorkspace::clearPanels);
    workspace.addFunc("movePanel", &EditorWorkspace::movePanel);
    workspace.addFunc("setPanelCapability", &EditorWorkspace::setPanelCapability);
    workspace.addFunc("setPanelContext", &EditorWorkspace::setPanelContext);
    workspace.addFunc("setPanelVisible", &EditorWorkspace::setPanelVisible);
    workspace.addFunc("setPanelSingleton", &EditorWorkspace::setPanelSingleton);
    workspace.addFunc("activatePanel", &EditorWorkspace::activatePanel);
    workspace.addFunc("getActivePanel", &EditorWorkspace::getActivePanel);
    workspace.addFunc("getPanelCount", &EditorWorkspace::getPanelCount);
    workspace.addFunc("getPanelId", &EditorWorkspace::getPanelId);
    workspace.addFunc("getPanelTitle", &EditorWorkspace::getPanelTitle);
    workspace.addFunc("getPanelRegion", &EditorWorkspace::getPanelRegion);
    workspace.addFunc("getPanelCapability", &EditorWorkspace::getPanelCapability);
    workspace.addFunc("getPanelContext", &EditorWorkspace::getPanelContext);
    workspace.addFunc("getPanelOrder", &EditorWorkspace::getPanelOrder);
    workspace.addFunc("getPanelVisible", &EditorWorkspace::getPanelVisible);
    workspace.addFunc("getPanelSingleton", &EditorWorkspace::getPanelSingleton);
    workspace.addFunc("setRegionSize", &EditorWorkspace::setRegionSize);
    workspace.addFunc("layout", &EditorWorkspace::layout);
    workspace.addFunc("getRegionX", &EditorWorkspace::getRegionX);
    workspace.addFunc("getRegionY", &EditorWorkspace::getRegionY);
    workspace.addFunc("getRegionW", &EditorWorkspace::getRegionW);
    workspace.addFunc("getRegionH", &EditorWorkspace::getRegionH);
    workspace.addFunc("setMode", &EditorWorkspace::setMode);
    workspace.addFunc("getMode", &EditorWorkspace::getMode);
    workspace.addFunc("select", &EditorWorkspace::select);
    workspace.addFunc("clearSelection", &EditorWorkspace::clearSelection);
    workspace.addFunc("getSelectionCount", &EditorWorkspace::getSelectionCount);
    workspace.addFunc("getSelectionItem", &EditorWorkspace::getSelectionItem);
    workspace.addFunc("getSelectionType", &EditorWorkspace::getSelectionType);
    workspace.addFunc("getPrimarySelection", &EditorWorkspace::getPrimarySelection);
    workspace.addFunc("getSelectionSequence", &EditorWorkspace::getSelectionSequence);
    workspace.addFunc("focus", &EditorWorkspace::focus);
    workspace.addFunc("getFocusedSurface", &EditorWorkspace::getFocusedSurface);
    workspace.addFunc("getRevision", &EditorWorkspace::getRevision);

    auto scriptTool = table.addClass<ScriptEditorTool>(
        "ScriptEditorTool", std::function<ScriptEditorTool*()>([]() -> ScriptEditorTool* { return nullptr; }), true);
    scriptTool.addFunc("setShortcut", &ScriptEditorTool::setShortcut);
    scriptTool.addFunc("setActivateCallback", &ScriptEditorTool::setActivateCallback);
    scriptTool.addFunc("setDeactivateCallback", &ScriptEditorTool::setDeactivateCallback);
    scriptTool.addFunc("setPointerCallback", &ScriptEditorTool::setPointerCallback);
    scriptTool.addFunc("setKeyCallback", &ScriptEditorTool::setKeyCallback);
    scriptTool.addFunc("setUpdateCallback", &ScriptEditorTool::setUpdateCallback);
    scriptTool.addFunc("setCancelCallback", &ScriptEditorTool::setCancelCallback);
}

void Editor::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Editor::getName);
    cls.addFunc("newGizmo", &Editor::newGizmo);
    cls.addFunc("newGizmoManager", &Editor::newGizmoManager);
    cls.addFunc("newReflectionProbeVisualizer", &Editor::newReflectionProbeVisualizer);
    cls.addFunc("newTileBuffer", &Editor::newTileBuffer);
    cls.addFunc("newBrush", &Editor::newBrush);
    cls.addFunc("newToolbar", &Editor::newToolbar);
    cls.addFunc("newInspector", &Editor::newInspector);
    cls.addFunc("newDock", &Editor::newDock);
    cls.addFunc("newHistory", &Editor::newHistory);
    cls.addFunc("newSession", &Editor::newSession);
    cls.addFunc("newWorkspace", &Editor::newWorkspace);
    cls.addFunc("newScriptTool", &Editor::newScriptTool);
    cls.addFunc("newConstantBrushFalloff", &Editor::newConstantBrushFalloff);
    cls.addFunc("newLinearBrushFalloff", &Editor::newLinearBrushFalloff);
    cls.addFunc("newSmoothBrushFalloff", &Editor::newSmoothBrushFalloff);
    cls.addFunc("newCircleBrushKernel", &Editor::newCircleBrushKernel);
    cls.addFunc("newBoxBrushKernel", &Editor::newBoxBrushKernel);
    cls.addFunc("newPaintIntFieldOperation", &Editor::newPaintIntFieldOperation);
    cls.addFunc("newAddScalarFieldOperation", &Editor::newAddScalarFieldOperation);
    cls.addFunc("newFieldBrushTool", &Editor::newFieldBrushTool);
    cls.addFunc("newSphereVolumeBrushKernel", &Editor::newSphereVolumeBrushKernel);
    cls.addFunc("newBoxVolumeBrushKernel", &Editor::newBoxVolumeBrushKernel);
    cls.addFunc("newPaintIntVolumeOperation", &Editor::newPaintIntVolumeOperation);
    cls.addFunc("newVolumeBrushTool", &Editor::newVolumeBrushTool);
    cls.addFunc("newTileBufferTarget", &Editor::newTileBufferTarget);
#ifdef EVENGINE_HAS_MAP
    cls.addFunc("newTileLayerTarget", &Editor::newTileLayerTarget);
#endif
#ifdef EVENGINE_HAS_VOXEL
    cls.addFunc("newVoxelWorldTarget", &Editor::newVoxelWorldTarget);
#endif
    cls.addFunc("registerScriptCommand", registerScriptCommand);
    cls.addFunc("unregisterScriptCommand", [](Editor* self, const std::string& id) {
        return self && self->commandService().unregisterCommand(CommandId(id), "script:" + id);
    });
#ifdef EVENGINE_HAS_PROCGEN
    cls.addFunc("newHeightmapTarget", &Editor::newHeightmapTarget);
    cls.addFunc("newHeightmapMesh", &Editor::newHeightmapMesh);
    cls.addFunc("updateHeightmapMesh", &Editor::updateHeightmapMesh);
    cls.addFunc("newHeightmapMeshSmooth", &Editor::newHeightmapMeshSmooth);
    cls.addFunc("updateHeightmapMeshSmooth", &Editor::updateHeightmapMeshSmooth);
    cls.addFunc("applyHeightmapBrush", &Editor::applyHeightmapBrush);
#endif
}

}  // namespace eve::editor
