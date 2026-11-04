"""ModelConverter — EVEngine ↔ Blender (bpy) bridge.

This package is invoked by the EVEngine native plugin as a subprocess:
    python -m eve_blender_converter convert <job.txt>

It imports Blender's Python package (`bpy`) to turn a primitive mesh into a
richer model by running a converter (a Python script or a Geometry Node group).
"""

__version__ = "0.1.0"
