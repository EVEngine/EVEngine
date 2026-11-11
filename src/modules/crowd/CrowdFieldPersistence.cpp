/** @file CrowdFieldPersistence.cpp
 * @brief crowd module-interface: crowd.field v1 detached grid/build data; no owner, ECS, event, script or tool face.
 * @remarks Cold explicit admission/encoding; all graph work remains in the canonical CrowdField kernel.
 */
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>
#include "crowd/CrowdField.h"
#include "crowd/CrowdTypes.h"
namespace eve::crowd {
namespace {
constexpr std::uint64_t storageLimit = 8ULL * 1024 * 1024 * 1024;
constexpr int           cellLimit    = 4 * 1024 * 1024;
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "crowd.field"))};
}
const Value::Array& tuple(const Value& v, std::size_t count) {
    auto* a = v.getIf<Value::Array>();
    if (!a || a->size() != count) invalid("Unknown or missing field tuple fields");
    return *a;
}
const Value::Array& array(const Value& v) {
    auto* a = v.getIf<Value::Array>();
    if (!a) invalid("Expected field array");
    return *a;
}
std::uint64_t counter(const Value& v) {
    auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected field counter text");
    std::uint64_t n = 0;
    auto          p = std::from_chars(s->data(), s->data() + s->size(), n);
    if (p.ec != std::errc{} || p.ptr != s->data() + s->size() || std::to_string(n) != *s)
        invalid("Invalid canonical field counter");
    return n;
}
int integer(const Value& v, int min = 0, int max = std::numeric_limits<int>::max()) {
    if (!v.isInt64() || v.asInt() < min || v.asInt() > max) invalid("Invalid field integer");
    return int(v.asInt());
}
float number(const Value& v) {
    if (!v.isDouble() && !v.isInt64()) invalid("Expected field number");
    double n = v.isDouble() ? v.asDouble() : double(v.asInt());
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) invalid("Invalid field float");
    return float(n);
}
bool boolean(const Value& v) {
    auto* b = v.getIf<bool>();
    if (!b) invalid("Expected field boolean");
    return *b;
}
template <class T, class Encode>
Value buffer(const std::vector<T>& values, Encode encode) {
    Value::Array a;
    a.reserve(values.size());
    for (const auto& value : values) a.push_back(encode(value));
    return Value::Array{std::to_string(values.capacity()), std::move(a)};
}
template <class T, class Decode>
void buffer(const Value& value, std::vector<T>& output, Decode decode, std::uint64_t& remaining) {
    const auto& a     = tuple(value, 2);
    auto        count = counter(a[0]);
    const auto& items = array(a[1]);
    if (count < items.size() || count > remaining / sizeof(T) || count > output.max_size())
        invalid("Invalid retained field capacity");
    remaining -= count * sizeof(T);
    output.reserve(std::size_t(count));
    if (output.capacity() != count)
        invalid("Backend cannot preserve exact retained field capacity", DiagnosticCode::Unsupported);
    for (const auto& item : items) output.push_back(decode(item));
}
}  // namespace
Result<void> CrowdField::validateArchiveState() const {
    auto fail = [](const char* message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "crowd.field"));
    };
    if (width_ < 0 || height_ < 0 || width_ > cellLimit || height_ > cellLimit ||
        std::int64_t(width_) * height_ > cellLimit || !std::isfinite(cellSize_) || cellSize_ < 0 ||
        !std::isfinite(originX_) || !std::isfinite(originY_))
        return fail("Invalid field geometry");
    const auto count = std::size_t(width_) * std::size_t(height_);
    if (cost_.size() != count || (!navigationCost_.empty() && navigationCost_.size() != count) ||
        integ_.size() != count || flowX_.size() != count || flowY_.size() != count)
        return fail("Field buffer lengths differ from geometry");
    if (inputRevision_ > revision_ || builtRevision_ > revision_ || flowCursor_ < 0)
        return fail("Invalid field revision or cursor");
    std::uint64_t remaining = storageLimit;
    auto          charge    = [&](auto& values) {
        auto capacity = values.capacity();
        using T       = typename std::decay_t<decltype(values)>::value_type;
        if (capacity > remaining / sizeof(T)) return false;
        remaining -= capacity * sizeof(T);
        return true;
    };
    if (!charge(cost_) || !charge(navigationCost_) || !charge(integ_) || !charge(flowX_) || !charge(flowY_) ||
        !charge(goals_) || !charge(buildHeap_))
        return fail("Field storage exceeds cache ceiling");
    for (const auto& values : {&cost_, &navigationCost_})
        for (float x : *values)
            if (!std::isfinite(x) || x < 0) return fail("Invalid terrain/projection cost");
    for (float x : integ_)
        if (!std::isfinite(x) || x < 0 || x > kUnreachable) return fail("Invalid integration cost");
    for (const auto& values : {&flowX_, &flowY_})
        for (float x : *values)
            if (!std::isfinite(x) || std::abs(x) > 1.f) return fail("Invalid field direction");
    if ((!buildHeap_.empty() && flowCursor_ != 0) ||
        (built_ && (!buildHeap_.empty() || (valid() ? std::size_t(flowCursor_) != count : flowCursor_ != 0))))
        return fail("Field build markers contradict heap/cursor progress");
    if (goals_.size() % 2) return fail("Incomplete field goal pair");
    std::vector<int> ids;
    ids.reserve(goals_.size() / 2);
    for (std::size_t i = 0; i < goals_.size(); i += 2) {
        if (!inBounds(goals_[i], goals_[i + 1])) return fail("Goal outside field");
        ids.push_back(index(goals_[i], goals_[i + 1]));
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return fail("Duplicate field goals");
    for (const auto& [cost, index] : buildHeap_)
        if (!std::isfinite(cost) || cost < 0 || cost >= kUnreachable || index < 0 || std::size_t(index) >= count)
            return fail("Invalid heap node");
    if (!std::is_heap(buildHeap_.begin(), buildHeap_.end(), std::greater<std::pair<float, int>>{}))
        return fail("Invalid field min-heap ordering");
    return Result<void>::success();
}
Result<Value> CrowdField::encodeState() const {
    auto valid = validateArchiveState();
    if (!valid) return Result<Value>::failure(valid.status());
    auto floating = [](float x) { return Value(x); };
    auto integral = [](int x) { return Value(x); };
    return Result<Value>::success(Value::object(
        {{"schema", "crowd.field"},
         {"version", 1},
         {"algorithm", int(flowBuildAlgorithmVersion)},
         {"geometry", Value::Array{width_, height_, cellSize_, originX_, originY_}},
         {"rules", Value::Array{navigationDiagonal_, destinationEntryCost_}},
         {"revisions",
          Value::Array{std::to_string(revision_), std::to_string(builtRevision_), std::to_string(inputRevision_)}},
         {"built", built_},
         {"flowCursor", flowCursor_},
         {"buffers",
          Value::Array{buffer(cost_, floating), buffer(navigationCost_, floating), buffer(integ_, floating),
                       buffer(flowX_, floating), buffer(flowY_, floating), buffer(goals_, integral),
                       buffer(buildHeap_, [](auto node) { return Value::Array{node.first, node.second}; })}}}));
}
Result<CrowdField> CrowdField::prepareState(const Value& value) {
    using R = Result<CrowdField>;
    try {
        const auto* root = value.getIf<Value::Object>();
        if (!root || root->size() != 9) invalid("Unknown or missing field fields");
        for (const char* key :
             {"schema", "version", "algorithm", "geometry", "rules", "revisions", "built", "flowCursor", "buffers"})
            if (!root->contains(key)) invalid("Missing field field");
        auto* schema = root->at("schema").getIf<std::string>();
        if (!schema || *schema != "crowd.field") invalid("Wrong field schema");
        if (integer(root->at("version")) != 1) invalid("Unknown field version", DiagnosticCode::UnknownVersion);
        if (integer(root->at("algorithm")) != int(flowBuildAlgorithmVersion))
            invalid("Unknown field build algorithm", DiagnosticCode::UnknownVersion);
        CrowdField  result;
        const auto& geometry = tuple(root->at("geometry"), 5);
        result.width_        = integer(geometry[0], 0, cellLimit);
        result.height_       = integer(geometry[1], 0, cellLimit);
        if (std::int64_t(result.width_) * result.height_ > cellLimit) invalid("Oversized field geometry");
        result.cellSize_             = number(geometry[2]);
        result.originX_              = number(geometry[3]);
        result.originY_              = number(geometry[4]);
        const auto& rules            = tuple(root->at("rules"), 2);
        result.navigationDiagonal_   = boolean(rules[0]);
        result.destinationEntryCost_ = boolean(rules[1]);
        const auto& revisions        = tuple(root->at("revisions"), 3);
        result.revision_             = counter(revisions[0]);
        result.builtRevision_        = counter(revisions[1]);
        result.inputRevision_        = counter(revisions[2]);
        result.built_                = boolean(root->at("built"));
        result.flowCursor_           = integer(root->at("flowCursor"));
        const auto&   buffers        = tuple(root->at("buffers"), 7);
        std::uint64_t remaining      = storageLimit;
        buffer(buffers[0], result.cost_, number, remaining);
        buffer(buffers[1], result.navigationCost_, number, remaining);
        buffer(buffers[2], result.integ_, number, remaining);
        buffer(buffers[3], result.flowX_, number, remaining);
        buffer(buffers[4], result.flowY_, number, remaining);
        buffer(buffers[5], result.goals_, [](const Value& v) { return integer(v); }, remaining);
        buffer(
            buffers[6], result.buildHeap_,
            [](const Value& v) {
                const auto& a = tuple(v, 2);
                return std::pair<float, int>{number(a[0]), integer(a[1])};
            },
            remaining);
        auto valid = result.validateArchiveState();
        if (!valid) return R::failure(valid.status());
        return R::success(std::move(result));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::crowd
