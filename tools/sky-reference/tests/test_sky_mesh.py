import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))
from sky_mesh import decode_sky_mesh


MESH = b'''
Geometry: 123, "Geometry::Sky", "Mesh" {
Vertices: *9 { a: 1,-2,3,4,-5,6,7,-8,9 }
PolygonVertexIndex: *6 { a: 0,1,-3,0,2,-2 }
LayerElementUV: 3 {
MappingInformationType: "ByPolygonVertex"
ReferenceInformationType: "IndexToDirect"
UV: *8 { a: 0,0.25,1,0.25,1,0.75,0.5,0.5 }
UVIndex: *6 { a: 0,1,2,3,2,1 }
}
LayerElementColor: 0 {
MappingInformationType: "ByPolygonVertex"
ReferenceInformationType: "IndexToDirect"
Colors: *8 { a: 0.2,0.3,0.4,1,0.7,0.8,0.9,1 }
ColorIndex: *6 { a: 0,0,0,1,0,0 }
}
}
'''


class SkyMeshTests(unittest.TestCase):
    def test_preserves_corner_seams_and_reverses_export_axes_and_uv(self):
        mesh = decode_sky_mesh(MESH)
        self.assertEqual(mesh["schema"], "eve.sky-mesh/1")
        self.assertEqual(len(mesh["vertices"]), 6)
        self.assertEqual(mesh["vertices"][0], [1, 2, 3, 0, 0.75, 0.2, 0.3, 0.4, 1])
        self.assertEqual(mesh["vertices"][3], [1, 2, 3, 0.5, 0.5, 0.7, 0.8, 0.9, 1])
        self.assertEqual(mesh["vertices"][2][:3], [7, 8, 9])

    def test_rejects_corrupt_or_unsupported_mesh_without_partial_output(self):
        changes = [(b"*9", b"*12"), (b"1,-2,3", b"nan,-2,3"),
                   (b"0,1,-3,0,2,-2", b"0,1,2,0,-3,-2"),
                   (b"0,1,-3,0,2,-2", b"0,1,-4,0,2,-2"),
                   (b"0,1,2,3,2,1", b"0,1,2,-1,2,1"),
                   (b"ByPolygonVertex", b"ByControlPoint"),
                   (b"0.2,0.3", b"1.2,0.3"), (b"LayerElementUV: 3", b"LayerElementUV: 2")]
        for before, after in changes:
            with self.subTest(before=before, after=after), self.assertRaises(ValueError):
                decode_sky_mesh(MESH.replace(before, after))
        for source in (MESH + MESH, b"Kaydara FBX Binary", b""):
            with self.assertRaises(ValueError):
                decode_sky_mesh(source)


if __name__ == "__main__":
    unittest.main()
