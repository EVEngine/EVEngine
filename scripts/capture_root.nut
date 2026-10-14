// Post-frame capture hook composed into the standard src/scripts/load.nut by
// capture_example.ps1. The standard startup owns modules, helpers and callbacks.
// The composed startup supplies isolated per-run settings before this hook.
if (capture_setup != "") dofile(capture_setup);
_capture_frames <- 0;
_capture_attempts <- 0;

// Returns true when the normal desktop frame loop should stop.
function capture_after_frame() {
    _capture_frames += 1;
    if (_capture_frames < capture_frame) return false;
    _capture_attempts += 1;
    if (gfx.saveFramePng(capture_path)) {
        print("CAPTURED " + capture_path + "\n");
        return true;
    }
    // The first request enables readback; retry after another presented frame.
    if (_capture_attempts > capture_tries) {
        print("CAPTURE_FAILED\n");
        return true;
    }
    return false;
}
