// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"tests","default":"module-composition","module":"graphics"}
*/
// clang-format on
#include <array>
#include "ShaderBindingContractSpv.h"
#include "graphics/vulkan/ShaderBindingValidation.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.resources.bindingPlansValidateStagesAndPreserveReloadAbi") {
    using namespace eve::graphics;
    using namespace eve::graphics::vulkan;
    using namespace shader_binding_fixture;
    std::array<std::byte, 16>       constants{};
    std::array<ShaderImageInput, 2> images;
    images[0].binding = 3;
    images[1].binding = 7;
    ShaderResourceInputs inputs{images, constants, {}};
    auto                 initial = prepareMeshResourceBindings(vertex, fragment, inputs, {});
#ifdef EVE_TEST_RESOURCE_VALIDATION
    REQUIRE(initial.ok());
    const auto& plan = initial.value();
    CHECK_EQ(plan.shader.bindings().size(), 2u);
    CHECK_EQ(plan.writes.bindings().size(), 3u);
    CHECK_EQ(plan.layout.size(), 3u);
    CHECK(plan.writes.validateLayout(1, plan.layout));
    auto conflict = prepareMeshResourceBindings(vertex, integerFragment, inputs, {});
    REQUIRE(!conflict.ok());
    REQUIRE(!conflict.diagnostics().empty());
    CHECK(conflict.diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
    CHECK_EQ(conflict.diagnostics()[0].path(), "shader.resources.set[1].binding[32]");
    CHECK_EQ(conflict.diagnostics()[0].details()[0].second, "1");
    CHECK_EQ(conflict.diagnostics()[0].details()[1].second, "32");
    auto incompatible = prepareMeshResourceBindings({}, integerFragment, inputs, plan.shader);
    REQUIRE(!incompatible.ok());
    CHECK(incompatible.diagnostics()[0].code() == eve::DiagnosticCode::Conflict);
    CHECK_EQ(plan.shader.bindings().size(), 2u);
    auto dropped = prepareMeshResourceBindings({}, imageOnlyFragment, inputs, plan.shader);
    REQUIRE(dropped.ok());
    auto reintroduced = prepareMeshResourceBindings({}, integerFragment, inputs, dropped.value().shader);
    REQUIRE(!reintroduced.ok());
    auto unused = prepareMeshResourceBindings({}, imageOnlyFragment, inputs, {});
    REQUIRE(unused.ok());
    auto activated = prepareMeshResourceBindings({}, fragment, inputs, unused.value().shader);
    REQUIRE(activated.ok());
    auto changedAfterActivation = prepareMeshResourceBindings({}, integerFragment, inputs, activated.value().shader);
    CHECK(!changedAfterActivation.ok());
    // Duplicate unused bindings must fail before creating a Vulkan layout.
    images[1].binding = 3;
    CHECK(!prepareMeshResourceBindings({}, imageOnlyFragment, inputs, {}).ok());
#else
    REQUIRE(!initial.ok());
    CHECK(initial.status().code() == eve::StatusCode::Unsupported);
    CHECK(initial.status().describe().find("SPIR-V validation provider is unavailable") != std::string::npos);
#endif
}
