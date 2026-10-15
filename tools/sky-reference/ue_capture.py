"""Editor capture worker. Run through capture.py in a disposable local project."""

import hashlib
import json
import os
import sys
import time
import traceback
from pathlib import Path

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture_spec import CVARS, FIXED_PROPERTIES, validate, validate_camera


def atmosphere_snapshot(actor):
    """Read effective values, including native defaults omitted by T3D export."""
    fields = (
        "ground_albedo", "other_tent_distribution", "bottom_radius", "atmosphere_height", "multi_scattering_factor",
        "rayleigh_scattering_scale", "rayleigh_scattering", "rayleigh_exponential_distribution",
        "mie_scattering_scale", "mie_scattering", "mie_absorption_scale", "mie_absorption",
        "mie_anisotropy", "mie_exponential_distribution", "other_absorption_scale",
        "other_absorption", "sky_luminance_factor", "sky_and_aerial_perspective_luminance_factor",
        "transmittance_min_light_elevation_angle", "trace_sample_count_scale",
    )
    result = []
    for component in actor.get_components_by_class(unreal.SkyAtmosphereComponent):
        values = {}
        for name in fields:
            value = component.get_editor_property(name)
            if isinstance(value, (float, int, bool)):
                values[name] = value
            elif isinstance(value, (unreal.LinearColor, unreal.Color)):
                values[name] = [value.r, value.g, value.b, value.a]
            elif name == "other_tent_distribution":
                values[name] = {key: value.get_editor_property(key) for key in ("tip_altitude", "tip_value", "width")}
            else:
                raise ValueError("unsupported atmosphere property type: " + name)
        settings = (
            "r.SkyAtmosphere.TransmittanceLUT.SampleCount", "r.SkyAtmosphere.TransmittanceLUT.Width",
            "r.SkyAtmosphere.TransmittanceLUT.Height", "r.SkyAtmosphere.TransmittanceLUT.UseSmallFormat",
            "r.SkyAtmosphere.MultiScatteringLUT.SampleCount", "r.SkyAtmosphere.MultiScatteringLUT.HighQuality",
            "r.SkyAtmosphere.MultiScatteringLUT.Width", "r.SkyAtmosphere.MultiScatteringLUT.Height",
            "r.SkyAtmosphere.FastSkyLUT", "r.SkyAtmosphere.FastSkyLUT.Width",
            "r.SkyAtmosphere.FastSkyLUT.Height", "r.SkyAtmosphere.FastSkyLUT.SampleCountMin",
            "r.SkyAtmosphere.FastSkyLUT.SampleCountMax", "r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax",
            "r.LUT.Size", "r.LUT.Shaper",
        )
        render_settings = {name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in settings}
        result.append({"component": component.get_name(), "values": values, "renderSettings": render_settings})
    if len(result) != 1:
        raise ValueError("expected exactly one resolved sky atmosphere component")
    return result[0]


class Capture:
    def __init__(self, staging, spec):
        self.staging, self.spec = staging, spec
        self.output = staging / "payload"
        self.output.mkdir()
        self.levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        self.report = {"schema": "eve.sky-reference-result/2", "status": "running",
                       "engine": unreal.SystemLibrary.get_engine_version(), "request": spec,
                       "cvars": CVARS, "fixedProperties": FIXED_PROPERTIES,
                       "exposure": {"method": "manual", "bias": 0, "physicalCamera": False},
                       "artifacts": []}
        self.index, self.task, self.repeated = 0, None, False
        self.handle = None

    def record_file(self, path, kind):
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError("capture did not produce " + str(path))
        self.report["artifacts"].append({"file": path.relative_to(self.output).as_posix(),
                                         "kind": kind, "byteLength": path.stat().st_size,
                                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})

    def setup(self):
        scene = self.spec["scenarios"][self.index]
        # The host stages captures under a unique directory, also used as a map namespace.
        map_path = "/Game/EveSkyReference/" + self.staging.parent.name.replace(".", "_").replace("-", "_") + "/" + scene["name"].replace("-", "_")
        if not self.levels.new_level(map_path):
            raise RuntimeError("could not create " + map_path)
        cls = unreal.load_class(None, "/Game/UltraDynamicSky/Blueprints/Ultra_Dynamic_Sky.Ultra_Dynamic_Sky_C")
        if cls is None:
            raise RuntimeError("UDS blueprint is missing")
        sky = self.actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0))
        settings = {**FIXED_PROPERTIES, "Time of Day": scene["timeOfDay"], "Cloud Coverage": scene["cloudCoverage"]}
        for name, value in settings.items():
            sky.set_editor_property(name, value)
        pose = self.spec["camera"]
        pitch, yaw, roll = pose["rotationDegrees"]
        self.camera = self.actors.spawn_actor_from_class(unreal.CameraActor,
            unreal.Vector(*pose["positionCm"]), unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll))
        component = self.camera.camera_component
        component.set_editor_property("field_of_view", pose["horizontalFovDegrees"])
        post = component.get_editor_property("post_process_settings")
        post.set_editor_property("override_auto_exposure_method", True)
        post.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
        post.set_editor_property("override_auto_exposure_bias", True)
        post.set_editor_property("auto_exposure_bias", 0.0)
        post.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
        component.set_editor_property("post_process_settings", post)
        component.set_editor_property("post_process_blend_weight", 1.0)
        position = self.camera.get_actor_location()
        rotation = self.camera.get_actor_rotation()
        forward = self.camera.get_actor_forward_vector()
        up = self.camera.get_actor_up_vector()
        self.report["resolvedCamera"] = {
            "positionCm": [position.x, position.y, position.z],
            "rotationDegrees": [rotation.pitch, rotation.yaw, rotation.roll],
            "forward": [forward.x, forward.y, forward.z],
            "up": [up.x, up.y, up.z],
            "horizontalFovDegrees": component.get_editor_property("field_of_view"),
        }
        validate_camera(self.report["resolvedCamera"], pose)
        world = self.editor.get_editor_world()
        for name, value in CVARS.items():
            unreal.SystemLibrary.execute_console_command(world, f"{name} {value}")
        self.editor.set_level_viewport_camera_info(self.camera.get_actor_location(), self.camera.get_actor_rotation())
        if not self.levels.save_current_level():
            raise RuntimeError("could not save reference map")
        path = self.output / (scene["name"] + ".actor.t3d")
        task = unreal.AssetExportTask()
        task.object, task.filename, task.exporter = sky, str(path), unreal.ObjectExporterT3D()
        task.automated, task.prompt, task.replace_identical = True, False, False
        if not unreal.Exporter.run_asset_export_task(task):
            raise RuntimeError("could not snapshot resolved sky actor")
        self.record_file(path, "actor")
        self.report["resolvedAtmosphere"] = atmosphere_snapshot(sky)
        self.started, self.task, self.repeated = time.monotonic(), None, False
        unreal.log("EVENGINE_SKY_REFERENCE warming " + scene["name"])

    def finish(self, error=None):
        self.report["status"] = "failed" if error else "captured"
        if error:
            self.report["error"] = error
        (self.staging / "result.json").write_text(json.dumps(self.report, indent=2) + "\n", encoding="utf-8")
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.quit_editor()

    def tick(self, dt):
        try:
            elapsed = time.monotonic() - self.started
            if elapsed > self.spec["warmupSeconds"] + 180:
                raise RuntimeError("screenshot timeout")
            if self.task is None and elapsed >= self.spec["warmupSeconds"]:
                scene = self.spec["scenarios"][self.index]
                self.path = self.output / (scene["name"] + ("-repeat" if self.repeated else "") + ".png")
                self.task = unreal.AutomationLibrary.take_high_res_screenshot(
                    *self.spec["size"], str(self.path), camera=self.camera)
                if not self.task.is_valid_task():
                    raise RuntimeError("invalid screenshot task")
            elif self.task is not None and self.task.is_task_done():
                self.record_file(self.path, "repeat" if self.repeated else "reference")
                if not self.repeated:
                    self.task, self.repeated = None, True
                    self.started = time.monotonic() - self.spec["warmupSeconds"] + 2
                else:
                    self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    staging = Path(os.environ["EVENGINE_SKY_CAPTURE_REQUEST"]).resolve().parent
    capture = None
    try:
        spec = validate(json.loads((staging / "request.json").read_text(encoding="utf-8")))
        if len(spec["scenarios"]) != 1:
            raise ValueError("each editor process must capture exactly one scene")
        capture = Capture(staging, spec)
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        capture.setup()
        capture.handle = unreal.register_slate_post_tick_callback(capture.tick)
    except Exception:
        if capture is not None:
            capture.finish(traceback.format_exc())
        else:
            (staging / "result.json").write_text(json.dumps({"status": "failed", "error": traceback.format_exc()}), encoding="utf-8")
            unreal.SystemLibrary.quit_editor()


if __name__ == "__main__":
    main()
