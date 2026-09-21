#include "archspace/editing/ArchSpaceTarget.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::archspace_editing {
namespace {

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

bool readNumber(const EditorValue* value, double& out) {
    if (!value) return false;
    if (const auto* real = value->getIf<double>()) {
        out = *real;
        return std::isfinite(out);
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        out = static_cast<double>(*integer);
        return true;
    }
    return false;
}

EditorValue vec2Value(const archspace::Vec2& v) { return EditorValue::Array{v.x, v.z}; }

EditorValue vec3Value(const archspace::Vec3& v) { return EditorValue::Array{v.x, v.y, v.z}; }

bool parseVec2(const EditorValue& value, archspace::Vec2& out) {
    const auto* array = value.getIf<EditorValue::Array>();
    if (!array || array->size() != 2) return false;
    return readNumber(&(*array)[0], out.x) && readNumber(&(*array)[1], out.z);
}

bool parseVec3(const EditorValue& value, archspace::Vec3& out) {
    const auto* array = value.getIf<EditorValue::Array>();
    if (!array || array->size() != 3) return false;
    return readNumber(&(*array)[0], out.x) && readNumber(&(*array)[1], out.y) && readNumber(&(*array)[2], out.z);
}

EditorValue nodeValue(const archspace::Node& node) {
    EditorValue::Array children;
    for (const auto& child : node.children) children.push_back(child);
    EditorValue::Array polygon;
    for (const auto& p : node.polygon) polygon.push_back(vec2Value(p));
    return EditorValue::Object{
        {"id", node.id},
        {"kind", std::string(archspace::nodeKindName(node.kind))},
        {"parentId", node.parentId},
        {"name", node.name},
        {"children", std::move(children)},
        {"elevation", node.elevation},
        {"height", node.height},
        {"start", vec2Value(node.start)},
        {"end", vec2Value(node.end)},
        {"thickness", node.thickness},
        {"polygon", std::move(polygon)},
        {"slabThickness", node.slabThickness},
        {"position", vec3Value(node.position)},
        {"yawDegrees", node.yawDegrees},
        {"catalogId", node.catalogId},
        {"openingKind", std::string(archspace::openingKindName(node.openingKind))},
        {"t", node.t},
        {"width", node.width},
        {"openingHeight", node.openingHeight},
        {"sill", node.sill},
    };
}

EditorResult<archspace::Node> parseNode(const EditorValue& value) {
    archspace::Node node;
    const auto*     id       = field(value, "id");
    const auto*     kind     = field(value, "kind");
    const auto*     parentId = field(value, "parentId");
    const auto*     name     = field(value, "name");
    const auto*     ids      = id ? id->getIf<std::string>() : nullptr;
    const auto*     kinds    = kind ? kind->getIf<std::string>() : nullptr;
    const auto*     parents  = parentId ? parentId->getIf<std::string>() : nullptr;
    const auto*     names    = name ? name->getIf<std::string>() : nullptr;
    if (!ids || ids->empty() || !kinds || !parents || !names)
        return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.node"),
                                                     "ArchSpace node identity is invalid");
    auto parsedKind = archspace::parseNodeKind(*kinds);
    if (!parsedKind.ok())
        return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.kind"),
                                                     "ArchSpace node kind is unknown");
    node.kind     = parsedKind.value();
    node.id       = *ids;
    node.parentId = *parents;
    node.name     = *names;
    if (const auto* children = field(value, "children")) {
        const auto* array = children->getIf<EditorValue::Array>();
        if (!array)
            return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.children"),
                                                         "ArchSpace children must be an array");
        for (const auto& child : *array) {
            const auto* text = child.getIf<std::string>();
            if (!text)
                return eve::editing::failed<archspace::Node>(
                    EditorStatus::Rejected, RuleId("editor.archspace.children"), "ArchSpace child id must be a string");
            node.children.push_back(*text);
        }
    }
    readNumber(field(value, "elevation"), node.elevation);
    readNumber(field(value, "height"), node.height);
    if (const auto* start = field(value, "start"); start && !parseVec2(*start, node.start))
        return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.wall"),
                                                     "ArchSpace wall start is invalid");
    if (const auto* end = field(value, "end"); end && !parseVec2(*end, node.end))
        return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.wall"),
                                                     "ArchSpace wall end is invalid");
    readNumber(field(value, "thickness"), node.thickness);
    if (const auto* polygon = field(value, "polygon")) {
        const auto* array = polygon->getIf<EditorValue::Array>();
        if (!array)
            return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.polygon"),
                                                         "ArchSpace polygon must be an array");
        for (const auto& point : *array) {
            archspace::Vec2 v;
            if (!parseVec2(point, v))
                return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.polygon"),
                                                             "ArchSpace polygon point is invalid");
            node.polygon.push_back(v);
        }
    }
    readNumber(field(value, "slabThickness"), node.slabThickness);
    if (const auto* position = field(value, "position"); position && !parseVec3(*position, node.position))
        return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.item"),
                                                     "ArchSpace item position is invalid");
    readNumber(field(value, "yawDegrees"), node.yawDegrees);
    if (const auto* catalog = field(value, "catalogId")) {
        const auto* text = catalog->getIf<std::string>();
        if (!text)
            return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.item"),
                                                         "ArchSpace catalog id must be a string");
        node.catalogId = *text;
    }
    if (const auto* opening = field(value, "openingKind")) {
        const auto* text = opening->getIf<std::string>();
        if (!text)
            return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.opening"),
                                                         "ArchSpace opening kind must be a string");
        auto kind = archspace::parseOpeningKind(*text);
        if (!kind.ok())
            return eve::editing::failed<archspace::Node>(EditorStatus::Rejected, RuleId("editor.archspace.opening"),
                                                         "ArchSpace opening kind is invalid");
        node.openingKind = kind.value();
    }
    readNumber(field(value, "t"), node.t);
    readNumber(field(value, "width"), node.width);
    readNumber(field(value, "openingHeight"), node.openingHeight);
    readNumber(field(value, "sill"), node.sill);
    return eve::editing::applied<archspace::Node>(std::move(node));
}

PropertyDescriptor prop(std::string path, PropertyType type, EditorValue defaultValue, const char* category,
                        double minimum = -1e9, double maximum = 1e9) {
    PropertyDescriptor d;
    d.path            = PropertyPath(std::move(path));
    d.displayNameKey  = "editor.archspace." + d.path.value();
    d.category        = category;
    d.type            = type;
    d.defaultValue    = std::move(defaultValue);
    d.flags           = PropertyFlag::EditorOnly;
    d.numeric.minimum = minimum;
    d.numeric.maximum = maximum;
    return d;
}

bool hasErrors(const std::vector<EditorDiagnostic>& values) {
    return std::any_of(values.begin(), values.end(),
                       [](const auto& d) { return d.severity() == DiagnosticSeverity::Error; });
}

}  // namespace

ArchSpaceDocumentTarget::ArchSpaceDocumentTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor ArchSpaceDocumentTarget::describe() const {
    return {TargetId(id_),
            "archspace-document",
            revisionValue(),
            false,
            {propertyCapabilityId(), IEditingSnapshotProvider::editingCapabilityId()}};
}

void* ArchSpaceDocumentTarget::queryCapability(const CapabilityId& capability) {
    if (capability == propertyCapabilityId()) return static_cast<IPropertyProvider*>(this);
    if (capability == IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorValue ArchSpaceDocumentTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaId", std::string("eve.archspace.document")},
                               {"schemaVersion", int64_t{1}},
                               {"content", contentValue()}};
}

bool ArchSpaceDocumentTarget::matches(const SelectionSnapshot& selection) const {
    if (selection.items.empty()) return false;
    for (const auto& item : selection.items) {
        if (item.target != TargetId(id_) || !document_.find(item.item.value())) return false;
    }
    return true;
}

const archspace::Node* ArchSpaceDocumentTarget::selectedNode(const SelectionSnapshot& selection) const {
    if (selection.items.size() != 1 || selection.items.front().target != TargetId(id_)) return nullptr;
    return document_.find(selection.items.front().item.value());
}

eve::Result<eve::Revision> ArchSpaceDocumentTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!matches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "ArchSpace selection mismatch", "editor.archspace.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revisionValue()));
}

PropertySchema ArchSpaceDocumentTarget::schema(const SelectionSnapshot& selection) const {
    PropertySchema         schema;
    const archspace::Node* node = selectedNode(selection);
    if (!node) return schema;
    schema.typeId = std::string("archspace.") + archspace::nodeKindName(node->kind);
    schema.properties.push_back(prop("name", PropertyType::String, node->name, "common"));
    switch (node->kind) {
        case archspace::NodeKind::Level:
            schema.properties.push_back(prop("elevation", PropertyType::Float, node->elevation, "level"));
            schema.properties.push_back(prop("height", PropertyType::Float, node->height, "level", 0.1, 1000));
            break;
        case archspace::NodeKind::Wall:
            schema.properties.push_back(prop("thickness", PropertyType::Float, node->thickness, "wall", 0.01, 10));
            schema.properties.push_back(prop("height", PropertyType::Float, node->height, "wall", 0.1, 1000));
            break;
        case archspace::NodeKind::Item:
            schema.properties.push_back(prop("catalogId", PropertyType::String, node->catalogId, "item"));
            schema.properties.push_back(prop("yawDegrees", PropertyType::Float, node->yawDegrees, "item"));
            break;
        case archspace::NodeKind::Opening:
            schema.properties.push_back(prop("t", PropertyType::Float, node->t, "opening", 0, 1));
            schema.properties.push_back(prop("width", PropertyType::Float, node->width, "opening", 0.01, 20));
            schema.properties.push_back(
                prop("openingHeight", PropertyType::Float, node->openingHeight, "opening", 0.01, 20));
            schema.properties.push_back(prop("sill", PropertyType::Float, node->sill, "opening", 0, 20));
            break;
        default: break;
    }
    return schema;
}

PropertyReadResult ArchSpaceDocumentTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    const archspace::Node* node = selectedNode(selection);
    if (!node || !schema(selection).find(path)) return {};
    if (path == PropertyPath("name")) return {PropertyReadState::Value, node->name, {}};
    if (path == PropertyPath("elevation")) return {PropertyReadState::Value, node->elevation, {}};
    if (path == PropertyPath("height")) return {PropertyReadState::Value, node->height, {}};
    if (path == PropertyPath("thickness")) return {PropertyReadState::Value, node->thickness, {}};
    if (path == PropertyPath("catalogId")) return {PropertyReadState::Value, node->catalogId, {}};
    if (path == PropertyPath("yawDegrees")) return {PropertyReadState::Value, node->yawDegrees, {}};
    if (path == PropertyPath("t")) return {PropertyReadState::Value, node->t, {}};
    if (path == PropertyPath("width")) return {PropertyReadState::Value, node->width, {}};
    if (path == PropertyPath("openingHeight")) return {PropertyReadState::Value, node->openingHeight, {}};
    if (path == PropertyPath("sill")) return {PropertyReadState::Value, node->sill, {}};
    return {};
}

EditorValue ArchSpaceDocumentTarget::contentValue() const {
    EditorValue::Array nodes;
    for (const auto& [id, node] : document_.nodes()) {
        (void)id;
        nodes.push_back(nodeValue(node));
    }
    return EditorValue::Object{{"rootId", document_.rootId()}, {"nodes", std::move(nodes)}};
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::replacement(EditorValue content, std::string property) const {
    DomainOperation op;
    op.type        = "archspace.document.replace.v1";
    op.inverseType = op.type;
    op.target      = TargetId(id_);
    op.payload     = std::move(content);
    op.inverse     = contentValue();
    op.hasInverse  = true;
    if (!property.empty()) op.affectedProperties.push_back(property);
    op.mergeKey = "archspace:" + id_ + ":" + (property.empty() ? "structure" : property);
    return eve::editing::applied<DomainOperation>(std::move(op));
}

EditorResult<void> ArchSpaceDocumentTarget::installContent(const EditorValue& content) {
    const auto* rootIdValue = field(content, "rootId");
    const auto* nodesValue  = field(content, "nodes");
    const auto* rootId      = rootIdValue ? rootIdValue->getIf<std::string>() : nullptr;
    const auto* nodes       = nodesValue ? nodesValue->getIf<EditorValue::Array>() : nullptr;
    if (!rootId || !nodes || !content.isWithinLimits(12, 250000, 32 * 1024 * 1024))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.payload"),
                                          "ArchSpace document payload is invalid or exceeds limits");
    if (nodes->size() > 100000)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.budget"),
                                          "ArchSpace document exceeds 100000 nodes");

    archspace::Document candidate;
    if (rootId->empty() && nodes->empty()) {
        document_ = std::move(candidate);
        return eve::editing::applied<void>();
    }
    // Insert sites first, then remaining nodes in dependency-friendly passes.
    std::vector<archspace::Node> parsed;
    parsed.reserve(nodes->size());
    for (const auto& value : *nodes) {
        auto node = parseNode(value);
        if (!node.ok())
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.node"),
                                              "ArchSpace document contains an invalid node");
        parsed.push_back(std::move(node.value()));
    }
    std::sort(parsed.begin(), parsed.end(), [](const archspace::Node& a, const archspace::Node& b) {
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    for (auto& node : parsed) {
        node.children.clear();  // re-linked by insert()
        auto inserted = candidate.insert(std::move(node));
        if (!inserted.ok())
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.insert"),
                                              "ArchSpace document failed hierarchical insert");
    }
    if (candidate.rootId() != *rootId)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.root"),
                                          "ArchSpace root id does not match document content");
    if (!candidate.diagnostics().empty())
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.invalid"),
                                          "ArchSpace document validation failed");
    document_ = std::move(candidate);
    return eve::editing::applied<void>();
}

std::vector<EditorDiagnostic> ArchSpaceDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> out;
    for (const auto& line : document_.diagnostics()) {
        out.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                   RuleId("editor.archspace.validate"), DiagnosticSeverity::Error,
                                                   line));
    }
    if (document_.nodeCount() > 100000) {
        out.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                   RuleId("editor.archspace.budget"), DiagnosticSeverity::Error,
                                                   "ArchSpace document exceeds 100000 nodes"));
    }
    return out;
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeSet(const SelectionSnapshot& selection,
                                                               const PropertyPath& path, const EditorValue& value,
                                                               PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    const auto             descriptor = schema(selection).find(path);
    const archspace::Node* node       = selectedNode(selection);
    if (!descriptor || !node || mode != PropertySetMode::Absolute)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.set"),
                                                     "ArchSpace property requires a matching absolute edit");
    auto checked = validatePropertyValue(*descriptor, value);
    if (!checked.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.value"),
                                                     "ArchSpace property value is invalid");
    archspace::Document candidate = document_;
    archspace::Node*    editable  = candidate.findMutable(node->id);
    if (!editable)
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound, RuleId("editor.archspace.node"),
                                                     "ArchSpace node is missing");
    if (path == PropertyPath("name")) {
        const auto* text = value.getIf<std::string>();
        if (!text)
            return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.name"),
                                                         "Name must be string");
        editable->name = *text;
    } else if (path == PropertyPath("catalogId")) {
        const auto* text = value.getIf<std::string>();
        if (!text)
            return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.catalog"),
                                                         "Catalog id must be string");
        editable->catalogId = *text;
    } else {
        double number = 0.0;
        if (!readNumber(&value, number))
            return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.number"),
                                                         "ArchSpace property requires a number");
        if (path == PropertyPath("elevation"))
            editable->elevation = number;
        else if (path == PropertyPath("height"))
            editable->height = number;
        else if (path == PropertyPath("thickness"))
            editable->thickness = number;
        else if (path == PropertyPath("yawDegrees"))
            editable->yawDegrees = number;
        else if (path == PropertyPath("t"))
            editable->t = number;
        else if (path == PropertyPath("width"))
            editable->width = number;
        else if (path == PropertyPath("openingHeight"))
            editable->openingHeight = number;
        else if (path == PropertyPath("sill"))
            editable->sill = number;
        else
            return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported, RuleId("editor.archspace.property"),
                                                         "Unsupported ArchSpace property");
    }
    auto replaced = candidate.replace(*editable);
    if (!replaced.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.invalid"),
                                                     "ArchSpace property update failed validation");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    if (hasErrors(staged.validate()))
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.invalid"),
                                                     "ArchSpace property update failed validation");
    return replacement(staged.contentValue(), path.value());
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeReset(const SelectionSnapshot& selection,
                                                                 const PropertyPath&      path) const {
    const auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported, RuleId("editor.archspace.property"),
                                                     "Unknown ArchSpace property");
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeBootstrap(const std::string& siteId,
                                                                     const std::string& buildingId,
                                                                     const std::string& levelId,
                                                                     double             levelHeight) const {
    archspace::Document candidate = document_;
    auto                boot      = candidate.bootstrap(siteId, buildingId, levelId, levelHeight);
    if (!boot.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.bootstrap"),
                                                     "ArchSpace bootstrap requires an empty document and unique ids");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    return replacement(staged.contentValue());
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeCreateRectRoom(
    const std::string& levelId, const std::string& roomId, std::string roomName, double originX, double originZ,
    double sizeX, double sizeZ, double wallHeight, double wallThickness, double slabThickness) const {
    if (!(std::isfinite(originX) && std::isfinite(originZ) && std::isfinite(sizeX) && std::isfinite(sizeZ)) ||
        sizeX <= 1e-6 || sizeZ <= 1e-6)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.room"),
                                                     "ArchSpace room size must be finite and positive");
    std::vector<archspace::Vec2> polygon = {
        {originX, originZ},
        {originX + sizeX, originZ},
        {originX + sizeX, originZ + sizeZ},
        {originX, originZ + sizeZ},
    };
    archspace::Document candidate = document_;
    auto created = candidate.createRoom(levelId, roomId, std::move(roomName), std::move(polygon), wallHeight,
                                        wallThickness, slabThickness);
    if (!created.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.room"),
                                                     "ArchSpace room could not be created under the given level");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    return replacement(staged.contentValue());
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeCreateWall(const std::string& levelId,
                                                                      const std::string& wallId, std::string wallName,
                                                                      archspace::Vec2 start, archspace::Vec2 end,
                                                                      double height, double thickness) const {
    archspace::Document candidate = document_;
    auto created = candidate.createWall(levelId, wallId, std::move(wallName), start, end, height, thickness);
    if (!created.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.wall"),
                                                     "ArchSpace wall could not be created under the given level");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    return replacement(staged.contentValue());
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeCreateOpening(const std::string&     wallId,
                                                                         const std::string&     openingId,
                                                                         archspace::OpeningKind kind, double t,
                                                                         double width, double height,
                                                                         double sill) const {
    archspace::Document candidate = document_;
    auto created = candidate.createOpening(wallId, openingId, openingId, kind, t, width, height, sill);
    if (!created.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.opening"),
                                                     "ArchSpace opening insert failed validation");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    return replacement(staged.contentValue());
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makePlaceItem(const std::string& levelId,
                                                                     const std::string& itemId, std::string catalogId,
                                                                     archspace::Vec3 position,
                                                                     double          yawDegrees) const {
    archspace::Document candidate = document_;
    auto placed =
        candidate.placeItem(levelId, itemId, itemId, std::move(catalogId), position, yawDegrees);
    if (!placed.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.archspace.item"),
                                                     "ArchSpace item insert failed validation");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    return replacement(staged.contentValue());
}

EditorResult<DomainOperation> ArchSpaceDocumentTarget::makeDeleteNode(const ObjectId& id) const {
    archspace::Document candidate = document_;
    auto                erased    = candidate.eraseCascade(id.value());
    if (!erased.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound, RuleId("editor.archspace.delete"),
                                                     "ArchSpace node was not found");
    ArchSpaceDocumentTarget staged(id_);
    staged.document_ = std::move(candidate);
    return replacement(staged.contentValue());
}

EditorResult<void> ArchSpaceDocumentTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_) || operation.type != "archspace.document.replace.v1")
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.operation"),
                                          "ArchSpace operation mismatch");
    ArchSpaceDocumentTarget candidate(id_);
    auto                    installed = candidate.installContent(operation.payload);
    if (!installed.ok()) return installed;
    if (hasErrors(candidate.validate()))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.archspace.invalid"),
                                          "ArchSpace document validation failed");
    document_ = std::move(candidate.document_);
    bumpRevision();
    widenDirty(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> ArchSpaceDocumentTarget::cloneDomainState() const {
    return std::make_unique<ArchSpaceDocumentTarget>(*this);
}

EditorResult<void> ArchSpaceDocumentTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<ArchSpaceDocumentTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.archspace.candidate"),
                                          "ArchSpace candidate mismatch");
    *this = *typed;
    return eve::editing::applied<void>();
}

EditorResult<void> ArchSpaceDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaIdValue = field(snapshot, "schemaId");
    const auto* versionValue  = field(snapshot, "schemaVersion");
    const auto* content       = field(snapshot, "content");
    const auto* schemaId      = schemaIdValue ? schemaIdValue->getIf<std::string>() : nullptr;
    const auto* version       = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    if (!schemaId || *schemaId != "eve.archspace.document" || !version || *version != 1 || !content)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.archspace.snapshot"),
                                          "Unsupported ArchSpace snapshot");
    DomainOperation op;
    op.target   = TargetId(id_);
    op.type     = "archspace.document.replace.v1";
    op.payload  = *content;
    auto result = applyDomainOperation(op);
    if (result.ok()) clearDirtyRegion();
    return result;
}

EditorResult<EditorGizmoSnapshot> ArchSpaceDocumentTarget::gizmo() const {
    EditorGizmoSnapshot snapshot;
    snapshot.status         = EditorStatus::Applied;
    snapshot.target         = id_;
    snapshot.targetRevision = revisionValue();
    for (const auto& [id, node] : document_.nodes()) {
        (void)id;
        if (node.kind == archspace::NodeKind::Wall) {
            EditorGizmoPrimitive primitive;
            primitive.id        = node.id;
            primitive.kind      = "segment";
            primitive.position  = {node.start.x,
                                  document_.find(node.parentId) ? document_.find(node.parentId)->elevation : 0.0,
                                   node.start.z};
            primitive.direction = {node.end.x - node.start.x, 0.0, node.end.z - node.start.z};
            primitive.length    = std::sqrt(primitive.direction[0] * primitive.direction[0] +
                                            primitive.direction[2] * primitive.direction[2]);
            primitive.color     = {0.2, 0.7, 1.0, 0.9};
            snapshot.primitives.push_back(std::move(primitive));
        } else if (node.kind == archspace::NodeKind::Zone && !node.polygon.empty()) {
            double minX = node.polygon.front().x, maxX = minX, minZ = node.polygon.front().z, maxZ = minZ;
            for (const auto& p : node.polygon) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minZ = std::min(minZ, p.z);
                maxZ = std::max(maxZ, p.z);
            }
            EditorGizmoPrimitive primitive;
            primitive.id       = node.id;
            primitive.kind     = "area";
            primitive.dashed   = true;
            primitive.position = {(minX + maxX) * 0.5, 0.02, (minZ + maxZ) * 0.5};
            primitive.size     = {maxX - minX, 0.01, maxZ - minZ};
            primitive.color    = {0.8, 0.2, 1.0, 0.25};
            snapshot.primitives.push_back(std::move(primitive));
        } else if (node.kind == archspace::NodeKind::Item) {
            EditorGizmoPrimitive primitive;
            primitive.id       = node.id;
            primitive.kind     = "point";
            primitive.position = {node.position.x, node.position.y, node.position.z};
            primitive.radius   = 0.15;
            primitive.color    = {1.0, 0.8, 0.2, 0.9};
            primitive.text     = node.catalogId;
            snapshot.primitives.push_back(std::move(primitive));
        } else if (node.kind == archspace::NodeKind::Opening) {
            const archspace::Node* wall = document_.find(node.parentId);
            if (!wall) continue;
            EditorGizmoPrimitive primitive;
            primitive.id       = node.id;
            primitive.kind     = "point";
            primitive.position = {wall->start.x + (wall->end.x - wall->start.x) * node.t,
                                  node.sill + node.openingHeight * 0.5,
                                  wall->start.z + (wall->end.z - wall->start.z) * node.t};
            primitive.radius   = 0.12;
            primitive.color    = {0.2, 1.0, 0.4, 0.9};
            primitive.text     = archspace::openingKindName(node.openingKind);
            snapshot.primitives.push_back(std::move(primitive));
        }
    }
    return eve::editing::applied<EditorGizmoSnapshot>(std::move(snapshot));
}

}  // namespace eve::archspace_editing
