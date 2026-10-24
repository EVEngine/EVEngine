#include "common/Runtime.h"
#include "fixtures/PluginReloadChecks.h"
#include "plugins/Plugins.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
TEST_CASE("plugins.reload.replacesCodeAndPreservesCapturedGenerations") {
    eve::Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    REQUIRE((plugin_reload_checks::replacement(runtime.vm()), true));
}

TEST_CASE("plugins.reload.failedCandidatesPreserveOldBindings") {
    eve::Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    REQUIRE((plugin_reload_checks::failedReplacement(runtime.vm()), true));
}

TEST_CASE("plugins.reload.repeatedReplacementUsesFreshInstances") {
    eve::Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    REQUIRE((plugin_reload_checks::repeatedReplacement(runtime.vm()), true));
}

TEST_CASE("plugins.load.failedCandidateCanBeRetried") {
    eve::Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    REQUIRE((plugin_reload_checks::failedInitialLoad(runtime.vm()), true));
}
#endif
