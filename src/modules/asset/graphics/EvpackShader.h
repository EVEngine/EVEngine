#pragma once
#include <array>
#include <memory>
#include <span>
#include "common/BorrowedRef.h"
#include "common/ResourceRef.h"

namespace eve::asset {
class EvpackResourceReader;
struct EvpackCapabilities;
struct ShaderAsset;
}  // namespace eve::asset
namespace eve::graphics {
class Graphics;
class Mesh;
class Texture;
}  // namespace eve::graphics
namespace eve::asset_graphics {
/**
 * @brief Unique lease on a validated package shader and its independent parameter instance.
 * @ownership Graphics owns the allocation and must outlive this lease; destroy/release leases before backend shutdown.
 * @lifetime No resource pointer escapes. Draw inputs are borrowed only during the call. Reload builds a new lease.
 * @thread Load, update, draw, release and destruction are graphics-thread affine, outside callbacks/reentrancy.
 */
class EvpackShader final {
public:
    ~EvpackShader();
    EvpackShader(const EvpackShader&)            = delete;
    EvpackShader& operator=(const EvpackShader&) = delete;
    /** @brief Validate then upload; failures preserve all existing instances. Reader is not retained. */
    [[nodiscard]] static Result<std::unique_ptr<EvpackShader>> load(const asset::EvpackResourceReader& reader,
                                                                    const AssetRef&                    asset,
                                                                    const asset::EvpackCapabilities&   capabilities,
                                                                    graphics::Graphics&                graphics);
    /** @brief Immutable CPU metadata borrowed until this lease is destroyed. */
    [[nodiscard]] const asset::ShaderAsset& definition() const noexcept;
    /** @brief Set one declared scalar/vector with exact finite component count; failure leaves it unchanged. */
    [[nodiscard]] Result<void> setParameter(std::string_view name, std::span<const float> values);
    /** @brief Draw through the owning Graphics in an open 3D frame; all arguments are immediate borrowed inputs. */
    [[nodiscard]] Result<void> drawMesh(graphics::Mesh& mesh, const std::array<float, 16>& transform,
                                        OptionalRef<graphics::Texture> texture, const std::array<float, 4>& tint);
    /** @brief Draw a sprite in the active canvas; rect is x/y/width/height in pixels. */
    [[nodiscard]] Result<void> drawSprite(OptionalRef<graphics::Texture> texture, const std::array<float, 4>& rect,
                                          const std::array<float, 4>& tint);
    /** @brief Release explicitly; failure retains the lease for retry. Destruction retries and reports errors. */
    [[nodiscard]] Result<void> release();

private:
    struct Impl;
    explicit EvpackShader(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_graphics
