#include "hd2d_editing/Hd2dTarget.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace eve::hd2d_editing {
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
EditorValue array(const float* v, int n) {
    EditorValue::Array a;
    for (int i = 0; i < n; ++i) a.push_back(v[i]);
    return a;
}
bool readArray(const EditorValue* v, float* out, int n) {
    const auto* a = v ? v->getIf<EditorValue::Array>() : nullptr;
    if (!a || static_cast<int>(a->size()) != n) return false;
    for (int i = 0; i < n; ++i) {
        const auto* x = (*a)[i].getIf<double>();
        if (!x || !std::isfinite(*x)) return false;
        out[i] = static_cast<float>(*x);
    }
    return true;
}
PropertyDescriptor prop(std::string p, PropertyType t, EditorValue v, const char* c, double min = -1e9,
                        double max = 1e9) {
    PropertyDescriptor d;
    d.path            = PropertyPath(p);
    d.displayNameKey  = "editor.hd2d." + p;
    d.category        = c;
    d.type            = t;
    d.defaultValue    = std::move(v);
    d.flags           = PropertyFlag::Runtime;
    d.numeric.minimum = min;
    d.numeric.maximum = max;
    return d;
}
bool errors(const std::vector<EditorDiagnostic>& d) {
    return std::any_of(d.begin(), d.end(), [](const auto& v) { return v.severity() == DiagnosticSeverity::Error; });
}
}  // namespace
Hd2dDocumentTarget::Hd2dDocumentTarget(std::string id) : id_(std::move(id)) {}
TargetDescriptor Hd2dDocumentTarget::describe() const {
    return {TargetId(id_), "hd2d-asset", revision_, false, {CapabilityId("eve.editor.target.hd2d-properties")}};
}
void* Hd2dDocumentTarget::queryCapability(const CapabilityId& c) {
    return c == CapabilityId("eve.editor.target.hd2d-properties") ? static_cast<IPropertyProvider*>(this) : nullptr;
}
bool Hd2dDocumentTarget::matches(const SelectionSnapshot& s) const {
    return s.items.size() == 1 && s.items.front().target == TargetId(id_) && s.items.front().item.value() == id_;
}
eve::Result<eve::Revision> Hd2dDocumentTarget::currentRevision(const SelectionSnapshot& s) const {
    if (!matches(s))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "HD2D selection mismatch", "editor.hd2d.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}
PropertySchema Hd2dDocumentTarget::schema(const SelectionSnapshot&) const {
    PropertySchema s;
    s.typeId     = "hd2d.asset";
    s.properties = {prop("asset.kind", PropertyType::Enum, "sprite", "asset"),
                    prop("asset.source", PropertyType::AssetRef, "", "asset"),
                    prop("sprite.grid", PropertyType::Vec2, EditorValue::Array{1.0, 1.0}, "sprite", 1, 4096),
                    prop("sprite.frame", PropertyType::Int, int64_t{0}, "sprite", 0, 16777215),
                    prop("sprite.animation", PropertyType::Vec2, EditorValue::Array{0.0, 0.0}, "sprite", 0, 16777215),
                    prop("sprite.fps", PropertyType::Float, 12.0, "sprite", .01, 1000),
                    prop("sprite.flipX", PropertyType::Bool, false, "sprite"),
                    prop("sprite.flipY", PropertyType::Bool, false, "sprite"),
                    prop("sprite.size", PropertyType::Vec2, EditorValue::Array{1.0, 1.0}, "sprite", .001, 100000),
                    prop("sprite.tint", PropertyType::Color, EditorValue::Array{1.0, 1.0, 1.0, 1.0}, "sprite", 0, 1),
                    prop("sprite.visible", PropertyType::Bool, true, "sprite"),
                    prop("tile.sideDepth", PropertyType::Float, 6.0, "tilemap", 0, 100000),
                    prop("tile.heightScale", PropertyType::Float, 1.0, "tilemap", 0, 100000),
                    prop("tile.wallUv", PropertyType::Vec4, EditorValue::Array{0.0, 0.0, .05, .05}, "tilemap", 0, 1)};
    s.properties[0].enumItems        = {"sprite", "tilemap"};
    s.properties[1].assetTypeFilters = {"image", "texture", "map-layer"};
    return s;
}
EditorValue Hd2dDocumentTarget::contentValue() const {
    const auto& v = value_;
    return EditorValue::Object{{"kind", v.kind},
                               {"source", v.sourceAsset},
                               {"columns", int64_t{v.columns}},
                               {"rows", int64_t{v.rows}},
                               {"frame", int64_t{v.frame}},
                               {"animStart", int64_t{v.animStart}},
                               {"animEnd", int64_t{v.animEnd}},
                               {"fps", v.fps},
                               {"flipX", v.flipX},
                               {"flipY", v.flipY},
                               {"size", array(v.size.data(), 2)},
                               {"tint", array(v.tint.data(), 4)},
                               {"visible", v.visible},
                               {"sideDepth", v.sideDepth},
                               {"heightScale", v.heightScale},
                               {"wallUv", array(v.wallUv.data(), 4)}};
}
PropertyReadResult Hd2dDocumentTarget::read(const SelectionSnapshot& s, const PropertyPath& p) const {
    if (!matches(s) || !schema(s).find(p)) return {};
    const auto& v = value_;
    EditorValue value;
    if (p == PropertyPath("asset.kind"))
        value = v.kind;
    else if (p == PropertyPath("asset.source"))
        value = v.sourceAsset;
    else if (p == PropertyPath("sprite.grid"))
        value = EditorValue::Array{static_cast<double>(v.columns), static_cast<double>(v.rows)};
    else if (p == PropertyPath("sprite.frame"))
        value = int64_t{v.frame};
    else if (p == PropertyPath("sprite.animation"))
        value = EditorValue::Array{static_cast<double>(v.animStart), static_cast<double>(v.animEnd)};
    else if (p == PropertyPath("sprite.fps"))
        value = v.fps;
    else if (p == PropertyPath("sprite.flipX"))
        value = v.flipX;
    else if (p == PropertyPath("sprite.flipY"))
        value = v.flipY;
    else if (p == PropertyPath("sprite.size"))
        value = array(v.size.data(), 2);
    else if (p == PropertyPath("sprite.tint"))
        value = array(v.tint.data(), 4);
    else if (p == PropertyPath("sprite.visible"))
        value = v.visible;
    else if (p == PropertyPath("tile.sideDepth"))
        value = v.sideDepth;
    else if (p == PropertyPath("tile.heightScale"))
        value = v.heightScale;
    else
        value = array(v.wallUv.data(), 4);
    return {PropertyReadState::Value, std::move(value), {}};
}
EditorResult<DomainOperation> Hd2dDocumentTarget::replacement(EditorValue c, std::string p) const {
    DomainOperation op;
    op.type        = "hd2d.document.replace.v1";
    op.inverseType = op.type;
    op.target      = TargetId(id_);
    op.payload     = std::move(c);
    op.inverse     = contentValue();
    op.hasInverse  = true;
    op.affectedProperties.push_back(p);
    op.mergeKey = "hd2d:" + id_ + ":" + p;
    return eve::editing::applied<DomainOperation>(std::move(op));
}
EditorResult<DomainOperation> Hd2dDocumentTarget::makeSet(const SelectionSnapshot& s, const PropertyPath& p,
                                                          const EditorValue& value, PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(s, p);
    auto d = schema(s).find(p);
    if (!matches(s) || !d || mode != PropertySetMode::Absolute || !validatePropertyValue(*d, value).ok())
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.hd2d.set", "HD2D property is invalid");
    auto  c = *this;
    auto& v = c.value_;
    if (p == PropertyPath("asset.kind"))
        v.kind = *value.getIf<std::string>();
    else if (p == PropertyPath("asset.source"))
        v.sourceAsset = *value.getIf<std::string>();
    else if (p == PropertyPath("sprite.grid")) {
        const auto& a = *value.getIf<EditorValue::Array>();
        v.columns     = static_cast<int>(*a[0].getIf<double>());
        v.rows        = static_cast<int>(*a[1].getIf<double>());
    } else if (p == PropertyPath("sprite.frame"))
        v.frame = static_cast<int>(*value.getIf<int64_t>());
    else if (p == PropertyPath("sprite.animation")) {
        const auto& a = *value.getIf<EditorValue::Array>();
        v.animStart   = static_cast<int>(*a[0].getIf<double>());
        v.animEnd     = static_cast<int>(*a[1].getIf<double>());
    } else if (p == PropertyPath("sprite.fps"))
        v.fps = static_cast<float>(*value.getIf<double>());
    else if (p == PropertyPath("sprite.flipX"))
        v.flipX = *value.getIf<bool>();
    else if (p == PropertyPath("sprite.flipY"))
        v.flipY = *value.getIf<bool>();
    else if (p == PropertyPath("sprite.size"))
        readArray(&value, v.size.data(), 2);
    else if (p == PropertyPath("sprite.tint"))
        readArray(&value, v.tint.data(), 4);
    else if (p == PropertyPath("sprite.visible"))
        v.visible = *value.getIf<bool>();
    else if (p == PropertyPath("tile.sideDepth"))
        v.sideDepth = static_cast<float>(*value.getIf<double>());
    else if (p == PropertyPath("tile.heightScale"))
        v.heightScale = static_cast<float>(*value.getIf<double>());
    else
        readArray(&value, v.wallUv.data(), 4);
    if (errors(c.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.hd2d.invalid",
                                     "HD2D edit produces an invalid preset");
    return replacement(c.contentValue(), p.value());
}
EditorResult<DomainOperation> Hd2dDocumentTarget::makeReset(const SelectionSnapshot& s, const PropertyPath& p) const {
    auto d = schema(s).find(p);
    if (!d) return fail<DomainOperation>(EditorStatus::Unsupported, "editor.hd2d.property", "Unknown HD2D property");
    return makeSet(s, p, d->defaultValue, PropertySetMode::Absolute);
}
std::vector<EditorDiagnostic> Hd2dDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> d;
    const auto&                   v = value_;
    if (v.kind != "sprite" && v.kind != "tilemap")
        d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId("editor.hd2d.kind"),
                                                 DiagnosticSeverity::Error, "HD2D kind must be sprite or tilemap"));
    if (v.sourceAsset.empty())
        d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                 RuleId("editor.hd2d.source"), DiagnosticSeverity::Warning,
                                                 "HD2D source asset is not assigned"));
    const long long frames = static_cast<long long>(v.columns) * v.rows;
    if (v.columns < 1 || v.rows < 1 || frames > 16777216 || v.frame < 0 || v.frame >= frames || v.animStart < 0 ||
        v.animEnd < v.animStart || v.animEnd >= frames || !std::isfinite(v.fps) || v.fps <= 0 || v.fps > 1000)
        d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId("editor.hd2d.frames"),
                                                 DiagnosticSeverity::Error,
                                                 "HD2D sprite grid, frame or animation range is invalid"));
    const bool sizeOk = std::all_of(v.size.begin(), v.size.end(), [](float n) { return std::isfinite(n) && n > 0; });
    const bool tintOk =
        std::all_of(v.tint.begin(), v.tint.end(), [](float n) { return std::isfinite(n) && n >= 0 && n <= 1; });
    const bool uvFinite =
        std::all_of(v.wallUv.begin(), v.wallUv.end(), [](float n) { return std::isfinite(n) && n >= 0 && n <= 1; });
    if (!sizeOk || !tintOk || !std::isfinite(v.sideDepth) || v.sideDepth < 0 || !std::isfinite(v.heightScale) ||
        v.heightScale < 0 || !uvFinite || v.wallUv[0] >= v.wallUv[2] || v.wallUv[1] >= v.wallUv[3])
        d.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument,
                                                 RuleId("editor.hd2d.presentation"), DiagnosticSeverity::Error,
                                                 "HD2D size, tint or tile extrusion values are invalid"));
    return d;
}
EditorResult<void> Hd2dDocumentTarget::applyDomainOperation(const DomainOperation& op) {
    if (op.target != TargetId(id_) || op.type != "hd2d.document.replace.v1" || !op.payload.isWithinLimits(5, 64, 4096))
        return fail<void>(EditorStatus::Rejected, "editor.hd2d.operation", "HD2D operation is invalid");
    Hd2dAssetValue v;
    const auto *   kind = field(op.payload, "kind"), *source = field(op.payload, "source"),
               *fx = field(op.payload, "flipX"), *fy = field(op.payload, "flipY"),
               *visible = field(op.payload, "visible");
    const auto* k       = kind ? kind->getIf<std::string>() : nullptr;
    const auto* s       = source ? source->getIf<std::string>() : nullptr;
    const auto* xb      = fx ? fx->getIf<bool>() : nullptr;
    const auto* yb      = fy ? fy->getIf<bool>() : nullptr;
    const auto* vb      = visible ? visible->getIf<bool>() : nullptr;
    if (!k || !s || !xb || !yb || !vb)
        return fail<void>(EditorStatus::Rejected, "editor.hd2d.payload", "HD2D text/bool field is invalid");
    v.kind                = *k;
    v.sourceAsset         = *s;
    v.flipX               = *xb;
    v.flipY               = *yb;
    v.visible             = *vb;
    int*        ints[]    = {&v.columns, &v.rows, &v.frame, &v.animStart, &v.animEnd};
    const char* ifields[] = {"columns", "rows", "frame", "animStart", "animEnd"};
    for (int i = 0; i < 5; ++i) {
        const auto* x = field(op.payload, ifields[i]);
        const auto* n = x ? x->getIf<int64_t>() : nullptr;
        if (!n) return fail<void>(EditorStatus::Rejected, "editor.hd2d.integer", "HD2D integer is invalid");
        *ints[i] = static_cast<int>(*n);
    }
    float*      floats[]  = {&v.fps, &v.sideDepth, &v.heightScale};
    const char* ffields[] = {"fps", "sideDepth", "heightScale"};
    for (int i = 0; i < 3; ++i) {
        const auto* x = field(op.payload, ffields[i]);
        const auto* n = x ? x->getIf<double>() : nullptr;
        if (!n || !std::isfinite(*n))
            return fail<void>(EditorStatus::Rejected, "editor.hd2d.number", "HD2D number is invalid");
        *floats[i] = static_cast<float>(*n);
    }
    if (!readArray(field(op.payload, "size"), v.size.data(), 2) ||
        !readArray(field(op.payload, "tint"), v.tint.data(), 4) ||
        !readArray(field(op.payload, "wallUv"), v.wallUv.data(), 4))
        return fail<void>(EditorStatus::Rejected, "editor.hd2d.vector", "HD2D vector is invalid");
    Hd2dDocumentTarget c(id_);
    c.value_ = v;
    if (errors(c.validate()))
        return fail<void>(EditorStatus::Rejected, "editor.hd2d.invalid", "HD2D document validation failed");
    value_ = v;
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}
std::unique_ptr<IDomainOperationTarget> Hd2dDocumentTarget::cloneDomainState() const {
    return std::make_unique<Hd2dDocumentTarget>(*this);
}
EditorResult<void> Hd2dDocumentTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> c) {
    auto* t = dynamic_cast<Hd2dDocumentTarget*>(c.get());
    if (!t || t->id_ != id_)
        return fail<void>(EditorStatus::Conflict, "editor.hd2d.candidate", "HD2D candidate mismatch");
    *this = *t;
    return eve::editing::applied<void>();
}
EditorValue Hd2dDocumentTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"content", contentValue()}};
}
EditorResult<void> Hd2dDocumentTarget::loadSnapshot(const EditorValue& s) {
    const auto *v = field(s, "schemaVersion"), *content = field(s, "content");
    const auto* version = v ? v->getIf<int64_t>() : nullptr;
    if (!version || *version != 1 || !content)
        return fail<void>(EditorStatus::Unsupported, "editor.hd2d.snapshot", "Unsupported HD2D snapshot");
    DomainOperation op;
    op.target   = TargetId(id_);
    op.type     = "hd2d.document.replace.v1";
    op.payload  = *content;
    auto result = applyDomainOperation(op);
    if (result.ok()) dirty_.clear();
    return result;
}
EditorResult<Hd2dFramePreview> Hd2dFramePreviewService::evaluate(const Hd2dDocumentTarget& d, float time) const {
    if (d.value().kind != "sprite" || !std::isfinite(time) || time < 0 || time > 86400)
        return fail<Hd2dFramePreview>(EditorStatus::Rejected, "editor.hd2d.preview",
                                      "HD2D frame preview requires a sprite and bounded time");
    const auto diagnostics = d.validate();
    if (errors(diagnostics))
        return fail<Hd2dFramePreview>(EditorStatus::Rejected, "editor.hd2d.preview-invalid", "HD2D preset is invalid");
    const auto&      v     = d.value();
    const int        span  = v.animEnd - v.animStart + 1;
    const int        frame = v.animStart + (static_cast<long long>(std::floor(time * v.fps)) % span);
    const int        x = frame % v.columns, y = frame / v.columns;
    Hd2dFramePreview p;
    p.frame = frame;
    p.uv    = {static_cast<float>(x) / v.columns, static_cast<float>(y) / v.rows, static_cast<float>(x + 1) / v.columns,
               static_cast<float>(y + 1) / v.rows};
    if (v.flipX) std::swap(p.uv[0], p.uv[2]);
    if (v.flipY) std::swap(p.uv[1], p.uv[3]);
    return eve::editing::applied<Hd2dFramePreview>(p);
}
}  // namespace eve::hd2d_editing
