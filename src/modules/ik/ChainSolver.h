#pragma once

#include "ik.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace eve::ik {
namespace detail {

/**
 * Options shared by the chain-scoped FABRIK solve used by Solver2D / Solver3D.
 * These mirror the knobs of ik::solver plus wrapper-level pose blending.
 */
template <unsigned D>
struct ChainOptions {
    float    tolerance = 1e-3f;
    unsigned max_iterations = 16;
    float    force = 0.f;     // 0 = hard IK; (0,1] blends the effector toward the target
    float    influence = 1.f; // 0..1 blend of the solved pose over the input pose

    bool       use_pole = false; // 3D only: pull middle joints toward a pole position
    ::ik::vec3 pole{};
    float      pole_weight = 1.f;
};

namespace {

template <unsigned D>
void placeOnLine(::ik::vec<float, D>& point, const ::ik::vec<float, D>& anchor,
                 float boneLength) {
    ::ik::vec<float, D> dir = point - anchor;
    float               len = ::ik::length(dir);
    if (len <= 1e-8f) {
        dir = ::ik::detail::default_forward<D>();
        len = 1.0f;
    }
    point = anchor + dir * (boneLength / len);
}

// Builds the bone path root_id .. tip_id (both inclusive). Returns an empty
// vector when the ids are out of range or tip is not a descendant of root.
template <unsigned D>
std::vector<::ik::bone<D>*> buildChain(::ik::skeleton<D>& sk, unsigned root_id,
                                       unsigned tip_id) {
    if (sk.bones().empty()) {
        sk.topologicalSort();
    }
    const auto& bones = sk.bones();
    if (root_id >= bones.size() || tip_id >= bones.size()) {
        return {};
    }

    std::vector<::ik::bone<D>*> chain;
    for (::ik::bone<D>* b = bones[tip_id]; b != nullptr; b = b->parent) {
        chain.push_back(b);
        if (b->id == root_id) {
            break;
        }
    }
    if (chain.empty() || chain.back()->id != root_id || chain.size() < 2) {
        return {};
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// One FABRIK reach: backward pass from the target, forward pass pinning the
// chain root. Only bones chain[0..tipIndex] participate.
template <unsigned D>
void reachTarget(::ik::ecs<D>& state, const std::vector<::ik::bone<D>*>& chain,
                 unsigned tipIndex, const ::ik::vec<float, D>& desired) {
    const ::ik::vec<float, D> root_pos = chain[0]->position(state);

    float chain_length = 0.f;
    for (unsigned i = 1; i <= tipIndex; ++i) {
        chain_length += chain[i]->length;
    }

    if (::ik::distance(root_pos, desired) >= chain_length) {
        const ::ik::vec<float, D> dir = ::ik::detail::safe_normalize(desired - root_pos);
        for (unsigned i = 1; i <= tipIndex; ++i) {
            chain[i]->position(state) = chain[i - 1]->position(state) + dir * chain[i]->length;
        }
        return;
    }

    // Backward reaching: place the effector and walk toward the root.
    chain[tipIndex]->position(state) = desired;
    for (int i = static_cast<int>(tipIndex); i >= 1; --i) {
        placeOnLine(chain[static_cast<size_t>(i) - 1]->position(state),
                    chain[static_cast<size_t>(i)]->position(state),
                    chain[static_cast<size_t>(i)]->length);
    }

    // Forward reaching: pin the chain root.
    chain[0]->position(state) = root_pos;
    for (unsigned i = 1; i <= tipIndex; ++i) {
        placeOnLine(chain[i]->position(state), chain[i - 1]->position(state),
                    chain[i]->length);
    }
}

// Pole vector: rotate the radial component (around the root->goal axis) of every
// middle joint toward the pole direction. Preserves each joint's distance from
// the axis, so for two-bone limbs this is the exact bend-direction control.
template <unsigned D>
void applyPole(::ik::ecs<D>& state, const std::vector<::ik::bone<D>*>& chain,
               const ::ik::vec<float, D>& goal, const ::ik::vec3& pole, float weight) {
    if constexpr (D == 3) {
        if (weight <= 0.f || chain.size() < 2) {
            return;
        }
        const ::ik::vec3 root_pos = chain[0]->position(state);
        const ::ik::vec3 to_goal = goal - root_pos;
        const float      goal_len = ::ik::length(to_goal);
        if (goal_len <= 1e-8f) {
            return;
        }
        const ::ik::vec3 axis = to_goal / goal_len;

        const ::ik::vec3 to_pole = pole - root_pos;
        const ::ik::vec3 proj = to_pole - axis * ::ik::dot(to_pole, axis);
        if (::ik::length_squared(proj) <= 1e-8f) {
            return; // pole lies on the root->goal axis: no bend direction to enforce
        }
        const ::ik::vec3 pole_dir = ::ik::detail::safe_normalize(proj);

        for (size_t i = 1; i + 1 < chain.size(); ++i) {
            const ::ik::vec3 v = chain[i]->position(state) - root_pos;
            const float      along = ::ik::dot(v, axis);
            ::ik::vec3       radial = v - axis * along;
            const float      radial_len = ::ik::length(radial);
            if (radial_len <= 1e-8f) {
                continue; // joint is on the axis; there is nothing to bend
            }
            const ::ik::vec3 dir = radial / radial_len;
            const ::ik::vec3 new_dir =
                ::ik::detail::safe_normalize(dir + (pole_dir - dir) * weight);
            chain[i]->position(state) = root_pos + axis * along + new_dir * radial_len;
        }
    }
}

// Reuse the upstream angle-constraint pass for the chain bones (excluding the
// chain root, which must stay pinned at its input position).
template <unsigned D>
void applyChainConstraints(::ik::ecs<D>& state,
                           const std::vector<::ik::bone<D>*>& chain) {
    if (chain.size() < 2) {
        return;
    }
    std::vector<::ik::bone<D>*> rest(chain.begin() + 1, chain.end());
    ::ik::detail::apply_angle_constraints<D>(rest, state);
}

}  // namespace

/**
 * FABRIK solve restricted to the bone chain root_id..tip_id. Unlike the plain
 * solver (which pins the skeleton root), the chain root stays at its input
 * position and bones outside the chain are never touched.
 *
 * Only targets whose bone id lies on the chain participate; the furthest target
 * also acts as the chain goal for the optional pole vector. Returns true when
 * every chain target is within `opt.tolerance` after solving.
 */
template <unsigned D>
bool solveChain(::ik::skeleton<D>& sk, ::ik::ecs<D>& state, unsigned root_id,
                unsigned tip_id, const std::vector<typename ::ik::solver<D>::target>& targets,
                const ChainOptions<D>& opt) {
    std::vector<::ik::bone<D>*> chain = buildChain(sk, root_id, tip_id);
    if (chain.size() < 2) {
        return false;
    }

    struct ChainTarget {
        unsigned                                index = 0;
        const typename ::ik::solver<D>::target *target = nullptr;
    };
    std::vector<ChainTarget> chain_targets;
    for (const auto& t : targets) {
        for (size_t i = 0; i < chain.size(); ++i) {
            if (chain[i]->id == t.bone_id) {
                chain_targets.push_back({static_cast<unsigned>(i), &t});
                break;
            }
        }
    }
    if (chain_targets.empty()) {
        return true;
    }

    // The furthest target drives the pole direction.
    const ChainTarget* goal = &chain_targets.front();
    for (const auto& ct : chain_targets) {
        if (ct.index > goal->index) {
            goal = &ct;
        }
    }

    std::vector<::ik::vec<float, D>> original_pos(chain.size());
    for (size_t i = 0; i < chain.size(); ++i) {
        original_pos[i] = chain[i]->position(state);
    }

    const float pole_weight = std::min(1.f, std::max(0.f, opt.pole_weight));
    const unsigned iterations = opt.max_iterations == 0 ? 1u : opt.max_iterations;
    bool reached = false;
    for (unsigned iter = 0; iter < iterations; ++iter) {
        for (const auto& ct : chain_targets) {
            ::ik::vec<float, D> desired = ct.target->position;
            if (opt.force > 0.f && opt.force < 1.f) {
                const ::ik::vec<float, D> cur = chain[ct.index]->position(state);
                desired = cur + (desired - cur) * (opt.force * ct.target->weight);
            }
            reachTarget(state, chain, ct.index, desired);
        }

        if (opt.use_pole && pole_weight > 0.f) {
            ::ik::vec<float, D> goal_pos = goal->target->position;
            if (opt.force > 0.f && opt.force < 1.f) {
                const ::ik::vec<float, D> cur = chain[goal->index]->position(state);
                goal_pos = cur + (goal_pos - cur) * (opt.force * goal->target->weight);
            }
            applyPole(state, chain, goal_pos, opt.pole, pole_weight);
        }

        applyChainConstraints(state, chain);
        sk.update_rotations(state);

        reached = true;
        for (const auto& ct : chain_targets) {
            if (::ik::distance(chain[ct.index]->position(state), ct.target->position) >
                opt.tolerance) {
                reached = false;
                break;
            }
        }
        if (reached) {
            break;
        }
    }

    if (opt.influence < 1.f) {
        const float t = std::min(1.f, std::max(0.f, opt.influence));
        for (size_t i = 0; i < chain.size(); ++i) {
            const auto &         orig = original_pos[i];
            ::ik::vec<float, D>& pos = chain[i]->position(state);
            pos = orig + (pos - orig) * t;
        }
        sk.update_rotations(state);
    }
    return reached;
}

}  // namespace detail
}  // namespace eve::ik
