#include "gpuagents/editing/GpuAgentsDocument.h"

#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::gpuagents_editing {
namespace {

EditorDiagnostic diagnostic(const char* rule, DiagnosticSeverity severity, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId(rule), severity,
                                        std::move(message));
}

const EditorValue* field(const EditorValue& value, const std::string& key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue settingsValue(const GpuAgentsSettings& s) {
    return EditorValue::Object{{"kind", s.kind},
                               {"maxAgents", s.maxAgents},
                               {"fixedDt", s.fixedDt},
                               {"agentRadius", s.agentRadius},
                               {"obstaclePredictTime", s.obstaclePredictTime},
                               {"seed", s.seed},
                               {"separationRadius", s.separationRadius},
                               {"cohesionRadius", s.cohesionRadius},
                               {"alignmentRadius", s.alignmentRadius},
                               {"separationWeight", s.separationWeight},
                               {"cohesionWeight", s.cohesionWeight},
                               {"alignmentWeight", s.alignmentWeight},
                               {"maxSpeed", s.maxSpeed},
                               {"moveSpeed", s.moveSpeed},
                               {"depositStrength", s.depositStrength},
                               {"trailDecayRate", s.trailDecayRate},
                               {"diffusionRate", s.diffusionRate},
                               {"trailFollow", s.trailFollow},
                               {"fieldResolution", s.fieldResolution},
                               {"worldSize", s.worldSize},
                               {"origin", EditorValue::Array{s.originX, s.originY, s.originZ}},
                               {"minSurfaceNormalZ", s.minSurfaceNormalZ},
                               {"lifeFieldSampler", s.lifeFieldSampler},
                               {"surfaceDataSampler", s.surfaceDataSampler}};
}

std::vector<EditorDiagnostic> validateSettings(const GpuAgentsSettings& s) {
    std::vector<EditorDiagnostic> out;
    if (s.kind != "Fish" && s.kind != "LifeNetwork" && s.kind != "Bird" && s.kind != "Petal") {
        out.push_back(diagnostic("editor.gpuagents.kind", DiagnosticSeverity::Error,
                                 "kind must be Fish, LifeNetwork, Bird, or Petal"));
    }
    if (s.maxAgents <= 0 || s.maxAgents > 100000) {
        out.push_back(
            diagnostic("editor.gpuagents.capacity", DiagnosticSeverity::Error, "maxAgents must be in (0, 100000]"));
    }
    if (!(s.fixedDt > 0.0) || s.fixedDt > 0.2) {
        out.push_back(diagnostic("editor.gpuagents.fixedDt", DiagnosticSeverity::Error, "fixedDt must be in (0, 0.2]"));
    }
    if (!(s.agentRadius > 0.0) || !(s.worldSize > 0.0) || s.fieldResolution < 2) {
        out.push_back(diagnostic("editor.gpuagents.range", DiagnosticSeverity::Error,
                                 "agentRadius/worldSize/fieldResolution out of range"));
    }
    if (s.lifeFieldSampler.empty() || s.surfaceDataSampler.empty()) {
        out.push_back(diagnostic("editor.gpuagents.samplers", DiagnosticSeverity::Error,
                                 "material sampler names must be non-empty"));
    }
    return out;
}

EditorResult<GpuAgentsSettings> parse(const EditorValue& value) {
    GpuAgentsSettings s;
    bool              complete = true;
    const auto        number   = [&](const char* key, double& destination) {
        const auto* entry = field(value, key);
        const auto* v     = entry ? entry->getIf<double>() : nullptr;
        if (v) destination = *v;
        return v != nullptr;
    };
    const auto integer = [&](const char* key, std::int64_t& destination) {
        const auto* entry = field(value, key);
        const auto* v     = entry ? entry->getIf<std::int64_t>() : nullptr;
        if (v) destination = *v;
        return v != nullptr;
    };
    const auto* kindValue = field(value, "kind");
    const auto* kind      = kindValue ? kindValue->getIf<std::string>() : nullptr;
    if (kind)
        s.kind = *kind;
    else
        complete = false;
    complete &= integer("maxAgents", s.maxAgents);
    complete &= number("fixedDt", s.fixedDt);
    complete &= number("agentRadius", s.agentRadius);
    complete &= number("obstaclePredictTime", s.obstaclePredictTime);
    complete &= integer("seed", s.seed);
    complete &= number("separationRadius", s.separationRadius);
    complete &= number("cohesionRadius", s.cohesionRadius);
    complete &= number("alignmentRadius", s.alignmentRadius);
    complete &= number("separationWeight", s.separationWeight);
    complete &= number("cohesionWeight", s.cohesionWeight);
    complete &= number("alignmentWeight", s.alignmentWeight);
    complete &= number("maxSpeed", s.maxSpeed);
    complete &= number("moveSpeed", s.moveSpeed);
    complete &= number("depositStrength", s.depositStrength);
    complete &= number("trailDecayRate", s.trailDecayRate);
    complete &= number("diffusionRate", s.diffusionRate);
    complete &= number("trailFollow", s.trailFollow);
    complete &= integer("fieldResolution", s.fieldResolution);
    complete &= number("worldSize", s.worldSize);
    complete &= number("minSurfaceNormalZ", s.minSurfaceNormalZ);
    const auto* originValue = field(value, "origin");
    const auto* origin      = originValue ? originValue->getIf<EditorValue::Array>() : nullptr;
    if (origin && origin->size() == 3) {
        const auto* x = (*origin)[0].getIf<double>();
        const auto* y = (*origin)[1].getIf<double>();
        const auto* z = (*origin)[2].getIf<double>();
        if (x && y && z) {
            s.originX = *x;
            s.originY = *y;
            s.originZ = *z;
        } else
            complete = false;
    } else
        complete = false;
    const auto* lifeSampler = field(value, "lifeFieldSampler");
    const auto* lifeStr     = lifeSampler ? lifeSampler->getIf<std::string>() : nullptr;
    if (lifeStr)
        s.lifeFieldSampler = *lifeStr;
    else
        complete = false;
    const auto* surfSampler = field(value, "surfaceDataSampler");
    const auto* surfStr     = surfSampler ? surfSampler->getIf<std::string>() : nullptr;
    if (surfStr)
        s.surfaceDataSampler = *surfStr;
    else
        complete = false;

    if (!complete) {
        return eve::editing::failed<GpuAgentsSettings>(EditorStatus::Rejected, RuleId("editor.gpuagents.fields"),
                                                       "GPU Agents settings are incomplete");
    }
    const auto diagnostics = validateSettings(s);
    if (std::any_of(diagnostics.begin(), diagnostics.end(),
                    [](const auto& d) { return d.severity() == DiagnosticSeverity::Error; })) {
        return eve::editing::failed<GpuAgentsSettings>(EditorStatus::Rejected, RuleId("editor.gpuagents.invalid"),
                                                       "GPU Agents settings are invalid");
    }
    return eve::editing::applied<GpuAgentsSettings>(s);
}

editing::PropertyDescriptor descriptor(const char* path, editing::PropertyType type, EditorValue defaultValue,
                                       const char* category, double minimum = 0, double maximum = 0,
                                       bool ranged = false) {
    editing::PropertyDescriptor d;
    d.path           = PropertyPath(path);
    d.displayNameKey = std::string("editor.gpuagents.") + path;
    d.category       = category;
    d.type           = type;
    d.flags          = editing::PropertyFlag::Runtime;
    d.defaultValue   = std::move(defaultValue);
    if (ranged) {
        d.numeric.minimum = minimum;
        d.numeric.maximum = maximum;
    }
    return d;
}

}  // namespace

PropertySchema gpuAgentsEffectSchema() {
    GpuAgentsSettings d;
    PropertySchema    s;
    s.typeId       = "gpuagents:effect-document";
    s.version      = 1;
    auto kind      = descriptor("kind", editing::PropertyType::String, d.kind, "effect");
    kind.enumItems = {"Fish", "LifeNetwork", "Bird", "Petal"};
    s.properties   = {
        std::move(kind),
        descriptor("maxAgents", editing::PropertyType::Int, d.maxAgents, "effect", 1, 100000, true),
        descriptor("fixedDt", editing::PropertyType::Float, d.fixedDt, "effect", 0.0001, 0.2, true),
        descriptor("agentRadius", editing::PropertyType::Float, d.agentRadius, "effect", 0.001, 100, true),
        descriptor("obstaclePredictTime", editing::PropertyType::Float, d.obstaclePredictTime, "effect", 0, 5, true),
        descriptor("seed", editing::PropertyType::Int, d.seed, "effect", 0, 2147483647, true),
        descriptor("separationRadius", editing::PropertyType::Float, d.separationRadius, "boids", 0.01, 100, true),
        descriptor("cohesionRadius", editing::PropertyType::Float, d.cohesionRadius, "boids", 0.01, 100, true),
        descriptor("alignmentRadius", editing::PropertyType::Float, d.alignmentRadius, "boids", 0.01, 100, true),
        descriptor("separationWeight", editing::PropertyType::Float, d.separationWeight, "boids", 0, 20, true),
        descriptor("cohesionWeight", editing::PropertyType::Float, d.cohesionWeight, "boids", 0, 20, true),
        descriptor("alignmentWeight", editing::PropertyType::Float, d.alignmentWeight, "boids", 0, 20, true),
        descriptor("maxSpeed", editing::PropertyType::Float, d.maxSpeed, "motion", 0.01, 100, true),
        descriptor("moveSpeed", editing::PropertyType::Float, d.moveSpeed, "life", 0.01, 100, true),
        descriptor("depositStrength", editing::PropertyType::Float, d.depositStrength, "life", 0, 10, true),
        descriptor("trailDecayRate", editing::PropertyType::Float, d.trailDecayRate, "life", 0, 10, true),
        descriptor("diffusionRate", editing::PropertyType::Float, d.diffusionRate, "life", 0, 1, true),
        descriptor("trailFollow", editing::PropertyType::Float, d.trailFollow, "life", 0, 20, true),
        descriptor("fieldResolution", editing::PropertyType::Int, d.fieldResolution, "life", 2, 1024, true),
        descriptor("worldSize", editing::PropertyType::Float, d.worldSize, "life", 0.01, 10000, true),
        descriptor("origin", editing::PropertyType::Vec3, EditorValue::Array{d.originX, d.originY, d.originZ}, "life"),
        descriptor("minSurfaceNormalZ", editing::PropertyType::Float, d.minSurfaceNormalZ, "life", 0, 1, true),
        descriptor("lifeFieldSampler", editing::PropertyType::String, d.lifeFieldSampler, "material"),
        descriptor("surfaceDataSampler", editing::PropertyType::String, d.surfaceDataSampler, "material"),
    };
    return s;
}

gpuagents::EffectProfile toEffectProfile(const GpuAgentsSettings& settings) {
    gpuagents::EffectProfile profile;
    if (settings.kind == "LifeNetwork") {
        profile                        = gpuagents::EffectProfile::makeLife(static_cast<int>(settings.maxAgents),
                                                                            static_cast<int>(settings.fieldResolution));
        profile.life.moveSpeed         = static_cast<float>(settings.moveSpeed);
        profile.life.depositStrength   = static_cast<float>(settings.depositStrength);
        profile.life.trailDecayRate    = static_cast<float>(settings.trailDecayRate);
        profile.life.diffusionRate     = static_cast<float>(settings.diffusionRate);
        profile.life.trailFollow       = static_cast<float>(settings.trailFollow);
        profile.life.worldSize         = static_cast<float>(settings.worldSize);
        profile.life.origin            = {static_cast<float>(settings.originX), static_cast<float>(settings.originY),
                                          static_cast<float>(settings.originZ)};
        profile.life.minSurfaceNormalZ = static_cast<float>(settings.minSurfaceNormalZ);
    } else if (settings.kind == "Bird") {
        profile                       = gpuagents::EffectProfile::makeBird(static_cast<int>(settings.maxAgents));
        profile.bird.separationRadius = static_cast<float>(settings.separationRadius);
        profile.bird.cohesionRadius   = static_cast<float>(settings.cohesionRadius);
        profile.bird.alignmentRadius  = static_cast<float>(settings.alignmentRadius);
        profile.bird.separationWeight = static_cast<float>(settings.separationWeight);
        profile.bird.cohesionWeight   = static_cast<float>(settings.cohesionWeight);
        profile.bird.alignmentWeight  = static_cast<float>(settings.alignmentWeight);
        profile.bird.maxSpeed         = static_cast<float>(settings.maxSpeed);
    } else if (settings.kind == "Petal") {
        profile = gpuagents::EffectProfile::makePetal(static_cast<int>(settings.maxAgents));
    } else {
        profile                       = gpuagents::EffectProfile::makeFish(static_cast<int>(settings.maxAgents));
        profile.fish.separationRadius = static_cast<float>(settings.separationRadius);
        profile.fish.cohesionRadius   = static_cast<float>(settings.cohesionRadius);
        profile.fish.alignmentRadius  = static_cast<float>(settings.alignmentRadius);
        profile.fish.separationWeight = static_cast<float>(settings.separationWeight);
        profile.fish.cohesionWeight   = static_cast<float>(settings.cohesionWeight);
        profile.fish.alignmentWeight  = static_cast<float>(settings.alignmentWeight);
        profile.fish.maxSpeed         = static_cast<float>(settings.maxSpeed);
    }
    auto& base               = profile.base();
    base.fixedDt             = static_cast<float>(settings.fixedDt);
    base.agentRadius         = static_cast<float>(settings.agentRadius);
    base.obstaclePredictTime = static_cast<float>(settings.obstaclePredictTime);
    base.seed                = static_cast<std::uint32_t>(settings.seed);
    return profile;
}

GpuAgentsDocumentTarget::GpuAgentsDocumentTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor GpuAgentsDocumentTarget::describe() const {
    return {TargetId(id_),
            "gpuagents-effect",
            revisionValue(),
            false,
            {CapabilityId("eve.editor.target.gpuagents-effect-properties")}};
}

void* GpuAgentsDocumentTarget::queryCapability(const CapabilityId& c) {
    return c == CapabilityId("eve.editor.target.gpuagents-effect-properties") ? static_cast<IPropertyProvider*>(this)
                                                                              : nullptr;
}

bool GpuAgentsDocumentTarget::matches(const SelectionSnapshot& s) const {
    return s.items.size() == 1 && s.items.front().target == TargetId(id_);
}

eve::Result<eve::Revision> GpuAgentsDocumentTarget::currentRevision(const SelectionSnapshot& s) const {
    if (!matches(s)) {
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "GPU Agents selection mismatch", "editor.gpuagents.selection"));
    }
    return eve::Result<eve::Revision>::success(eve::Revision(revisionValue()));
}

PropertySchema GpuAgentsDocumentTarget::schema(const SelectionSnapshot&) const { return gpuAgentsEffectSchema(); }

PropertyReadResult GpuAgentsDocumentTarget::read(const SelectionSnapshot& s, const PropertyPath& p) const {
    if (!matches(s) || !schema(s).find(p)) return {};
    const auto* v = field(settingsValue(settings_), p.value());
    return v ? PropertyReadResult{editing::PropertyReadState::Value, *v, {}} : PropertyReadResult{};
}

EditorResult<DomainOperation> GpuAgentsDocumentTarget::makeSet(const SelectionSnapshot& s, const PropertyPath& p,
                                                               const EditorValue& v, PropertySetMode m) const {
    if (m == PropertySetMode::Reset) return makeReset(s, p);
    auto d = schema(s).find(p);
    if (!matches(s) || !d || m != PropertySetMode::Absolute) {
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.gpuagents.set"),
                                                     "GPU Agents property requires a matching absolute edit");
    }
    auto checked = validatePropertyValue(*d, v);
    if (!checked.ok()) return EditorResult<DomainOperation>::failure(checked.status());
    EditorValue candidate                                = settingsValue(settings_);
    (*candidate.getIf<EditorValue::Object>())[p.value()] = v;
    auto parsed                                          = parse(candidate);
    if (!parsed.ok()) return EditorResult<DomainOperation>::failure(parsed.status());
    DomainOperation op;
    op.type        = "gpuagents.settings.replace.v1";
    op.inverseType = op.type;
    op.target      = TargetId(id_);
    op.payload     = settingsValue(parsed.value());
    op.inverse     = settingsValue(settings_);
    op.hasInverse  = true;
    return eve::editing::applied<DomainOperation>(std::move(op));
}

EditorResult<DomainOperation> GpuAgentsDocumentTarget::makeReset(const SelectionSnapshot& s,
                                                                 const PropertyPath&      p) const {
    auto d = schema(s).find(p);
    if (!matches(s) || !d) {
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.gpuagents.reset"),
                                                     "GPU Agents property reset requires a matching selection");
    }
    return makeSet(s, p, d->defaultValue, PropertySetMode::Absolute);
}

EditorResult<void> GpuAgentsDocumentTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_) || operation.type != "gpuagents.settings.replace.v1") {
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.gpuagents.apply"),
                                          "Unsupported GPU Agents domain operation");
    }
    auto parsed = parse(operation.payload);
    if (!parsed.ok()) return EditorResult<void>::failure(parsed.status());
    settings_ = parsed.value();
    bumpRevision();
    return eve::editing::applied();
}

std::vector<EditorDiagnostic> GpuAgentsDocumentTarget::validate() const { return validateSettings(settings_); }

EditorValue GpuAgentsDocumentTarget::snapshotValue() const { return settingsValue(settings_); }

EditorResult<void> GpuAgentsDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    auto parsed = parse(snapshot);
    if (!parsed.ok()) return EditorResult<void>::failure(parsed.status());
    settings_ = parsed.value();
    bumpRevision();
    return eve::editing::applied();
}

EditorResult<void> GpuAgentsRuntimeApplier::apply(const GpuAgentsDocumentTarget& target,
                                                  gpuagents::EffectBackend*      backend,
                                                  gpuagents::GpuAgentWorld*      world) const {
    if (!backend) {
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.gpuagents.runtime"),
                                          "EffectBackend is required");
    }
    const auto diagnostics = target.validate();
    if (std::any_of(diagnostics.begin(), diagnostics.end(),
                    [](const auto& d) { return d.severity() == DiagnosticSeverity::Error; })) {
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.gpuagents.runtime-invalid"),
                                          "Cannot apply invalid GPU Agents settings");
    }
    const auto profile = toEffectProfile(target.settings());
    auto       cfg     = backend->configure(profile, target.targetId().value());
    if (!cfg.ok()) return EditorResult<void>::failure(cfg.status());
    if (world && target.settings().kind == "LifeNetwork") {
        world->surface().initFlat(
            {static_cast<float>(target.settings().originX), static_cast<float>(target.settings().originY),
             static_cast<float>(target.settings().originZ)},
            static_cast<float>(target.settings().worldSize), static_cast<int>(target.settings().fieldResolution),
            static_cast<float>(target.settings().originY));
    }
    return eve::editing::applied();
}

}  // namespace eve::gpuagents_editing
