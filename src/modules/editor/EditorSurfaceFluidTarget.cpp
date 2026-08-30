#include "editor/EditorSurfaceFluidTarget.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::editor {
namespace {
template <class T> EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}
const EditorValue* field(const EditorValue& value, const std::string& key) {
    const auto* object = value.getIf<EditorValue::Object>(); if (!object) return nullptr;
    auto found = object->find(key); return found == object->end() ? nullptr : &found->second;
}
EditorValue settingsValue(const SurfaceFluidSettings& s) {
    return EditorValue::Object{{"gravity", EditorValue::Array{s.gravityX, s.gravityY, s.gravityZ}},
        {"friction", s.friction}, {"maxSpeed", s.maxSpeed}, {"adhesionAcceleration", s.adhesionAcceleration},
        {"maxCrossings", int64_t{s.maxCrossings}}, {"contactAngleDegrees", s.contactAngleDegrees},
        {"mergeRadiusScale", s.mergeRadiusScale}, {"trailDeposition", s.trailDeposition},
        {"airDrag", s.airDrag}, {"reattachDistance", s.reattachDistance}, {"diffusion", s.diffusion},
        {"evaporation", s.evaporation}, {"maxWetness", s.maxWetness},
        {"velocityStretch", s.velocityStretch}, {"maxAspectRatio", s.maxAspectRatio},
        {"surfaceOffset", s.surfaceOffset}, {"dryRoughness", s.dryRoughness},
        {"wetRoughness", s.wetRoughness}, {"drySpecular", s.drySpecular},
        {"wetSpecular", s.wetSpecular}, {"wetDarkening", s.wetDarkening},
        {"normalStrength", s.normalStrength}};
}
std::vector<EditorDiagnostic> validateSettings(const SurfaceFluidSettings& s) {
    std::vector<EditorDiagnostic> out;
    const double finite[]{s.gravityX,s.gravityY,s.gravityZ,s.friction,s.maxSpeed,s.adhesionAcceleration,
        s.contactAngleDegrees,s.mergeRadiusScale,s.trailDeposition,s.airDrag,s.reattachDistance,s.diffusion,
        s.evaporation,s.maxWetness,s.velocityStretch,s.maxAspectRatio,s.surfaceOffset,s.dryRoughness,
        s.wetRoughness,s.drySpecular,s.wetSpecular,s.wetDarkening,s.normalStrength};
    if (std::any_of(std::begin(finite), std::end(finite), [](double v){ return !std::isfinite(v); }))
        out.push_back({RuleId("editor.surface-fluid.nonfinite"), DiagnosticSeverity::Error,
                       "Surface fluid values must be finite"});
    if (s.friction < 0 || s.maxSpeed <= 0 || s.adhesionAcceleration < 0 || s.maxCrossings < 1 ||
        s.maxCrossings > 1024 || s.contactAngleDegrees <= 0 || s.contactAngleDegrees >= 180 ||
        s.mergeRadiusScale <= 0 || s.trailDeposition < 0 || s.airDrag < 0 || s.reattachDistance < 0 ||
        s.diffusion < 0 || s.evaporation < 0 || s.maxWetness <= 0 || s.velocityStretch < 0 ||
        s.maxAspectRatio < 1 || s.surfaceOffset < 0 || s.dryRoughness < 0 || s.dryRoughness > 1 ||
        s.wetRoughness < 0 || s.wetRoughness > 1 || s.drySpecular < 0 || s.drySpecular > 1 ||
        s.wetSpecular < 0 || s.wetSpecular > 1 || s.wetDarkening < 0 || s.wetDarkening > 1 ||
        s.normalStrength < 0 || s.normalStrength > 1)
        out.push_back({RuleId("editor.surface-fluid.range"), DiagnosticSeverity::Error,
                       "Surface fluid values are outside safe runtime ranges"});
    if (s.wetRoughness > s.dryRoughness)
        out.push_back({RuleId("editor.surface-fluid.roughness-order"), DiagnosticSeverity::Warning,
                       "Wet roughness exceeds dry roughness"});
    if (s.wetSpecular < s.drySpecular)
        out.push_back({RuleId("editor.surface-fluid.specular-order"), DiagnosticSeverity::Warning,
                       "Wet specular is lower than dry specular"});
    return out;
}
EditorResult<SurfaceFluidSettings> parse(const EditorValue& value) {
    SurfaceFluidSettings s;
    const auto number = [&](const char* key, double& destination) {
        const auto* entry = field(value, key); const auto* v = entry ? entry->getIf<double>() : nullptr;
        if (v) destination = *v;
        return v != nullptr;
    };
    const auto* gravityValue = field(value, "gravity");
    const auto* gravity = gravityValue ? gravityValue->getIf<EditorValue::Array>() : nullptr;
    const auto* crossingsValue = field(value, "maxCrossings");
    const auto* crossings = crossingsValue ? crossingsValue->getIf<int64_t>() : nullptr;
    if (!gravity || gravity->size()!=3 || !crossings) return fail<SurfaceFluidSettings>(EditorStatus::Rejected,
        "editor.surface-fluid.fields", "Surface fluid settings are incomplete");
    double* gravityOut[]{&s.gravityX,&s.gravityY,&s.gravityZ};
    for (int i=0;i<3;++i) { const auto* v=(*gravity)[i].getIf<double>(); if(!v) return fail<SurfaceFluidSettings>(EditorStatus::Rejected,"editor.surface-fluid.gravity","Gravity must be a Vec3"); *gravityOut[i]=*v; }
    s.maxCrossings=static_cast<int>(*crossings);
    bool complete=true;
    complete &= number("friction",s.friction); complete &= number("maxSpeed",s.maxSpeed);
    complete &= number("adhesionAcceleration",s.adhesionAcceleration); complete &= number("contactAngleDegrees",s.contactAngleDegrees);
    complete &= number("mergeRadiusScale",s.mergeRadiusScale); complete &= number("trailDeposition",s.trailDeposition);
    complete &= number("airDrag",s.airDrag); complete &= number("reattachDistance",s.reattachDistance);
    complete &= number("diffusion",s.diffusion); complete &= number("evaporation",s.evaporation);
    complete &= number("maxWetness",s.maxWetness); complete &= number("velocityStretch",s.velocityStretch);
    complete &= number("maxAspectRatio",s.maxAspectRatio); complete &= number("surfaceOffset",s.surfaceOffset);
    complete &= number("dryRoughness",s.dryRoughness); complete &= number("wetRoughness",s.wetRoughness);
    complete &= number("drySpecular",s.drySpecular); complete &= number("wetSpecular",s.wetSpecular);
    complete &= number("wetDarkening",s.wetDarkening); complete &= number("normalStrength",s.normalStrength);
    if (!complete) return fail<SurfaceFluidSettings>(EditorStatus::Rejected,"editor.surface-fluid.fields","Surface fluid settings are incomplete");
    const auto diagnostics=validateSettings(s);
    if (std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& d){return d.severity==DiagnosticSeverity::Error;}))
        return fail<SurfaceFluidSettings>(EditorStatus::Rejected,"editor.surface-fluid.invalid","Surface fluid settings are invalid");
    return EditorResult<SurfaceFluidSettings>::applied(s);
}
PropertyDescriptor descriptor(const char* path, PropertyType type, EditorValue defaultValue,
                              const char* category, double minimum=0, double maximum=0, bool ranged=false) {
    PropertyDescriptor d; d.path=PropertyPath(path); d.displayNameKey=std::string("editor.surface-fluid.")+path;
    d.category=category; d.type=type; d.flags=PropertyFlag::Runtime; d.defaultValue=std::move(defaultValue);
    if(ranged){d.numeric.minimum=minimum;d.numeric.maximum=maximum;} return d;
}
}

SurfaceFluidTarget::SurfaceFluidTarget(std::string id):id_(std::move(id)){}
TargetDescriptor SurfaceFluidTarget::describe() const {return {TargetId(id_),"surface-fluid",revision_,false,{CapabilityId("eve.editor.target.surface-fluid-properties")}};}
void* SurfaceFluidTarget::queryCapability(const CapabilityId& c){return c==CapabilityId("eve.editor.target.surface-fluid-properties")?static_cast<IPropertyProvider*>(this):nullptr;}
bool SurfaceFluidTarget::matches(const SelectionSnapshot& s)const{return s.items.size()==1&&s.items.front().target==TargetId(id_);}
eve::Result<eve::Revision> SurfaceFluidTarget::currentRevision(const SelectionSnapshot&s)const{
    if(!matches(s))return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,"Surface fluid selection mismatch","editor.surface-fluid.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}
PropertySchema SurfaceFluidTarget::schema(const SelectionSnapshot&)const{
    SurfaceFluidSettings d; PropertySchema s; s.typeId="fluids.surface"; s.properties={
      descriptor("gravity",PropertyType::Vec3,EditorValue::Array{d.gravityX,d.gravityY,d.gravityZ},"droplet"),
      descriptor("friction",PropertyType::Float,d.friction,"droplet",0,1000,true),descriptor("maxSpeed",PropertyType::Float,d.maxSpeed,"droplet",0.0001,100000,true),
      descriptor("adhesionAcceleration",PropertyType::Float,d.adhesionAcceleration,"droplet",0,100000,true),descriptor("maxCrossings",PropertyType::Int,int64_t{d.maxCrossings},"droplet",1,1024,true),
      descriptor("contactAngleDegrees",PropertyType::Float,d.contactAngleDegrees,"droplet",0.001,179.999,true),descriptor("mergeRadiusScale",PropertyType::Float,d.mergeRadiusScale,"droplet",0.0001,1000,true),
      descriptor("trailDeposition",PropertyType::Float,d.trailDeposition,"droplet",0,1000,true),descriptor("airDrag",PropertyType::Float,d.airDrag,"droplet",0,1000,true),descriptor("reattachDistance",PropertyType::Float,d.reattachDistance,"droplet",0,1000,true),
      descriptor("diffusion",PropertyType::Float,d.diffusion,"wetness",0,1000,true),descriptor("evaporation",PropertyType::Float,d.evaporation,"wetness",0,1000,true),descriptor("maxWetness",PropertyType::Float,d.maxWetness,"wetness",0.0001,1000,true),
      descriptor("velocityStretch",PropertyType::Float,d.velocityStretch,"render",0,1000,true),descriptor("maxAspectRatio",PropertyType::Float,d.maxAspectRatio,"render",1,1000,true),descriptor("surfaceOffset",PropertyType::Float,d.surfaceOffset,"render",0,1000,true),
      descriptor("dryRoughness",PropertyType::Float,d.dryRoughness,"material",0,1,true),descriptor("wetRoughness",PropertyType::Float,d.wetRoughness,"material",0,1,true),descriptor("drySpecular",PropertyType::Float,d.drySpecular,"material",0,1,true),descriptor("wetSpecular",PropertyType::Float,d.wetSpecular,"material",0,1,true),descriptor("wetDarkening",PropertyType::Float,d.wetDarkening,"material",0,1,true),descriptor("normalStrength",PropertyType::Float,d.normalStrength,"material",0,1,true)};return s;
}
PropertyReadResult SurfaceFluidTarget::read(const SelectionSnapshot&s,const PropertyPath&p)const{if(!matches(s)||!schema(s).find(p))return{};const auto* v=field(settingsValue(settings_),p.value());return v?PropertyReadResult{PropertyReadState::Value,*v,{}}:PropertyReadResult{};}
EditorResult<DomainOperation> SurfaceFluidTarget::makeSet(const SelectionSnapshot&s,const PropertyPath&p,const EditorValue&v,PropertySetMode m)const{
    if (m == PropertySetMode::Reset) return makeReset(s, p);
    auto d = schema(s).find(p);
    if (!matches(s) || !d || m != PropertySetMode::Absolute)
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.surface-fluid.set",
                                     "Surface fluid property requires a matching absolute edit");
    auto checked=validatePropertyValue(*d,v);if(!checked.isAccepted()){EditorResult<DomainOperation> r;r.status=checked.status;r.diagnostics=std::move(checked.diagnostics);return r;}
    EditorValue candidate=settingsValue(settings_);(*candidate.getIf<EditorValue::Object>())[p.value()]=v;auto parsed=parse(candidate);if(!parsed.value)return fail<DomainOperation>(parsed.status,"editor.surface-fluid.invalid","Surface fluid property conflicts with other settings");
    DomainOperation op;op.type="surface-fluid.settings.replace.v1";op.inverseType=op.type;op.target=TargetId(id_);op.payload=settingsValue(*parsed.value);op.inverse=settingsValue(settings_);op.hasInverse=true;op.affectedProperties.push_back(p.value());op.mergeKey="surface-fluid:"+id_+":"+p.value();return EditorResult<DomainOperation>::applied(std::move(op));
}
EditorResult<DomainOperation> SurfaceFluidTarget::makeReset(const SelectionSnapshot&s,const PropertyPath&p)const{auto d=schema(s).find(p);if(!d)return fail<DomainOperation>(EditorStatus::Unsupported,"editor.surface-fluid.property","Unknown surface fluid property");return makeSet(s,p,d->defaultValue,PropertySetMode::Absolute);}
EditorResult<void> SurfaceFluidTarget::applyDomainOperation(const DomainOperation&op){if(op.target!=TargetId(id_)||op.type!="surface-fluid.settings.replace.v1")return fail<void>(EditorStatus::Rejected,"editor.surface-fluid.operation","Surface fluid operation mismatch");auto parsed=parse(op.payload);if(!parsed.value)return fail<void>(parsed.status,"editor.surface-fluid.invalid","Invalid surface fluid operation payload");settings_=*parsed.value;++revision_;dirty_.include(0,0);return EditorResult<void>::applied();}
std::vector<EditorDiagnostic> SurfaceFluidTarget::validate()const{return validateSettings(settings_);}
EditorValue SurfaceFluidTarget::snapshotValue()const{return EditorValue::Object{{"schemaVersion",int64_t{1}},{"settings",settingsValue(settings_)}};}
EditorResult<void> SurfaceFluidTarget::loadSnapshot(const EditorValue&snapshot){const auto*v=field(snapshot,"schemaVersion"),*s=field(snapshot,"settings");const auto*version=v?v->getIf<int64_t>():nullptr;if(!version||*version!=1||!s)return fail<void>(EditorStatus::Unsupported,"editor.surface-fluid.snapshot","Unsupported surface fluid snapshot");auto parsed=parse(*s);if(!parsed.value)return fail<void>(parsed.status,"editor.surface-fluid.invalid","Invalid surface fluid snapshot");settings_=*parsed.value;++revision_;dirty_.clear();return EditorResult<void>::applied();}

}  // namespace eve::editor
