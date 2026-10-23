#include "procgen/ProcgenScriptSupport.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
struct NativeProxyProbe {};
}  // namespace

TEST_CASE("procgen.script.nativeProxyAddressReuseInvalidatesOldInstance") {
    using namespace eve::procgen;
    NativeProxyProbe storage;
    int              oldReleases = 0, freshReleases = 0;
    ssq::VM          vm(2048, ssq::Libs::ALL);
    vm.addClass("NativeProxyProbe", ssq::Class::Ctor<NativeProxyProbe()>());
    const ProcgenMeshBuildHandleRef oldRef{eve::RuntimeHandle<ProcgenMeshBuildHandleTag>(0, 1), 1};
    const ProcgenMeshBuildHandleRef freshRef{eve::RuntimeHandle<ProcgenMeshBuildHandleTag>(0, 2), 1};
    auto resolve = [&](ProcgenMeshBuildHandleRef) { return eve::script::Borrowed<NativeProxyProbe>(storage); };
    {
        auto old         = makeOwnedNativeProxy<NativeProxyProbe>(vm.getHandle(),
                                                                  eve::Result<ProcgenMeshBuildHandleRef>::success(oldRef),
                                                                  resolve, [&](ProcgenMeshBuildHandleRef) {
                                                              ++oldReleases;
                                                              return eve::Result<void>::success();
                                                          });
        auto oldInstance = old.get<ssq::Instance>("value");
        REQUIRE(oldInstance.toPtrUnsafe<NativeProxyProbe*>() == &storage);
        auto fresh = makeOwnedNativeProxy<NativeProxyProbe>(vm.getHandle(),
                                                            eve::Result<ProcgenMeshBuildHandleRef>::success(freshRef),
                                                            resolve, [&](ProcgenMeshBuildHandleRef) {
                                                                ++freshReleases;
                                                                return eve::Result<void>::success();
                                                            });
        REQUIRE(oldInstance.toPtrUnsafe<NativeProxyProbe*>() == nullptr);
        REQUIRE(fresh.get<ssq::Instance>("value").toPtrUnsafe<NativeProxyProbe*>() == &storage);
        const auto current = nativeProxyReference<ProcgenMeshBuildHandleRef>(&storage);
        REQUIRE(current.has_value());
        REQUIRE(current->packed() == freshRef.packed());
        vm.set("freshProxy", fresh);
    }
    // Destroying the stale proxy must not unregister or release the new generation.
    REQUIRE(oldReleases == 0);
    REQUIRE(freshReleases == 0);
    REQUIRE(nativeProxyReference<ProcgenMeshBuildHandleRef>(&storage).has_value());
    vm.run(vm.compileSource("freshProxy=null; collectgarbage();"));
    REQUIRE(freshReleases == 1);
    REQUIRE(!nativeProxyReference<ProcgenMeshBuildHandleRef>(&storage).has_value());
}
