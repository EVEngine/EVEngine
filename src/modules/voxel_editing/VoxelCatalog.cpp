#include "voxel_editing/VoxelCatalog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

namespace eve::voxel_editing {
namespace {

template <class T>
EditorResult<T> fail(EditorStatus s, const char* r, std::string m) {
    return eve::editing::failed<T>(s, RuleId(r), std::move(m));
}

const EditorValue* field(const EditorValue& v, const char* k) {
    const auto* o = v.getIf<EditorValue::Object>();
    if (!o) return nullptr;
    const auto i = o->find(k);
    return i == o->end() ? nullptr : &i->second;
}

bool errors(const std::vector<EditorDiagnostic>& d) {
    return std::any_of(d.begin(), d.end(), [](const auto& v) { return v.severity() == DiagnosticSeverity::Error; });
}

int pack(int x, int y, int z) { return x | (y << 8) | (z << 16); }

bool inBounds(const VoxelModelValue& model, int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < model.sizeX && y < model.sizeY && z < model.sizeZ;
}

bool validSize(int n) { return n >= 1 && n <= kVoxelModelMaxSize; }

EditorValue socketValue(const VoxelSocket& s) {
    return EditorValue::Object{{"tag", s.tag}, {"kind", std::string(voxelSocketKindName(s.kind))}};
}

EditorResult<VoxelSocket> parseSocket(const EditorValue& v) {
    const auto* tag   = field(v, "tag");
    const auto* kind  = field(v, "kind");
    const auto* tags  = tag ? tag->getIf<std::string>() : nullptr;
    const auto* kinds = kind ? kind->getIf<std::string>() : nullptr;
    if (!tags || !kinds)
        return fail<VoxelSocket>(EditorStatus::Rejected, "editor.voxel-model.socket",
                                 "Voxel socket requires tag and kind");
    VoxelSocket out;
    out.tag  = *tags;
    out.kind = voxelSocketKindFromName(*kinds);
    if (*kinds != voxelSocketKindName(out.kind) && !kinds->empty())
        return fail<VoxelSocket>(EditorStatus::Rejected, "editor.voxel-model.socket-kind", "Unknown voxel socket kind");
    return eve::editing::applied<VoxelSocket>(std::move(out));
}

EditorResult<std::array<VoxelSocket, 6>> parseSockets(const EditorValue* value) {
    const auto* a = value ? value->getIf<EditorValue::Array>() : nullptr;
    if (!a || a->size() != 6)
        return fail<std::array<VoxelSocket, 6>>(EditorStatus::Rejected, "editor.voxel-model.sockets",
                                                "Voxel sockets require six faces");
    std::array<VoxelSocket, 6> out{};
    for (int i = 0; i < 6; ++i) {
        auto parsed = parseSocket((*a)[static_cast<std::size_t>(i)]);
        if (!parsed.ok())
            return fail<std::array<VoxelSocket, 6>>(parsed.code(), "editor.voxel-model.socket",
                                                    "Voxel socket cannot be parsed");
        out[static_cast<std::size_t>(i)] = parsed.value();
    }
    return eve::editing::applied<std::array<VoxelSocket, 6>>(out);
}

EditorValue voxelsValue(const std::vector<VoxelCoord>& voxels) {
    EditorValue::Array a;
    a.reserve(voxels.size());
    for (const auto& v : voxels) a.push_back(int64_t{pack(v.x, v.y, v.z)});
    return a;
}

EditorResult<std::vector<VoxelCoord>> parseVoxels(const EditorValue* value) {
    const auto* a = value ? value->getIf<EditorValue::Array>() : nullptr;
    if (!a)
        return fail<std::vector<VoxelCoord>>(EditorStatus::Rejected, "editor.voxel-model.voxels",
                                             "Voxel occupancy must be an array");
    if (a->size() > static_cast<std::size_t>(kVoxelModelMaxOccupied))
        return fail<std::vector<VoxelCoord>>(EditorStatus::Rejected, "editor.voxel-model.voxels-capacity",
                                             "Model occupancy exceeds 4096 cells");
    std::set<int> unique;
    std::vector<VoxelCoord> out;
    out.reserve(a->size());
    for (const auto& entry : *a) {
        const auto* n = entry.getIf<int64_t>();
        if (!n || *n < 0)
            return fail<std::vector<VoxelCoord>>(EditorStatus::Rejected, "editor.voxel-model.voxel",
                                                 "Voxel cell must be a packed non-negative integer");
        VoxelCoord coord;
        coord.x = static_cast<int>(*n) & 255;
        coord.y = (static_cast<int>(*n) >> 8) & 255;
        coord.z = (static_cast<int>(*n) >> 16) & 255;
        if (!unique.insert(static_cast<int>(*n)).second)
            return fail<std::vector<VoxelCoord>>(EditorStatus::Rejected, "editor.voxel-model.voxel-dup",
                                                 "Voxel cells must be unique");
        out.push_back(coord);
    }
    return eve::editing::applied<std::vector<VoxelCoord>>(std::move(out));
}

const int64_t* intField(const EditorValue& v, const char* key) {
    const auto* f = field(v, key);
    return f ? f->getIf<int64_t>() : nullptr;
}

EditorValue modelValue(const VoxelModelValue& v) {
    EditorValue::Array sockets;
    for (const auto& s : v.sockets) sockets.push_back(socketValue(s));
    return EditorValue::Object{{"id", v.id.value()},
                               {"name", v.name},
                               {"sizeX", int64_t{v.sizeX}},
                               {"sizeY", int64_t{v.sizeY}},
                               {"sizeZ", int64_t{v.sizeZ}},
                               {"sockets", std::move(sockets)},
                               {"voxels", voxelsValue(v.voxels)}};
}

EditorResult<VoxelModelValue> parseModel(const EditorValue& v) {
    const auto *id = field(v, "id"), *name = field(v, "name");
    const auto* ids   = id ? id->getIf<std::string>() : nullptr;
    const auto* names = name ? name->getIf<std::string>() : nullptr;
    const auto* sx    = intField(v, "sizeX");
    const auto* sy    = intField(v, "sizeY");
    const auto* sz    = intField(v, "sizeZ");
    auto        sockets = parseSockets(field(v, "sockets"));
    auto        voxels  = parseVoxels(field(v, "voxels"));
    if (!ids || ids->empty() || !names || names->empty() || !sx || !sy || !sz || !sockets.ok() || !voxels.ok())
        return fail<VoxelModelValue>(EditorStatus::Rejected, "editor.voxel-model.entry",
                                     "Voxel model entry is invalid");
    VoxelModelValue out;
    out.id      = ObjectId(*ids);
    out.name    = *names;
    out.sizeX   = static_cast<int>(*sx);
    out.sizeY   = static_cast<int>(*sy);
    out.sizeZ   = static_cast<int>(*sz);
    out.sockets = sockets.value();
    out.voxels  = std::move(voxels).takeValue();
    return eve::editing::applied<VoxelModelValue>(std::move(out));
}

PropertyDescriptor prop(std::string path, PropertyType type, EditorValue value) {
    PropertyDescriptor d;
    d.path           = PropertyPath(path);
    d.displayNameKey = "editor.voxel-model." + path;
    d.category       = "voxel-model";
    d.type           = type;
    d.defaultValue   = std::move(value);
    d.flags          = PropertyFlag::Runtime;
    return d;
}

bool parseFacePath(const PropertyPath& path, int& face, std::string& fieldName) {
    const std::string& text = path.value();
    const std::string  head = "model.socket.";
    if (text.rfind(head, 0) != 0) return false;
    const auto rest = text.substr(head.size());
    const auto dot  = rest.find('.');
    if (dot == std::string::npos) return false;
    try {
        face = std::stoi(rest.substr(0, dot));
    } catch (...) {
        return false;
    }
    if (face < 0 || face > 5) return false;
    fieldName = rest.substr(dot + 1);
    return fieldName == "tag" || fieldName == "kind";
}

}  // namespace

VoxelSocketKind voxelSocketKindFromName(std::string_view name) {
    if (name == "symmetric") return VoxelSocketKind::Symmetric;
    if (name == "male") return VoxelSocketKind::Male;
    if (name == "female") return VoxelSocketKind::Female;
    return VoxelSocketKind::None;
}

const char* voxelSocketKindName(VoxelSocketKind kind) {
    switch (kind) {
        case VoxelSocketKind::Symmetric:
            return "symmetric";
        case VoxelSocketKind::Male:
            return "male";
        case VoxelSocketKind::Female:
            return "female";
        case VoxelSocketKind::None:
        default:
            return "none";
    }
}

bool canJoinVoxelSockets(const VoxelSocket& a, const VoxelSocket& b) {
    if (a.kind == VoxelSocketKind::None || b.kind == VoxelSocketKind::None) return false;
    if (a.tag.empty() || a.tag != b.tag) return false;
    if (a.kind == VoxelSocketKind::Symmetric && b.kind == VoxelSocketKind::Symmetric) return true;
    return (a.kind == VoxelSocketKind::Male && b.kind == VoxelSocketKind::Female) ||
           (a.kind == VoxelSocketKind::Female && b.kind == VoxelSocketKind::Male);
}

int voxelOppositeFace(int face) {
    static constexpr int opp[] = {1, 0, 3, 2, 5, 4};
    if (face < 0 || face > 5) return face;
    return opp[face];
}

bool isVoxelModelOccupied(const VoxelModelValue& model, int x, int y, int z) {
    return std::any_of(model.voxels.begin(), model.voxels.end(),
                       [&](const auto& v) { return v.x == x && v.y == y && v.z == z; });
}

VoxelCellFill voxelClassifyModelFill(const VoxelModelValue& model) {
    if (model.voxels.empty()) return VoxelCellFill::Empty;
    const int volume = model.sizeX * model.sizeY * model.sizeZ;
    if (volume > 0 && static_cast<int>(model.voxels.size()) == volume) return VoxelCellFill::Filled;
    return VoxelCellFill::Partial;
}

VoxelPick pickVoxelModel(const VoxelModelValue& model, float ox, float oy, float oz, float dx, float dy, float dz,
                         float maxDistance) {
    VoxelPick pick;
    const float lenSq = dx * dx + dy * dy + dz * dz;
    if (lenSq <= 1e-12f || maxDistance <= 0.f) return pick;
    const float invLen = 1.f / std::sqrt(lenSq);
    const float rx     = dx * invLen;
    const float ry     = dy * invLen;
    const float rz     = dz * invLen;
    int         ix     = static_cast<int>(std::floor(ox));
    int         iy     = static_cast<int>(std::floor(oy));
    int         iz     = static_cast<int>(std::floor(oz));
    int         lastX  = ix;
    int         lastY  = iy;
    int         lastZ  = iz;
    bool        haveEmpty = false;
    int         emptyX = 0;
    int         emptyY = 0;
    int         emptyZ = 0;
    auto recordEmpty = [&](int x, int y, int z) {
        if (haveEmpty || !inBounds(model, x, y, z) || isVoxelModelOccupied(model, x, y, z)) return;
        haveEmpty = true;
        emptyX    = x;
        emptyY    = y;
        emptyZ    = z;
    };
    if (isVoxelModelOccupied(model, ix, iy, iz)) {
        pick.hit       = true;
        pick.canAttach = inBounds(model, ix, iy, iz);
        pick.hitX = pick.prevX = ix;
        pick.hitY = pick.prevY = iy;
        pick.hitZ = pick.prevZ = iz;
        return pick;
    }
    recordEmpty(ix, iy, iz);
    const float inf     = std::numeric_limits<float>::infinity();
    const int   stepX   = rx > 0.f ? 1 : -1;
    const int   stepY   = ry > 0.f ? 1 : -1;
    const int   stepZ   = rz > 0.f ? 1 : -1;
    const float absInvX = rx != 0.f ? std::fabs(1.f / rx) : inf;
    const float absInvY = ry != 0.f ? std::fabs(1.f / ry) : inf;
    const float absInvZ = rz != 0.f ? std::fabs(1.f / rz) : inf;
    float       tMaxX   = rx != 0.f ? (rx > 0.f ? float(ix + 1) - ox : ox - float(ix)) * absInvX : inf;
    float       tMaxY   = ry != 0.f ? (ry > 0.f ? float(iy + 1) - oy : oy - float(iy)) * absInvY : inf;
    float       tMaxZ   = rz != 0.f ? (rz > 0.f ? float(iz + 1) - oz : oz - float(iz)) * absInvZ : inf;
    for (int iter = 0; iter < 4096; ++iter) {
        float t = 0.f;
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            ix += stepX;
            t = tMaxX;
            tMaxX += absInvX;
        } else if (tMaxY <= tMaxZ) {
            iy += stepY;
            t = tMaxY;
            tMaxY += absInvY;
        } else {
            iz += stepZ;
            t = tMaxZ;
            tMaxZ += absInvZ;
        }
        if (t > maxDistance) break;
        if (isVoxelModelOccupied(model, ix, iy, iz)) {
            pick.hit  = true;
            pick.hitX = ix;
            pick.hitY = iy;
            pick.hitZ = iz;
            pick.prevX = lastX;
            pick.prevY = lastY;
            pick.prevZ = lastZ;
            pick.canAttach =
                inBounds(model, lastX, lastY, lastZ) && !isVoxelModelOccupied(model, lastX, lastY, lastZ);
            return pick;
        }
        recordEmpty(ix, iy, iz);
        lastX = ix;
        lastY = iy;
        lastZ = iz;
    }
    if (haveEmpty) {
        pick.canAttach = true;
        pick.prevX     = emptyX;
        pick.prevY     = emptyY;
        pick.prevZ     = emptyZ;
    }
    return pick;
}

VoxelCatalogTarget::VoxelCatalogTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor VoxelCatalogTarget::describe() const {
    return {TargetId(id_),
            "voxel-catalog",
            revision_,
            false,
            {propertyCapabilityId(), IEditingSnapshotProvider::editingCapabilityId()}};
}

void* VoxelCatalogTarget::queryCapability(const CapabilityId& c) {
    if (c == propertyCapabilityId()) return static_cast<IPropertyProvider*>(this);
    if (c == IEditingSnapshotProvider::editingCapabilityId()) return static_cast<IEditingSnapshotProvider*>(this);
    return nullptr;
}

const VoxelModelValue* VoxelCatalogTarget::findModel(const ObjectId& id) const {
    const auto it = std::find_if(models_.begin(), models_.end(), [&](const auto& v) { return v.id == id; });
    return it == models_.end() ? nullptr : &*it;
}

VoxelModelValue* VoxelCatalogTarget::findModelMut(const ObjectId& id) {
    return const_cast<VoxelModelValue*>(findModel(id));
}

bool VoxelCatalogTarget::matches(const SelectionSnapshot& s) const {
    if (s.items.empty()) return false;
    for (const auto& i : s.items) {
        if (i.target != TargetId(id_) || !findModel(ObjectId(i.item.value()))) return false;
    }
    return true;
}

eve::Result<eve::Revision> VoxelCatalogTarget::currentRevision(const SelectionSnapshot& s) const {
    if (!matches(s))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "Voxel catalog selection mismatch",
                                                                          "editor.voxel-model.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema VoxelCatalogTarget::schema(const SelectionSnapshot&) const {
    PropertySchema schema;
    schema.typeId = "voxel.model";
    schema.properties.push_back(prop("model.name", PropertyType::String, ""));
    for (int face = 0; face < 6; ++face) {
        schema.properties.push_back(prop("model.socket." + std::to_string(face) + ".tag", PropertyType::String, ""));
        schema.properties.push_back(
            prop("model.socket." + std::to_string(face) + ".kind", PropertyType::String, "none"));
    }
    return schema;
}

PropertyReadResult VoxelCatalogTarget::read(const SelectionSnapshot& s, const PropertyPath& p) const {
    if (!matches(s) || !schema(s).find(p)) return {};
    std::optional<EditorValue> common;
    for (const auto& i : s.items) {
        const auto* model = findModel(ObjectId(i.item.value()));
        if (!model) return {};
        EditorValue value;
        if (p == PropertyPath("model.name"))
            value = model->name;
        else {
            int         face = 0;
            std::string fieldName;
            if (!parseFacePath(p, face, fieldName)) return {};
            const auto& socket = model->sockets[static_cast<std::size_t>(face)];
            value              = fieldName == "tag" ? EditorValue(socket.tag)
                                                    : EditorValue(std::string(voxelSocketKindName(socket.kind)));
        }
        if (!common)
            common = value;
        else if (*common != value)
            return {PropertyReadState::Mixed, {}, {}};
    }
    return {PropertyReadState::Value, *common, {}};
}

EditorValue VoxelCatalogTarget::contentValue() const {
    EditorValue::Array models;
    for (const auto& v : models_) models.push_back(modelValue(v));
    return EditorValue::Object{{"models", std::move(models)}};
}

EditorResult<DomainOperation> VoxelCatalogTarget::replacement(EditorValue content, std::string property) const {
    DomainOperation op;
    op.type        = "voxel.catalog.replace.v1";
    op.inverseType = op.type;
    op.target      = TargetId(id_);
    op.payload     = std::move(content);
    op.inverse     = contentValue();
    op.hasInverse  = true;
    if (!property.empty()) op.affectedProperties.push_back(property);
    op.mergeKey = "voxel-catalog:" + id_ + ":" + (property.empty() ? "structure" : property);
    return eve::editing::applied<DomainOperation>(std::move(op));
}

EditorResult<DomainOperation> VoxelCatalogTarget::makeSet(const SelectionSnapshot& s, const PropertyPath& p,
                                                          const EditorValue& value, PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(s, p);
    if (!matches(s) || !schema(s).find(p) || mode != PropertySetMode::Absolute)
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.set",
                                     "Voxel model property edit is invalid");
    auto c = *this;
    for (const auto& i : s.items) {
        auto* model = c.findModelMut(ObjectId(i.item.value()));
        if (!model) continue;
        if (p == PropertyPath("model.name")) {
            const auto* name = value.getIf<std::string>();
            if (!name || name->empty())
                return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.name",
                                             "Model name must be non-empty");
            model->name = *name;
        } else {
            int         face = 0;
            std::string fieldName;
            if (!parseFacePath(p, face, fieldName))
                return fail<DomainOperation>(EditorStatus::Unsupported, "editor.voxel-model.property",
                                             "Unknown voxel model property");
            auto& socket = model->sockets[static_cast<std::size_t>(face)];
            if (fieldName == "tag") {
                const auto* tag = value.getIf<std::string>();
                if (!tag)
                    return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.tag",
                                                 "Socket tag must be a string");
                socket.tag = *tag;
            } else {
                const auto* kind = value.getIf<std::string>();
                if (!kind)
                    return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.kind",
                                                 "Socket kind must be a string");
                socket.kind = voxelSocketKindFromName(*kind);
                if (*kind != voxelSocketKindName(socket.kind))
                    return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.kind",
                                                 "Unknown voxel socket kind");
            }
        }
    }
    if (errors(c.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.invalid",
                                     "Voxel model property edit failed validation");
    return replacement(c.contentValue(), p.value());
}

EditorResult<DomainOperation> VoxelCatalogTarget::makeReset(const SelectionSnapshot& s, const PropertyPath& p) const {
    auto d = schema(s).find(p);
    if (!d)
        return fail<DomainOperation>(EditorStatus::Unsupported, "editor.voxel-model.property",
                                     "Unknown voxel model property");
    return makeSet(s, p, d->defaultValue, PropertySetMode::Absolute);
}

EditorResult<DomainOperation> VoxelCatalogTarget::makeCreateModel(const VoxelModelValue& v) const {
    if (v.id.empty() || v.name.empty() || findModel(v.id) ||
        std::any_of(models_.begin(), models_.end(), [&](const auto& x) { return x.name == v.name; }))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.identity",
                                     "Voxel model ID and name must be unique");
    auto c = *this;
    c.models_.push_back(v);
    if (errors(c.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.invalid",
                                     "Voxel model create failed validation");
    return replacement(c.contentValue());
}

EditorResult<DomainOperation> VoxelCatalogTarget::makeDeleteModel(const ObjectId& id) const {
    if (!findModel(id))
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.voxel-model.entry", "Voxel model does not exist");
    auto c = *this;
    std::erase_if(c.models_, [&](const auto& v) { return v.id == id; });
    return replacement(c.contentValue());
}

EditorResult<DomainOperation> VoxelCatalogTarget::makeSetVoxel(const ObjectId& model, int x, int y, int z,
                                                               bool occupied) const {
    const auto* current = findModel(model);
    if (!current)
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.voxel-model.entry", "Voxel model does not exist");
    if (!inBounds(*current, x, y, z))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.range",
                                     "Voxel is outside the model bounds");
    auto  c    = *this;
    auto* dest = c.findModelMut(model);
    const auto it =
        std::find_if(dest->voxels.begin(), dest->voxels.end(), [&](const auto& v) { return v.x == x && v.y == y && v.z == z; });
    if (occupied && it == dest->voxels.end())
        dest->voxels.push_back({x, y, z});
    else if (!occupied && it != dest->voxels.end())
        dest->voxels.erase(it);
    if (errors(c.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.voxel-model.invalid",
                                     "Voxel occupancy edit failed validation");
    return replacement(c.contentValue(), "model.voxels");
}

std::vector<EditorDiagnostic> VoxelCatalogTarget::validate() const {
    std::vector<EditorDiagnostic> d;
    std::set<std::string>         ids, names;
    for (const auto& model : models_) {
        if (model.id.empty() || model.name.empty() || !ids.insert(model.id.value()).second ||
            !names.insert(model.name).second)
            d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::Conflict, RuleId("editor.voxel-model.identity"),
                                                     DiagnosticSeverity::Error,
                                                     "Voxel model IDs and names must be non-empty and unique"));
        if (!validSize(model.sizeX) || !validSize(model.sizeY) || !validSize(model.sizeZ))
            d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                     RuleId("editor.voxel-model.size"), DiagnosticSeverity::Error,
                                                     "Model size must be 1..32 on each axis"));
        std::set<int> unique;
        for (const auto& v : model.voxels) {
            if (!inBounds(model, v.x, v.y, v.z) || !unique.insert(pack(v.x, v.y, v.z)).second)
                d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument,
                                                         RuleId("editor.voxel-model.voxel"), DiagnosticSeverity::Error,
                                                         "Occupied cells must be unique and inside the model"));
        }
        if (static_cast<int>(model.voxels.size()) > kVoxelModelMaxOccupied)
            d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                     RuleId("editor.voxel-model.capacity"), DiagnosticSeverity::Error,
                                                     "Model occupancy exceeds 4096 cells"));
        if (model.voxels.empty())
            d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                     RuleId("editor.voxel-model.empty"), DiagnosticSeverity::Warning,
                                                     "Voxel model has no occupied cells"));
    }
    if (models_.empty())
        d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                 RuleId("editor.voxel-model.empty-project"), DiagnosticSeverity::Warning,
                                                 "Voxel project has no models"));
    return d;
}

EditorResult<void> VoxelCatalogTarget::applyDomainOperation(const DomainOperation& op) {
    if (op.target != TargetId(id_) || op.type != "voxel.catalog.replace.v1")
        return fail<void>(EditorStatus::Rejected, "editor.voxel-model.operation", "Voxel catalog operation mismatch");
    const auto* models    = field(op.payload, "models");
    const auto* modelArray = models ? models->getIf<EditorValue::Array>() : nullptr;
    if (!modelArray || !op.payload.isWithinLimits(8, 20000, 1024 * 1024))
        return fail<void>(EditorStatus::Rejected, "editor.voxel-model.payload", "Voxel catalog payload exceeds limits");
    VoxelCatalogTarget c(id_);
    for (const auto& v : *modelArray) {
        auto parsed = parseModel(v);
        if (!parsed.ok())
            return fail<void>(EditorStatus::Rejected, "editor.voxel-model.entry", "Voxel model cannot be parsed");
        c.models_.push_back(parsed.value());
    }
    if (errors(c.validate()))
        return fail<void>(EditorStatus::Rejected, "editor.voxel-model.invalid", "Voxel catalog validation failed");
    models_ = std::move(c.models_);
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> VoxelCatalogTarget::cloneDomainState() const {
    return std::make_unique<VoxelCatalogTarget>(*this);
}

EditorResult<void> VoxelCatalogTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> c) {
    auto* t = dynamic_cast<VoxelCatalogTarget*>(c.get());
    if (!t || t->id_ != id_)
        return fail<void>(EditorStatus::Conflict, "editor.voxel-model.candidate", "Voxel catalog candidate mismatch");
    *this = *t;
    return eve::editing::applied<void>();
}

EditorValue VoxelCatalogTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"content", contentValue()}};
}

EditorResult<void> VoxelCatalogTarget::loadSnapshot(const EditorValue& s) {
    const auto *v = field(s, "schemaVersion"), *content = field(s, "content");
    const auto* version = v ? v->getIf<int64_t>() : nullptr;
    if (!version || *version != 1 || !content)
        return fail<void>(EditorStatus::Unsupported, "editor.voxel-model.snapshot",
                          "Unsupported Voxel catalog snapshot");
    DomainOperation op;
    op.target   = TargetId(id_);
    op.type     = "voxel.catalog.replace.v1";
    op.payload  = *content;
    auto result = applyDomainOperation(op);
    if (result.ok()) dirty_.clear();
    return result;
}

std::vector<ObjectId> VoxelCatalogTarget::hullJoinPartners(const ObjectId& model, int face) const {
    std::vector<ObjectId> partners;
    const auto*           self = findModel(model);
    if (!self || face < 0 || face > 5) return partners;
    const auto opposite = voxelOppositeFace(face);
    const auto& socket  = self->sockets[static_cast<std::size_t>(face)];
    for (const auto& other : models_) {
        if (other.id == model) continue;
        if (canJoinVoxelSockets(socket, other.sockets[static_cast<std::size_t>(opposite)]))
            partners.push_back(other.id);
    }
    return partners;
}

}  // namespace eve::voxel_editing
