using System;
using System.IO;
using System.Linq;
using EasyRoads3Dv3;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/** @brief Isolate an original example's circular author input through public SDK calls. */
public static class UnityBundledCurveProbe {
    [Serializable]
    public class Report {
        public string schema  = "eve.tests.bundled-circular-probe";
        public int    version = 1;
        public Vector3[] markers, originalCenters, isolatedCenters;
        public string[] controls;
        public Variant[] variants;
    }
    [Serializable]
    public class Variant {
        public string name;
        public Vector3[] markers, centers;
    }
    public static void Run() {
        var args  = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-curveOutput");
        if (index < 0)
            throw new ArgumentException("-curveOutput required");
        EditorSceneManager.OpenScene("Assets/EasyRoads3D scenes/scene main.unity", OpenSceneMode.Single);
        var raw = UnityEngine.Object.FindObjectsByType<ERModularRoad>(FindObjectsSortMode.None)
                      .Single(r => r.name == "Default Road 0045");
        var source   = new ERRoad(raw);
        var markers  = source.GetMarkerPositions();
        var original = source.GetSplinePointsCenter();
        var network  = new ERRoadNetwork();
        var road     = network.CreateRoad("original-input-isolation", new ERRoadType { roadWidth = 6 }, markers);
        road.SetTerrainDeformation(false);
        road.SetRandomBumpiness(0, 0, 15, 35);
        road.SetResolution(source.GetResolution(), false);
        var controls = new string[markers.Length];
        for (int m = 0; m < markers.Length; ++m) {
            var type = source.GetMarkerControlType(m);
            road.SetMarkerControlType(m, type);
            road.SetSplineStrength(m, source.GetSplineStrength(m), false);
            controls[m] = type.ToString();
            if (road.GetMarkerControlType(m) != type)
                throw new InvalidDataException("Controller was not applied");
        }
        road.Refresh();
        var variants = new Variant[3];
        for (int v = 0; v < variants.Length; ++v) {
            var input = (Vector3[])markers.Clone();
            if (v == 0)
                input[3] = input[2] + new Vector3(40, 0, 0);
            if (v == 1)
                input[0] = input[1] + (input[0] - input[1]) * 2;
            var candidate = network.CreateRoad("circular-contract-" + v, new ERRoadType { roadWidth = 6 }, input);
            candidate.SetTerrainDeformation(false);
            candidate.SetRandomBumpiness(0, 0, 15, 35);
            candidate.SetResolution(source.GetResolution(), false);
            for (int m = 0; m < input.Length; ++m) {
                candidate.SetMarkerControlType(m, source.GetMarkerControlType(m));
                candidate.SetSplineStrength(m, v == 2 ? .25f : source.GetSplineStrength(m), false);
            }
            candidate.Refresh();
            variants[v] =
                new Variant { name    = new[] { "outgoing-east", "predecessor-distance-double", "strength-quarter" }[v],
                              markers = input, centers = candidate.GetSplinePointsCenter() };
        }
        File.WriteAllText(
            args[index + 1],
            JsonUtility.ToJson(new Report { markers = markers, controls = controls, originalCenters = original,
                                            isolatedCenters = road.GetSplinePointsCenter(), variants = variants },
                               true));
        Debug.Log("BUNDLED_CURVE_PROBE_COMPLETE");
    }
}
