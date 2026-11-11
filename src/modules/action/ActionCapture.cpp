#include "action/ActionCapture.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include "action/Action.h"
#include "action/ActionTargetingCodecInternal.h"
namespace eve::action {
namespace {
template <class T>
Result<T> reject(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "action.checkpoint"));
}
bool sameHandle(const ecs::EntityHandle& a, const ecs::EntityHandle& b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
Result<CapturedActionReference> freezeReference(ecs::EntityHandle handle, std::optional<SubjectRef> stable,
                                                ecs::Table& world, std::span<const ActionEntityBinding> bindings) {
    if (handle.table && handle.table != &world)
        return reject<CapturedActionReference>(DiagnosticCode::StaleHandle,
                                               "Action reference belongs to another world");
    for (const auto& binding : bindings) {
        if (!handle.table || !sameHandle(binding.entity, handle)) continue;
        if (!binding.subject.isValid() || (stable && *stable != binding.subject))
            return reject<CapturedActionReference>(DiagnosticCode::Conflict, "Ambiguous action entity identity");
        stable = binding.subject;
    }
    if (!stable || !stable->isValid())
        return reject<CapturedActionReference>(DiagnosticCode::NotFound,
                                               "Referenced action entity needs a stable identity");
    return Result<CapturedActionReference>::success({*stable, handle.table && ecs::try_get(handle)
                                                                  ? ActionEntityPresence::Present
                                                                  : ActionEntityPresence::Retired});
}
Result<ecs::EntityHandle> bindReference(const CapturedActionReference& reference, ecs::Table& world,
                                        std::span<const ActionEntityBinding> bindings) {
    if (reference.presence == ActionEntityPresence::Retired) return Result<ecs::EntityHandle>::success({});
    std::optional<ecs::EntityHandle> found;
    for (const auto& binding : bindings) {
        if (binding.subject != reference.subject) continue;
        if (binding.entity.table != &world || !ecs::try_get(binding.entity))
            return reject<ecs::EntityHandle>(DiagnosticCode::StaleHandle,
                                             "Restored action reference is not live in target world");
        for (const auto& other : bindings)
            if (sameHandle(other.entity, binding.entity) && other.subject != binding.subject)
                return reject<ecs::EntityHandle>(DiagnosticCode::Conflict,
                                                 "One restored incarnation has multiple stable identities");
        if (found && !sameHandle(*found, binding.entity))
            return reject<ecs::EntityHandle>(DiagnosticCode::Conflict,
                                             "Restored action identity has multiple incarnations");
        found = binding.entity;
    }
    if (!found) return reject<ecs::EntityHandle>(DiagnosticCode::NotFound, "Restored action identity is missing");
    return Result<ecs::EntityHandle>::success(*found);
}
bool terminal(ActionPhase phase) noexcept {
    return phase == ActionPhase::Completed || phase == ActionPhase::Cancelled || phase == ActionPhase::Failed;
}
struct FlagGuard {
    bool& value;
    explicit FlagGuard(bool& flag) : value(flag) { value = true; }
    ~FlagGuard() { value = false; }
};
}  // namespace
struct ActionCapture::Impl {
    ActionActivePipeline                 pipeline = ActionActivePipeline::LegacyPorts;
    PersistentId                         epoch;
    ActionExecutionId                    next;
    std::vector<CapturedActionExecution> executions;
};
namespace {
namespace archive {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "action.runtime"))};
}
template <class T>
T take(Result<T> result) {
    if (!result) throw Invalid{result.status()};
    return std::move(result).takeValue();
}
void checked(Result<void> result) {
    if (!result) throw Invalid{result.status()};
}
const Value::Array& array(const Value& v) {
    auto* a = v.getIf<Value::Array>();
    if (!a) invalid("Expected action array");
    return *a;
}
const Value::Array& tuple(const Value& v, std::size_t n) {
    const auto& a = array(v);
    if (a.size() != n) invalid("Unknown or missing action tuple fields");
    return a;
}
const std::string& text(const Value& v) {
    auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected action text");
    return *s;
}
std::uint64_t counter(const Value& v) {
    const auto&   s = text(v);
    std::uint64_t n = 0;
    auto          p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s)
        invalid("Invalid action counter");
    return n;
}
std::int64_t integer(const Value& v) {
    if (!v.isInt64()) invalid("Expected action integer");
    return v.asInt();
}
int enumeration(const Value& v, int max) {
    auto n = integer(v);
    if (n < 0 || n > max) invalid("Unknown action enum");
    return int(n);
}
bool boolean(const Value& v) {
    auto* b = v.getIf<bool>();
    if (!b) invalid("Expected action boolean");
    return *b;
}
Duration duration(const Value& v) {
    auto n = integer(v);
    if (n < 0) invalid("Negative action duration");
    return Duration(n);
}
SubjectRef subject(const Value& v) {
    const auto& s  = text(v);
    auto        id = PersistentId::parse(s);
    if (!id || id->isNil() || id->format() != s) invalid("Invalid canonical action subject");
    return SubjectRef::fromPersistentId(*id);
}
Value optionalSubject(const std::optional<SubjectRef>& s) { return s ? Value(s->format()) : Value{}; }
std::optional<SubjectRef> optionalSubject(const Value& v) {
    return v.isNull() ? std::nullopt : std::optional(subject(v));
}
Value reference(const CapturedActionReference& r) { return Value::Array{r.subject.format(), int(r.presence)}; }
CapturedActionReference reference(const Value& v) {
    const auto& a = tuple(v, 2);
    return {subject(a[0]), ActionEntityPresence(enumeration(a[1], 1))};
}
Value location(const sensing::TargetLocation& l) {
    return std::visit([](const auto& p) -> Value { return Value::Array{int(p.space()), p.x(), p.y(), p.z()}; }, l);
}
sensing::TargetLocation location(const Value& v) {
    const auto& a      = tuple(v, 4);
    int         space  = enumeration(a[0], 3);
    auto        number = [](const Value& v) -> float {
        if (!v.isDouble() && !v.isInt64()) invalid("Expected location number");
        double n = v.isDouble() ? v.asDouble() : double(v.asInt());
        if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) invalid("Invalid location float");
        return float(n);
    };
    auto cell = [](const Value& v) -> std::int32_t {
        auto n = integer(v);
        if (n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max())
            invalid("Invalid location cell");
        return std::int32_t(n);
    };
    if (space == int(sensing::CoordinateSpace::World2D)) {
        if (number(a[3]) != 0) invalid("Nonzero World2D z");
        return take(sensing::WorldPoint::world2D(number(a[1]), number(a[2])));
    }
    if (space == int(sensing::CoordinateSpace::World3D))
        return take(sensing::WorldPoint::world3D(number(a[1]), number(a[2]), number(a[3])));
    if (space == int(sensing::CoordinateSpace::Grid2D)) {
        if (cell(a[3]) != 0) invalid("Nonzero Grid2D z");
        return sensing::GridPoint::grid2D(cell(a[1]), cell(a[2]));
    }
    return sensing::GridPoint::grid3D(cell(a[1]), cell(a[2]), cell(a[3]));
}
Value query(const sensing::TargetingQuery& q) {
    checked(q.validate());
    return Value::Array{q.origin.format(), location(q.originLocation), detail::encodeActionTargeting(q.spec)};
}
sensing::TargetingQuery query(const Value& v) {
    const auto&             a = tuple(v, 3);
    sensing::TargetingQuery q{subject(a[0]), location(a[1]), take(detail::decodeActionTargeting(a[2]))};
    checked(q.validate());
    return q;
}
Value targets(const sensing::TargetSet& set) {
    Value::Array ids;
    for (auto s : set.subjects()) ids.push_back(s.format());
    Value area;
    if (set.worldArea() || set.gridArea()) {
        sensing::TargetingSpec spec;
        if (set.worldArea()) {
            spec.space     = set.worldArea()->space();
            spec.worldArea = set.worldArea();
        } else {
            spec.space    = set.gridArea()->space();
            spec.gridArea = set.gridArea();
        }
        area = detail::encodeActionTargeting(spec);
    }
    return Value::Array{std::move(ids), optionalSubject(set.primary()), set.point() ? location(*set.point()) : Value{},
                        std::move(area)};
}
sensing::TargetSet targets(const Value& v) {
    const auto&        a = tuple(v, 4);
    sensing::TargetSet set;
    for (const auto& s : array(a[0])) {
        auto id = subject(s);
        if (std::find(set.subjects().begin(), set.subjects().end(), id) != set.subjects().end())
            invalid("Duplicate resolved target");
        checked(set.addSubject(id));
    }
    if (!a[1].isNull()) checked(set.setPrimary(subject(a[1])));
    if (!a[2].isNull()) checked(set.setPoint(location(a[2])));
    if (!a[3].isNull()) {
        auto spec = take(detail::decodeActionTargeting(a[3]));
        // A projection carries geometry only: reject hidden, ignored selection policy.
        sensing::TargetingSpec canonical;
        canonical.space     = spec.space;
        canonical.worldArea = spec.worldArea;
        canonical.gridArea  = spec.gridArea;
        if (take(detail::encodeActionTargeting(canonical).toJson()) != take(a[3].toJson()))
            invalid("Non-geometric resolved area fields");
        if (spec.worldArea)
            checked(set.setArea(*spec.worldArea));
        else if (spec.gridArea)
            checked(set.setArea(*spec.gridArea));
        else
            invalid("Missing resolved area");
    }
    return set;
}
Value status(const Status& s) {
    Value::Array ds;
    for (const auto& d : s.diagnostics()) {
        Value::Array details;
        for (const auto& [k, v] : d.details()) details.push_back(Value::Array{k, v});
        ds.push_back(
            Value::Array{int(d.code()), int(d.severity()), d.message(), d.path(), std::move(details), d.source()});
    }
    return Value::Array{int(s.code()), std::move(ds)};
}
Status status(const Value& v) {
    const auto&             a    = tuple(v, 2);
    auto                    code = StatusCode(enumeration(a[0], int(StatusCode::Failed)));
    std::vector<Diagnostic> ds;
    for (const auto& row : array(a[1])) {
        const auto&       d = tuple(row, 6);
        DiagnosticDetails details;
        for (const auto& row : array(d[4])) {
            const auto& pair = tuple(row, 2);
            details.emplace_back(text(pair[0]), text(pair[1]));
        }
        ds.emplace_back(DiagnosticCode(enumeration(d[0], int(DiagnosticCode::TypeMismatch))),
                        Severity(enumeration(d[1], 3)), text(d[2]), text(d[3]), std::move(details), text(d[5]));
    }
    return Status(code, std::move(ds));
}
Value receipt(const ActionResourceReceiptAudit& r) {
    Value::Array costs;
    for (const auto& item : r.cost.items()) costs.push_back(Value::Array{item.resource.value(), item.amount.value()});
    return Value::Array{std::to_string(r.account), std::to_string(r.id.value()), std::to_string(r.reservation.value()),
                        int(r.operation), std::move(costs)};
}
ActionResourceReceiptAudit receipt(const Value& v) {
    const auto&                         a = tuple(v, 5);
    std::vector<resource::ResourceCost> costs;
    for (const auto& row : array(a[4])) {
        const auto& c = tuple(row, 2);
        costs.push_back(take(resource::ResourceCost::create(text(c[0]), integer(c[1]))));
    }
    auto                       cost = take(resource::CostSpec::create(std::move(costs)));
    ActionResourceReceiptAudit result{counter(a[0]), resource::ReceiptId(counter(a[1])),
                                      resource::ReservationId(counter(a[2])),
                                      resource::ReceiptOperation(enumeration(a[3], 1)), std::move(cost)};
    if (!result.account || result.id.isZero()) invalid("Invalid resource audit identity");
    // CostSpec admission must not sort, merge or otherwise repair wire facts.
    if (take(receipt(result).toJson()) != take(v.toJson())) invalid("Noncanonical resource audit cost");
    return result;
}
Value transaction(const transaction::TransactionReceipt& r) {
    return Value::Array{r.identity.format(),
                        r.transactionId,
                        r.correlationId,
                        r.causationId,
                        int(r.state),
                        std::to_string(r.participantCount),
                        std::to_string(r.preparedCount),
                        std::to_string(r.committedCount),
                        std::to_string(r.rolledBackCount),
                        std::to_string(r.compensatedCount)};
}
transaction::TransactionReceipt transaction(const Value& v) {
    const auto&                     a = tuple(v, 10);
    transaction::TransactionReceipt r;
    auto                            id = TransactionId::parse(text(a[0]));
    if (!id || id->format() != text(a[0])) invalid("Invalid transaction identity");
    r.identity      = *id;
    r.transactionId = text(a[1]);
    r.correlationId = text(a[2]);
    r.causationId   = text(a[3]);
    r.state         = transaction::CoordinatorState(enumeration(a[4], 5));
    auto size       = [](const Value& v) -> std::size_t {
        auto n = counter(v);
        if (n > std::numeric_limits<std::size_t>::max()) invalid("Transaction count overflow");
        return std::size_t(n);
    };
    r.participantCount = size(a[5]);
    r.preparedCount    = size(a[6]);
    r.committedCount   = size(a[7]);
    r.rolledBackCount  = size(a[8]);
    r.compensatedCount = size(a[9]);
    if (r.preparedCount > r.participantCount || r.committedCount > r.preparedCount ||
        r.rolledBackCount > r.preparedCount || r.compensatedCount > r.committedCount)
        invalid("Impossible transaction audit counts");
    return r;
}
}  // namespace archive
}  // namespace
struct PreparedActionRestore::Impl {
    ActionActivePipeline                                          pipeline = ActionActivePipeline::LegacyPorts;
    std::weak_ptr<ecs::Table>                                     world;
    ActionExecutionId                                             next;
    std::map<ActionExecutionId, std::unique_ptr<ActionExecution>> executions;
};
ActionCapture::ActionCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ActionCapture::~ActionCapture()                                   = default;
ActionCapture::ActionCapture(ActionCapture&&) noexcept            = default;
ActionCapture& ActionCapture::operator=(ActionCapture&&) noexcept = default;
PersistentId   ActionCapture::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::optional<ActionActivePipeline> ActionCapture::activePipeline() const noexcept {
    if (!impl_) return std::nullopt;
    return impl_->pipeline;
}
ActionExecutionId ActionCapture::nextExecutionId() const noexcept { return impl_ ? impl_->next : ActionExecutionId{}; }
std::span<const CapturedActionExecution> ActionCapture::executions() const noexcept {
    return impl_ ? std::span<const CapturedActionExecution>(impl_->executions)
                 : std::span<const CapturedActionExecution>{};
}
Result<SnapshotEnvelope> ActionCapture::seal(Revision revision, SimulationTick tick,
                                             const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || revision.isZero())
        return reject<SnapshotEnvelope>(DiagnosticCode::PreconditionViolation,
                                        "Action sealing needs live capture and nonzero revision");
    try {
        Value::Array rows;
        for (const auto& row : impl_->executions) {
            if (!row.execution) archive::invalid("Missing captured execution");
            const auto&  e = *row.execution;
            const auto&  r = e.request_;
            Value::Array refs;
            for (const auto& ref : row.targets) refs.push_back(archive::reference(ref));
            Value::Array overrides;
            for (const auto& [index, timing] : e.sectionTimingOverrides_)
                overrides.push_back(Value::Array{std::to_string(index), timing.targetDuration.nanoseconds(),
                                                 timing.authoredAnchor.nanoseconds(),
                                                 timing.simulationElapsed.nanoseconds()});
            rows.push_back(Value::Array{
                std::to_string(e.id_.value()), archive::take(e.definition_.encodeState()),
                Value::Array{r.actionId.format(), r.targetingQuery ? archive::query(*r.targetingQuery) : Value{},
                             r.parameters, r.transactionId, std::to_string(r.requestedTick.value()),
                             archive::optionalSubject(r.sourceSubject)},
                row.source ? archive::reference(*row.source) : Value{}, std::move(refs), int(e.phase_),
                e.phaseElapsed_.nanoseconds(), e.totalElapsed_.nanoseconds(), std::to_string(e.lastTick_.value()),
                archive::status(e.status_), e.resolvedTargets_ ? archive::targets(*e.resolvedTargets_) : Value{},
                e.receipt_ ? archive::receipt(*e.receipt_) : Value{},
                e.transactionReceipt_ ? archive::transaction(*e.transactionReceipt_) : Value{}, e.activeExecuted_,
                e.timelineStarted_, std::move(overrides)});
        }
        Value payload = Value::object({{"pipeline", int(impl_->pipeline)},
                                       {"next", std::to_string(impl_->next.value())},
                                       {"executions", std::move(rows)}});
        return makeSnapshotEnvelope("eve.action-runtime", *LogicalId::fromParts("action", "runtime"), SchemaVersion(1),
                                    impl_->epoch, revision, tick, std::move(payload), hash);
    } catch (const archive::Invalid& error) {
        return R::failure(error.status);
    }
}
Result<ActionCapture> ActionCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                           const SnapshotHashProvider& hash) {
    using R       = Result<ActionCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return reject<ActionCapture>(DiagnosticCode::UnknownVersion, "Unknown action runtime version");
    if (snapshot.type != "eve.action-runtime" || snapshot.schema != *LogicalId::fromParts("action", "runtime") ||
        epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return reject<ActionCapture>(DiagnosticCode::Conflict, "Action runtime save boundary mismatch");
    try {
        const auto* root = snapshot.payload.getIf<Value::Object>();
        if (!root || root->size() != 3 || !root->contains("pipeline") || !root->contains("next") ||
            !root->contains("executions"))
            archive::invalid("Unknown or missing action runtime fields");
        auto result      = std::make_unique<Impl>();
        result->epoch    = epoch;
        result->pipeline = ActionActivePipeline(archive::enumeration(root->at("pipeline"), 1));
        result->next     = ActionExecutionId(archive::counter(root->at("next")));
        if (result->next.isZero()) archive::invalid("Zero action id watermark");
        ActionExecutionId previous;
        for (const auto& value : archive::array(root->at("executions"))) {
            const auto& a  = archive::tuple(value, 16);
            auto        id = ActionExecutionId(archive::counter(a[0]));
            if (id.isZero() || id <= previous || id >= result->next)
                archive::invalid("Action ids must be ordered unique below watermark");
            previous                 = id;
            auto          definition = archive::take(ActionDefinition::prepareState(a[1]));
            const auto&   request    = archive::tuple(a[2], 6);
            ActionRequest req;
            auto          name = LogicalId::parse(archive::text(request[0]));
            if (!name || name->format() != archive::text(request[0])) archive::invalid("Invalid action request id");
            req.actionId = *name;
            if (!request[1].isNull()) req.targetingQuery = archive::query(request[1]);
            const auto* parameters = request[2].getIf<Value::Object>();
            if (!parameters) archive::invalid("Expected action parameters object");
            req.parameters    = *parameters;
            req.transactionId = archive::text(request[3]);
            req.requestedTick = SimulationTick(archive::counter(request[4]));
            req.sourceSubject = archive::optionalSubject(request[5]);
            CapturedActionExecution row;
            if (!a[3].isNull()) {
                row.source = archive::reference(a[3]);
                if (req.sourceSubject != row.source->subject) archive::invalid("Action source correspondence differs");
            }
            for (const auto& ref : archive::array(a[4])) {
                row.targets.push_back(archive::reference(ref));
                req.targetSubjects.push_back(row.targets.back().subject);
            }
            // Validate stable-only requests using inert placeholders; never resolve or authorize entities.
            req.targetEntities.resize(row.targets.size());
            archive::checked(req.validate(definition));
            req.targetEntities.clear();
            auto e = std::shared_ptr<ActionExecution>(new ActionExecution(id, std::move(definition), std::move(req)));
            e->phase_        = ActionPhase(archive::enumeration(a[5], int(ActionPhase::Failed)));
            e->phaseElapsed_ = archive::duration(a[6]);
            e->totalElapsed_ = archive::duration(a[7]);
            e->lastTick_     = SimulationTick(archive::counter(a[8]));
            e->status_       = archive::status(a[9]);
            if (!a[10].isNull()) e->resolvedTargets_ = archive::targets(a[10]);
            if (!a[11].isNull()) e->receipt_ = archive::receipt(a[11]);
            if (!a[12].isNull()) e->transactionReceipt_ = archive::transaction(a[12]);
            e->activeExecuted_  = archive::boolean(a[13]);
            e->timelineStarted_ = archive::boolean(a[14]);
            std::optional<std::size_t> priorIndex;
            for (const auto& timing : archive::array(a[15])) {
                const auto& t     = archive::tuple(timing, 4);
                auto        index = archive::counter(t[0]);
                if (index > std::numeric_limits<std::size_t>::max() || !e->definition_.timeline ||
                    (priorIndex && index <= *priorIndex))
                    archive::invalid("Invalid ordered timeline override index");
                const auto range = archive::take(e->definition_.timeline->sectionRange(std::size_t(index)));
                ActionExecution::SectionTimingOverride item{archive::duration(t[1]), archive::duration(t[2]),
                                                            archive::duration(t[3])};
                if (item.targetDuration.isZero() || item.simulationElapsed > item.targetDuration ||
                    item.authoredAnchor < range.first || item.authoredAnchor > range.second)
                    archive::invalid("Invalid timeline override anchor/duration");
                priorIndex = std::size_t(index);
                e->sectionTimingOverrides_.emplace(std::size_t(index), item);
            }
            const auto  total  = archive::take(e->definition_.timing.total());
            const auto& timing = e->definition_.timing;
            if (e->totalElapsed_ > total || e->phaseElapsed_ > e->totalElapsed_)
                archive::invalid("Action elapsed exceeds timing");
            if ((e->phase_ == ActionPhase::Failed || e->phase_ == ActionPhase::Cancelled) == e->status_.isSuccess())
                archive::invalid("Action phase/status disagree");
            if (e->phase_ == ActionPhase::Cancelled && e->status_.code() != StatusCode::Cancelled)
                archive::invalid("Cancelled action needs cancelled status");
            if ((e->phase_ == ActionPhase::Requested || e->phase_ == ActionPhase::Validating) &&
                (!e->totalElapsed_.isZero() || e->activeExecuted_ || e->timelineStarted_))
                archive::invalid("Unstarted action carries progress");
            // Validating is synchronous inside advance; capture rejects every reentrant observation.
            if (e->phase_ == ActionPhase::Validating) archive::invalid("Validating is not an idle checkpoint phase");
            if (e->phase_ == ActionPhase::Requested && e->resolvedTargets_)
                archive::invalid("Unvalidated query carries selection");
            if (e->definition_.timeline && e->phase_ >= ActionPhase::Windup && e->phase_ <= ActionPhase::Completed &&
                !e->timelineStarted_)
                archive::invalid("Started action lost timeline sample marker");
            if (e->phase_ == ActionPhase::Windup &&
                (e->activeExecuted_ || timing.windup.isZero() || e->phaseElapsed_ >= timing.windup ||
                 e->totalElapsed_ != e->phaseElapsed_))
                archive::invalid("Invalid Windup cursor");
            if (e->phase_ == ActionPhase::Active &&
                (!e->activeExecuted_ || timing.active.isZero() || e->phaseElapsed_ >= timing.active ||
                 e->totalElapsed_.nanoseconds() != timing.windup.nanoseconds() + e->phaseElapsed_.nanoseconds()))
                archive::invalid("Invalid Active cursor");
            if (e->phase_ == ActionPhase::Recover &&
                (!e->activeExecuted_ || timing.recover.isZero() || e->phaseElapsed_ >= timing.recover ||
                 e->totalElapsed_.nanoseconds() !=
                     timing.windup.nanoseconds() + timing.active.nanoseconds() + e->phaseElapsed_.nanoseconds()))
                archive::invalid("Invalid Recover cursor");
            if (e->phase_ == ActionPhase::Completed &&
                (!e->activeExecuted_ || e->totalElapsed_ != total || !e->phaseElapsed_.isZero()))
                archive::invalid("Invalid completed cursor");
            if (e->timelineStarted_ && !e->definition_.timeline) archive::invalid("Timeline cursor without timeline");
            if (e->definition_.targetingMode != TargetingMode::Query && e->resolvedTargets_)
                archive::invalid("Resolved targets on nonquery action");
            if (e->definition_.targetingMode == TargetingMode::Query && e->phase_ >= ActionPhase::Windup &&
                e->phase_ <= ActionPhase::Completed && !e->resolvedTargets_)
                archive::invalid("Validated query lost resolved targets");
            if (e->receipt_) {
                if (!e->activeExecuted_ || !e->definition_.cost ||
                    e->receipt_->operation != resource::ReceiptOperation::Debit ||
                    e->receipt_->cost != *e->definition_.cost)
                    archive::invalid("Resource audit differs from committed action");
            }
            if (e->transactionReceipt_ &&
                (!e->activeExecuted_ || e->transactionReceipt_->state != transaction::CoordinatorState::Committed))
                archive::invalid("Transaction audit differs from committed action");
            if ((e->receipt_ && e->transactionReceipt_) ||
                (e->activeExecuted_ && e->definition_.cost && !e->receipt_ && !e->transactionReceipt_))
                archive::invalid("Committed action lost or duplicated audit");
            row.execution = std::move(e);
            result->executions.push_back(std::move(row));
        }
        // JSON serializability applies also to arbitrary request/status payloads.
        archive::take(snapshot.payload.toJson());
        return R::success(ActionCapture(std::move(result)));
    } catch (const archive::Invalid& error) {
        return R::failure(error.status);
    }
}
PreparedActionRestore::PreparedActionRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedActionRestore::~PreparedActionRestore()                                           = default;
PreparedActionRestore::PreparedActionRestore(PreparedActionRestore&&) noexcept            = default;
PreparedActionRestore& PreparedActionRestore::operator=(PreparedActionRestore&&) noexcept = default;
Result<ActionCapture>  ActionRuntime::capture(PersistentId epoch, std::shared_ptr<ecs::Table> world,
                                              std::span<const ActionEntityBinding> bindings) const {
    using R = Result<ActionCapture>;
    if (faulted_ || advancing_ || snapshotting_ || !world || world->is_deferred())
        return reject<ActionCapture>(DiagnosticCode::PreconditionViolation,
                                      "Action capture needs an idle actual world/runtime");
    if (epoch.isNil()) return reject<ActionCapture>(DiagnosticCode::InvalidArgument, "Action save epoch is nil");
    FlagGuard guard(snapshotting_);
    auto      candidate = std::make_unique<ActionCapture::Impl>();
    candidate->epoch    = epoch;
    candidate->pipeline =
        services_.transactionEffect ? ActionActivePipeline::Transaction : ActionActivePipeline::LegacyPorts;
    candidate->next = nextId_;
    candidate->executions.reserve(executions_.size());
    for (const auto& [id, execution] : executions_) {
        if (!execution || id != execution->id_ || id.isZero() || id >= nextId_)
            return reject<ActionCapture>(DiagnosticCode::InvariantViolation,
                                          "Action execution identity exceeds its runtime watermark");
        auto                    copy = std::shared_ptr<ActionExecution>(new ActionExecution(*execution));
        CapturedActionExecution row;
        if (copy->request_.source) {
            auto source = freezeReference(*copy->request_.source, copy->request_.sourceSubject, *world, bindings);
            if (!source) return R::failure(source.status());
            row.source                   = std::move(source).takeValue();
            copy->request_.sourceSubject = row.source->subject;
        }
        for (std::size_t i = 0; i < copy->request_.targetEntities.size(); ++i) {
            const auto stable = copy->request_.targetSubjects.empty()
                                     ? std::optional<SubjectRef>{}
                                     : std::optional<SubjectRef>{copy->request_.targetSubjects[i]};
            auto       target = freezeReference(copy->request_.targetEntities[i], stable, *world, bindings);
            if (!target) return R::failure(target.status());
            row.targets.push_back(std::move(target).takeValue());
        }
        copy->request_.source.reset();
        copy->request_.targetEntities.clear();
        copy->request_.targetSubjects.clear();
        for (const auto& target : row.targets) copy->request_.targetSubjects.push_back(target.subject);
        row.execution = std::move(copy);
        candidate->executions.push_back(std::move(row));
    }
    return R::success(ActionCapture(std::move(candidate)));
}
Result<PreparedActionRestore> PreparedActionRestore::prepare(const ActionCapture&                 captured,
                                                             std::shared_ptr<ecs::Table>          world,
                                                             std::span<const ActionEntityBinding> bindings) {
    using R = Result<PreparedActionRestore>;
    if (!captured.impl_ || !world || world->is_deferred())
        return reject<PreparedActionRestore>(DiagnosticCode::PreconditionViolation,
                                             "Action restore needs frozen facts and idle target world");
    auto candidate      = std::make_unique<Impl>();
    candidate->world    = world;
    candidate->pipeline = captured.impl_->pipeline;
    candidate->next     = captured.impl_->next;
    for (const auto& row : captured.impl_->executions) {
        auto copy = std::unique_ptr<ActionExecution>(new ActionExecution(*row.execution));
        if (row.source) {
            auto source = bindReference(*row.source, *world, bindings);
            if (!source) return R::failure(source.status());
            copy->request_.source = std::move(source).takeValue();
        }
        for (const auto& reference : row.targets) {
            auto target = bindReference(reference, *world, bindings);
            if (!target) return R::failure(target.status());
            copy->request_.targetEntities.push_back(std::move(target).takeValue());
        }
        auto valid = copy->request_.validate(copy->definition_);
        if (!valid) return R::failure(valid.status());
        const auto id                = copy->id_;
        const auto [entry, inserted] = candidate->executions.emplace(id, std::move(copy));
        if (!inserted)
            return reject<PreparedActionRestore>(DiagnosticCode::Conflict, "Duplicate frozen action execution");
    }
    return R::success(PreparedActionRestore(std::move(candidate)));
}
Result<void> ActionRuntime::restore(PreparedActionRestore&& prepared) {
    if (advancing_ || snapshotting_ || !prepared.impl_)
        return reject<void>(DiagnosticCode::PreconditionViolation,
                            "Action runtime is active or restore token consumed");
    FlagGuard guard(snapshotting_);
    auto      world = prepared.impl_->world.lock();
    if (!world || world->is_deferred())
        return reject<void>(DiagnosticCode::StaleHandle, "Action restore target world retired or is in a View");
    auto live = [&](const ecs::EntityHandle& handle) {
        return !handle.table || (handle.table == world.get() && ecs::try_get(handle));
    };
    for (const auto& [id, execution] : prepared.impl_->executions) {
        const auto& request = execution->request_;
        if ((request.source && !live(*request.source)) ||
            !std::all_of(request.targetEntities.begin(), request.targetEntities.end(), live))
            return reject<void>(DiagnosticCode::StaleHandle,
                                "Action restore target incarnation changed after preparation");
        if (terminal(execution->phase_) || execution->activeExecuted_) continue;
        const auto& definition  = execution->definition_;
        const bool  transaction = services_.transactionEffect != nullptr;
        if (transaction != (prepared.impl_->pipeline == ActionActivePipeline::Transaction))
            return reject<void>(DiagnosticCode::Unsupported,
                                "Future action Active dispatch mode differs from captured runtime");
        if ((definition.cost && !services_.resources && !(transaction && services_.transactionAccount)) ||
            ((definition.activeExecutionRequired || !definition.effectIds.empty()) && !transaction &&
             !services_.effects))
            return reject<void>(DiagnosticCode::Unsupported,
                                "Future action Active requires missing resource/effect providers");
        if (execution->phase_ == ActionPhase::Requested || execution->phase_ == ActionPhase::Validating) {
            if ((definition.targetingMode == TargetingMode::Query && !services_.targeting) ||
                (!(definition.condition.kind() == condition::ConditionKind::All &&
                   definition.condition.children().empty() && definition.condition.isValid()) &&
                 !services_.conditions))
                return reject<void>(DiagnosticCode::Unsupported, "Future action validation requires missing providers");
        }
    }
    executions_.swap(prepared.impl_->executions);
    nextId_  = prepared.impl_->next;
    faulted_ = false;
    prepared.impl_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::action
