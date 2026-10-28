def convert(ctx):
    """Box -> low-poly stone.

    Scales the input to `size`, subdivides to `detail`, then displaces a seeded
    vertex group along normals for an organic rock surface. Same seed -> same rock.
    """
    p = ctx.params
    seed = int(p.get("seed", 1))
    size = float(p.get("size", 1.2))
    detail = float(p.get("detail", 0.6))
    mo = ctx.mesh_ops

    mo.scale_object(ctx.obj, size, size * 0.82, size)
    subdivisions = max(1, round(2 + detail * 3))
    mo.remesh_quads(ctx.obj, subdivisions)

    group = "deform"
    mo.weighted_random_group(ctx.obj, seed, group, 0.25, 1.0)
    mo.smooth_group(ctx.obj, group, 2)
    strength = 0.45 + detail * 0.35
    mo.displace_vertex_weights(ctx.obj, seed, strength, group)
