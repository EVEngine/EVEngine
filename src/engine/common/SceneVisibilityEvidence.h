#pragma once
#include "common/Value.h"

namespace eve {
/**
 * @brief Optional native authoring visibility provider; no borrowed resource survives a call.
 * @thread Engine owner thread, explicit editing calls only; no callbacks or locks.
 * @lifetime Provider is bound by Graphics startup and revoked on matching teardown.
 */
class ISceneVisibilityEvidence {
public:
    static constexpr const char* capabilityName = "ISceneVisibilityEvidence";
    /** @brief Destroy the provider after revocation. */
    virtual ~ISceneVisibilityEvidence() = default;
    /**
     * @brief Validate a complete named projection and return owning generation proof.
     * @cost Linear in projected nodes and draws per preview preparation; no GPU work.
     */
    [[nodiscard]] virtual Result<Value> inspect(const std::string& host, const Value& request) = 0;
    /**
     * @brief Revalidate proof and measure the supplied candidate into private targets.
     * @cost Blocking GPU raster/readback per scene and target, amortized per explicit candidate check.
     * @return Owning evidence; stale geometry/projection rejects before rasterization.
     */
    [[nodiscard]] virtual Result<Value> measure(const std::string& host, const Value& request, const Value& proof) = 0;
};
}  // namespace eve
