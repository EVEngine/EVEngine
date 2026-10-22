#include "common/Assert.h"
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
float Crowd::Impl::resolveOverlapsPass() {
    ++workReport.contactPasses;
    const int   n         = int(actions.size());
    const float maxRadius = radii.empty() ? 0.f : *std::max_element(radii.begin(), radii.end());
    const auto  spend     = [&](std::int64_t amount = 1) {
        const auto limit = budgetActive ? activeBudget.maxPairVisits : std::numeric_limits<std::int64_t>::max();
        if (amount > limit - workReport.pairVisits) throw WorkLimit{"Crowd pair construction budget exhausted"};
        workReport.pairVisits += amount;
    };
    const auto maxPairs = budgetActive ? activeBudget.maxContactPairs : std::int64_t(1000000);
    spend(std::int64_t(n) * 3);
    correctionXs.assign(size_t(n), 0.f);
    correctionYs.assign(size_t(n), 0.f);
    pairCounts.assign(size_t(n), 0);
    std::size_t total = 0;
    for (int i = 0; i < n; ++i) {
        spend();
        forEachNeighbor(xs[size_t(i)], ys[size_t(i)], radii[size_t(i)] + maxRadius, [&](int j) {
            if (j <= i || !canContact(size_t(i), size_t(j))) return;
            spend();
            if (total >= static_cast<std::uint64_t>(maxPairs)) throw WorkLimit{"Crowd contact pair capacity exhausted"};
            ++pairCounts[size_t(i)];
            ++total;
        });
    }
    spend(std::int64_t(n) + 1);
    pairStarts.resize(size_t(n) + 1);
    std::size_t prefix = 0;
    for (int i = 0; i < n; ++i) {
        spend();
        pairStarts[size_t(i)] = prefix;
        prefix += pairCounts[size_t(i)];
    }
    pairStarts[size_t(n)] = prefix;
    spend(static_cast<std::int64_t>(total));
    contactPairs.resize(total);
    workReport.contactPairsPeak = std::max(workReport.contactPairsPeak, static_cast<std::int64_t>(total));
    for (int i = 0; i < n; ++i) {
        std::size_t cursor = pairStarts[size_t(i)];
        forEachNeighbor(xs[size_t(i)], ys[size_t(i)], radii[size_t(i)] + maxRadius, [&](int j) {
            if (j <= i || !canContact(size_t(i), size_t(j))) return;
            spend();
            EV_ASSERT(cursor < pairStarts[size_t(i) + 1], "Contact snapshot changed during pair fill");
            contactPairs[cursor++] = tieKeys[size_t(i)] < tieKeys[size_t(j)] ? ContactPair{i, j} : ContactPair{j, i};
        });
        EV_ASSERT(cursor == pairStarts[size_t(i) + 1], "Contact pair count/fill mismatch");
    }
    std::sort(contactPairs.begin(), contactPairs.end(), [&](const ContactPair& a, const ContactPair& b) {
        spend();
        if (tieKeys[size_t(a.left)] != tieKeys[size_t(b.left)])
            return tieKeys[size_t(a.left)] < tieKeys[size_t(b.left)];
        return tieKeys[size_t(a.right)] < tieKeys[size_t(b.right)];
    });
    // Reserve all bounded scratch work before allocation. Each pair has exactly two incidents.
    spend(std::int64_t(n) * 3 + 1 + static_cast<std::int64_t>(total) * 6);
    pairCounts.assign(size_t(n), 0);
    for (const auto pair : contactPairs) {
        ++pairCounts[size_t(pair.left)];
        ++pairCounts[size_t(pair.right)];
    }
    incidentStarts.resize(size_t(n) + 1);
    prefix = 0;
    for (int i = 0; i < n; ++i) {
        incidentStarts[size_t(i)] = prefix;
        prefix += pairCounts[size_t(i)];
        pairCounts[size_t(i)] = incidentStarts[size_t(i)];
    }
    incidentStarts[size_t(n)] = prefix;
    incidentPairs.resize(total * 2);
    for (std::size_t k = 0; k < total; ++k) {
        const auto pair                                 = contactPairs[k];
        incidentPairs[pairCounts[size_t(pair.left)]++]  = k;
        incidentPairs[pairCounts[size_t(pair.right)]++] = k;
    }
    if (budgetActive &&
        static_cast<std::int64_t>(total) > activeBudget.maxContactChecks - workReport.contactPairsChecked)
        throw WorkLimit{"Crowd contact check budget exhausted"};
    workReport.contactPairsChecked += static_cast<std::int64_t>(total);
    contactDeltas.assign(total, ContactDelta{});
    dispatchWork(total, [&](std::size_t first, std::size_t last) {
        for (std::size_t k = first; k < last; ++k) {
            const auto  pair  = contactPairs[k];
            auto&       delta = contactDeltas[k];
            const int   i = pair.left, j = pair.right;
            float       dx   = xs[size_t(j)] - xs[size_t(i)];
            float       dy   = ys[size_t(j)] - ys[size_t(i)];
            float       d2   = dx * dx + dy * dy;
            const float minD = radii[size_t(i)] + radii[size_t(j)];
            if (d2 >= minD * minD) continue;
            const bool exactOverlap = d2 <= 1e-9f;
            if (exactOverlap) {
                const auto          left = tieKeys[size_t(i)], right = tieKeys[size_t(j)];
                const auto          first = std::min(left, right), second = std::max(left, right);
                const std::uint64_t hash =
                    first.value ^ (second.value + 0x9e3779b97f4a7c15ULL + (first.value << 6) + (first.value >> 2)) ^
                    (std::uint64_t(first.anonymous) << 62) ^ (std::uint64_t(second.anonymous) << 63);
                const float angle = static_cast<float>(hash % 104729ULL) / 104729.0f * 6.28318530717958647692f;
                dx                = std::cos(angle);
                dy                = std::sin(angle);
                if (left > right) {
                    dx = -dx;
                    dy = -dy;
                }
                d2 = 1.0f;
            }
            const float d             = std::sqrt(d2);
            const float push          = minD - (exactOverlap ? 0.0f : d);
            delta.penetration         = push;
            const float nx            = dx / d;
            const float ny            = dy / d;
            float       leftShare     = 0.5f;
            float       rightShare    = 0.5f;
            const auto& leftPolicy    = interactions[size_t(i)];
            const auto& rightPolicy   = interactions[size_t(j)];
            const float leftMobility  = leftPolicy.holdPosition ? 0.f : leftPolicy.pushability;
            const float rightMobility = rightPolicy.holdPosition ? 0.f : rightPolicy.pushability;
            if (leftMobility + rightMobility == 0.f) continue;
            leftShare  = leftMobility / (leftMobility + rightMobility);
            rightShare = 1.f - leftShare;
            // Preserve the existing priority preference only when both can yield.
            // Priority never overrides an explicit immovable/hold policy.
            if (leftMobility > 0.f && rightMobility > 0.f &&
                avoidancePriorities[size_t(i)] > avoidancePriorities[size_t(j)]) {
                leftShare  = 0.0f;
                rightShare = 1.0f;
            } else if (leftMobility > 0.f && rightMobility > 0.f &&
                       avoidancePriorities[size_t(j)] > avoidancePriorities[size_t(i)]) {
                leftShare  = 1.0f;
                rightShare = 0.0f;
            }
            delta.leftX  = nx * push * leftShare;
            delta.leftY  = ny * push * leftShare;
            delta.rightX = nx * push * rightShare;
            delta.rightY = ny * push * rightShare;
        }
    });
    float maxPenetration = 0.f;
    for (const auto& delta : contactDeltas) maxPenetration = std::max(maxPenetration, delta.penetration);
    spend(static_cast<std::int64_t>(total) * 3 + n);
    dispatchWork(size_t(n), [&](std::size_t first, std::size_t last) {
        for (int i = int(first); i < int(last); ++i) {
            // Incident indices retain canonical pair order, independent of worker completion order.
            for (std::size_t cursor = incidentStarts[size_t(i)]; cursor < incidentStarts[size_t(i) + 1]; ++cursor) {
                const auto  k     = incidentPairs[cursor];
                const auto& delta = contactDeltas[k];
                if (contactPairs[k].left == i) {
                    correctionXs[size_t(i)] -= delta.leftX;
                    correctionYs[size_t(i)] -= delta.leftY;
                } else {
                    correctionXs[size_t(i)] += delta.rightX;
                    correctionYs[size_t(i)] += delta.rightY;
                }
            }
            // Bound dense-cluster corrections so a single pass cannot eject an agent.
            const float length = std::hypot(correctionXs[size_t(i)], correctionYs[size_t(i)]);
            const float limit  = std::max(radii[size_t(i)], 1e-4f);
            const float scale  = length > limit ? limit / length : 1.f;
            xs[size_t(i)] += correctionXs[size_t(i)] * scale;
            ys[size_t(i)] += correctionYs[size_t(i)] * scale;
        }
    });
    return maxPenetration;
}


}  // namespace eve::crowd
