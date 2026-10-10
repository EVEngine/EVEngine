"""Static contracts for release-blocking Android and iOS SDK validation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "sdk-release.yml"
SDK_TEST = ROOT / ".github" / "scripts" / "test-sdk.sh"
APK_RUNNER = ROOT / ".github" / "scripts" / "run-consumer-apks.sh"
CONSUMER_TEST = ROOT / ".github" / "scripts" / "consumer-test.sh"
SDK_INSTALL = ROOT / "cmake" / "install_sdk.cmake"
THIRD_PARTY_BUILD = ROOT / "cmake" / "third_party_build.cmake"
ANDROID_GRADLE = ROOT / "platform" / "android" / "apk" / "app" / "build.gradle.kts"
VULKAN_GRAPHICS = ROOT / "src" / "modules" / "graphics" / "vulkan" / "Graphics.cpp"
SCRIPT_RUNTIME = ROOT / "src" / "engine" / "common" / "Runtime.cpp"


def job_block(workflow: str, job: str, next_job: str | None = None) -> str:
    start = workflow.index(f"  {job}:\n")
    if next_job is None:
        return workflow[start:]
    end = workflow.index(f"  {next_job}:\n", start)
    return workflow[start:end]


class MobileSdkReleaseGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.sdk_test = SDK_TEST.read_text(encoding="utf-8")
        cls.apk_runner = APK_RUNNER.read_text(encoding="utf-8")
        cls.consumer_test = CONSUMER_TEST.read_text(encoding="utf-8")
        cls.sdk_install = SDK_INSTALL.read_text(encoding="utf-8")
        cls.third_party_build = THIRD_PARTY_BUILD.read_text(encoding="utf-8")
        cls.android_gradle = ANDROID_GRADLE.read_text(encoding="utf-8")
        cls.vulkan_graphics = VULKAN_GRAPHICS.read_text(encoding="utf-8")
        cls.script_runtime = SCRIPT_RUNTIME.read_text(encoding="utf-8")

    def test_android_emulator_is_a_hard_gate_for_every_consumer_host(self) -> None:
        block = job_block(self.workflow, "run-android", "ios-sim")
        self.assertNotIn("continue-on-error:", block)
        self.assertIn("needs: [consumer-win32, consumer-linux, consumer-macosx]", block)
        self.assertIn("pattern: android-apk-*", block)
        self.assertIn("merge-multiple: true", block)
        self.assertIn("runs-on: ubuntu-latest", block)
        self.assertIn("api-level: 35", block)
        self.assertIn("arch: x86_64", block)
        self.assertIn("emulator-boot-timeout: 1800", block)
        self.assertIn("-gpu swiftshader", block)
        self.assertIn("-accel on", block)
        self.assertIn("disable-linux-hw-accel: false", block)
        self.assertIn("Enable KVM access", block)
        self.assertIn("actions/checkout@", block)
        self.assertIn("bash .github/scripts/run-consumer-apks.sh apk", block)
        self.assertIn('expected_abi="${4:-arm64-v8a}"', self.apk_runner)
        self.assertIn('^lib/${expected_abi}/', self.apk_runner)
        self.assertIn("^lib/(x86|x86_64)/", self.apk_runner)
        self.assertIn("EVE_CI_GAME_OK", self.apk_runner)
        self.assertIn('expected_count="${2:-3}"', self.apk_runner)
        self.assertIn('"$apksigner" sign', self.apk_runner)
        self.assertIn('adb install -r "$signed_apk"', self.apk_runner)
        self.assertEqual(3, self.workflow.count("name: android-apk-${{ env.HOST }}"))

    def test_ios_artifact_and_simulator_are_hard_gates(self) -> None:
        ios_build = job_block(self.workflow, "ios", "linux")
        self.assertIn("test-sdk.sh dist/eve-sdk/ios ios", ios_build)
        simulator = job_block(self.workflow, "ios-sim")
        self.assertNotIn("continue-on-error:", simulator)
        self.assertIn("make build/ios-sim-debug", simulator)
        self.assertIn("EVE_CI_GAME_OK", simulator)

    def test_mobile_sdk_checks_do_not_skip_when_prerequisites_are_missing(self) -> None:
        self.assertNotIn("SKIP: android APK smoke needs", self.sdk_test)
        self.assertIn('fail "android APK smoke needs', self.sdk_test)
        self.assertIn('if [ "$PLAT" = "ios" ]', self.sdk_test)
        self.assertIn("lipo -archs", self.sdk_test)

    def test_android_sdk_requires_sdl_runtime_at_install_and_consume_time(self) -> None:
        self.assertIn('foreach(_eve_required_lib IN ITEMS libSDL2.so)', self.sdk_install)
        self.assertIn('message(FATAL_ERROR', self.sdk_install)
        self.assertIn('missing lib/$lib', self.sdk_test)
        self.assertIn('bash gradlew :app:assembleDebug', self.sdk_test)
        self.assertIn('lib/arm64-v8a/libSDL2.so', self.consumer_test)

    def test_x86_android_build_disables_arm_neon_probe(self) -> None:
        self.assertIn('ANDROID_ABI MATCHES "^x86(_64)?$"', self.third_party_build)
        self.assertIn("-DALSOFT_CPUEXT_NEON=OFF", self.third_party_build)

    def test_android_apk_abi_is_overridable_for_native_emulator_gate(self) -> None:
        self.assertIn('gradleProperty("evengineAbi").getOrElse("arm64-v8a")', self.android_gradle)
        self.assertIn('abiFilters += listOf(evengineAbi)', self.android_gradle)
        self.assertIn('-PevengineAbi="$ANDROID_PACKAGE_ABI"', self.sdk_test)
        self.assertIn('ANDROID_ABI STREQUAL "x86_64"', self.sdk_install)
        self.assertIn('set(_eve_android_ndk_triple "x86_64-linux-android")', self.sdk_install)

    def test_vulkan_feature_query_matches_negotiated_instance_version(self) -> None:
        self.assertIn("vkEnumerateInstanceVersion(&loaderVersion)", self.vulkan_graphics)
        self.assertIn("app.apiVersion       = apiVersion", self.vulkan_graphics)
        self.assertIn(
            "negotiatedInstanceApiVersion() >= VK_API_VERSION_1_2", self.vulkan_graphics
        )
        self.assertIn(
            "if (gpuDrivenCaps_.api12) deviceBuilder.add_pNext(&vk12Enable)",
            self.vulkan_graphics,
        )
        self.assertIn(
            "createInfo.vulkanApiVersion = VK_API_VERSION_1_0", self.vulkan_graphics
        )

    def test_android_script_output_is_observable_in_logcat(self) -> None:
        self.assertIn('__android_log_vprint(ANDROID_LOG_INFO, "EVEngine"', self.script_runtime)
        self.assertIn(
            "vm->setPrintFunc(&androidScriptPrint, &androidScriptError)",
            self.script_runtime,
        )


if __name__ == "__main__":
    unittest.main()
