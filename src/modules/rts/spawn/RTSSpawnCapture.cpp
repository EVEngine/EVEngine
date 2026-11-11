#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include "rts/RTSArchetype.h"
#include "rts/spawn/RTSSpawnCaptureInternal.h"
#include "weapon/WeaponCapture.h"
namespace eve::rts_spawn {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "rts.spawn"));
}
}  // namespace
RTSSpawnCapture::RTSSpawnCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSSpawnCapture::~RTSSpawnCapture()                                     = default;
RTSSpawnCapture::RTSSpawnCapture(RTSSpawnCapture&&) noexcept            = default;
RTSSpawnCapture& RTSSpawnCapture::operator=(RTSSpawnCapture&&) noexcept = default;
PersistentId     RTSSpawnCapture::saveEpoch() const noexcept { return impl_ ? impl_->saveEpoch : PersistentId{}; }
SubjectRef       RTSSpawnCapture::faction() const noexcept { return impl_ ? impl_->faction : SubjectRef{}; }
PersistentId     RTSSpawnCapture::sourceOwner() const noexcept { return impl_ ? impl_->owner : PersistentId{}; }
std::uint64_t    RTSSpawnCapture::sourceEpoch() const noexcept { return impl_ ? impl_->epoch : 0; }
spawn::WorldIncarnation RTSSpawnCapture::sourceIncarnation() const noexcept {
    return impl_ ? impl_->incarnation : spawn::WorldIncarnation{};
}
std::span<const CapturedSpawnUnit> RTSSpawnCapture::units() const noexcept {
    return impl_ ? std::span<const CapturedSpawnUnit>(impl_->units) : std::span<const CapturedSpawnUnit>{};
}
std::span<const CapturedFutureUnit> RTSSpawnCapture::futureUnits() const noexcept {
    return impl_ ? std::span<const CapturedFutureUnit>(impl_->future) : std::span<const CapturedFutureUnit>{};
}
Result<std::shared_ptr<const rts::PreparedUnitArchetype>> RTSSpawnCapture::resolveFuture(
    const DefinitionRef& reference) const {
    using R = Result<std::shared_ptr<const rts::PreparedUnitArchetype>>;
    if (!impl_)
        return fail<std::shared_ptr<const rts::PreparedUnitArchetype>>(DiagnosticCode::PreconditionViolation,
                                                                       "Saved provenance was moved");
    for (const auto& future : impl_->future)
        if (future.reference == reference && future.recipe && future.recipe->source()) return R::success(future.recipe);
    return fail<std::shared_ptr<const rts::PreparedUnitArchetype>>(DiagnosticCode::NotFound,
                                                                   "Saved future recipe not admitted");
}
std::span<const CapturedSpawnReceipt> RTSSpawnCapture::receipts() const noexcept {
    return impl_ ? std::span<const CapturedSpawnReceipt>(impl_->receipts) : std::span<const CapturedSpawnReceipt>{};
}
std::uint64_t           RTSSpawnCapture::lifecycleSequence() const noexcept { return impl_ ? impl_->sequence : 0; }
const rts::CommandSpec* RTSSpawnCapture::initialCommand() const noexcept {
    return impl_ ? impl_->command.get() : nullptr;
}
SubjectRef RTSSpawnCapture::initialCommandTarget() const noexcept {
    return impl_ ? impl_->commandTarget : SubjectRef{};
}
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "rts.spawn.archive"))};
}
template <class T>
T take(Result<T> result) {
    if (!result) throw Invalid{result.status()};
    return std::move(result).takeValue();
}
void checked(Result<void> result) {
    if (!result) throw Invalid{result.status()};
}
const std::string& text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected source archive text");
    return *s;
}
const Value::Array& array(const Value& v) {
    const auto* a = v.getIf<Value::Array>();
    if (!a) invalid("Expected source archive array");
    return *a;
}
const Value::Array& tuple(const Value& v, std::size_t size) {
    const auto& a = array(v);
    if (a.size() != size) invalid("Unknown or missing source tuple fields");
    return a;
}
PersistentId identity(const Value& v) {
    const auto& s  = text(v);
    const auto  id = PersistentId::parse(s);
    if (!id || id->isNil() || id->format() != s) invalid("Expected canonical non-nil source UUID");
    return *id;
}
std::uint64_t counter(const Value& v, bool zero = true) {
    const auto&   s = text(v);
    std::uint64_t n = 0;
    const auto    p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s || (!zero && !n))
        invalid("Invalid canonical source counter");
    return n;
}
std::int64_t integer(const Value& v, std::int64_t lo, std::int64_t hi) {
    if (!v.isInt64() || v.asInt() < lo || v.asInt() > hi) invalid("Source integer out of range");
    return v.asInt();
}
double number(const Value& v) {
    if (!v.isInt64() && !v.isDouble()) invalid("Expected source numeric value");
    const auto n = v.isDouble() ? v.asDouble() : static_cast<double>(v.asInt());
    if (!std::isfinite(n) || !std::isfinite(static_cast<float>(n)))
        invalid("Source number exceeds native finite range");
    return n;
}
bool boolean(const Value& v) {
    const auto* result = v.getIf<bool>();
    if (!result) invalid("Expected source boolean");
    return *result;
}
DefinitionRef reference(const Value& v) {
    auto result = take(DefinitionRef::parse(text(v)));
    if (result.id().namespaceName() != "unit" || result.format() != text(v))
        invalid("Expected canonical unit reference");
    return result;
}
Value sourceValue(const spawn::SpawnerHandle& source, const RTSSpawnCapture& capture) {
    if (source.owner != capture.sourceOwner() || source.epoch != capture.sourceEpoch() || source.owner.isNil() ||
        !source.epoch || !source.slot.isValid() || source.incarnation != capture.sourceIncarnation())
        invalid("Source address contradicts captured owner/epoch/incarnation");
    return Value::Array{std::int64_t(source.slot.index()), std::int64_t(source.slot.generation())};
}
spawn::SpawnerHandle sourceAddress(const Value& v, PersistentId owner, std::uint64_t epoch) {
    const auto& a          = tuple(v, 2);
    const auto  slot       = integer(a[0], 0, std::numeric_limits<std::uint32_t>::max() - 1);
    const auto  generation = integer(a[1], 1, std::numeric_limits<std::uint32_t>::max());
    if (owner.isNil() || !epoch) invalid("Source slot without bound owner/epoch");
    return {RuntimeHandle<spawn::SpawnerTag>(static_cast<std::uint32_t>(slot), static_cast<std::uint32_t>(generation)),
            owner,
            epoch,
            {}};
}
Value              point(rts::WorldPosition p) { return Value::Array{p.x, p.y}; }
rts::WorldPosition position(const Value& v) {
    const auto& a = tuple(v, 2);
    return {static_cast<float>(number(a[0])), static_cast<float>(number(a[1]))};
}
Value commandValue(const RTSSpawnCapture& capture) {
    const auto* command = capture.initialCommand();
    if (!command) {
        if (capture.initialCommandTarget().isValid()) invalid("Initial target without command");
        return {};
    }
    checked(command->validate());
    if (command->targetEntity.table) invalid("Initial command retains runtime target");
    return Value::Array{
        rts::orderKindName(command->kind),
        point(command->target),
        command->definitionId,
        command->priority,
        command->timeoutSeconds,
        point(command->secondaryTarget),
        command->radius,
        command->append,
        capture.initialCommandTarget().isValid() ? Value(capture.initialCommandTarget().format()) : Value{}};
}
Value encode(const RTSSpawnCapture& capture) {
    if (capture.saveEpoch().isNil() || !capture.faction().isValid() ||
        (capture.sourceOwner().isNil() != (capture.sourceEpoch() == 0)))
        invalid("Invalid source capture boundary");
    if (capture.sourceOwner().isNil() &&
        (!capture.units().empty() || !capture.receipts().empty() || capture.lifecycleSequence()))
        invalid("Unbound source owns published facts");
    Value::Array members, future, receipts;
    PersistentId previous;
    for (const auto& member : capture.units()) {
        const auto id = member.subject.persistentId();
        if (id.isNil() || (!previous.isNil() && !(previous < id)) || !member.recipe || !member.recipe->source())
            invalid("Invalid or unordered source member/recipe");
        previous = id;
        if (member.presence != CapturedUnitPresence::Present && member.presence != CapturedUnitPresence::Absent)
            invalid("Unknown unit presence");
        if (member.weapon &&
            (member.presence != CapturedUnitPresence::Present || member.weapon->saveEpoch() != capture.saveEpoch()))
            invalid("Weapon without present member or matching save epoch");
        members.push_back(
            Value::Array{member.subject.format(), member.origin ? sourceValue(*member.origin, capture) : Value{},
                         member.presence == CapturedUnitPresence::Present, take(member.recipe->encodeState()),
                         member.weapon ? take(member.weapon->encodeState()) : Value{}});
    }
    std::string lastReference;
    for (const auto& item : capture.futureUnits()) {
        const auto name = item.reference.format();
        if (!item.recipe || !item.recipe->source() || item.reference.id().namespaceName() != "unit" ||
            (!lastReference.empty() && !(lastReference < name)) || item.recipe->source()->get().type != "unit" ||
            item.recipe->source()->get().id != item.reference.id().name())
            invalid("Invalid future recipe reference or order");
        lastReference = name;
        future.push_back(Value::Array{name, take(item.recipe->encodeState())});
    }
    std::optional<std::pair<std::uint32_t, std::uint32_t>> lastSource;
    std::set<PersistentId>                                 operations;
    for (const auto& receipt : capture.receipts()) {
        const auto key = std::pair{receipt.source.slot.index(), receipt.source.slot.generation()};
        if ((lastSource && !(lastSource.value() < key)) || receipt.operation.isNil() || receipt.ordinal.isZero() ||
            !operations.insert(receipt.operation).second || receipt.units.empty() || receipt.units.size() > 1024 ||
            receipt.units.size() != receipt.positions.size())
            invalid("Invalid bounded receipt scope or shape");
        lastSource = key;
        Value::Array           units, positions;
        std::set<PersistentId> subjects;
        for (std::size_t i = 0; i < receipt.units.size(); ++i) {
            const auto& unit = receipt.units[i];
            const auto  p    = receipt.positions[i];
            if (!unit.subject.isValid() || !subjects.insert(unit.subject.persistentId()).second ||
                unit.definition.id().namespaceName() != "unit" || !std::isfinite(p.x) || !std::isfinite(p.y) ||
                !std::isfinite(static_cast<float>(p.x)) || !std::isfinite(static_cast<float>(p.y)))
                invalid("Invalid receipt member or native position");
            units.push_back(Value::Array{unit.subject.format(), unit.definition.format()});
            positions.push_back(Value::Array{p.x, p.y});
        }
        receipts.push_back(Value::Array{sourceValue(receipt.source, capture), receipt.operation.format(),
                                        std::to_string(receipt.ordinal.value()), Value(std::move(units)),
                                        Value(std::move(positions))});
    }
    return Value::object(
        {{"faction", capture.faction().format()},
         {"source", Value::Array{capture.sourceOwner().isNil() ? Value{} : Value(capture.sourceOwner().format()),
                                 std::to_string(capture.sourceEpoch())}},
         {"sequence", std::to_string(capture.lifecycleSequence())},
         {"command", commandValue(capture)},
         {"units", Value(std::move(members))},
         {"future", Value(std::move(future))},
         {"receipts", Value(std::move(receipts))}});
}
}  // namespace
Result<SnapshotEnvelope> RTSSpawnCapture::seal(Revision revision, SimulationTick tick,
                                               const SnapshotHashProvider& hash) const {
    if (!impl_ || revision.isZero())
        return fail<SnapshotEnvelope>(DiagnosticCode::PreconditionViolation, "Source capture moved or zero revision");
    try {
        auto payload = encode(*this);
        return makeSnapshotEnvelope("eve.rts-spawn", *LogicalId::fromParts("rts", "spawn-state"), SchemaVersion(1),
                                    impl_->saveEpoch, revision, tick, std::move(payload), hash);
    } catch (const Invalid& error) {
        return Result<SnapshotEnvelope>::failure(error.status);
    }
}
Result<RTSSpawnCapture> RTSSpawnCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                               const SnapshotHashProvider& hash) {
    using R       = Result<RTSSpawnCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return fail<RTSSpawnCapture>(DiagnosticCode::UnknownVersion, "Unknown RTS source version");
    if (snapshot.type != "eve.rts-spawn" || snapshot.schema != *LogicalId::fromParts("rts", "spawn-state") ||
        epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return fail<RTSSpawnCapture>(DiagnosticCode::Conflict, "RTS source boundary mismatch");
    try {
        const auto* root = snapshot.payload.getIf<Value::Object>();
        if (!root || root->size() != 7) invalid("Unknown or missing source fields");
        for (const auto* key : {"faction", "source", "sequence", "command", "units", "future", "receipts"})
            if (!root->contains(key)) invalid("Missing source field");
        auto impl         = std::make_unique<Impl>();
        impl->saveEpoch   = epoch;
        impl->faction     = SubjectRef::fromPersistentId(identity(root->at("faction")));
        const auto& scope = tuple(root->at("source"), 2);
        if (!scope[0].isNull()) impl->owner = identity(scope[0]);
        impl->epoch    = counter(scope[1]);
        impl->sequence = counter(root->at("sequence"));
        if (!root->at("command").isNull()) {
            const auto& a       = tuple(root->at("command"), 9);
            auto        command = std::make_unique<rts::CommandSpec>();
            bool        found   = false;
            const auto& kind    = text(a[0]);
            for (int n = 0; n <= int(rts::OrderKind::SupplyRelay); ++n) {
                const auto value = static_cast<rts::OrderKind>(n);
                if (kind == rts::orderKindName(value)) {
                    command->kind = value;
                    found         = true;
                    break;
                }
            }
            if (!found) invalid("Unknown initial command kind");
            command->target       = position(a[1]);
            command->definitionId = text(a[2]);
            command->priority =
                static_cast<int>(integer(a[3], std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
            if (!a[4].isDouble() && !a[4].isInt64()) invalid("Expected command timeout");
            command->timeoutSeconds  = a[4].isDouble() ? a[4].asDouble() : static_cast<double>(a[4].asInt());
            command->secondaryTarget = position(a[5]);
            command->radius          = static_cast<float>(number(a[6]));
            command->append          = boolean(a[7]);
            if (!a[8].isNull()) impl->commandTarget = SubjectRef::fromPersistentId(identity(a[8]));
            checked(command->validate());
            impl->command = std::move(command);
        }
        for (const auto& value : array(root->at("units"))) {
            const auto&       a = tuple(value, 5);
            CapturedSpawnUnit member;
            member.subject = SubjectRef::fromPersistentId(identity(a[0]));
            if (!a[1].isNull()) member.origin = sourceAddress(a[1], impl->owner, impl->epoch);
            member.presence = boolean(a[2]) ? CapturedUnitPresence::Present : CapturedUnitPresence::Absent;
            member.recipe   = std::make_shared<const rts::PreparedUnitArchetype>(
                take(rts::PreparedUnitArchetype::prepareState(a[3])));
            if (!a[4].isNull())
                member.weapon =
                    std::make_shared<const weapon::WeaponCapture>(take(weapon::WeaponCapture::prepareState(a[4])));
            impl->units.push_back(std::move(member));
        }
        for (const auto& value : array(root->at("future"))) {
            const auto& a      = tuple(value, 2);
            auto        ref    = reference(a[0]);
            auto        recipe = take(rts::PreparedUnitArchetype::prepareState(a[1]));
            impl->future.push_back(
                {std::move(ref), std::make_shared<const rts::PreparedUnitArchetype>(std::move(recipe))});
        }
        for (const auto& value : array(root->at("receipts"))) {
            const auto&          a = tuple(value, 5);
            CapturedSpawnReceipt receipt;
            receipt.source    = sourceAddress(a[0], impl->owner, impl->epoch);
            receipt.operation = identity(a[1]);
            receipt.ordinal   = spawn::OperationOrdinal(counter(a[2], false));
            for (const auto& value : array(a[3])) {
                const auto& member  = tuple(value, 2);
                auto        subject = identity(member[0]);
                auto        ref     = reference(member[1]);
                receipt.units.push_back({SubjectRef::fromPersistentId(subject), std::move(ref)});
            }
            for (const auto& value : array(a[4])) {
                const auto& p = tuple(value, 2);
                receipt.positions.push_back({number(p[0]), number(p[1])});
            }
            impl->receipts.push_back(std::move(receipt));
        }
        RTSSpawnCapture result(std::move(impl));
        auto canonical = encode(result);  // Same invariant kernel as native sealing; no source lease is manufactured.
        (void)canonical;
        return R::success(std::move(result));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::rts_spawn
