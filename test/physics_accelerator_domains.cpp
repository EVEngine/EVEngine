#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "physics/backend/SimulationBackend.h"

#include <array>
#include <memory>
#include <string>

namespace {

using namespace eve::physics;

class CapabilityReset {
public:
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

void noOpStep(void*, const eve::SimulationStep&, const SimulationSettings&) noexcept {}

std::unique_ptr<ISimulationBackend> cpuBackend() {
    static int state = 0;
    return detail::makeCallbackSimulationBackend(&state, &noOpStep, SimulationBackendKind::Cpu,
                                                  SimulationDeterminism::ToleranceBounded);
}

class FailingStepBackend final : public ISimulationBackend {
public:
    eve::Result<void> step(const eve::SimulationStep&, const SimulationSettings&) override {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "injected accelerator dispatch failure", "physics.accelerator.step"));
    }
    [[nodiscard]] SimulationObservation observation() const noexcept override { return {}; }
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Gpu; }
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return SimulationDeterminism::ToleranceBounded;
    }
};

enum class ProviderBehavior { Succeed, SucceedWithWarning, CreateFailure, StepFailure };

class SatelliteProvider final : public IAcceleratorBackendProvider {
public:
    explicit SatelliteProvider(ProviderBehavior behavior) : behavior_(behavior) {}

    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override {
        return domain == SimulationBackendDomain::Cloth2D || domain == SimulationBackendDomain::Cloth3D ||
               domain == SimulationBackendDomain::SurfaceFluid;
    }

    eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain domain, void*) override {
        if (!supports(domain)) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "satellite domain unsupported", "physics.accelerator.domain"));
        }
        if (behavior_ == ProviderBehavior::CreateFailure) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "injected accelerator creation failure", "physics.accelerator.create"));
        }
        if (behavior_ == ProviderBehavior::StepFailure) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::success(std::make_unique<FailingStepBackend>());
        }
        if (behavior_ == ProviderBehavior::SucceedWithWarning) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
                detail::makeMockAcceleratorBackend(),
                eve::Status(eve::StatusCode::Applied,
                            {eve::Diagnostic::warning(eve::DiagnosticCode::Unsupported,
                                                      "accelerator selected with reduced precision",
                                                      "physics.accelerator.registry.precision")}));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(detail::makeMockAcceleratorBackend());
    }

private:
    ProviderBehavior behavior_;
};

class DomainProvider final : public IAcceleratorBackendProvider {
public:
    DomainProvider(SimulationBackendDomain domain, ProviderBehavior behavior) : domain_(domain), behavior_(behavior) {}

    [[nodiscard]] bool supports(SimulationBackendDomain domain) const noexcept override { return domain == domain_; }

    eve::Result<std::unique_ptr<ISimulationBackend>> create(SimulationBackendDomain, void*) override {
        ++createCount;
        if (behavior_ == ProviderBehavior::CreateFailure) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "domain provider creation failed", "physics.accelerator.registry.create"));
        }
        if (behavior_ == ProviderBehavior::SucceedWithWarning) {
            return eve::Result<std::unique_ptr<ISimulationBackend>>::success(
                detail::makeMockAcceleratorBackend(),
                eve::Status(eve::StatusCode::Applied,
                            {eve::Diagnostic::warning(eve::DiagnosticCode::Unsupported,
                                                      "accelerator selected with reduced precision",
                                                      "physics.accelerator.registry.precision")}));
        }
        return eve::Result<std::unique_ptr<ISimulationBackend>>::success(detail::makeMockAcceleratorBackend());
    }

    int createCount = 0;

private:
    SimulationBackendDomain domain_;
    ProviderBehavior        behavior_;
};

constexpr std::array kSatelliteDomains = {SimulationBackendDomain::Cloth2D, SimulationBackendDomain::Cloth3D,
                                          SimulationBackendDomain::SurfaceFluid};

std::string diagnosticDetail(const eve::Diagnostic& diagnostic, const std::string& key) {
    for (const auto& [name, value] : diagnostic.details()) {
        if (name == key) return value;
    }
    return {};
}

}  // namespace

TEST_CASE("physics.accelerator.satellitesReportAbsentProviderPerDomain") {
    CapabilityReset reset;
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        REQUIRE_EQ(selected.diagnostics().size(), std::size_t{1});
        CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "domain"),
                 std::string(simulationBackendDomainName(domain)));
        auto value = std::move(selected).takeValue();
        CHECK(value.usedFallback);
        CHECK_EQ(value.actualKind, SimulationBackendKind::Cpu);
    }
}

TEST_CASE("physics.accelerator.satellitesSelectPresentProvider") {
    CapabilityReset reset;
    SatelliteProvider provider(ProviderBehavior::Succeed);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        CHECK(selected.diagnostics().empty());
        auto value = std::move(selected).takeValue();
        CHECK(!value.usedFallback);
        CHECK_EQ(value.actualKind, SimulationBackendKind::MockAccelerator);
    }
}

TEST_CASE("physics.accelerator.satellitesPreserveCreateFailure") {
    CapabilityReset reset;
    SatelliteProvider provider(ProviderBehavior::CreateFailure);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        REQUIRE_EQ(selected.diagnostics().size(), std::size_t{2});
        CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "domain"),
                 std::string(simulationBackendDomainName(domain)));
        CHECK_EQ(selected.diagnostics()[1].message(), std::string("injected accelerator creation failure"));
        CHECK(std::move(selected).takeValue().usedFallback);
    }
}

TEST_CASE("physics.accelerator.satellitesStepFailureDoesNotAdvanceObservation") {
    CapabilityReset reset;
    SatelliteProvider provider(ProviderBehavior::StepFailure);
    eve::cap::provide<IAcceleratorBackendProvider>(&provider);
    for (const auto domain : kSatelliteDomains) {
        auto selected = detail::selectSimulationBackend(domain, cpuBackend(), nullptr, true);
        REQUIRE(selected.ok());
        auto value = std::move(selected).takeValue();
        auto result = value.backend->step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)},
                                          SimulationSettings{});
        CHECK(!result.ok());
        CHECK_EQ(result.error()->path(), std::string("physics.accelerator.step"));
        CHECK_EQ(value.backend->observation().stepCount, std::uint64_t{0});
        CHECK(value.backend->observation().lastTick.isZero());
    }
}

TEST_CASE("physics.accelerator.registryComposesIndependentClothAndFluidProviders") {
    CapabilityReset reset;
    DomainProvider clothProvider(SimulationBackendDomain::Cloth2D, ProviderBehavior::Succeed);
    DomainProvider fluidProvider(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::Succeed);
    auto clothRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(clothProvider, 10);
    auto fluidRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(fluidProvider, 20);
    REQUIRE(clothRegistrationResult.ok());
    REQUIRE(fluidRegistrationResult.ok());
    auto clothRegistration = std::move(clothRegistrationResult).takeValue();
    auto fluidRegistration = std::move(fluidRegistrationResult).takeValue();

    auto cloth = detail::selectSimulationBackend(SimulationBackendDomain::Cloth2D, cpuBackend(), nullptr, true);
    REQUIRE(cloth.ok());
    CHECK(!std::move(cloth).takeValue().usedFallback);
    auto fluid = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(fluid.ok());
    CHECK(!std::move(fluid).takeValue().usedFallback);
    CHECK_EQ(clothProvider.createCount, 1);
    CHECK_EQ(fluidProvider.createCount, 1);
}

TEST_CASE("physics.accelerator.registryRemovalIsProviderLocal") {
    CapabilityReset reset;
    DomainProvider clothProvider(SimulationBackendDomain::Cloth3D, ProviderBehavior::Succeed);
    DomainProvider fluidProvider(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::Succeed);
    auto clothRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(clothProvider);
    auto fluidRegistrationResult = AcceleratorBackendProviderRegistration::registerProvider(fluidProvider);
    REQUIRE(clothRegistrationResult.ok());
    REQUIRE(fluidRegistrationResult.ok());
    auto clothRegistration = std::move(clothRegistrationResult).takeValue();
    auto fluidRegistration = std::move(fluidRegistrationResult).takeValue();

    clothRegistration.reset();
    auto cloth = detail::selectSimulationBackend(SimulationBackendDomain::Cloth3D, cpuBackend(), nullptr, true);
    REQUIRE(cloth.ok());
    CHECK(std::move(cloth).takeValue().usedFallback);
    auto fluid = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(fluid.ok());
    CHECK(!std::move(fluid).takeValue().usedFallback);
}

TEST_CASE("physics.accelerator.registryTriesNextSupportingProviderAfterFailure") {
    CapabilityReset reset;
    DomainProvider failing(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::CreateFailure);
    DomainProvider succeeding(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::Succeed);
    auto firstResult = AcceleratorBackendProviderRegistration::registerProvider(failing, 0);
    auto secondResult = AcceleratorBackendProviderRegistration::registerProvider(succeeding, 10);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    auto first = std::move(firstResult).takeValue();
    auto second = std::move(secondResult).takeValue();

    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t{1});
    CHECK_EQ(selected.diagnostics().front().message(), std::string("domain provider creation failed"));
    CHECK(selected.diagnostics().front().severity() == eve::Severity::Warning);
    CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "providerAttempt"), std::string("1"));
    CHECK(!std::move(selected).takeValue().usedFallback);
    CHECK_EQ(failing.createCount, 1);
    CHECK_EQ(succeeding.createCount, 1);
}

TEST_CASE("physics.accelerator.registryRejectsDuplicateLiveRegistration") {
    CapabilityReset reset;
    DomainProvider provider(SimulationBackendDomain::Cloth2D, ProviderBehavior::Succeed);
    auto firstResult = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(firstResult.ok());
    auto first = std::move(firstResult).takeValue();

    auto duplicate = AcceleratorBackendProviderRegistration::registerProvider(provider);
    CHECK(!duplicate.ok());
    REQUIRE(duplicate.error());
    CHECK_EQ(duplicate.error()->code(), eve::DiagnosticCode::Conflict);

    first.reset();
    auto replacement = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(replacement.ok());
    std::move(replacement).takeValue();
}

TEST_CASE("physics.accelerator.registryPreservesSuccessfulProviderDiagnostics") {
    CapabilityReset reset;
    DomainProvider provider(SimulationBackendDomain::SurfaceFluid, ProviderBehavior::SucceedWithWarning);
    auto registrationResult = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(registrationResult.ok());
    auto registration = std::move(registrationResult).takeValue();

    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::SurfaceFluid, cpuBackend(), nullptr, true);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.diagnostics().size(), std::size_t{1});
    CHECK_EQ(selected.diagnostics().front().message(), std::string("accelerator selected with reduced precision"));
    CHECK_EQ(diagnosticDetail(selected.diagnostics().front(), "providerAttempt"), std::string("1"));
    CHECK(!std::move(selected).takeValue().usedFallback);
}

TEST_CASE("physics.accelerator.registryPreservesLegacySingleProviderCompatibility") {
    CapabilityReset reset;
    DomainProvider legacy(SimulationBackendDomain::Cloth2D, ProviderBehavior::Succeed);
    eve::cap::provide<IAcceleratorBackendProvider>(&legacy);

    auto selected = detail::selectSimulationBackend(SimulationBackendDomain::Cloth2D, cpuBackend(), nullptr, true);
    REQUIRE(selected.ok());
    CHECK(!std::move(selected).takeValue().usedFallback);
    CHECK_EQ(legacy.createCount, 1);
}
