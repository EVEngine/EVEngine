#include "scene_editor/EditorSceneComponentPayload.h"
#include "scene_editor/EditorSceneTarget.h"
#include "editor/EditorTransactionService.h"
#include "audio_editor/EditorAudioTarget.h"
#include "material_editor/EditorMaterialTarget.h"
#include "physics_editor/EditorPhysicsTarget.h"

#include "zeroerr/unittest.h"

#include <memory>

using namespace eve::editor;

namespace {

const EditorValue* field(const EditorValue& value, const char* name) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

class HealthPayloadProvider final : public ISceneComponentPayloadProvider,
                                    public IDomainOperationTarget,
                                    public IDomainOperationTargetStaging {
public:
    explicit HealthPayloadProvider(std::string id = "health-payload") : id_(std::move(id)) {}

    const std::string& componentType() const override {
        static const std::string type = "health";
        return type;
    }
    TargetId targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return {}; }
    void clearDirtyRegion() override {}

    std::vector<SceneComponentPayloadRef> components(const TargetId& scene,
                                                      const ObjectId& object) const override {
        if (object != ObjectId("hero")) return {};
        return {{scene, object, StableId("hero.health"), componentType(), generation_, revision_}};
    }

    EditorResult<IDomainOperationTarget*> payloadOperationTarget(
        const SelectionSnapshot&) const override {
        return eve::editing::applied<IDomainOperationTarget*>(
            const_cast<HealthPayloadProvider*>(this));
    }

    std::vector<EditorDiagnostic> validateComponent(
        const SceneComponentPayloadRef& component) const override {
        if (component.generation != generation_)
            return {eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("test.health.stale"),
                DiagnosticSeverity::Error, "Health component reference is stale")};
        return {};
    }

    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override {
        return eve::Result<eve::Revision>::success(eve::Revision(revision_));
    }

    PropertySchema schema(const SelectionSnapshot&) const override {
        PropertySchema result;
        result.typeId = componentType();
        PropertyDescriptor points;
        points.path = PropertyPath("points");
        points.type = PropertyType::Float;
        points.defaultValue = 100.0;
        points.numeric.minimum = 0.0;
        result.properties.push_back(std::move(points));
        return result;
    }

    PropertyReadResult read(const SelectionSnapshot& selection,
                            const PropertyPath& path) const override {
        if (selection.items.size() != 1 || selection.items.front().item != StableId("hero.health") ||
            path != PropertyPath("points"))
            return {};
        return {PropertyReadState::Value, points_, {}};
    }

    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection,
                                          const PropertyPath& path, const EditorValue& value,
                                          PropertySetMode mode) const override {
        if (mode != PropertySetMode::Absolute || selection.items.size() != 1 ||
            selection.items.front().item != StableId("hero.health") ||
            path != PropertyPath("points"))
            return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                RuleId("test.health.selection"), "Invalid health component selection");
        const auto validation = validatePropertyValue(schema(selection).properties.front(), value);
        if (!validation.ok()) return EditorResult<DomainOperation>::failure(validation.status());
        DomainOperation operation;
        operation.type = "test.health.set.v1";
        operation.target = TargetId(id_);
        operation.payload = EditorValue::Object{{"component", "hero.health"}, {"value", value}};
        operation.inverse = EditorValue::Object{{"component", "hero.health"}, {"value", points_}};
        operation.hasInverse = true;
        operation.affectedObjects.push_back({TargetId(id_), "hero.health", generation_});
        operation.affectedProperties.push_back(path.value());
        operation.mergeKey = "health:hero.health:points";
        return eve::editing::applied<DomainOperation>(std::move(operation));
    }

    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override {
        return makeSet(selection, path, 100.0, PropertySetMode::Absolute);
    }

    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.target != TargetId(id_) || operation.type != "test.health.set.v1")
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.health.operation"),
                                             "Invalid health operation");
        const EditorValue* component = field(operation.payload, "component");
        const EditorValue* value = field(operation.payload, "value");
        const auto* componentId = component ? component->getIf<std::string>() : nullptr;
        const auto* number = value ? value->getIf<double>() : nullptr;
        if (!componentId || *componentId != "hero.health" || !number || *number < 0.0)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.health.payload"),
                                             "Invalid health payload");
        points_ = *number;
        ++revision_;
        return eve::editing::applied<void>();
    }

    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<HealthPayloadProvider>(*this);
    }

    EditorResult<void> commitDomainState(
        std::unique_ptr<IDomainOperationTarget> candidate) override {
        auto* typed = dynamic_cast<HealthPayloadProvider*>(candidate.get());
        if (!typed)
            return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("test.health.candidate"),
                                             "Invalid health candidate");
        *this = *typed;
        return eve::editing::applied<void>();
    }

    double points() const { return points_; }
    void invalidate() { ++generation_; }

private:
    std::string id_;
    unsigned long long revision_ = 1;
    std::uint64_t generation_ = 7;
    double points_ = 80.0;
};

class RecordingAudioSink final : public IAudioSourceRuntimeSink {
public:
    EditorResult<void> publish(const AudioSourceTarget& candidate) override {
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Rejected,
                                             RuleId("test.audio.runtime-rejected"),
                                             "Runtime rejected candidate");
        SelectionSnapshot selection;
        selection.items.push_back({SelectionDomain::Scene, TargetId(candidate.targetId()),
                                   StableId("source"), "audio.source"});
        const auto read = candidate.read(selection, PropertyPath("play.volume"));
        const auto* value = read.value.getIf<double>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Failed,
                                             RuleId("test.audio.runtime-value"),
                                             "Candidate volume is unavailable");
        volume = *value;
        revision = candidate.revision();
        ++publishes;
        return eve::editing::applied<void>();
    }

    bool reject = false;
    double volume = 1.0;
    Revision revision = 0;
    int publishes = 0;
};

class RecordingMaterialSink final : public IMaterialRuntimeSink {
public:
    EditorResult<void> publish(const MaterialDocumentTarget& candidate) override {
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Rejected,
                                             RuleId("test.material.runtime-rejected"),
                                             "Runtime rejected material candidate");
        SelectionSnapshot selection;
        selection.items.push_back({SelectionDomain::Asset, TargetId(candidate.targetId()),
                                   StableId("material"), "graphics.material"});
        const auto read = candidate.read(selection, PropertyPath("shading.roughness"));
        const auto* value = read.value.getIf<double>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Failed,
                                             RuleId("test.material.runtime-value"),
                                             "Candidate roughness is unavailable");
        roughness = *value;
        revision = candidate.revision();
        ++publishes;
        return eve::editing::applied<void>();
    }

    bool reject = false;
    double roughness = 0.45;
    Revision revision = 0;
    int publishes = 0;
};

EditorResult<TransactionReceipt> commit(IDomainOperationTarget& target,
                                        LocalTransactionBackend& transactions,
                                        const DomainOperation& operation,
                                        Revision baseRevision,
                                        const char* id) {
    TransactionSpec specification;
    specification.id = TransactionId(id);
    specification.label = "Component payload edit";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = baseRevision;
    auto begun = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.begin"),
                                                       "Could not begin transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok())
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.append"),
                                                       "Could not append operation");
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.scene.component_payload_registry_routes_schema_transactions_and_undo") {
    HealthPayloadProvider provider;
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    CHECK_EQ(static_cast<int>(registry.registerProvider(&provider).code()),
             static_cast<int>(EditorStatus::Conflict));

    SceneDocumentTarget scene("scene");
    scene.bindComponentPayloads(&registry);
    auto* payloads = static_cast<ISceneComponentPayloadTarget*>(
        scene.queryCapability(ISceneComponentPayloadTarget::editorCapabilityId()));
    REQUIRE(payloads);
    CHECK_EQ(scene.describe().capabilities.back(),
             ISceneComponentPayloadTarget::editorCapabilityId());

    auto references = payloads->componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), size_t{1});
    CHECK_EQ(references.value().front().component, StableId("hero.health"));
    auto selection = makeSceneComponentSelection("scene-inspector", references.value(), 12);
    REQUIRE(selection.ok());

    auto properties = payloads->propertyProvider(selection.value());
    auto target = payloads->operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(target.ok());
    CHECK_EQ(properties.value()->schema(selection.value()).typeId, std::string("health"));
    CHECK_EQ(*properties.value()->read(selection.value(), PropertyPath("points")).value.getIf<double>(),
             80.0);

    auto operation = properties.value()->makeSet(selection.value(), PropertyPath("points"), 25.0,
                                                  PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    LocalWorldAuthority authority(target.value());
    LocalTransactionBackend transactions(&authority);
    const Revision baseRevision = target.value()->revision();
    REQUIRE(commit(*target.value(), transactions, operation.value(), baseRevision, "health.set").ok());
    CHECK_EQ(provider.points(), 25.0);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(provider.points(), 80.0);

    auto stale = commit(*target.value(), transactions, operation.value(), baseRevision, "health.stale");
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.scene.component_payload_registry_rejects_mixed_and_stale_references") {
    HealthPayloadProvider provider;
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    auto references = registry.componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(references.ok());
    SceneComponentPayloadRef stale = references.value().front();
    provider.invalidate();
    auto diagnostics = registry.validatePayload(stale);
    CHECK_EQ(static_cast<int>(diagnostics.code()), static_cast<int>(EditorStatus::Conflict));

    SelectionSnapshot mixed;
    mixed.items = {{SelectionDomain::Scene, TargetId("scene"), StableId("hero.health"), "health"},
                   {SelectionDomain::Scene, TargetId("scene"), StableId("hero.audio"), "audio"}};
    CHECK_EQ(static_cast<int>(registry.propertyProvider(mixed).code()),
             static_cast<int>(EditorStatus::Rejected));
    const auto unregisterResult = registry.unregisterProvider(&provider);
    REQUIRE_EQ(static_cast<int>(unregisterResult),
               static_cast<int>(SceneComponentChange::Changed));
    auto selection = makeSceneComponentSelection("scene", references.value());
    REQUIRE(selection.ok());
    CHECK_EQ(static_cast<int>(registry.propertyProvider(selection.value()).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

TEST_CASE("editor.scene.component_bindings_reuse_audio_physics_and_material_targets") {
    AudioSourceTarget audio("hero-audio");
    PhysicsColliderTarget collider("hero-collider", 3);
    MaterialDocumentTarget material("hero-material");
    SceneComponentPropertyBindings audioBindings("audio.source");
    SceneComponentPropertyBindings physicsBindings("physics.collider3d");
    SceneComponentPropertyBindings materialBindings("graphics.material");

    const TargetId scene("scene");
    const ObjectId hero("hero");
    REQUIRE(audioBindings.bind(
        {scene, hero, StableId("hero.audio"), "audio.source", 1, 0},
        {SelectionDomain::Scene, TargetId(audio.targetId()), StableId("source"), "audio.source"},
        &audio, &audio, [&] { return audio.validate(); }).ok());
    REQUIRE(physicsBindings.bind(
        {scene, hero, StableId("hero.collider"), "physics.collider3d", 2, 0},
        {SelectionDomain::Scene, TargetId(collider.targetId()), StableId("collider"),
         "physics.collider3d"},
        &collider, &collider, [&] { return collider.validate(); }).ok());
    REQUIRE(materialBindings.bind(
        {scene, hero, StableId("hero.material"), "graphics.material", 3, 0},
        {SelectionDomain::Scene, TargetId(material.targetId()), StableId("material"),
         "graphics.material"},
        &material, &material, [&] { return material.validate(); }).ok());

    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&audioBindings).ok());
    REQUIRE(registry.registerProvider(&physicsBindings).ok());
    REQUIRE(registry.registerProvider(&materialBindings).ok());
    auto all = registry.componentPayloads(scene, hero);
    REQUIRE(all.ok());
    REQUIRE_EQ(all.value().size(), size_t{3});
    CHECK_EQ(all.value().at(0).type, std::string("audio.source"));
    CHECK_EQ(all.value().at(1).type, std::string("graphics.material"));
    CHECK_EQ(all.value().at(2).type, std::string("physics.collider3d"));

    const auto audioRef = all.value().at(0);
    auto selection = makeSceneComponentSelection("inspector", {audioRef});
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    CHECK_EQ(properties.value()->schema(selection.value()).typeId, std::string("audio.source"));
    auto setVolume = properties.value()->makeSet(
        selection.value(), PropertyPath("play.volume"), 0.25, PropertySetMode::Absolute);
    REQUIRE(setVolume.ok());
    LocalWorldAuthority authority(operationTarget.value());
    LocalTransactionBackend transactions(&authority);
    REQUIRE(commit(*operationTarget.value(), transactions, setVolume.value(),
                   operationTarget.value()->revision(), "component.audio.volume").ok());
    CHECK_EQ(*audio.read({"module", {{SelectionDomain::Scene, TargetId(audio.targetId()),
                                     StableId("source"), "audio.source"}}, {}, 0},
                         PropertyPath("play.volume")).value.getIf<double>(), 0.25);
    CHECK_EQ(static_cast<int>(registry.validatePayload(audioRef).code()),
             static_cast<int>(EditorStatus::Conflict));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(*audio.read({"module", {{SelectionDomain::Scene, TargetId(audio.targetId()),
                                     StableId("source"), "audio.source"}}, {}, 0},
                         PropertyPath("play.volume")).value.getIf<double>(), 1.0);

    auto colliderSelection = makeSceneComponentSelection("inspector", {all.value().at(2)});
    REQUIRE(colliderSelection.ok());
    CHECK(registry.propertyProvider(colliderSelection.value()).value()->schema(
              colliderSelection.value()).find(PropertyPath("material.friction")) != nullptr);
    auto materialSelection = makeSceneComponentSelection("inspector", {all.value().at(1)});
    REQUIRE(materialSelection.ok());
    CHECK(registry.propertyProvider(materialSelection.value()).value()->schema(
              materialSelection.value()).find(PropertyPath("shading.roughness")) != nullptr);
}

TEST_CASE("editor.scene.audio_component_publication_is_staged_live_and_reversible") {
    RecordingAudioSink sink;
    AudioSourcePublishingTarget live("live-audio", &sink);
    SceneComponentPropertyBindings bindings("audio.source");
    REQUIRE(bindings.bind(
        {TargetId("scene"), ObjectId("hero"), StableId("hero.audio"), "audio.source", 1, 0},
        {SelectionDomain::Scene, TargetId(live.targetId()), StableId("source"), "audio.source"},
        &live.authoringTarget(), &live,
        [&] { return live.authoringTarget().validate(); }).ok());
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&bindings).ok());
    auto components = registry.componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(components.ok());
    auto selection = makeSceneComponentSelection("inspector", components.value());
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());

    auto change = properties.value()->makeSet(selection.value(), PropertyPath("play.volume"),
                                               0.4, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    LocalWorldAuthority authority(operationTarget.value());
    LocalTransactionBackend transactions(&authority);
    REQUIRE(commit(*operationTarget.value(), transactions, change.value(),
                   operationTarget.value()->revision(), "audio.live.volume").ok());
    CHECK_EQ(sink.volume, 0.4);
    CHECK_EQ(sink.revision, live.revision());
    CHECK_EQ(sink.publishes, 1);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(sink.volume, 1.0);
    CHECK_EQ(sink.publishes, 2);

    auto rejected = properties.value()->makeSet(selection.value(), PropertyPath("play.volume"),
                                                 0.2, PropertySetMode::Absolute);
    REQUIRE(rejected.ok());
    sink.reject = true;
    const Revision before = live.revision();
    auto failed = commit(*operationTarget.value(), transactions, rejected.value(), before,
                         "audio.live.rejected");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(live.revision(), before);
    CHECK_EQ(sink.volume, 1.0);
}

TEST_CASE("editor.scene.material_component_publication_is_staged_live_and_reversible") {
    RecordingMaterialSink sink;
    MaterialPublishingTarget live("live-material", &sink);
    SceneComponentPropertyBindings bindings("graphics.material");
    REQUIRE(bindings.bind(
        {TargetId("scene"), ObjectId("hero"), StableId("hero.material"),
         "graphics.material", 2, 0},
        {SelectionDomain::Asset, TargetId(live.targetId()), StableId("material"),
         "graphics.material"},
        &live.authoringTarget(), &live,
        [&] { return live.authoringTarget().validate(); }).ok());
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&bindings).ok());
    auto components = registry.componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(components.ok());
    auto selection = makeSceneComponentSelection("inspector", components.value());
    REQUIRE(selection.ok());
    auto properties = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());

    auto change = properties.value()->makeSet(selection.value(),
                                               PropertyPath("shading.roughness"), 0.8,
                                               PropertySetMode::Absolute);
    REQUIRE(change.ok());
    LocalWorldAuthority authority(operationTarget.value());
    LocalTransactionBackend transactions(&authority);
    REQUIRE(commit(*operationTarget.value(), transactions, change.value(),
                   operationTarget.value()->revision(), "material.live.roughness").ok());
    CHECK_EQ(sink.roughness, 0.8);
    CHECK_EQ(sink.revision, live.revision());
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(sink.roughness, 0.45);

    auto rejected = properties.value()->makeSet(selection.value(),
                                                 PropertyPath("shading.roughness"), 0.6,
                                                 PropertySetMode::Absolute);
    REQUIRE(rejected.ok());
    sink.reject = true;
    const Revision before = live.revision();
    auto failed = commit(*operationTarget.value(), transactions, rejected.value(), before,
                         "material.live.rejected");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(live.revision(), before);
    CHECK_EQ(sink.roughness, 0.45);
}
