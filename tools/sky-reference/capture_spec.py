"""Versioned, bounded input contract shared by the capture host and Unreal."""

import math
import re


SCHEMA = "eve.sky-reference-capture/1"
CVARS = {
    "r.EyeAdaptationQuality": 0,
    "r.ScreenPercentage": 100,
    "r.DynamicRes.OperationMode": 0,
    "r.Test.FreezeTemporalSequences": 1,
    "r.Tonemapper.GrainQuantization": 0,
    "t.MaxFPS": 30,
}
FIXED_PROPERTIES = {
    "Animate Time of Day": False,
    "Random Starting Time": False,
    "Randomize Cloud Formation on Run": False,
    "Clouds Move with Time of Day": False,
    "Cloud Speed": 0.0,
    "Cloud Phase": 0.0,
    "Twinkle Speed": 0.0,
    "Tiling Stars Constant Speed": 0.0,
    "Apply Exposure Settings": False,
}


def camera_basis(rotation):
    """Unreal X-forward/Z-up basis for explicitly ordered pitch, yaw, roll degrees."""
    pitch, yaw, roll = (math.radians(value) for value in rotation)
    sp, cp, sy, cy, sr, cr = math.sin(pitch), math.cos(pitch), math.sin(yaw), math.cos(yaw), math.sin(roll), math.cos(roll)
    return [cp * cy, cp * sy, sp], [-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp]


def validate_camera(actual, requested):
    """Validate read-back pose, not just the requested constructor arguments."""
    keys = {"positionCm", "rotationDegrees", "forward", "up", "horizontalFovDegrees"}
    if not isinstance(actual, dict) or set(actual) != keys:
        raise ValueError("missing or invalid resolved camera")
    forward, up = camera_basis(requested["rotationDegrees"])
    for name, expected, tolerance in (("positionCm", requested["positionCm"], .001),
                                       ("forward", forward, 1e-5), ("up", up, 1e-5)):
        values = actual[name]
        if not isinstance(values, list) or len(values) != 3:
            raise ValueError("invalid resolved camera " + name)
        for value, wanted in zip(values, expected, strict=True):
            number(value, -1e10, 1e10, "resolved camera " + name)
            if abs(value - wanted) > tolerance:
                raise ValueError("resolved camera mismatch: " + name)
    if not isinstance(actual["rotationDegrees"], list) or len(actual["rotationDegrees"]) != 3:
        raise ValueError("invalid resolved camera rotation")
    for value in actual["rotationDegrees"]:
        number(value, -360, 360, "resolved camera rotation")
    number(actual["horizontalFovDegrees"], 1, 170, "resolved camera FOV")
    if abs(actual["horizontalFovDegrees"] - requested["horizontalFovDegrees"]) > 1e-4:
        raise ValueError("resolved camera mismatch: FOV")


def number(value, low, high, name):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError("invalid " + name)


def validate(spec):
    """Reject unknown versions/fields instead of guessing a migration or default."""
    fields = {"schema", "size", "warmupSeconds", "camera", "scenarios"}
    if not isinstance(spec, dict) or set(spec) != fields or spec["schema"] != SCHEMA:
        raise ValueError("unrecognized capture schema or fields")
    size = spec["size"]
    if not isinstance(size, list) or len(size) != 2 or any(type(v) is not int or not 64 <= v <= 8192 for v in size):
        raise ValueError("size must contain two integers in [64,8192]")
    number(spec["warmupSeconds"], 2, 300, "warmupSeconds")
    camera = spec["camera"]
    if not isinstance(camera, dict) or set(camera) != {"positionCm", "rotationDegrees", "horizontalFovDegrees"}:
        raise ValueError("invalid camera fields")
    for name, limit in (("positionCm", 1e9), ("rotationDegrees", 360)):
        values = camera[name]
        if not isinstance(values, list) or len(values) != 3:
            raise ValueError("camera vectors need three components")
        for value in values:
            number(value, -limit, limit, name)
    number(camera["horizontalFovDegrees"], 1, 170, "horizontalFovDegrees")
    scenarios = spec["scenarios"]
    if not isinstance(scenarios, list) or not 1 <= len(scenarios) <= 64:
        raise ValueError("expected 1..64 scenarios")
    seen = set()
    for scene in scenarios:
        if not isinstance(scene, dict) or set(scene) != {"name", "timeOfDay", "cloudCoverage"}:
            raise ValueError("invalid scenario fields")
        name = scene["name"]
        if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", name) or name in seen:
            raise ValueError("invalid or duplicate scenario name")
        seen.add(name)
        number(scene["timeOfDay"], 0, 2400, "timeOfDay")
        number(scene["cloudCoverage"], 0, 10, "cloudCoverage")
    return spec
