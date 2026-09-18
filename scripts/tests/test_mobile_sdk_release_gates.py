"""Static contracts for release-blocking Android and iOS SDK validation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "sdk-release.yml"
SDK_TEST = ROOT / ".github" / "scripts" / "test-sdk.sh"


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

    def test_android_emulator_is_a_hard_gate_for_every_consumer_host(self) -> None:
        block = job_block(self.workflow, "run-android", "ios-sim")
        self.assertNotIn("continue-on-error:", block)
        self.assertIn("needs: [consumer-win32, consumer-linux, consumer-macosx]", block)
        self.assertIn("pattern: android-apk-*", block)
        self.assertIn("merge-multiple: true", block)
        self.assertIn("for apk_file in apk/*.apk", block)
        self.assertIn('if [ "$found" -ne 3 ]', block)
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


if __name__ == "__main__":
    unittest.main()
