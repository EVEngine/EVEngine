// eve-hot-path
#include "physics/field/GravityField.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace eve::physics::field {
namespace {
using Vec                    = std::array<double, 3>;
constexpr std::size_t noNode = std::numeric_limits<std::size_t>::max();
enum class TargetMode { Sources, Independent };

Vec    subtract(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double norm(Vec a) {
    const double squared = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
    // Normalized field coordinates ordinarily need only a square root. Retain
    // scaled hypot for squared underflow/overflow, including distinct tiny offsets.
    if (squared >= std::numeric_limits<double>::min() && std::isfinite(squared)) return std::sqrt(squared);
    return std::hypot(a[0], a[1], a[2]);
}
bool finite(Vec a) { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }

template <class T>
Result<T> failed(DiagnosticCode code, const char* message) {
    if (code == DiagnosticCode::Failed)
        return Result<T>::failure(Status::failure(
            StatusCode::Rejected, Diagnostic::error(code, message, "physics.field.gravity.budgetExceeded")));
    return Result<T>::failure(Diagnostic::error(code, message, "physics.field.gravity"));
}

struct Power {
    unsigned x, y, z;
    unsigned degree() const { return x + y + z; }
};

double choose(unsigned n, unsigned k) {
    double value = 1.0;
    for (unsigned i = 1; i <= k; ++i) value = value * (n + 1 - i) / i;
    return value;
}

struct Translation {
    std::size_t a, b, sum;
    double      coefficient;
};

struct KernelStep {
    std::array<std::size_t, 6> lower{};
    std::array<double, 6>      weight{};
    double                     inverseDegree = 1.0;
};

struct LocalTranslation {
    std::size_t source, kernel;
    double      coefficient;
};

// A fixed-order Cartesian basis, indexed once per snapshot; no runtime string lookups.
struct Basis {
    std::vector<Power>            powers;
    std::vector<Translation>      translations;
    std::vector<LocalTranslation> localTranslations;
    std::vector<std::size_t>      localStarts;
    std::array<int, 343>          indices{};
    std::array<KernelStep, 84>    kernelSteps{};
    std::array<unsigned, 84>      monomialAxes{};
    explicit Basis(unsigned order) {
        indices.fill(-1);
        for (unsigned n = 0; n <= order; ++n)
            for (unsigned x = 0; x <= n; ++x)
                for (unsigned y = 0; y <= n - x; ++y) {
                    Power p{x, y, n - x - y};
                    indices[p.x * 49 + p.y * 7 + p.z] = static_cast<int>(powers.size());
                    powers.push_back(p);
                }
        for (std::size_t i = 1; i < powers.size(); ++i) {
            const auto p    = powers[i];
            monomialAxes[i] = p.z ? 2 : p.y ? 1 : 0;
            const std::array<unsigned, 3> exponents{p.x, p.y, p.z};
            const unsigned                axis   = p.x ? 0 : p.y ? 1 : 2;
            const double                  degree = exponents[axis];
            auto&                         step   = kernelSteps[i];
            step.inverseDegree                   = 1.0 / degree;
            for (unsigned j = 0; j < 3; ++j) {
                if (exponents[j] > 0) {
                    auto q = exponents;
                    --q[j];
                    step.lower[j]  = static_cast<std::size_t>(index({q[0], q[1], q[2]}));
                    step.weight[j] = j == axis ? 2 * degree - 1 : 2 * degree;
                }
                if (exponents[j] > 1) {
                    auto q = exponents;
                    q[j] -= 2;
                    step.lower[3 + j]  = static_cast<std::size_t>(index({q[0], q[1], q[2]}));
                    step.weight[3 + j] = j == axis ? degree - 1 : degree;
                }
            }
        }
        for (std::size_t a = 0; a < powers.size(); ++a)
            for (std::size_t b = 0; b < powers.size(); ++b) {
                const Power sum{powers[a].x + powers[b].x, powers[a].y + powers[b].y, powers[a].z + powers[b].z};
                const int   c = index(sum);
                if (c >= 0)
                    translations.push_back(
                        {a, b, static_cast<std::size_t>(c),
                         choose(sum.x, powers[a].x) * choose(sum.y, powers[a].y) * choose(sum.z, powers[a].z)});
            }
        // Group M2L terms by destination so each local coefficient stays in a
        // scalar accumulator. Source order within each destination is unchanged.
        localStarts.resize(powers.size() + 1);
        for (const auto& term : translations) ++localStarts[term.b + 1];
        std::partial_sum(localStarts.begin(), localStarts.end(), localStarts.begin());
        auto cursor = localStarts;
        localTranslations.resize(translations.size());
        for (const auto& term : translations)
            localTranslations[cursor[term.b]++] = {term.a, term.sum,
                                                   powers[term.a].degree() % 2 ? -term.coefficient : term.coefficient};
    }
    int index(Power p) const {
        if (p.x > 6 || p.y > 6 || p.z > 6) return -1;
        return indices[p.x * 49 + p.y * 7 + p.z];
    }
};

std::array<double, 84> monomials(const Basis& basis, Vec d) {
    std::array<double, 84> values{};
    values[0] = 1.0;
    // Removing the last nonzero axis preserves the original x, then y, then z
    // multiplication sequence while sharing prefixes between monomials.
    for (std::size_t i = 1; i < basis.powers.size(); ++i) {
        const auto axis = basis.monomialAxes[i];
        values[i]       = values[basis.kernelSteps[i].lower[axis]] * d[axis];
    }
    return values;
}

struct Node {
    Vec                        center{};
    double                     half   = 0.0;
    double                     radius = 0.0;
    std::size_t                begin = 0, end = 0;
    std::array<std::size_t, 8> children{};
    // Target-only nodes have no multipole. An empty local means no accepted
    // far-field contribution on this node or any propagated ancestor.
    std::vector<double> multipole, local;
    double              error        = 0.0;
    double              subtreeError = 0.0;
    double              maxChildError = 0.0;
    std::size_t         parent       = noNode;
    bool                leaf         = true;
    Node(Vec c, double h, std::size_t first, std::size_t last, std::size_t count)
        : center(c), half(h), begin(first), end(last), multipole(count) {
        children.fill(noNode);
    }
};

// Taylor coefficients of F(u)=-1/|d+u|. The identity
// (r^2+2*d.u+|u|^2)*partial_i F = -(d_i+u_i)*F determines each
// nonconstant coefficient from at most six lower-degree coefficients.
// Basis preparation caches those dependencies; no polynomial convolution is needed.
std::array<double, 84> kernel(const Basis& basis, Vec d) {
    const double           r     = norm(d);
    const double           invR2 = 1.0 / (r * r);
    std::array<double, 84> result{};
    result[0] = -1.0 / r;
    for (std::size_t i = 1; i < basis.powers.size(); ++i) {
        const auto& step = basis.kernelSteps[i];
        double      sum  = 0.0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            sum += step.weight[axis] * d[axis] * result[step.lower[axis]];
            sum += step.weight[3 + axis] * result[step.lower[3 + axis]];
        }
        result[i] = -sum * invR2 * step.inverseDegree;
    }
    return result;
}
}  // namespace

struct GravityField::Impl {
    static Result<std::unique_ptr<Impl>> build(std::span<const Particle> input, std::span<const FieldTarget> targets,
                                               FieldStamp stamp, GravityPolicy policy, TargetMode mode);
    FieldStamp                           stamp;
    GravityPolicy                        policy;
    GravityMetrics                       metrics;
    Basis                                basis;
    std::vector<Particle>                particles;
    std::vector<Vec>                     originalPositions;
    std::vector<std::size_t>             order, scratch;
    std::vector<Node>                    nodes;
    std::vector<std::pair<std::size_t, std::size_t>> near;
    TargetMode                                       targetMode  = TargetMode::Sources;
    std::size_t                                      sourceCount = 0;
    std::vector<std::optional<std::size_t>>          excludedSources;
    std::vector<std::size_t>                         sourceRanks;
    double                                           accelerationScale = 1.0, potentialScale = 1.0;
    DiagnosticCode                                   failureCode    = DiagnosticCode::None;
    const char*                                      failureMessage = "";
    std::size_t                                      openingRejected = 0;
    std::size_t                                      errorRejected   = 0;

    Impl(FieldStamp s, GravityPolicy p) : stamp(s), policy(p), basis(p.expansionOrder) {}

    void fail(DiagnosticCode code, const char* message) {
        if (failureCode == DiagnosticCode::None) {
            failureCode    = code;
            failureMessage = message;
        }
    }

    std::size_t buildNode(Vec center, double half, std::size_t begin, std::size_t end, unsigned depth,
                          bool sourceTree = true) {
        if (failureCode != DiagnosticCode::None) return noNode;
        if (nodes.size() >= policy.maxNodes) {
            fail(DiagnosticCode::Failed, "Node budget exceeded");
            return noNode;
        }
        const std::size_t id = nodes.size();
        nodes.emplace_back(center, half, begin, end, sourceTree ? basis.powers.size() : 0);
        metrics.treeDepth = std::max(metrics.treeDepth, depth);
        for (std::size_t i = begin; i < end; ++i)
            nodes[id].radius = std::max(nodes[id].radius, norm(subtract(particles[order[i]].position, center)));
        metrics.partitionVisits += end - begin;
        if (end - begin <= policy.leafCapacity || depth == policy.maxDepth || half == 0.0) {
            if (!sourceTree) return id;
            for (std::size_t i = begin; i < end; ++i) {
                const auto& source = particles[order[i]];
                const Vec   d      = subtract(source.position, center);
                const auto  values = monomials(basis, d);
                for (std::size_t j = 0; j < basis.powers.size(); ++j) nodes[id].multipole[j] += source.mass * values[j];
            }
            return id;
        }
        std::array<std::size_t, 8> counts{}, starts{}, cursor{};
        auto                       octant = [&](std::size_t index) {
            const auto& x = particles[index].position;
            return static_cast<unsigned>((x[0] >= center[0] ? 1 : 0) | (x[1] >= center[1] ? 2 : 0) |
                                                               (x[2] >= center[2] ? 4 : 0));
        };
        for (std::size_t i = begin; i < end; ++i) ++counts[octant(order[i])];
        // Compress unary paths; partitionVisits still counts the work at every level.
        unsigned occupied = 0, only = 0;
        for (unsigned c = 0; c < 8; ++c) {
            if (counts[c] != 0) {
                ++occupied;
                only = c;
            }
        }
        if (occupied == 1) {
            Vec childCenter = center;
            for (unsigned axis = 0; axis < 3; ++axis) childCenter[axis] += (only & (1u << axis) ? 0.5 : -0.5) * half;
            nodes.pop_back();
            return buildNode(childCenter, half * 0.5, begin, end, depth + 1, sourceTree);
        }
        starts[0] = begin;
        for (unsigned c = 1; c < 8; ++c) starts[c] = starts[c - 1] + counts[c - 1];
        cursor = starts;
        for (std::size_t i = begin; i < end; ++i) scratch[cursor[octant(order[i])]++] = order[i];
        std::copy(scratch.begin() + static_cast<std::ptrdiff_t>(begin),
                  scratch.begin() + static_cast<std::ptrdiff_t>(end),
                  order.begin() + static_cast<std::ptrdiff_t>(begin));
        nodes[id].leaf = false;
        for (unsigned c = 0; c < 8; ++c) {
            if (counts[c] == 0) continue;
            Vec childCenter = center;
            for (unsigned axis = 0; axis < 3; ++axis) childCenter[axis] += (c & (1u << axis) ? 0.5 : -0.5) * half;
            const auto child =
                buildNode(childCenter, half * 0.5, starts[c], starts[c] + counts[c], depth + 1, sourceTree);
            if (child == noNode) return noNode;
            nodes[id].children[c] = child;
            nodes[child].parent   = id;
            if (!sourceTree) continue;
            const Vec  delta  = subtract(nodes[child].center, center);
            const auto powers = monomials(basis, delta);
            for (const auto& term : basis.translations)
                nodes[id].multipole[term.sum] += term.coefficient * powers[term.b] * nodes[child].multipole[term.a];
        }
        return id;
    }

    double usedError(std::size_t target) const {
        double total = nodes[target].subtreeError;
        for (auto parent = nodes[target].parent; parent != noNode; parent = nodes[parent].parent)
            total = std::nextafter(total + nodes[parent].error, INFINITY);
        return total;
    }

    void chargeError(std::size_t target, double bound) {
        nodes[target].error = std::nextafter(nodes[target].error + bound, INFINITY);
        for (auto id = target; id != noNode; id = nodes[id].parent) {
            nodes[id].subtreeError = std::nextafter(nodes[id].error + nodes[id].maxChildError, INFINITY);
            if (nodes[id].parent != noNode) {
                auto& parent = nodes[nodes[id].parent];
                parent.maxChildError = std::max(parent.maxChildError, nodes[id].subtreeError);
            }
        }
    }

    void interact(std::size_t source, std::size_t target) {
        if (failureCode != DiagnosticCode::None) return;
        if (++metrics.traversalPairs > policy.maxInteractions) {
            fail(DiagnosticCode::Failed, "Interaction traversal budget exceeded");
            return;
        }
        const auto&  s     = nodes[source];
        auto&        t     = nodes[target];
        const Vec    d     = subtract(t.center, s.center);
        const double r     = norm(d);
        const double q     = r > 0.0 ? (s.radius + t.radius) / r : 1.0;
        double       bound = std::numeric_limits<double>::infinity();
        const bool opened = source != target && q < policy.openingRatio;
        if (opened) {
            const double oneMinus = 1.0 - q;
            const double a        = policy.expansionOrder + 2.0;
            // Sum_{k=p}^infinity (k+2)^2 q^k bounds the gradient tail of the
            // Laplace homogeneous expansion; source masses are nonnegative and sum to one.
            const double tail =
                std::pow(q, static_cast<int>(policy.expansionOrder)) *
                (a * a / oneMinus + 2 * a * q / (oneMinus * oneMinus) + q * (1 + q) / (oneMinus * oneMinus * oneMinus));
            bound = accelerationScale * s.multipole[0] / (r * r) * tail;
        }
        if (!opened) ++openingRejected;
        // Spend only the remaining maximum error over every descendant target.
        // Direct pairs consume no truncation budget; later ancestor acceptance
        // must include already charged descendants, not merely its own local error.
        if (std::isfinite(bound) &&
            std::nextafter(bound + usedError(target), INFINITY) < policy.absoluteAccelerationError) {
            const auto coefficients = kernel(basis, d);
            if (t.local.empty()) t.local.resize(basis.powers.size());
            for (std::size_t b = 0; b < basis.powers.size(); ++b) {
                double value = t.local[b];
                for (auto i = basis.localStarts[b]; i < basis.localStarts[b + 1]; ++i) {
                    const auto& term = basis.localTranslations[i];
                    value += coefficients[term.kernel] * term.coefficient * s.multipole[term.source];
                }
                t.local[b] = value;
            }
            chargeError(target, bound);
            ++metrics.farInteractions;
            return;
        }
        if (opened) ++errorRejected;
        if (s.leaf && t.leaf) {
            const auto ns = s.end - s.begin, nt = t.end - t.begin;
            const auto partners = source == target ? ns - 1 : nt;
            if (partners != 0 && ns > std::numeric_limits<std::size_t>::max() / partners) {
                fail(DiagnosticCode::Failed, "Near-field pair budget exceeded");
                return;
            }
            auto count = ns * partners;
            if (targetMode == TargetMode::Independent)
                for (auto ti = t.begin; ti < t.end; ++ti) {
                    const auto excluded = excludedSources[order[ti] - sourceCount];
                    if (excluded && sourceRanks[*excluded] >= s.begin && sourceRanks[*excluded] < s.end) --count;
                }
            if (count > policy.maxDirectPairs - metrics.directPairs) {
                fail(DiagnosticCode::Failed, "Near-field pair budget exceeded");
                return;
            }
            metrics.directPairs += count;
            near.emplace_back(source, target);
            ++metrics.nearInteractions;
        } else if (!s.leaf && (t.leaf || s.half >= t.half)) {
            for (const auto child : s.children)
                if (child != noNode) interact(child, target);
        } else {
            for (const auto child : t.children)
                if (child != noNode) interact(source, child);
        }
    }

    void propagate(std::size_t id) {
        const auto& parent = nodes[id];
        for (auto childId : parent.children) {
            if (childId == noNode) continue;
            auto& child = nodes[childId];
            if (!parent.local.empty()) {
                if (child.local.empty()) child.local.resize(basis.powers.size());
                const Vec  delta  = subtract(child.center, parent.center);
                const auto powers = monomials(basis, delta);
                for (const auto& term : basis.translations)
                    child.local[term.a] += parent.local[term.sum] * term.coefficient * powers[term.b];
            }
            child.error += parent.error;
            propagate(childId);
        }
    }
};

GravityField::GravityField(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GravityField::~GravityField()                                   = default;
GravityField::GravityField(GravityField&&) noexcept             = default;
GravityField&  GravityField::operator=(GravityField&&) noexcept = default;
GravityMetrics GravityField::metrics() const noexcept { return impl_ ? impl_->metrics : GravityMetrics{}; }

Result<GravityField> GravityField::build(std::span<const Particle> input, FieldStamp stamp, GravityPolicy policy) {
    auto prepared = Impl::build(input, {}, stamp, policy, TargetMode::Sources);
    if (!prepared.ok()) return Result<GravityField>::failure(prepared.status());
    return Result<GravityField>::success(GravityField(std::move(prepared.value())));
}

Result<GravityField> GravityField::buildAtTargets(std::span<const Particle> input, std::span<const FieldTarget> targets,
                                                  FieldStamp stamp, GravityPolicy policy) {
    auto prepared = Impl::build(input, targets, stamp, policy, TargetMode::Independent);
    if (!prepared.ok()) return Result<GravityField>::failure(prepared.status());
    return Result<GravityField>::success(GravityField(std::move(prepared.value())));
}
Result<std::unique_ptr<GravityField::Impl>> GravityField::Impl::build(std::span<const Particle>    input,
                                                                      std::span<const FieldTarget> targets,
                                                                      FieldStamp stamp, GravityPolicy policy,
                                                                      TargetMode mode) {
    if (input.empty() || input.size() > policy.maxParticles || targets.size() > policy.maxParticles - input.size() ||
        policy.expansionOrder < 2 || policy.expansionOrder > 6 || policy.leafCapacity == 0 || policy.maxDepth == 0 ||
        policy.maxDepth > 64 || policy.maxNodes == 0 || policy.maxInteractions == 0 ||
        !std::isfinite(policy.gravitationalConstant) || policy.gravitationalConstant <= 0 ||
        !std::isfinite(policy.absoluteAccelerationError) || policy.absoluteAccelerationError <= 0 ||
        !std::isfinite(policy.openingRatio) || policy.openingRatio <= 0 || policy.openingRatio >= 0.9) {
        return failed<std::unique_ptr<Impl>>(DiagnosticCode::InvalidArgument, "Invalid source count or field policy");
    }
    try {
        auto impl           = std::make_unique<Impl>(stamp, policy);
        impl->targetMode    = mode;
        impl->sourceCount   = input.size();
        const auto rowCount = input.size() + targets.size();
        Vec        lo = input.front().position, hi = lo;
        double     totalMass = 0.0;
        for (const auto& p : input) {
            if (!finite(p.position) || !std::isfinite(p.mass) || p.mass <= 0)
                return failed<std::unique_ptr<Impl>>(DiagnosticCode::InvalidArgument,
                                                     "Finite positions and positive masses required");
            totalMass += p.mass;
            for (unsigned axis = 0; axis < 3; ++axis) {
                lo[axis] = std::min(lo[axis], p.position[axis]);
                hi[axis] = std::max(hi[axis], p.position[axis]);
            }
        }
        for (const auto& target : targets) {
            if (!finite(target.position) ||
                (target.excludedSource &&
                 (*target.excludedSource >= input.size() || target.position != input[*target.excludedSource].position)))
                return failed<std::unique_ptr<Impl>>(
                    DiagnosticCode::InvalidArgument,
                    "Targets require finite positions and exactly coincident excluded sources");
            for (unsigned axis = 0; axis < 3; ++axis) {
                lo[axis] = std::min(lo[axis], target.position[axis]);
                hi[axis] = std::max(hi[axis], target.position[axis]);
            }
        }
        Vec    center{};
        double half = 0.0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            center[axis] = lo[axis] * 0.5 + hi[axis] * 0.5;
            half = std::max(half, std::max(std::abs(hi[axis] - center[axis]), std::abs(lo[axis] - center[axis])));
        }
        if (half == 0.0) half = 1.0;
        impl->potentialScale    = policy.gravitationalConstant * totalMass / half;
        impl->accelerationScale = impl->potentialScale / half;
        if (!std::isfinite(totalMass) || !std::isfinite(half) || !std::isfinite(impl->potentialScale) ||
            !std::isfinite(impl->accelerationScale) || impl->accelerationScale <= 0 || impl->potentialScale <= 0)
            return failed<std::unique_ptr<Impl>>(DiagnosticCode::Unsupported,
                                                 "Normalize source units before field construction");
        impl->particles.reserve(rowCount);
        impl->originalPositions.reserve(rowCount);
        for (const auto& p : input) {
            Vec position{};
            for (unsigned axis = 0; axis < 3; ++axis) position[axis] = (p.position[axis] - center[axis]) / half;
            const double mass = p.mass / totalMass;
            if (mass == 0.0)
                return failed<std::unique_ptr<Impl>>(DiagnosticCode::Unsupported, "Mass dynamic range underflow");
            impl->particles.push_back({position, mass});
            impl->originalPositions.push_back(p.position);
        }
        impl->excludedSources.reserve(targets.size());
        for (const auto& target : targets) {
            Vec position{};
            for (unsigned axis = 0; axis < 3; ++axis) position[axis] = (target.position[axis] - center[axis]) / half;
            impl->particles.push_back({position, 0});
            impl->originalPositions.push_back(target.position);
            impl->excludedSources.push_back(target.excludedSource);
        }
        impl->order.resize(rowCount);
        impl->scratch.resize(rowCount);
        std::iota(impl->order.begin(), impl->order.end(), std::size_t{0});
        const auto sourceRoot = impl->buildNode({}, 1.0, 0, input.size(), 0);
        if (impl->failureCode != DiagnosticCode::None)
            return failed<std::unique_ptr<Impl>>(impl->failureCode, impl->failureMessage);
        auto targetRoot = sourceRoot;
        if (mode == TargetMode::Independent) {
            targetRoot = targets.empty() ? noNode : impl->buildNode({}, 1.0, input.size(), rowCount, 0, false);
            if (impl->failureCode != DiagnosticCode::None)
                return failed<std::unique_ptr<Impl>>(impl->failureCode, impl->failureMessage);
            impl->sourceRanks.resize(input.size());
            for (std::size_t rank = 0; rank < input.size(); ++rank) impl->sourceRanks[impl->order[rank]] = rank;
        }
        if (targetRoot != noNode) impl->interact(sourceRoot, targetRoot);
        if (impl->failureCode != DiagnosticCode::None) {
            if (impl->failureCode == DiagnosticCode::Failed) {
                const std::string message = std::string(impl->failureMessage) +
                    "; traversal=" + std::to_string(impl->metrics.traversalPairs) +
                    "; far=" + std::to_string(impl->metrics.farInteractions) +
                    "; direct=" + std::to_string(impl->metrics.directPairs) +
                    "; near=" + std::to_string(impl->metrics.nearInteractions) +
                    "; opening_rejected=" + std::to_string(impl->openingRejected) +
                    "; error_rejected=" + std::to_string(impl->errorRejected);
                return Result<std::unique_ptr<Impl>>::failure(Status::failure(
                    StatusCode::Rejected, Diagnostic::error(impl->failureCode, message,
                                                            "physics.field.gravity.budgetExceeded")));
            }
            return failed<std::unique_ptr<Impl>>(impl->failureCode, impl->failureMessage);
        }
        if (targetRoot != noNode && impl->metrics.farInteractions != 0) impl->propagate(targetRoot);
        for (const auto& node : impl->nodes) {
            if (!std::isfinite(node.error))
                return failed<std::unique_ptr<Impl>>(DiagnosticCode::Unsupported, "Error bound overflow");
            for (double v : node.local)
                if (!std::isfinite(v))
                    return failed<std::unique_ptr<Impl>>(DiagnosticCode::Unsupported,
                                                         "Local expansion overflow; normalize coordinates");
        }
        impl->metrics.particles        = input.size();
        impl->metrics.targets          = mode == TargetMode::Sources ? input.size() : targets.size();
        impl->metrics.nodes            = impl->nodes.size();
        impl->metrics.coefficientCount = impl->basis.powers.size();
        return Result<std::unique_ptr<Impl>>::success(std::move(impl));
    } catch (const std::bad_alloc&) {
        return failed<std::unique_ptr<Impl>>(DiagnosticCode::Failed, "Field allocation failed");
    }
}

Result<std::vector<GravitySample>> GravityField::evaluate(FieldStamp expected) const {
    if (!impl_ || impl_->stamp != expected)
        return failed<std::vector<GravitySample>>(DiagnosticCode::StaleHandle, "Field tick or revision is stale");
    const auto& data = *impl_;
    try {
        std::vector<GravitySample> output(data.metrics.targets);
        for (const auto& node : data.nodes) {
            if (!node.leaf || node.local.empty()) continue;
            for (std::size_t k = node.begin; k < node.end; ++k) {
                const auto id = data.order[k];
                if (data.targetMode == TargetMode::Independent && id < data.sourceCount) continue;
                const auto outputId         = data.targetMode == TargetMode::Independent ? id - data.sourceCount : id;
                const auto delta            = subtract(data.particles[id].position, node.center);
                const auto values           = monomials(data.basis, delta);
                auto&      sample           = output[outputId];
                sample.truncationErrorBound = node.error;
                for (std::size_t a = 0; a < data.basis.powers.size(); ++a) {
                    const auto  p     = data.basis.powers[a];
                    const auto& lower = data.basis.kernelSteps[a].lower;
                    sample.potential += node.local[a] * values[a];
                    if (p.x) sample.acceleration[0] -= node.local[a] * p.x * values[lower[0]];
                    if (p.y) sample.acceleration[1] -= node.local[a] * p.y * values[lower[1]];
                    if (p.z) sample.acceleration[2] -= node.local[a] * p.z * values[lower[2]];
                }
            }
        }
        for (const auto& [source, target] : data.near) {
            const auto& s = data.nodes[source];
            const auto& t = data.nodes[target];
            for (std::size_t ti = t.begin; ti < t.end; ++ti) {
                const auto targetId = data.order[ti];
                const auto outputId =
                    data.targetMode == TargetMode::Independent ? targetId - data.sourceCount : targetId;
                auto& sample = output[outputId];
                for (std::size_t si = s.begin; si < s.end; ++si) {
                    const auto sourceId = data.order[si];
                    if (data.targetMode == TargetMode::Sources ? sourceId == targetId
                                                               : data.excludedSources[outputId] == sourceId)
                        continue;
                    const auto&  particle = data.particles[sourceId];
                    const Vec    d        = subtract(particle.position, data.particles[targetId].position);
                    const double r        = norm(d);
                    if (r == 0.0) {
                        if (data.originalPositions[sourceId] != data.originalPositions[targetId])
                            return failed<std::vector<GravitySample>>(
                                DiagnosticCode::Unsupported,
                                "Normalization lost source separation; use a better-conditioned reference frame");
                        return failed<std::vector<GravitySample>>(DiagnosticCode::InvalidArgument,
                                                                  "Coincident distinct point sources are singular");
                    }
                    const double strength = particle.mass / r;
                    sample.potential -= strength;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        sample.acceleration[axis] += (d[axis] / r) * (strength / r);
                }
            }
        }
        for (auto& sample : output) {
            sample.potential *= data.potentialScale;
            for (auto& a : sample.acceleration) a *= data.accelerationScale;
            if (!finite(sample.acceleration) || !std::isfinite(sample.potential))
                return failed<std::vector<GravitySample>>(DiagnosticCode::Unsupported, "Field evaluation overflow");
        }
        return Result<std::vector<GravitySample>>::success(std::move(output));
    } catch (const std::bad_alloc&) {
        return failed<std::vector<GravitySample>>(DiagnosticCode::Failed, "Field output allocation failed");
    }
}

}  // namespace eve::physics::field
