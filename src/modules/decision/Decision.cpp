#include "decision/Decision.h"

#include "common/Json.h"
#include "common/SquirrelBinding.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <utility>

namespace eve::decision {
namespace {

/** @brief Script-owned handle proxy; the decision context remains module-owned. */
struct ScriptDecisionContext {
    explicit ScriptDecisionContext(DecisionContextHandleRef value) : reference(value) {}
    ~ScriptDecisionContext() noexcept {
        Decision::release(reference).ignore("script decision context proxy destruction");
    }
    DecisionContextHandleRef reference;
};

template <class T>
eve::Result<T> decisionFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "decision"));
}

eve::Result<void> decisionApplied() {
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<bool> decisionTriggered(bool triggered) {
    return eve::Result<bool>::success(
        triggered, eve::Status::success(triggered ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<void> snapshotFailure(std::string path, std::string message) {
    return decisionFailure<void>(eve::DiagnosticCode::SerializationError, std::move(message), std::move(path));
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) {
        return eve::script::projectStatusResult(vm, reference.status(), false, false);
    }

    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned decision proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned decision allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

std::string quote(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\';
        }
        output << character;
    }
    return output.str() + '"';
}

bool isScalar(const eve::json::Value& value) {
    return value.isNull() || value.isBool() || value.isNumber() || value.isString();
}

std::string canonicalize(const eve::json::Value& value) {
    if (value.isNull()) {
        return "null";
    }
    if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    }
    if (value.isNumber()) {
        return value.asString();
    }
    return quote(value.asString());
}

bool parseNumber(const std::string& text, float& value) {
    try {
        size_t parsedLength = 0;
        value               = std::stof(text, &parsedLength);
        return parsedLength == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

}  // namespace

eve::Result<void> DecisionContext::set(const std::string& board, const std::string& key, const std::string& valueJson) {
    std::string parseError;
    auto        document = eve::json::Document::parse(valueJson, &parseError);
    if (board.empty()) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "blackboard name must not be empty",
                                     "board");
    }
    if (key.empty()) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "blackboard key must not be empty", "key");
    }
    if (!document.valid()) {
        return decisionFailure<void>(eve::DiagnosticCode::ParseError,
                                     parseError.empty() ? "invalid blackboard JSON" : parseError, "value");
    }
    if (!isScalar(document.root())) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "blackboard value must be a JSON scalar",
                                     "value");
    }

    boards_[board][key] = canonicalize(document.root());
    return decisionApplied();
}

std::string DecisionContext::get(const std::string& board, const std::string& key,
                                 const std::string& fallbackJson) const {
    auto boardIt = boards_.find(board);
    if (boardIt == boards_.end()) {
        return fallbackJson;
    }

    auto valueIt = boardIt->second.find(key);
    return valueIt == boardIt->second.end() ? fallbackJson : valueIt->second;
}
eve::Result<void> DecisionContext::addTransition(const std::string& m, const std::string& f, const std::string& t,
                                                 const std::string& to) {
    if (m.empty() || f.empty() || t.empty() || to.empty())
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "FSM transition names must not be empty",
                                     "transition");
    transitions_[m][{f, t}] = to;
    return decisionApplied();
}
eve::Result<void> DecisionContext::setState(const std::string& m, const std::string& s) {
    if (m.empty() || s.empty())
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "FSM machine and state names must not be empty", "state");
    states_[m] = s;
    return decisionApplied();
}
eve::Result<bool> DecisionContext::trigger(const std::string& m, const std::string& t) {
    if (m.empty() || t.empty())
        return decisionFailure<bool>(eve::DiagnosticCode::InvalidArgument,
                                     "FSM machine and trigger names must not be empty", "trigger");
    auto s = states_.find(m);
    auto a = transitions_.find(m);
    if (s == states_.end())
        return decisionFailure<bool>(eve::DiagnosticCode::NotFound, "FSM machine has no current state", "machine");
    if (a == transitions_.end()) return decisionTriggered(false);
    auto i = a->second.find({s->second, t});
    if (i == a->second.end()) return decisionTriggered(false);
    s->second = i->second;
    return decisionTriggered(true);
}
std::string DecisionContext::state(const std::string& m) const {
    auto i = states_.find(m);
    return i == states_.end() ? std::string{} : i->second;
}
float DecisionContext::utility(const std::string& considerationsCsv) {
    std::stringstream input(considerationsCsv);
    std::string       consideration;
    double            weightedSum = 0;
    double            totalWeight = 0;

    while (std::getline(input, consideration, ',')) {
        auto separator = consideration.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        float value  = 0;
        float weight = 0;
        if (parseNumber(consideration.substr(0, separator), value) &&
            parseNumber(consideration.substr(separator + 1), weight) && weight > 0) {
            weightedSum += std::clamp(value, 0.f, 1.f) * weight;
            totalWeight += weight;
        }
    }

    return totalWeight ? float(weightedSum / totalWeight) : 0.f;
}

std::string DecisionContext::choose(const std::string& options) {
    std::stringstream input(options);
    std::string       option;
    std::string       bestName;
    float             bestScore = -1;

    while (std::getline(input, option, ';')) {
        auto separator = option.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }

        auto  name  = option.substr(0, separator);
        float score = utility(option.substr(separator + 1));
        if (score > bestScore || (score == bestScore && (bestName.empty() || name < bestName))) {
            bestScore = score;
            bestName  = name;
        }
    }

    return bestName;
}

eve::Result<void> DecisionContext::newGrid(const std::string& name, int width, int height, float cellSize,
                                           float originX, float originY) {
    if (name.empty()) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "influence grid name must not be empty",
                                     "name");
    }
    if (width <= 0 || height <= 0 || size_t(width) > 10000000 / size_t(height)) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "influence grid dimensions are invalid or too large", "dimensions");
    }
    if (!std::isfinite(cellSize) || cellSize <= 0 || !std::isfinite(originX) || !std::isfinite(originY)) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "influence grid geometry must be finite and have a positive cell size",
                                     "geometry");
    }

    grids_[name] = {width, height, cellSize, originX, originY, std::vector<float>(size_t(width) * height)};
    return decisionApplied();
}

eve::Result<void> DecisionContext::setCell(const std::string& name, int x, int y, float value) {
    auto gridIt = grids_.find(name);
    if (gridIt == grids_.end()) {
        return decisionFailure<void>(eve::DiagnosticCode::NotFound, "influence grid does not exist", "grid");
    }
    if (x < 0 || y < 0 || x >= gridIt->second.width || y >= gridIt->second.height || !std::isfinite(value)) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "influence grid cell or value is invalid",
                                     "cell");
    }

    gridIt->second.values[size_t(y) * gridIt->second.width + x] = value;
    return decisionApplied();
}

eve::Result<void> DecisionContext::addCell(const std::string& name, int x, int y, float delta) {
    auto gridIt = grids_.find(name);
    if (gridIt == grids_.end()) {
        return decisionFailure<void>(eve::DiagnosticCode::NotFound, "influence grid does not exist", "grid");
    }
    if (x < 0 || y < 0 || x >= gridIt->second.width || y >= gridIt->second.height || !std::isfinite(delta)) {
        return decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "influence grid cell or value is invalid",
                                     "cell");
    }

    auto& cell = gridIt->second.values[size_t(y) * gridIt->second.width + x];
    if (!std::isfinite(cell + delta)) {
        return decisionFailure<void>(eve::DiagnosticCode::Failed, "influence grid cell update overflowed", "cell");
    }

    cell += delta;
    return decisionApplied();
}

float DecisionContext::sample(const std::string& name, float worldX, float worldY, float fallback) const {
    auto gridIt = grids_.find(name);
    if (gridIt == grids_.end()) {
        return fallback;
    }

    const Grid& grid  = gridIt->second;
    int         cellX = int(std::floor((worldX - grid.originX) / grid.cellSize));
    int         cellY = int(std::floor((worldY - grid.originY) / grid.cellSize));
    if (cellX < 0 || cellY < 0 || cellX >= grid.width || cellY >= grid.height) {
        return fallback;
    }

    return grid.values[size_t(cellY) * grid.width + cellX];
}
std::string DecisionContext::snapshotJson() const {
    std::ostringstream o;
    o << std::setprecision(9) << "{\"version\":1,\"boards\":{";
    bool a = true;
    for (auto& [b, vs] : boards_) {
        if (!a) o << ',';
        a = false;
        o << quote(b) << ":{";
        bool f = true;
        for (auto& [k, v] : vs) {
            if (!f) o << ',';
            f = false;
            o << quote(k) << ':' << v;
        }
        o << '}';
    }
    o << "},\"states\":{";
    a = true;
    for (auto& [m, s] : states_) {
        if (!a) o << ',';
        a = false;
        o << quote(m) << ':' << quote(s);
    }
    o << "},\"transitions\":[";
    a = true;
    for (auto& [m, ts] : transitions_)
        for (auto& [key, to] : ts) {
            if (!a) o << ',';
            a = false;
            o << "[" << quote(m) << ',' << quote(key.first) << ',' << quote(key.second) << ',' << quote(to) << "]";
        }
    o << "],\"grids\":[";
    a = true;
    for (auto& [n, g] : grids_) {
        if (!a) o << ',';
        a = false;
        o << "{\"name\":" << quote(n) << ",\"w\":" << g.width << ",\"h\":" << g.height << ",\"cell\":" << g.cellSize
          << ",\"ox\":" << g.originX << ",\"oy\":" << g.originY << ",\"values\":[";
        for (size_t i = 0; i < g.values.size(); ++i) {
            if (i) o << ',';
            o << g.values[i];
        }
        o << "]}";
    }
    return o.str() + "]}";
}
eve::Result<void> DecisionContext::restoreJson(const std::string& j) {
    std::string e;
    auto        d = eve::json::Document::parse(j, &e);
    if (!d.valid())
        return decisionFailure<void>(eve::DiagnosticCode::ParseError, e.empty() ? "invalid decision snapshot JSON" : e,
                                     "snapshot");
    if (!d.root().isObject()) return snapshotFailure("snapshot", "decision snapshot must be a JSON object");
    const auto version = d.root().get("version");
    if (!version.isIntegerLiteral() || version.asInt() != 1)
        return decisionFailure<void>(eve::DiagnosticCode::UnknownVersion, "unsupported decision snapshot version",
                                     "version");
    DecisionContext n;
    auto            b = d.root().get("boards");
    if (!b.isObject()) return snapshotFailure("boards", "decision snapshot boards must be an object");
    for (auto& bn : b.keys()) {
        auto v = b.get(bn.c_str());
        if (!v.isObject()) return snapshotFailure("boards." + bn, "blackboard must be an object");
        for (auto& k : v.keys()) {
            const auto value = v.get(k.c_str());
            if (!isScalar(value))
                return snapshotFailure("boards." + bn + "." + k, "blackboard value must be a JSON scalar");
            auto result = n.set(bn, k, canonicalize(value));
            if (!result.ok()) return eve::Result<void>::failure(result.status());
        }
    }
    auto st = d.root().get("states");
    if (!st.isObject()) return snapshotFailure("states", "decision snapshot states must be an object");
    for (auto& m : st.keys()) {
        const auto state = st.get(m.c_str());
        if (!state.isString()) return snapshotFailure("states." + m, "FSM state must be a string");
        auto result = n.setState(m, state.asString());
        if (!result.ok()) return eve::Result<void>::failure(result.status());
    }
    auto tr = d.root().get("transitions");
    if (!tr.isArray()) return snapshotFailure("transitions", "decision snapshot transitions must be an array");
    for (size_t i = 0; i < tr.size(); ++i) {
        auto v = tr.at(i);
        if (!v.isArray() || v.size() != 4)
            return snapshotFailure("transitions[" + std::to_string(i) + "]",
                                   "FSM transition must contain four strings");
        for (size_t field = 0; field < 4; ++field)
            if (!v.at(field).isString())
                return snapshotFailure("transitions[" + std::to_string(i) + "]",
                                       "FSM transition must contain four strings");
        auto result = n.addTransition(v.at(0).asString(), v.at(1).asString(), v.at(2).asString(), v.at(3).asString());
        if (!result.ok()) return eve::Result<void>::failure(result.status());
    }
    auto gs = d.root().get("grids");
    if (!gs.isArray()) return snapshotFailure("grids", "decision snapshot grids must be an array");
    for (size_t i = 0; i < gs.size(); ++i) {
        auto v = gs.at(i);
        if (!v.isObject())
            return snapshotFailure("grids[" + std::to_string(i) + "]", "influence grid must be an object");
        int        w = v.getInt("w"), h = v.getInt("h");
        const auto name = v.get("name");
        const auto cell = v.get("cell");
        const auto ox   = v.get("ox");
        const auto oy   = v.get("oy");
        if (!name.isString() || !cell.isNumber() || !ox.isNumber() || !oy.isNumber())
            return snapshotFailure("grids[" + std::to_string(i) + "]", "influence grid fields are malformed");
        auto grid =
            n.newGrid(name.asString(), w, h, float(cell.asDouble()), float(ox.asDouble()), float(oy.asDouble()));
        if (!grid.ok()) return eve::Result<void>::failure(grid.status());
        auto vals = v.get("values");
        if (!vals.isArray() || vals.size() != size_t(w) * h)
            return snapshotFailure("grids[" + std::to_string(i) + "].values",
                                   "influence grid values have the wrong size");
        for (size_t z = 0; z < vals.size(); ++z) {
            const auto value = vals.at(z);
            if (!value.isNumber() || !std::isfinite(value.asDouble()))
                return snapshotFailure("grids[" + std::to_string(i) + "].values[" + std::to_string(z) + "]",
                                       "influence grid cell must be a finite number");
            auto result = n.setCell(name.asString(), int(z % size_t(w)), int(z / size_t(w)), float(value.asDouble()));
            if (!result.ok()) return eve::Result<void>::failure(result.status());
        }
    }
    boards_      = std::move(n.boards_);
    states_      = std::move(n.states_);
    transitions_ = std::move(n.transitions_);
    grids_       = std::move(n.grids_);
    return decisionApplied();
}
eve::Result<DecisionContextHandleRef> Decision::newContext() {
    Decision* module = Decision::create();
    return module->contexts_.emplace(std::make_unique<DecisionContext>());
}

eve::script::Borrowed<DecisionContext> Decision::resolve(DecisionContextHandleRef reference) noexcept {
    Decision* module = ModuleManager::getInstance<Decision>("Decision");
    if (!module) return {};
    return module->contexts_.resolve(reference);
}

eve::Result<void> Decision::release(DecisionContextHandleRef reference) {
    Decision* module = ModuleManager::getInstance<Decision>("Decision");
    if (!module)
        return decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "Decision module is no longer loaded",
                                     "context");
    return module->contexts_.erase(reference);
}

bool Decision::isStale(DecisionContextHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Decision* module = ModuleManager::getInstance<Decision>("Decision");
    return !module || module->contexts_.isStale(reference);
}

Module_IMPL(Decision, new Decision());
void Decision::expose(ssq::Table& t) {
    const HSQUIRRELVM vm = t.getHandle();
    auto x = t.addClass<DecisionContext>("DecisionContext", std::function<DecisionContext*()>([]() { return nullptr; }),
                                         false);
    x.addFunc("set",
              [vm](DecisionContext* value, const std::string& board, const std::string& key, const std::string& json) {
                  if (!value)
                      return eve::script::projectResult(
                          vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "decision context must not be null", "context"));
                  return eve::script::projectResult(vm, value->set(board, key, json));
              });
    x.addFunc("get", &DecisionContext::get);
    x.addFunc("addTransition", [vm](DecisionContext* value, const std::string& machine, const std::string& from,
                                    const std::string& trigger, const std::string& to) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "decision context must not be null",
                                          "context"));
        return eve::script::projectResult(vm, value->addTransition(machine, from, trigger, to));
    });
    x.addFunc("setState", [vm](DecisionContext* value, const std::string& machine, const std::string& state) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "decision context must not be null",
                                          "context"));
        return eve::script::projectResult(vm, value->setState(machine, state));
    });
    x.addFunc("trigger", [vm](DecisionContext* value, const std::string& machine, const std::string& trigger) {
        if (!value)
            return eve::script::projectResult(vm,
                                              decisionFailure<bool>(eve::DiagnosticCode::InvalidArgument,
                                                                    "decision context must not be null", "context"),
                                              [](bool fired) { return eve::Value(fired); });
        return eve::script::projectResult(vm, value->trigger(machine, trigger),
                                          [](bool fired) { return eve::Value(fired); });
    });
    x.addFunc("state", &DecisionContext::state);
    x.addFunc("utility", [](DecisionContext*, const std::string& s) { return DecisionContext::utility(s); });
    x.addFunc("choose", [](DecisionContext*, const std::string& s) { return DecisionContext::choose(s); });
    x.addFunc("newGrid", [vm](DecisionContext* value, const std::string& name, int width, int height, float cellSize,
                              float originX, float originY) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "decision context must not be null",
                                          "context"));
        return eve::script::projectResult(vm, value->newGrid(name, width, height, cellSize, originX, originY));
    });
    x.addFunc("setCell", [vm](DecisionContext* value, const std::string& name, int x, int y, float cell) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "decision context must not be null",
                                          "context"));
        return eve::script::projectResult(vm, value->setCell(name, x, y, cell));
    });
    x.addFunc("addCell", [vm](DecisionContext* value, const std::string& name, int x, int y, float delta) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "decision context must not be null",
                                          "context"));
        return eve::script::projectResult(vm, value->addCell(name, x, y, delta));
    });
    x.addFunc("sample", &DecisionContext::sample);
    x.addFunc("snapshotJson", &DecisionContext::snapshotJson);
    x.addFunc("restoreJson", [vm](DecisionContext* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument, "decision context must not be null",
                                          "context"));
        return eve::script::projectResult(vm, value->restoreJson(json));
    });
    auto owned = t.addClass<ScriptDecisionContext>(
        "DecisionContextProxy", std::function<ScriptDecisionContext*()>([] { return nullptr; }), true);
    owned.addFunc("ownership", [](ScriptDecisionContext*) { return std::string("owned"); });
    owned.addFunc("ownerEpoch", [](ScriptDecisionContext* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    owned.addFunc("handle", [](ScriptDecisionContext* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    owned.addFunc("isStale",
                  [](ScriptDecisionContext* value) { return !value || Decision::isStale(value->reference); });
    owned.addFunc("release", [vm](ScriptDecisionContext* value) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                          "owned decision context proxy must not be null", "context"));
        return eve::script::projectResult(vm, Decision::release(value->reference));
    });
    owned.addFunc("setState", [vm](ScriptDecisionContext* value, const std::string& machine, const std::string& state) {
        if (!value)
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                          "owned decision context proxy must not be null", "context"));
        auto view = Decision::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->setState(machine, state));
    });
    owned.addFunc("set", [vm](ScriptDecisionContext* value, const std::string& board, const std::string& key,
                              const std::string& json) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->set(board, key, json));
    });
    owned.addFunc("get", [](ScriptDecisionContext* value, const std::string& board, const std::string& key,
                            const std::string& fallback) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        return view.isBound() ? view->get(board, key, fallback) : fallback;
    });
    owned.addFunc("addTransition", [vm](ScriptDecisionContext* value, const std::string& machine,
                                        const std::string& from, const std::string& trigger, const std::string& to) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->addTransition(machine, from, trigger, to));
    });
    owned.addFunc(
        "trigger", [vm](ScriptDecisionContext* value, const std::string& machine, const std::string& trigger) {
            auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
            if (!view.isBound())
                return eve::script::projectResult(
                    vm,
                    decisionFailure<bool>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"),
                    [](bool fired) { return eve::Value(fired); });
            return eve::script::projectResult(vm, view->trigger(machine, trigger),
                                              [](bool fired) { return eve::Value(fired); });
        });
    owned.addFunc("state", [](ScriptDecisionContext* value, const std::string& machine) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        return view.isBound() ? view->state(machine) : std::string{};
    });
    owned.addFunc("newGrid", [vm](ScriptDecisionContext* value, const std::string& name, int width, int height,
                                  float cellSize, float originX, float originY) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->newGrid(name, width, height, cellSize, originX, originY));
    });
    owned.addFunc("setCell", [vm](ScriptDecisionContext* value, const std::string& name, int x, int y, float cell) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->setCell(name, x, y, cell));
    });
    owned.addFunc("addCell", [vm](ScriptDecisionContext* value, const std::string& name, int x, int y, float delta) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->addCell(name, x, y, delta));
    });
    owned.addFunc("sample", [](ScriptDecisionContext* value, const std::string& name, float worldX, float worldY,
                               float fallback) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        return view.isBound() ? view->sample(name, worldX, worldY, fallback) : fallback;
    });
    owned.addFunc("snapshotJson", [](ScriptDecisionContext* value) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        return view.isBound() ? view->snapshotJson() : std::string{};
    });
    owned.addFunc("restoreJson", [vm](ScriptDecisionContext* value, const std::string& json) {
        auto view = value ? Decision::resolve(value->reference) : eve::script::Borrowed<DecisionContext>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, decisionFailure<void>(eve::DiagnosticCode::StaleHandle, "owned decision context handle is stale",
                                          "context"));
        return eve::script::projectResult(vm, view->restoreJson(json));
    });
    owned.addFunc("utility",
                  [](ScriptDecisionContext*, const std::string& input) { return DecisionContext::utility(input); });
    owned.addFunc("choose",
                  [](ScriptDecisionContext*, const std::string& input) { return DecisionContext::choose(input); });
    auto c = t.addClass(name, Decision::create, false);
    expose(c);
}
void Decision::expose(ssq::Class& c) {
    c.addFunc("getName", &Decision::getName);
    c.addFunc("newContext", [vm = c.getHandle()](Decision*) -> ssq::Table {
        return makeOwnedProxy<DecisionContextHandleRef, ScriptDecisionContext>(
            vm, Decision::newContext(), [](DecisionContextHandleRef ref) { return Decision::release(ref); });
    });
}
}  // namespace eve::decision
