using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using EasyRoads3Dv3;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Controlled derivatives of the bundled road, not additional official demo scenes.
public static class UnityBundledVariants {
    [Serializable]
    public class Derivation {
        public string schema  = "eve.tests.bundled-derivation", sourceScene, sourceRoad, variant, generatorSha256;
        public int    version = 1;
        public Vector3[] originalMarkers, markers, sdkCenters;
    }
    private static string Hash(string path) {
        using (var hash = SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(File.ReadAllBytes(path)))
            .Replace("-", "")
            .ToLowerInvariant();
    }
    public static void Run() {
        var args  = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-exportRoot");
        if (index < 0)
            throw new ArgumentException("-exportRoot required");
        const string path = "Assets/EasyRoads3D scenes/scene main.unity";
        var          names =
            new[] { "exit-east", "exit-north", "exit-south", "mirror-x", "half-scale", "uphill", "downhill", "banked" };
        for (int variant = 0; variant < names.Length; ++variant) {
            EditorSceneManager.OpenScene(path, OpenSceneMode.Single);
            var raw = UnityEngine.Object.FindObjectsByType<ERModularRoad>(FindObjectsSortMode.None)
                          .Single(r => r.name == "Default Road 0045");
            var source    = new ERRoad(raw);
            var original  = source.GetMarkerPositions();
            var points    = (Vector3[])original.Clone();
            var shape     = raw.roadShape.ToArray();
            var materials = raw.GetComponentsInChildren<MeshRenderer>()
                                .SelectMany(r => r.sharedMaterials)
                                .Where(m => m != null)
                                .ToArray();
            for (int m = 0; m < points.Length; ++m) {
                if (variant == 3)
                    points[m].x = original[1].x * 2 - points[m].x;
                if (variant == 4)
                    points[m] = original[1] + (points[m] - original[1]) * .5f;
                if (variant == 5)
                    points[m].y += 12f * m / (points.Length - 1);
                if (variant == 6)
                    points[m].y += 12f * (points.Length - 1 - m) / (points.Length - 1);
                if (variant == 7)
                    points[m].y += 3;
            }
            if (variant < 3)
                points[3] = points[2] + new[] { Vector3.right, Vector3.forward, Vector3.back }[variant] * 40;
            // Keep original terrain and lights; hide the other demo roads and junction context.
            foreach (var renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
                renderer.enabled = false;
            foreach (var oldRoad in UnityEngine.Object.FindObjectsByType<ERModularRoad>(FindObjectsSortMode.None))
                if (oldRoad != raw)
                    oldRoad.gameObject.SetActive(false);
            foreach (var terrain in Terrain.activeTerrains) {
                terrain.terrainData = UnityEngine.Object.Instantiate(terrain.terrainData);
                var collider        = terrain.GetComponent<TerrainCollider>();
                if (collider != null)
                    collider.terrainData = terrain.terrainData;
            }
            var road = source;
            road.SetTerrainDeformation(false);
            road.UnConnectStart();
            road.UnConnectEnd();
            road.SetMarkerPositions(points);
            road.SetRandomBumpiness(0, 0, 15, 35);
            road.SetResolution(source.GetResolution(), false);
            for (int m = 0; m < points.Length; ++m) {
                if (!road.SetMarkerControlType(m, source.GetMarkerControlType(m)))
                    throw new InvalidDataException("Control rejected");
                road.SetSplineStrength(m, source.GetSplineStrength(m), false);
                if (variant == 7)
                    road.SetMarkerTilting(10, m);
            }
            road.Refresh();
            if (!road.roadScript.roadShape.SequenceEqual(shape))
                throw new InvalidDataException("Derived road lost original cross-section");
            foreach (var renderer in road.roadScript.GetComponentsInChildren<MeshRenderer>()) {
                renderer.enabled = true;
                foreach (var material in renderer.sharedMaterials)
                    if (material != null && !materials.Contains(material))
                        throw new InvalidDataException("Derived road changed original material: " + material.name);
            }
            string name = "derived-" + (variant + 1).ToString("D2") + "-" + names[variant];
            string dir  = Path.Combine(args[index + 1], name);
            UnityBundledSceneExport.ExportCurrent(name, path, dir);
            File.WriteAllText(
                Path.Combine(dir, "derivation.json"),
                JsonUtility.ToJson(
                    new Derivation { sourceScene = path, sourceRoad = "Default Road 0045", variant = names[variant],
                                     generatorSha256 = Hash("Assets/Editor/UnityBundledVariants.cs"),
                                     originalMarkers = original, markers = points,
                                     sdkCenters = road.GetSplinePointsCenter() },
                    true));
        }
        Debug.Log("BUNDLED_VARIANTS_COMPLETE count=8");
    }
}
