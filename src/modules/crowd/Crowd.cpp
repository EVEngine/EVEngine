#include "crowd/CrowdInternal.h"

#include "common/Exception.h"


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


namespace eve::crowd {
namespace {

enum Action : int32_t { kIdle = 0, kFlow = 1, kSeek = 2, kBoids = 3 };

int actionFromName(const std::string &name) {
    if (name == "idle") return kIdle;
    if (name == "flow") return kFlow;
    if (name == "seek") return kSeek;
    if (name == "boids") return kBoids;
    return -1;
}

const char *actionName(int action) {
    switch (action) {
    case kIdle: return "idle";
    case kFlow: return "flow";
    case kSeek: return "seek";
    case kBoids: return "boids";
    default: return "";
    }
}


}  // namespace


Module_IMPL(Crowd, new Crowd());

Crowd::Crowd() : impl_(std::make_unique<Impl>()) {}
Crowd::~Crowd() = default;

// --- 流场 ---

void Crowd::resizeField(int width, int height, float cellSize, float originX, float originY) {
    impl_->field.resize(width, height, cellSize, originX, originY);
}

void Crowd::setBlocked(int cx, int cy, bool blocked) {
    impl_->field.setBlocked(cx, cy, blocked);
}

void Crowd::setCellCost(int cx, int cy, float cost) {
    impl_->field.setCellCost(cx, cy, cost);
}

float Crowd::getCellCost(int cx, int cy) const { return impl_->field.getCellCost(cx, cy); }

void Crowd::buildFlowField(int gx, int gy) {
    impl_->field.setGoal(gx, gy);
    impl_->field.build();
}

void Crowd::addFlowGoal(int gx, int gy) { impl_->field.addGoal(gx, gy); }

void Crowd::clearFlowGoals() { impl_->field.clearGoals(); }

void Crowd::build() { impl_->field.build(); }

bool Crowd::isFieldBuilt() const { return impl_->field.isBuilt(); }

bool Crowd::isReachable(int cx, int cy) const { return impl_->field.isReachable(cx, cy); }

int Crowd::getFieldWidth() const { return impl_->field.getWidth(); }
int Crowd::getFieldHeight() const { return impl_->field.getHeight(); }
float Crowd::getCellSize() const { return impl_->field.getCellSize(); }
float Crowd::getFieldOriginX() const { return impl_->field.getOriginX(); }
float Crowd::getFieldOriginY() const { return impl_->field.getOriginY(); }

FlowVec Crowd::flowAtWorld(float wx, float wy) const {
    FlowVec v;
    impl_->field.flowAtWorld(wx, wy, v.x, v.y);
    return v;
}

float Crowd::costAtWorld(float wx, float wy) const { return impl_->field.costAtWorld(wx, wy); }

FlowVec Crowd::flowAtCell(int cx, int cy) const {
    FlowVec v;
    impl_->field.flowAtCell(cx, cy, v.x, v.y);
    return v;
}

// --- 单位 ---

int Crowd::addAgent(float x, float y, float heading, float radius) {
    auto &d = *impl_;
    if (int(d.actions.size()) >= d.maxAgents) return -1;
    const int id = int(d.xs.size());
    d.xs.push_back(x);
    d.ys.push_back(y);
    d.headings.push_back(heading);
    d.vxs.push_back(0.f);
    d.vys.push_back(0.f);
    d.speeds.push_back(0.f);
    d.radii.push_back(radius > 0.f ? radius : d.defaultRadius);
    d.maxSpeeds.push_back(d.defaultSpeed);
    d.maxAccels.push_back(d.defaultSpeed * 2.f);
    d.turnRates.push_back(d.defaultTurnRate);
    d.actions.push_back(kFlow);
    d.datas.push_back(0);
    d.avoidancePriorities.push_back(0);
    d.hasTargets.push_back(0);
    d.interactions.emplace_back();
    d.targetXs.push_back(0.f);
    d.targetYs.push_back(0.f);
    d.wanderPhases.push_back(float(id) * 2.399963f);
    d.stableIds.emplace_back();
    return id;
}

int Crowd::addNamedAgent(const std::string &stableId, float x, float y, float heading, float radius) {
    auto &d = *impl_;
    if (stableId.empty() || d.namedAgents.count(stableId) != 0) return -1;
    const int index = addAgent(x, y, heading, radius);
    if (index < 0) return -1;
    d.stableIds[static_cast<size_t>(index)] = stableId;
    d.namedAgents[stableId] = index;
    return index;
}

bool Crowd::hasNamedAgent(const std::string &stableId) const {
    return impl_->namedAgents.count(stableId) != 0;
}

int Crowd::getNamedAgentIndex(const std::string &stableId) const {
    const auto found = impl_->namedAgents.find(stableId);
    return found == impl_->namedAgents.end() ? -1 : found->second;
}

std::string Crowd::getAgentStableId(int index) const {
    return impl_->validId(index) ? impl_->stableIds[static_cast<size_t>(index)] : std::string{};
}

bool Crowd::removeNamedAgent(const std::string &stableId) {
    const int index = getNamedAgentIndex(stableId);
    return index >= 0 && removeAgent(index);
}

bool Crowd::removeAgent(int id) {
    auto &d = *impl_;
    if (!d.validId(id)) return false;
    const int last = int(d.xs.size()) - 1;
    const std::string removedStableId = d.stableIds[static_cast<size_t>(id)];
    if (id != last) {
        d.xs[size_t(id)] = d.xs[size_t(last)];
        d.ys[size_t(id)] = d.ys[size_t(last)];
        d.headings[size_t(id)] = d.headings[size_t(last)];
        d.vxs[size_t(id)] = d.vxs[size_t(last)];
        d.vys[size_t(id)] = d.vys[size_t(last)];
        d.speeds[size_t(id)] = d.speeds[size_t(last)];
        d.radii[size_t(id)] = d.radii[size_t(last)];
        d.maxSpeeds[size_t(id)] = d.maxSpeeds[size_t(last)];
        d.maxAccels[size_t(id)] = d.maxAccels[size_t(last)];
        d.turnRates[size_t(id)] = d.turnRates[size_t(last)];
        d.actions[size_t(id)] = d.actions[size_t(last)];
        d.datas[size_t(id)] = d.datas[size_t(last)];
        d.avoidancePriorities[size_t(id)] = d.avoidancePriorities[size_t(last)];
        d.hasTargets[size_t(id)] = d.hasTargets[size_t(last)];
        d.interactions[size_t(id)]        = d.interactions[size_t(last)];
        d.targetXs[size_t(id)] = d.targetXs[size_t(last)];
        d.targetYs[size_t(id)] = d.targetYs[size_t(last)];
        d.wanderPhases[size_t(id)] = d.wanderPhases[size_t(last)];
        d.stableIds[size_t(id)] = d.stableIds[size_t(last)];
        if (!d.stableIds[size_t(id)].empty()) d.namedAgents[d.stableIds[size_t(id)]] = id;
    }
    if (!removedStableId.empty()) d.namedAgents.erase(removedStableId);
    d.xs.pop_back();
    d.ys.pop_back();
    d.headings.pop_back();
    d.vxs.pop_back();
    d.vys.pop_back();
    d.speeds.pop_back();
    d.radii.pop_back();
    d.maxSpeeds.pop_back();
    d.maxAccels.pop_back();
    d.turnRates.pop_back();
    d.actions.pop_back();
    d.datas.pop_back();
    d.avoidancePriorities.pop_back();
    d.hasTargets.pop_back();
    d.interactions.pop_back();
    d.targetXs.pop_back();
    d.targetYs.pop_back();
    d.wanderPhases.pop_back();
    d.stableIds.pop_back();
    return true;
}

void Crowd::clearAgents() {
    auto &d = *impl_;
    d.xs.clear();
    d.ys.clear();
    d.headings.clear();
    d.vxs.clear();
    d.vys.clear();
    d.speeds.clear();
    d.radii.clear();
    d.maxSpeeds.clear();
    d.maxAccels.clear();
    d.turnRates.clear();
    d.actions.clear();
    d.datas.clear();
    d.avoidancePriorities.clear();
    d.hasTargets.clear();
    d.interactions.clear();
    d.targetXs.clear();
    d.targetYs.clear();
    d.wanderPhases.clear();
    d.stableIds.clear();
    d.namedAgents.clear();
    d.gridW = d.gridH = 0;
}

int Crowd::getAgentCount() const { return int(impl_->xs.size()); }

void Crowd::setMaxAgents(int maxAgents) {
    impl_->maxAgents = std::max(maxAgents, 0);
}

int Crowd::getMaxAgents() const { return impl_->maxAgents; }

bool Crowd::setAgentAction(int id, const std::string &action) {
    if (!impl_->validId(id)) return false;
    const int a = actionFromName(action);
    if (a < 0) return false;
    impl_->actions[size_t(id)] = a;
    return true;
}

std::string Crowd::getAgentAction(int id) const {
    if (!impl_->validId(id)) return "";
    return actionName(impl_->actions[size_t(id)]);
}

bool Crowd::setAgentTarget(int id, float tx, float ty) {
    if (!impl_->validId(id)) return false;
    impl_->hasTargets[size_t(id)] = 1;
    impl_->targetXs[size_t(id)] = tx;
    impl_->targetYs[size_t(id)] = ty;
    return true;
}

bool Crowd::clearAgentTarget(int id) {
    if (!impl_->validId(id)) return false;
    impl_->hasTargets[size_t(id)] = 0;
    return true;
}

bool Crowd::setAgentSpeed(int id, float speed) {
    if (!impl_->validId(id) || speed < 0.f) return false;
    impl_->maxSpeeds[size_t(id)] = speed;
    return true;
}

bool Crowd::setAgentAccel(int id, float accel) {
    if (!impl_->validId(id) || accel < 0.f) return false;
    impl_->maxAccels[size_t(id)] = accel;
    return true;
}

bool Crowd::setAgentTurnRate(int id, float radPerSec) {
    if (!impl_->validId(id) || radPerSec < 0.f) return false;
    impl_->turnRates[size_t(id)] = radPerSec;
    return true;
}

bool Crowd::setAgentRadius(int id, float radius) {
    if (!impl_->validId(id) || radius < 0.f) return false;
    impl_->radii[size_t(id)] = radius;
    return true;
}

bool Crowd::setAgentData(int id, int data) {
    if (!impl_->validId(id)) return false;
    impl_->datas[size_t(id)] = data;
    return true;
}

int Crowd::getAgentData(int id) const {
    if (!impl_->validId(id)) return 0;
    return impl_->datas[size_t(id)];
}

Result<void> Crowd::setAgentAvoidancePriority(int id, int priority) {
    if (!impl_->validId(id))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "Crowd avoidance-priority agent slot was not found", "id"));
    impl_->avoidancePriorities[size_t(id)] = priority;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

int Crowd::getAgentAvoidancePriority(int id) const {
    return impl_->validId(id) ? impl_->avoidancePriorities[size_t(id)] : 0;
}

bool Crowd::setAgentPosition(int id, float x, float y) {
    if (!impl_->validId(id)) return false;
    impl_->xs[size_t(id)] = x;
    impl_->ys[size_t(id)] = y;
    return true;
}

AgentState Crowd::getAgentState(int id) const {
    AgentState s;
    if (!impl_->validId(id)) {
        s.action = -1;
        return s;
    }
    s.x = impl_->xs[size_t(id)];
    s.y = impl_->ys[size_t(id)];
    s.heading = impl_->headings[size_t(id)];
    s.speed = impl_->speeds[size_t(id)];
    s.vx = impl_->vxs[size_t(id)];
    s.vy = impl_->vys[size_t(id)];
    s.action = impl_->actions[size_t(id)];
    s.data = impl_->datas[size_t(id)];
    s.avoidancePriority = impl_->avoidancePriorities[size_t(id)];
    return s;
}

// --- 群体参数 ---

void Crowd::setDefaultSpeed(float speed) { impl_->defaultSpeed = std::max(speed, 0.f); }
void Crowd::setDefaultRadius(float radius) { impl_->defaultRadius = std::max(radius, 0.f); }
void Crowd::setDefaultTurnRate(float radPerSec) {
    impl_->defaultTurnRate = std::max(radPerSec, 0.f);
}
void Crowd::setArriveRadius(float radius) { impl_->arriveRadius = std::max(radius, 0.f); }
void Crowd::setSeparationRadius(float radius) { impl_->sepRadius = std::max(radius, 0.f); }
void Crowd::setPerceptionRadius(float radius) { impl_->perceiveRadius = std::max(radius, 0.f); }
void Crowd::setSeparationWeight(float weight) { impl_->sepWeight = weight; }
void Crowd::setAlignmentWeight(float weight) { impl_->alignWeight = weight; }
void Crowd::setCohesionWeight(float weight) { impl_->cohesionWeight = weight; }
void Crowd::setWanderWeight(float weight) { impl_->wanderWeight = weight; }
void Crowd::setGoalWeight(float weight) { impl_->goalWeight = weight; }
void Crowd::setResolveOverlaps(bool enable) { impl_->resolveOverlaps = enable; }
void Crowd::setClampToField(bool enable) { impl_->clampToField = enable; }


}  // namespace eve::crowd
