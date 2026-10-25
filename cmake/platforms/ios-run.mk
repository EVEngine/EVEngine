# Install Debug .app to the first connected physical iOS/iPadOS device.
install/ios-debug:
	@test -n "$$(security find-identity -v -p codesigning 2>/dev/null | sed -n '/Apple Development/p')" \
	  || (echo "No Apple Development signing identity. Open Xcode → Settings → Accounts, add Apple ID, then rebuild with IOS_DEVELOPMENT_TEAM=<TeamID>."; exit 1)
	@APP="$(IOS_APP)"; \
	  if [ ! -d "$$APP" ]; then \
	    APP=$$(find build/ios-debug -name 'eve.app' -type d 2>/dev/null | head -1); \
	  fi; \
	  test -n "$$APP" -a -d "$$APP" || (echo "eve.app not found; run make build/ios-debug first"; exit 1); \
	  echo "Installing $$APP"; \
	  if xcrun --find devicectl >/dev/null 2>&1; then \
	    DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	    test -n "$$DEV" || (echo "No connected/available iOS device found (devicectl)."; exit 1); \
	    echo "Device $$DEV"; \
	    xcrun devicectl device install app --device $$DEV "$$APP"; \
	  elif command -v ios-deploy >/dev/null 2>&1; then \
	    ios-deploy --bundle "$$APP"; \
	  else \
	    echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	  fi

run/ios-debug: install/ios-debug
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	  xcrun devicectl device process launch --device $$DEV $(IOS_BUNDLE_ID); \
	elif command -v ios-deploy >/dev/null 2>&1; then \
	  ios-deploy --justlaunch --bundle "$$(find build/ios-debug -name 'eve.app' -type d | head -1)"; \
	fi

log/ios:
	@echo "Streaming device logs (Ctrl-C to stop)..."
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  xcrun devicectl device info log --help >/dev/null 2>&1 || true; \
	  log stream --predicate 'processImagePath CONTAINS "eve" OR process CONTAINS "EVEngine"' --style compact; \
	else \
	  log stream --predicate 'processImagePath CONTAINS "eve"' --style compact; \
	fi

# ---- iOS test app: install / launch / logs (mirrors run/android-test-debug) ----
install/ios-test-debug:
	@test -n "$$(security find-identity -v -p codesigning 2>/dev/null | sed -n '/Apple Development/p')" \
	  || (echo "No Apple Development signing identity. Open Xcode → Settings → Accounts, add Apple ID, then rebuild with IOS_DEVELOPMENT_TEAM=<TeamID>."; exit 1)
	@APP="$(IOS_TEST_APP)"; \
	  if [ ! -d "$$APP" ]; then \
	    APP=$$(find build/ios-debug-test -name 'eve.app' -type d 2>/dev/null | head -1); \
	  fi; \
	  test -n "$$APP" -a -d "$$APP" || (echo "eve.app not found; run make build/ios-debug-test first"; exit 1); \
	  echo "Installing $$APP"; \
	  if xcrun --find devicectl >/dev/null 2>&1; then \
	    DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	    test -n "$$DEV" || (echo "No connected/available iOS device found (devicectl)."; exit 1); \
	    echo "Device $$DEV"; \
	    xcrun devicectl device install app --device $$DEV "$$APP"; \
	  elif command -v ios-deploy >/dev/null 2>&1; then \
	    ios-deploy --bundle "$$APP"; \
	  else \
	    echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	  fi

run/ios-test-debug: install/ios-test-debug
	@if xcrun --find devicectl >/dev/null 2>&1; then \
	  DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	  if [ -n "$(FILTER)" ]; then \
	    xcrun devicectl device process launch --terminate-existing --console --device $$DEV $(IOS_TEST_BUNDLE_ID) -- -evengine.test.filter "$(FILTER)"; \
	  else \
	    xcrun devicectl device process launch --terminate-existing --console --device $$DEV $(IOS_TEST_BUNDLE_ID); \
	  fi; \
	elif command -v ios-deploy >/dev/null 2>&1; then \
	  if [ -n "$(FILTER)" ]; then \
	    ios-deploy --justlaunch --bundle "$$(find build/ios-debug-test -name 'eve.app' -type d | head -1)" --args "-evengine.test.filter $(FILTER)"; \
	  else \
	    ios-deploy --justlaunch --bundle "$$(find build/ios-debug-test -name 'eve.app' -type d | head -1)"; \
	  fi; \
	else \
	  echo "Need xcrun devicectl (Xcode 15+) or ios-deploy"; exit 1; \
	fi

# Run the full suite on a device, one test file per app launch. Per-file
# process isolation mirrors CI's per-case CTest isolation: rapid window
# create/destroy across hundreds of tests trips the iOS watchdog (SIGKILL), so
# a crash in one file only loses that file's results.
IOS_TEST_RESULTS ?= build/ios-test-results
run/ios-test-all-debug: install/ios-test-debug
	@mkdir -p "$(IOS_TEST_RESULTS)"; rm -f "$(IOS_TEST_RESULTS)"/*.log; \
	DEV=$$(xcrun devicectl list devices 2>/dev/null | sed -nE '/connected|available/s/.*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}).*/\1/p' | head -1); \
	test -n "$$DEV" || (echo "No connected/available iOS device found (devicectl)."; exit 1); \
	ran=0; \
	for f in test/*.cpp $$(find test/unit test/integration -name '*.cpp' -type f); do \
	  b=$$(basename "$$f"); \
	  [ "$$b" = "main.cpp" ] && continue; \
	  ran=$$((ran+1)); \
	  echo "== $$b =="; \
	  xcrun devicectl --timeout 300 device process launch --terminate-existing --console --device "$$DEV" $(IOS_TEST_BUNDLE_ID) -- -evengine.test.file "$$b" > "$(IOS_TEST_RESULTS)/$${b%.cpp}.log" 2>&1 || true; \
	done; \
	echo "Ran $$ran test files; logs in $(IOS_TEST_RESULTS)/"; \
	echo "--- per-file results ---"; \
	passed=0; crashed=0; failed=0; \
	for l in "$(IOS_TEST_RESULTS)"/*.log; do \
	  b=$$(basename "$$l" .log); \
	  if grep -aq "signal" "$$l"; then \
	    echo "CRASH $$b ($$(grep -aoE 'signal [0-9]+' "$$l" | head -1))"; \
	    crashed=$$((crashed+1)); \
	  elif grep -aq "❌" "$$l"; then \
	    echo "FAIL  $$b"; \
	    failed=$$((failed+1)); \
	  else \
	    echo "PASS  $$b"; \
	    passed=$$((passed+1)); \
	  fi; \
	done; \
	echo "--- summary: PASS=$$passed FAIL=$$failed CRASH=$$crashed ---"

log/ios-test:
	@echo "Streaming iOS test logs (Ctrl-C to stop)..."
	log stream --predicate 'subsystem == "$(IOS_TEST_BUNDLE_ID)"' --style compact

# ---- iOS simulator test app: install / launch / logs (no signing needed) ----
install/ios-sim-test-debug:
	@APP="$(IOS_SIM_TEST_APP)"; \
	  if [ ! -d "$$APP" ]; then \
	    APP=$$(find build/ios-sim-debug-test -name 'eve.app' -type d 2>/dev/null | head -1); \
	  fi; \
	  test -n "$$APP" -a -d "$$APP" || (echo "eve.app not found; run make build/ios-sim-debug-test first"; exit 1); \
	  SIM=$$(xcrun simctl list devices available 2>/dev/null | sed -n 's/.*(\([0-9A-Fa-f-]\{36\}\)).*/\1/p' | head -1); \
	  test -n "$$SIM" || (echo "No available iOS simulator"; exit 1); \
	  xcrun simctl boot "$$SIM" 2>/dev/null || true; \
	  xcrun simctl bootstatus "$$SIM" -b >/dev/null 2>&1; \
	  echo "Installing $$APP on simulator $$SIM"; \
	  xcrun simctl install "$$SIM" "$$APP"

run/ios-sim-test-debug: install/ios-sim-test-debug
	@SIM=$$(xcrun simctl list devices available 2>/dev/null | sed -n 's/.*(\([0-9A-Fa-f-]\{36\}\)).*/\1/p' | head -1); \
	  if [ -n "$(FILTER)" ]; then \
	    xcrun simctl launch "$$SIM" $(IOS_TEST_BUNDLE_ID) -evengine.test.filter "$(FILTER)"; \
	  else \
	    xcrun simctl launch "$$SIM" $(IOS_TEST_BUNDLE_ID); \
	  fi

log/ios-sim-test:
	@echo "Streaming simulator test logs (Ctrl-C to stop)..."
	xcrun simctl spawn booted log stream --predicate 'subsystem == "$(IOS_TEST_BUNDLE_ID)"' --style compact

test: test/$(PLATFORM)-debug

# Fetch large classic render scenes for ClassicScenes.* tests
# (Sponza, DamagedHelmet, FlightHelmet, BoomBox, WaterBottle, Corset, …).
# Cornell Box is committed under test/assets/classic/cornell/.
