#include "roster/Roster.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <utility>

namespace eve::roster {
namespace {

bool emptyId(std::string_view value) { return value.empty(); }

template <class Container>
auto findOccupant(Container& items, std::string_view subject) {
    return std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.subject == subject; });
}

}  // namespace

Roster::Roster()  = default;
Roster::~Roster() = default;

Module_IMPL(Roster, new Roster());

Result<void> Roster::fail(DiagnosticCode code, std::string message, std::string path) const {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "roster"));
}

template <class T>
Result<T> Roster::failT(DiagnosticCode code, std::string message, std::string path) const {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "roster"));
}

void Roster::clear() {
    posts_.clear();
    squads_.clear();
    policies_.clear();
    vacancies_.clear();
    spawnRequests_.clear();
    nextRequestSerial_ = 1;
}

void Roster::pushVacancy(std::string kind, std::string targetId, std::string role, std::string lostSubject,
                         std::uint64_t tick) {
    vacancies_.push_back(Vacancy{std::move(kind), std::move(targetId), std::move(role), std::move(lostSubject), tick});
}

Result<void> Roster::registerPost(std::string postId, int capacity, std::string defaultRole) {
    if (emptyId(postId) || capacity < 1 || emptyId(defaultRole))
        return fail(DiagnosticCode::InvalidArgument, "post id, capacity>=1 and defaultRole are required", "post");
    if (posts_.contains(postId)) return fail(DiagnosticCode::AlreadyExists, "post id already registered", "postId");
    posts_.emplace(std::move(postId), Post{capacity, std::move(defaultRole), {}});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::removePost(std::string_view postId, std::uint64_t tick) {
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return fail(DiagnosticCode::NotFound, "post was not found", "postId");
    for (auto& occupant : it->second.occupants) pushVacancy("post", it->first, occupant.role, occupant.subject, tick);
    posts_.erase(it);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::assignPost(std::string_view postId, std::string subject, std::string role) {
    if (emptyId(postId) || emptyId(subject))
        return fail(DiagnosticCode::InvalidArgument, "post id and subject are required", "assignPost");
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return fail(DiagnosticCode::NotFound, "post was not found", "postId");
    for (const auto& [id, post] : posts_) {
        (void)id;
        if (findOccupant(post.occupants, subject) != post.occupants.end())
            return fail(DiagnosticCode::Conflict, "subject already occupies a post", "subject");
    }
    if (static_cast<int>(it->second.occupants.size()) >= it->second.capacity)
        return fail(DiagnosticCode::Conflict, "post is at capacity", "postId");
    if (role.empty()) role = it->second.defaultRole;
    it->second.occupants.push_back(Occupant{std::move(subject), std::move(role)});
    std::sort(it->second.occupants.begin(), it->second.occupants.end(),
              [](const Occupant& a, const Occupant& b) { return a.subject < b.subject; });
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::releasePost(std::string_view postId, std::string_view subject, std::uint64_t tick) {
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return fail(DiagnosticCode::NotFound, "post was not found", "postId");
    auto occupant = findOccupant(it->second.occupants, subject);
    if (occupant == it->second.occupants.end())
        return fail(DiagnosticCode::NotFound, "subject is not assigned to this post", "subject");
    pushVacancy("post", it->first, occupant->role, occupant->subject, tick);
    it->second.occupants.erase(occupant);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<int> Roster::postOccupancy(std::string_view postId) const {
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return failT<int>(DiagnosticCode::NotFound, "post was not found", "postId");
    return Result<int>::success(static_cast<int>(it->second.occupants.size()));
}

Result<int> Roster::postCapacity(std::string_view postId) const {
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return failT<int>(DiagnosticCode::NotFound, "post was not found", "postId");
    return Result<int>::success(it->second.capacity);
}

Result<std::string> Roster::postAssigneeAt(std::string_view postId, int index) const {
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return failT<std::string>(DiagnosticCode::NotFound, "post was not found", "postId");
    if (index < 0 || static_cast<std::size_t>(index) >= it->second.occupants.size())
        return failT<std::string>(DiagnosticCode::InvalidArgument, "assignee index out of range", "index");
    return Result<std::string>::success(it->second.occupants[static_cast<std::size_t>(index)].subject);
}

Result<std::string> Roster::postRole(std::string_view postId, std::string_view subject) const {
    auto it = posts_.find(std::string(postId));
    if (it == posts_.end()) return failT<std::string>(DiagnosticCode::NotFound, "post was not found", "postId");
    auto occupant = findOccupant(it->second.occupants, subject);
    if (occupant == it->second.occupants.end())
        return failT<std::string>(DiagnosticCode::NotFound, "subject is not assigned to this post", "subject");
    return Result<std::string>::success(occupant->role);
}

Result<void> Roster::createSquad(std::string squadId, int capacity, std::string intent) {
    if (emptyId(squadId) || capacity < 1)
        return fail(DiagnosticCode::InvalidArgument, "squad id and capacity>=1 are required", "squad");
    if (squads_.contains(squadId)) return fail(DiagnosticCode::AlreadyExists, "squad id already registered", "squadId");
    squads_.emplace(std::move(squadId), Squad{capacity, std::move(intent), {}});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::destroySquad(std::string_view squadId, std::uint64_t tick) {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return fail(DiagnosticCode::NotFound, "squad was not found", "squadId");
    for (auto& member : it->second.members) pushVacancy("squad", it->first, member.role, member.subject, tick);
    squads_.erase(it);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::addMember(std::string_view squadId, std::string subject, std::string role) {
    if (emptyId(squadId) || emptyId(subject) || emptyId(role))
        return fail(DiagnosticCode::InvalidArgument, "squad id, subject and role are required", "addMember");
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return fail(DiagnosticCode::NotFound, "squad was not found", "squadId");
    if (findOccupant(it->second.members, subject) != it->second.members.end())
        return fail(DiagnosticCode::Conflict, "subject is already a squad member", "subject");
    if (static_cast<int>(it->second.members.size()) >= it->second.capacity)
        return fail(DiagnosticCode::Conflict, "squad is at capacity", "squadId");
    it->second.members.push_back(Occupant{std::move(subject), std::move(role)});
    std::sort(it->second.members.begin(), it->second.members.end(),
              [](const Occupant& a, const Occupant& b) { return a.subject < b.subject; });
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::removeMember(std::string_view squadId, std::string_view subject, std::uint64_t tick) {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return fail(DiagnosticCode::NotFound, "squad was not found", "squadId");
    auto member = findOccupant(it->second.members, subject);
    if (member == it->second.members.end())
        return fail(DiagnosticCode::NotFound, "subject is not a squad member", "subject");
    pushVacancy("squad", it->first, member->role, member->subject, tick);
    it->second.members.erase(member);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::setIntent(std::string_view squadId, std::string intent) {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return fail(DiagnosticCode::NotFound, "squad was not found", "squadId");
    it->second.intent = std::move(intent);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::string> Roster::intent(std::string_view squadId) const {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return failT<std::string>(DiagnosticCode::NotFound, "squad was not found", "squadId");
    return Result<std::string>::success(it->second.intent);
}

Result<int> Roster::memberCount(std::string_view squadId) const {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return failT<int>(DiagnosticCode::NotFound, "squad was not found", "squadId");
    return Result<int>::success(static_cast<int>(it->second.members.size()));
}

Result<std::string> Roster::memberAt(std::string_view squadId, int index) const {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return failT<std::string>(DiagnosticCode::NotFound, "squad was not found", "squadId");
    if (index < 0 || static_cast<std::size_t>(index) >= it->second.members.size())
        return failT<std::string>(DiagnosticCode::InvalidArgument, "member index out of range", "index");
    return Result<std::string>::success(it->second.members[static_cast<std::size_t>(index)].subject);
}

Result<std::string> Roster::memberRole(std::string_view squadId, std::string_view subject) const {
    auto it = squads_.find(std::string(squadId));
    if (it == squads_.end()) return failT<std::string>(DiagnosticCode::NotFound, "squad was not found", "squadId");
    auto member = findOccupant(it->second.members, subject);
    if (member == it->second.members.end())
        return failT<std::string>(DiagnosticCode::NotFound, "subject is not a squad member", "subject");
    return Result<std::string>::success(member->role);
}

Result<int> Roster::releaseSubject(std::string_view subject, std::uint64_t tick) {
    if (emptyId(subject)) return failT<int>(DiagnosticCode::InvalidArgument, "subject is required", "subject");
    int released = 0;
    for (auto& [postId, post] : posts_) {
        auto occupant = findOccupant(post.occupants, subject);
        if (occupant == post.occupants.end()) continue;
        pushVacancy("post", postId, occupant->role, occupant->subject, tick);
        post.occupants.erase(occupant);
        ++released;
    }
    for (auto& [squadId, squad] : squads_) {
        auto member = findOccupant(squad.members, subject);
        if (member == squad.members.end()) continue;
        pushVacancy("squad", squadId, member->role, member->subject, tick);
        squad.members.erase(member);
        ++released;
    }
    return Result<int>::success(released, Status::success(released == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

int Roster::vacancyCount() const { return static_cast<int>(vacancies_.size()); }

Result<Vacancy> Roster::vacancyAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= vacancies_.size())
        return failT<Vacancy>(DiagnosticCode::InvalidArgument, "vacancy index out of range", "index");
    return Result<Vacancy>::success(vacancies_[static_cast<std::size_t>(index)]);
}

void Roster::clearVacancies() { vacancies_.clear(); }

Result<void> Roster::setPolicy(std::string policyId, std::string preferredDef, int delayTicks, int budget,
                               std::string postId, std::string squadId, std::string role) {
    if (emptyId(policyId) || emptyId(preferredDef) || delayTicks < 0 || budget < 0)
        return fail(DiagnosticCode::InvalidArgument,
                    "policy id, preferredDef, non-negative delayTicks and budget are required", "policy");
    if (!postId.empty() && !squadId.empty())
        return fail(DiagnosticCode::InvalidArgument, "policy may bind a post or a squad, not both", "policy");
    auto& policy        = policies_[policyId];
    policy.preferredDef = std::move(preferredDef);
    policy.delayTicks   = delayTicks;
    policy.budget       = budget;
    policy.postId       = std::move(postId);
    policy.squadId      = std::move(squadId);
    policy.role         = std::move(role);
    // Keep spent/fallbacks across upsert so live budgets do not reset silently.
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::setFallback(std::string_view policyId, std::string preferredDef, std::string fallbackDef) {
    auto it = policies_.find(std::string(policyId));
    if (it == policies_.end()) return fail(DiagnosticCode::NotFound, "policy was not found", "policyId");
    if (emptyId(preferredDef) || emptyId(fallbackDef) || preferredDef == fallbackDef)
        return fail(DiagnosticCode::InvalidArgument, "fallback needs distinct preferred and fallback defs", "fallback");
    // Reject one-step cycles; longer cycles are detected when failing a request.
    if (it->second.fallbacks[fallbackDef] == preferredDef)
        return fail(DiagnosticCode::Conflict, "fallback chain contains a cycle", "fallback");
    it->second.fallbacks[std::move(preferredDef)] = std::move(fallbackDef);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::clearPolicy(std::string_view policyId) {
    auto it = policies_.find(std::string(policyId));
    if (it == policies_.end()) return fail(DiagnosticCode::NotFound, "policy was not found", "policyId");
    policies_.erase(it);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool Roster::policyMatches(const Policy& policy, const Vacancy& vacancy) const {
    if (!policy.postId.empty() && (vacancy.kind != "post" || vacancy.targetId != policy.postId)) return false;
    if (!policy.squadId.empty() && (vacancy.kind != "squad" || vacancy.targetId != policy.squadId)) return false;
    if (!policy.role.empty() && vacancy.role != policy.role) return false;
    return true;
}

bool Roster::budgetAvailable(const Policy& policy) const { return policy.budget == 0 || policy.spent < policy.budget; }

std::string Roster::nextRequestId() { return "spawn:" + std::to_string(nextRequestSerial_++); }

Result<int> Roster::step(std::uint64_t tick) {
    int                  created = 0;
    std::vector<Vacancy> remaining;
    remaining.reserve(vacancies_.size());
    for (auto& vacancy : vacancies_) {
        if (vacancy.tick > tick) {
            remaining.push_back(std::move(vacancy));
            continue;
        }
        bool matched = false;
        for (auto& [policyId, policy] : policies_) {
            if (!policyMatches(policy, vacancy)) continue;
            if (static_cast<std::uint64_t>(policy.delayTicks) > tick - vacancy.tick) {
                remaining.push_back(std::move(vacancy));
                matched = true;
                break;
            }
            if (!budgetAvailable(policy)) {
                // Budget exhausted: drop this vacancy for this policy family.
                matched = true;
                break;
            }
            SpawnRequest request;
            request.requestId    = nextRequestId();
            request.policyId     = policyId;
            request.preferredDef = policy.preferredDef;
            request.role         = vacancy.role;
            if (vacancy.kind == "post") request.postId = vacancy.targetId;
            if (vacancy.kind == "squad") request.squadId = vacancy.targetId;
            request.readyAtTick = tick;
            spawnRequests_.push_back(std::move(request));
            ++created;
            matched = true;
            break;
        }
        if (!matched) remaining.push_back(std::move(vacancy));
    }
    vacancies_ = std::move(remaining);
    return Result<int>::success(created, Status::success(created == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

int Roster::spawnRequestCount() const { return static_cast<int>(spawnRequests_.size()); }

Result<SpawnRequest> Roster::spawnRequestAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= spawnRequests_.size())
        return failT<SpawnRequest>(DiagnosticCode::InvalidArgument, "spawn request index out of range", "index");
    return Result<SpawnRequest>::success(spawnRequests_[static_cast<std::size_t>(index)]);
}

Result<void> Roster::eraseSpawnRequest(std::string_view requestId) {
    auto it = std::find_if(spawnRequests_.begin(), spawnRequests_.end(),
                           [&](const SpawnRequest& request) { return request.requestId == requestId; });
    if (it == spawnRequests_.end()) return fail(DiagnosticCode::NotFound, "spawn request was not found", "requestId");
    spawnRequests_.erase(it);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::ackSpawnRequest(std::string_view requestId) {
    auto it = std::find_if(spawnRequests_.begin(), spawnRequests_.end(),
                           [&](const SpawnRequest& request) { return request.requestId == requestId; });
    if (it == spawnRequests_.end()) return fail(DiagnosticCode::NotFound, "spawn request was not found", "requestId");
    auto policy = policies_.find(it->policyId);
    if (policy != policies_.end() && policy->second.budget > 0) ++policy->second.spent;
    spawnRequests_.erase(it);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Roster::failSpawnRequest(std::string_view requestId, std::uint64_t tick) {
    auto it = std::find_if(spawnRequests_.begin(), spawnRequests_.end(),
                           [&](const SpawnRequest& request) { return request.requestId == requestId; });
    if (it == spawnRequests_.end()) return fail(DiagnosticCode::NotFound, "spawn request was not found", "requestId");
    SpawnRequest failed = *it;
    spawnRequests_.erase(it);
    auto policy = policies_.find(failed.policyId);
    if (policy == policies_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    auto fallback = policy->second.fallbacks.find(failed.preferredDef);
    if (fallback == policy->second.fallbacks.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (!budgetAvailable(policy->second)) return Result<void>::success(Status::success(StatusCode::NoOp));
    SpawnRequest retry = failed;
    retry.requestId    = nextRequestId();
    retry.preferredDef = fallback->second;
    retry.readyAtTick  = tick;
    spawnRequests_.push_back(std::move(retry));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::roster
