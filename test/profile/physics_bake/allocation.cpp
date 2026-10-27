// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "probe",
  "default": "module-composition",
  "module": "physics_backend",
  "task": "physics-bake"
}
*/
// clang-format on

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include "physics/backend/bake/collapse/CollapseCaptureSource.h"
#include "physics/backend/bake/collapse/CollapsePlayback.h"

namespace {
bool         failNextAllocation = false;
class Source final : public eve::physics::CollapseCaptureSource {
public:
    int                                                   boneCount() const noexcept override { return 1; }
    std::shared_ptr<const eve::physics::FractureGeometry> fragmentGeometry() const noexcept override { return {}; }
    eve::Result<eve::physics::BonePresentation>           bonePresentation(int) const override {
        return eve::Result<eve::physics::BonePresentation>::success({});
    }
};
void check(bool value) {
    if (!value) throw std::runtime_error("allocation contract failed");
}
}  // namespace
// Isolated executable: fail one asset-sized allocation, allowing diagnostics to allocate.
// This models a recoverable admission failure, not sustained system-wide exhaustion.
void* operator new(std::size_t size) {
    // Debug STL moves can allocate small iterator proxies before prepare's body.
    // The shared asset/control block must contain the complete CollapseAnimation.
    if (failNextAllocation && size >= sizeof(eve::physics::CollapseAnimation)) {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
int  main() {
    try {
        Source                          source;
        eve::physics::CollapseAnimation clip;
        clip.capture(source, 0).expect("capture");
        clip.capture(source, 1).expect("capture");
        auto existing = eve::physics::CollapsePlayback::prepare(clip).expect("existing player");
        existing.seek(.5).expect("existing time");
        auto candidate     = clip;
        failNextAllocation = true;
        // Model an iterator-proxy allocation during argument movement.
        void* proxy = ::operator new(1);
        ::operator delete(proxy);
        check(failNextAllocation);
        auto rejected = eve::physics::CollapsePlayback::prepare(std::move(candidate));
        check(!failNextAllocation);
        check(!rejected.ok());
        check(rejected.status().code() == eve::StatusCode::Failed);
        check(existing.time() == .5);
        check(existing.sample(0).ok());
        check(clip.frameCount() == 2);
        auto retried = eve::physics::CollapsePlayback::prepare(clip);
        check(retried.ok());
        check(retried.value().time() == 0);
        std::cout << "allocation failure returned checked diagnostic; existing playback and retry passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
