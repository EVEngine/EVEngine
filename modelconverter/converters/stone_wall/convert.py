def convert(ctx):
    """Box -> stone wall.

    Lays the input box out as a wall (`length` x `height` x `depth`), subdivides
    it into horizontal stone courses, and adds a small seeded jitter so adjacent
    stones read as separate blocks. Deterministic per `seed`.
    """
    p = ctx.params
    seed = int(p.get("seed", 1))
    length = float(p.get("length", 3.0))
    height = float(p.get("height", 2.0))
    depth = float(p.get("depth", 0.6))
    detail = float(p.get("detail", 0.5))
    mo = ctx.mesh_ops

    mo.scale_object(ctx.obj, length, height, depth)

    # One horizontal course roughly every 0.3 m; plus lengthwise splits.
    courses = max(2, round(height / 0.3))
    mo.remesh_quads(ctx.obj, courses)

    # Subtle jitter on the wall faces to imply individual stones.
    group = "face"
    mo.weighted_random_group(ctx.obj, seed, group, 0.0, 0.5)
    mo.smooth_group(ctx.obj, group, 1)
    mo.displace_vertex_weights(ctx.obj, seed, 0.03 + detail * 0.03, group)
