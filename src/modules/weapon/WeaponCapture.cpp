#include "weapon/WeaponCapture.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include "attributes/AttributeProjectionCapture.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponCaptureCodecInternal.h"
namespace eve::weapon {
namespace {
bool sameHandle(const ecs::EntityHandle& a, const ecs::EntityHandle& b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
template <class T, class E>
T* existing(E& entity) noexcept {
    auto* buffer = entity.getComponentManager().template getComponentBuffer<T>();
    return buffer && buffer->has(entity.id) ? buffer->ptr(entity.id) : nullptr;
}
Result<WeaponCapture> fail(DiagnosticCode code, const char* message) {
    return Result<WeaponCapture>::failure(Diagnostic::error(code, message, "weapon.capture"));
}
CapturedWeaponAttributes selectedAttributes(const attributes::AttributeProjectionCapture& native) {
    CapturedWeaponAttributes result;
    result.initialized                               = *native.initialized();
    result.revision                                  = native.revision();
    const auto&                               values = native.values()->get();
    constexpr std::array<std::string_view, 4> names{
        WeaponAttributeAdapter::manaAttribute, WeaponAttributeAdapter::maxManaAttribute,
        WeaponAttributeAdapter::staminaAttribute, WeaponAttributeAdapter::maxStaminaAttribute};
    for (const auto name : names) result.bases.push_back({std::string(name), values.getBase(std::string(name))});
    for (int index = 0; index < values.modifierCount(); ++index) {
        const auto* modifier = values.modifierAt(index);
        if (modifier && std::find(names.begin(), names.end(), modifier->attribute) != names.end())
            result.modifiers.push_back(*modifier);
    }
    return result;
}
}  // namespace
Result<Value> CapturedAmmoPool::encodeState() const {
    static_assert(std::numeric_limits<int>::digits == 31, "Pool v1 stores signed int32 facts");
    if (archiveId.isNil())
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil pool archive reference required"));
    return Result<Value>::success(Value::object({{"schema", "weapon.ammo-pool"},
                                                 {"version", 1},
                                                 {"archive", archiveId.format()},
                                                 {"name", identity.id},
                                                 {"ammo_type", identity.ammoType},
                                                 {"count", state.count},
                                                 {"max", state.max}}));
}
Result<CapturedAmmoPool> CapturedAmmoPool::prepareState(const Value& state) {
    using R          = Result<CapturedAmmoPool>;
    const auto* root = state.getIf<Value::Object>();
    if (!root || root->size() != 7 || !root->contains("schema") || !root->contains("version") ||
        !root->contains("archive") || !root->contains("name") || !root->contains("ammo_type") ||
        !root->contains("count") || !root->contains("max"))
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unknown or missing pool fields"));
    const auto* schema = root->at("schema").getIf<std::string>();
    if (!schema || *schema != "weapon.ammo-pool")
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid pool schema"));
    if (!root->at("version").isInt64() || root->at("version").asInt() != 1)
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown pool version"));
    const auto* archive = root->at("archive").getIf<std::string>();
    const auto* name    = root->at("name").getIf<std::string>();
    const auto* ammo    = root->at("ammo_type").getIf<std::string>();
    if (!archive || !name || !ammo || !root->at("count").isInt64() || !root->at("max").isInt64())
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Invalid pool field types"));
    auto id = PersistentId::parse(*archive);
    if (!id || id->isNil() || id->format() != *archive)
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Noncanonical pool archive identity"));
    const auto count   = root->at("count").asInt();
    const auto maximum = root->at("max").asInt();
    if (count < std::numeric_limits<int>::min() || count > std::numeric_limits<int>::max() ||
        maximum < std::numeric_limits<int>::min() || maximum > std::numeric_limits<int>::max())
        return R::failure(Diagnostic::error(DiagnosticCode::ParseError, "Pool integer overflow"));
    return R::success(CapturedAmmoPool{{*name, *ammo}, {static_cast<int>(count), static_cast<int>(maximum)}, *id});
}
struct WeaponPoolBindings::Impl {
    struct Entry {
        AmmoPoolLink link;
        PersistentId id;
    };
    PersistentId epoch;
    using Key = std::tuple<std::type_index, std::size_t, std::uint64_t>;
    std::map<Key, Entry> entries;
};
WeaponPoolBindings::WeaponPoolBindings(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WeaponPoolBindings::~WeaponPoolBindings()                                        = default;
WeaponPoolBindings::WeaponPoolBindings(WeaponPoolBindings&&) noexcept            = default;
WeaponPoolBindings& WeaponPoolBindings::operator=(WeaponPoolBindings&&) noexcept = default;
PersistentId        WeaponPoolBindings::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::size_t         WeaponPoolBindings::size() const noexcept { return impl_ ? impl_->entries.size() : 0; }
Result<WeaponPoolBindings> WeaponPoolBindings::capture(ecs::Table& world, PersistentId epoch) {
    using R = Result<WeaponPoolBindings>;
    if (epoch.isNil())
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Non-nil pool graph epoch required"));
    if (world.is_deferred())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Pool graph needs idle world"));
    auto impl   = std::make_unique<Impl>();
    impl->epoch = epoch;
    struct Group {
        AmmoPoolLink link;
        std::string  member;
    };
    std::map<Impl::Key, Group> groups;
    std::set<std::string>      members;
    if (world.getManager(std::type_index(typeid(WeaponEntity)))) {
        auto view = ecs::View<WeaponEntity>(world);
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto&       gun   = *it.entity();
            const auto* state = existing<WeaponEntity::State>(gun);
            if (!state || !state->ammoPool.isBound()) continue;
            auto* pool = state->ammoPool.resolve();
            if (!pool || state->ammoPool.handle().table != &world)
                return R::failure(
                    Diagnostic::error(DiagnosticCode::StaleHandle, "Weapon pool graph has a retired or foreign pool"));
            const auto* binding = existing<WeaponEntity::DefinitionBinding>(gun);
            if (!binding || binding->identity.instanceId.isNil())
                return R::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "Pool member needs stable weapon instance identity"));
            auto member = binding->identity.instanceId.format();
            if (!members.insert(member).second)
                return R::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Pool members reuse weapon instance identity"));
            const auto& handle = state->ammoPool.handle();
            auto [entry, inserted] =
                groups.try_emplace({handle.type, handle.id, handle.generation}, Group{state->ammoPool, member});
            if (!inserted && member < entry->second.member) entry->second.member = std::move(member);
        }
    }
    for (auto& [key, group] : groups)
        impl->entries.emplace(key, Impl::Entry{group.link, epoch.child("weapon-ammo-pool/" + group.member)});
    return R::success(WeaponPoolBindings(std::move(impl)));
}
struct WeaponCapture::Impl {
    PersistentId                                          epoch;
    CapturedWeapon                                        facts;
    std::optional<attributes::AttributeProjectionCapture> attributes;
};
WeaponCapture::WeaponCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WeaponCapture::~WeaponCapture()                                   = default;
WeaponCapture::WeaponCapture(WeaponCapture&&) noexcept            = default;
WeaponCapture& WeaponCapture::operator=(WeaponCapture&&) noexcept = default;
PersistentId   WeaponCapture::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::optional<std::reference_wrapper<const CapturedWeapon>> WeaponCapture::facts() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->facts);
}
std::optional<std::reference_wrapper<const attributes::AttributeProjectionCapture>> WeaponCapture::attributes()
    const noexcept {
    if (!impl_ || !impl_->attributes) return std::nullopt;
    return std::cref(*impl_->attributes);
}
Result<Value> WeaponCapture::encodeState() const {
    using namespace capture_codec;
    if (!impl_)
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Weapon capture moved"));
    try {
        const auto& f = impl_->facts;
        if (static_cast<int>(f.state.resource.kind) > 4 || static_cast<int>(f.state.stage) > 3 ||
            static_cast<int>(f.state.selector) > 2)
            invalid("Invalid native runtime enum");
        if (f.request.targetHandle.table) invalid("Captured request must not retain an ECS target handle");
        auto result =
            Value::object({{"schema", "weapon.capture"},
                           {"version", 1},
                           {"epoch", impl_->epoch.format()},
                           {"identity", encode(f.identity)},
                           {"binding", Value::Array{f.binding.identity.instanceId.format(),
                                                    f.binding.identity.definition.id().format(),
                                                    std::to_string(f.binding.identity.definitionGeneration.value()),
                                                    encode(f.binding.reloadPolicy), f.binding.active}},
                           {"definition", encode(f.definition)},
                           {"state", encodeRuntime(f.state)},
                           {"request", encode(f.request)},
                           {"target", f.target.isValid() ? Value(f.target.format()) : Value{}},
                           {"stages", f.stages ? encode(*f.stages) : Value{}},
                           {"aim", encode(f.aim)},
                           {"attributes", impl_->attributes ? take(impl_->attributes->encodeState()) : Value{}},
                           {"pool", f.pool ? take(f.pool->encodeState()) : Value{}}});
        auto admitted = prepareState(result);
        if (!admitted) return Result<Value>::failure(admitted.status());
        return Result<Value>::success(std::move(result));
    } catch (const Invalid& error) {
        return Result<Value>::failure(error.status);
    }
}
Result<WeaponCapture> WeaponCapture::prepareState(const Value& state) {
    using namespace capture_codec;
    using R = Result<WeaponCapture>;
    try {
        const auto* root = state.getIf<Value::Object>();
        if (!root || root->size() != 13) invalid("Unknown or missing weapon capture fields");
        for (const auto* key : {"schema", "version", "epoch", "identity", "binding", "definition", "state", "request",
                                "target", "stages", "aim", "attributes", "pool"})
            if (!root->contains(key)) invalid("Missing weapon capture field");
        if (text(root->at("schema")) != "weapon.capture") invalid("Invalid weapon capture schema");
        if (!root->at("version").isInt64() || root->at("version").asInt() != 1)
            return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown weapon capture version"));
        auto impl   = std::make_unique<Impl>();
        impl->epoch = id(root->at("epoch"));
        auto& f     = impl->facts;
        decode(root->at("identity"), f.identity);
        const auto* binding = root->at("binding").getIf<Value::Array>();
        if (!binding || binding->size() != 5) invalid("Unknown or missing weapon binding tuple fields");
        const auto instance  = id((*binding)[0]);
        auto       reference = take(DefinitionRef::parse(text((*binding)[1])));
        if (reference.id().format() != text((*binding)[1]) || reference.id().namespaceName() != "weapon")
            invalid("Noncanonical weapon definition reference");
        f.binding.identity = take(
            definition::InstanceIdentity::create(instance, std::move(reference), Generation(counter((*binding)[2]))));
        decode((*binding)[3], f.binding.reloadPolicy);
        decode((*binding)[4], f.binding.active);
        decode(root->at("definition"), f.definition);
        if (f.definition.id != f.binding.identity.definition.id().name())
            invalid("Weapon definition identities disagree");
        f.state = take(decodeRuntime(root->at("state")));
        // Reuse the existing runtime ledger; full capture additionally refuses aliases, missing resource fields
        // and ignored keys rather than accepting a legacy partial runtime snapshot as complete facts.
        if (take(encodeRuntime(f.state).toJson()) != take(root->at("state").toJson()))
            invalid("Weapon runtime state is not complete canonical data");
        decode(root->at("request"), f.request);
        if (!root->at("target").isNull()) f.target = SubjectRef::fromPersistentId(id(root->at("target")));
        if (!root->at("stages").isNull()) {
            f.stages.emplace();
            decode(root->at("stages"), *f.stages);
        }
        decode(root->at("aim"), f.aim);
        if (!root->at("attributes").isNull()) {
            impl->attributes.emplace(
                take(attributes::AttributeProjectionCapture::prepareState(root->at("attributes"))));
            f.attributes = selectedAttributes(*impl->attributes);
        }
        if (!root->at("pool").isNull()) f.pool = take(CapturedAmmoPool::prepareState(root->at("pool")));
        return R::success(WeaponCapture(std::move(impl)));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<WeaponCapture> WeaponCapture::capture(WeaponEntity& weapon, PersistentId saveEpoch,
                                             std::span<const WeaponCaptureTarget> targets,
                                             const WeaponPoolBindings&            pools) {
    using R          = Result<WeaponCapture>;
    const auto owner = ecs::handle_of(&weapon);
    if (saveEpoch.isNil()) return fail(DiagnosticCode::InvalidArgument, "Non-nil save epoch required");
    if (!owner.table || ecs::try_get(owner) != &weapon)
        return fail(DiagnosticCode::StaleHandle, "Live weapon owner required");
    if (owner.table->is_deferred())
        return fail(DiagnosticCode::PreconditionViolation, "Weapon freeze requires an idle world outside Views");
    const auto* identity   = existing<WeaponEntity::Identity>(weapon);
    const auto* binding    = existing<WeaponEntity::DefinitionBinding>(weapon);
    const auto* definition = existing<WeaponEntity::Definition>(weapon);
    const auto* state      = existing<WeaponEntity::State>(weapon);
    const auto* aim        = existing<WeaponEntity::Aim>(weapon);
    if (!identity || !binding || !definition || !definition->def || !state || !aim)
        return fail(DiagnosticCode::NotFound, "Required weapon components or definition missing");
    if (binding->identity.instanceId.isNil() || !binding->identity.definition.id().isValid() ||
        binding->identity.definitionGeneration.isZero())
        return fail(DiagnosticCode::NotFound, "Weapon requires a canonical instance and exact definition identity");
    if (!pools.impl_ || pools.impl_->epoch != saveEpoch)
        return fail(DiagnosticCode::PreconditionViolation,
                    "Weapon pool graph is moved or belongs to another checkpoint");
    auto candidate              = std::make_unique<Impl>();
    candidate->epoch            = saveEpoch;
    auto& result                = candidate->facts;
    result.identity             = *identity;
    result.binding              = *binding;
    result.definition           = *definition->def;
    result.state                = {state->resource,    state->cooldown,  state->burstRemaining, state->burstTimer,
                                   state->jammed,      state->stage,     state->stageTimer,     state->currentSpread,
                                   state->recoilPitch, state->recoilYaw, state->selector,       state->aiming};
    result.request              = state->lastRequest;
    result.request.targetHandle = {};
    const auto target           = state->lastRequest.targetHandle;
    if (target.table) {
        if (target.table != owner.table || !ecs::try_get(target))
            return fail(DiagnosticCode::StaleHandle, "Staged weapon target has retired or belongs to another world");
        bool matched = false;
        for (const auto& mapping : targets) {
            if (!sameHandle(mapping.entity, target)) continue;
            if (!mapping.subject.isValid() || (matched && result.target != mapping.subject))
                return fail(DiagnosticCode::InvalidArgument, "Staged target has invalid or ambiguous stable identity");
            matched       = true;
            result.target = mapping.subject;
        }
        if (!matched) return fail(DiagnosticCode::NotFound, "Staged target requires an authoritative stable identity");
    }
    if (state->ownedStages && state->stages != state->ownedStages.get())
        return fail(DiagnosticCode::Conflict, "Owned weapon stages have an inconsistent borrowed view");
    if (state->stages) result.stages = *state->stages;
    result.aim = *aim;
    if (state->ammoPool.isBound()) {
        auto* pool = state->ammoPool.resolve();
        if (!pool) return fail(DiagnosticCode::StaleHandle, "Bound ammunition pool has retired");
        const auto* poolIdentity = existing<AmmoPoolEntity::Identity>(*pool);
        const auto* poolState    = existing<AmmoPoolEntity::State>(*pool);
        if (!poolIdentity || !poolState)
            return fail(DiagnosticCode::NotFound, "Shared ammunition pool requires existing identity and state");
        const auto& handle = state->ammoPool.handle();
        const auto  entry  = pools.impl_->entries.find({handle.type, handle.id, handle.generation});
        if (entry == pools.impl_->entries.end() || entry->second.link.resolve() != pool)
            return fail(DiagnosticCode::StaleHandle, "Actual shared pool is absent from prepared graph");
        const auto archive = entry->second.id;
        result.pool        = CapturedAmmoPool{*poolIdentity, *poolState, archive};
    }
    if (const auto* selected = existing<WeaponEntity::Attributes>(weapon)) {
        const auto& values = selected->values;
        if ((values.owner().table && !sameHandle(values.owner(), owner)) ||
            (values.initialized() && !sameHandle(values.owner(), owner)))
            return fail(DiagnosticCode::StaleHandle, "Canonical weapon attributes have a different or missing owner");
        auto native = values.capture();
        if (!native) return R::failure(native.status());
        candidate->attributes.emplace(std::move(native).takeValue());
        result.attributes = selectedAttributes(*candidate->attributes);
    }
    return R::success(WeaponCapture(std::move(candidate)));
}
}  // namespace eve::weapon
