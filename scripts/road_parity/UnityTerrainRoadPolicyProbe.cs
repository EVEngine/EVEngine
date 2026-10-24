using System;
using System.IO;
using EasyRoads3Dv3;
using UnityEditor.SceneManagement;
using UnityEngine;

// Disjoint author policies and explicit road selection, using only documented public calls.
public static class UnityTerrainRoadPolicyProbe
{
    [Serializable] public class Sample
    {
        public string selection;
        public float[] firstCenter, secondCenter;
        public int changedTexels;
        public float maximumRestoreError;
    }
    [Serializable] public class Report
    {
        public string schema = "eve.tests.easyroads-per-road-terrain-policy";
        public int version = 1, randomSeed = 1729;
        public string unityVersion;
        public Sample[] samples;
        public bool checksPassed;
    }
    public static void Run()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-terrainPolicyOutput");
        if (index < 0 || index + 1 >= args.Length) throw new ArgumentException("-terrainPolicyOutput required");
        var report = new Report { unityVersion = Application.unityVersion,
            samples = new[] { Generate("all"), Generate("first"), Generate("second") }, checksPassed = true };
        foreach (Sample sample in report.samples) {
            bool first = sample.selection != "second", second = sample.selection != "first";
            float[] expectedFirst = first ? new[] { .6f, .4f, 0f } : new[] { 1f, 0f, 0f };
            float[] expectedSecond = second ? new[] { .2f, 0f, .8f } : new[] { 1f, 0f, 0f };
            for (int c = 0; c < 3; ++c)
                if (Mathf.Abs(sample.firstCenter[c] - expectedFirst[c]) > 1f / 255f ||
                    Mathf.Abs(sample.secondCenter[c] - expectedSecond[c]) > 1f / 255f) report.checksPassed = false;
            if (sample.changedTexels == 0 || sample.maximumRestoreError > 0.0001f) report.checksPassed = false;
        }
        File.WriteAllText(args[index + 1], JsonUtility.ToJson(report, true));
        if (!report.checksPassed) throw new InvalidDataException("Per-road terrain policy or selected build differs");
        Debug.Log("TERRAIN_ROAD_POLICY_PROBE_COMPLETE");
    }
    private static Sample Generate(string selection)
    {
        UnityEngine.Random.InitState(1729);
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var texture = new Texture2D(2, 2);
        texture.SetPixels(new[] { Color.white, Color.white, Color.white, Color.white }); texture.Apply();
        var data = new TerrainData { heightmapResolution = 65, alphamapResolution = 64,
            size = new Vector3(64, 10, 64) };
        data.terrainLayers = new[] { new TerrainLayer { diffuseTexture = texture },
            new TerrainLayer { diffuseTexture = texture }, new TerrainLayer { diffuseTexture = texture } };
        var heights = new float[65, 65];
        for (int z = 0; z < 65; ++z) for (int x = 0; x < 65; ++x) heights[z, x] = .5f;
        data.SetHeights(0, 0, heights);
        var weights = new float[64, 64, 3];
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) weights[z, x, 0] = 1;
        data.SetAlphamaps(0, 0, weights);
        var terrain = Terrain.CreateTerrainGameObject(data).GetComponent<Terrain>();
        terrain.transform.position = new Vector3(-32, 100, -32); terrain.Flush();
        var network = new ERRoadNetwork();
        ERRoad first = Road(network, "first", -12, 1, .4f);
        ERRoad second = Road(network, "second", 12, 2, .8f);
        if (selection == "all") network.BuildRoadNetwork(true, false, false);
        else network.BuildRoadNetwork(true, false, false, new[] { selection == "first" ? first : second });
        float[,,] built = data.GetAlphamaps(0, 0, 64, 64);
        var sample = new Sample { selection = selection,
            firstCenter = new[] { built[20, 32, 0], built[20, 32, 1], built[20, 32, 2] },
            secondCenter = new[] { built[44, 32, 0], built[44, 32, 1], built[44, 32, 2] } };
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x)
            if (Mathf.Abs(built[z, x, 0] - 1) > .0001f) ++sample.changedTexels;
        network.RestoreRoadNetwork();
        float[,,] restored = data.GetAlphamaps(0, 0, 64, 64);
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) for (int c = 0; c < 3; ++c)
            sample.maximumRestoreError = Mathf.Max(sample.maximumRestoreError, Mathf.Abs(restored[z, x, c] - weights[z, x, c]));
        return sample;
    }
    private static ERRoad Road(ERRoadNetwork network, string name, float z, int layer, float opacity)
    {
        var road = network.CreateRoad(name, new ERRoadType { roadTypeName = name, roadWidth = 6 },
            new[] { new Vector3(-24, 106, z), new Vector3(0, 106, z), new Vector3(24, 106, z) });
        if (road == null) throw new InvalidDataException("Road creation failed");
        road.SetTerrainDeformation(true); road.SetRandomBumpiness(0, 0, 15, 35);
        road.SetSplatmap(true, layer, 0, 0, opacity);
        for (int i = 0; i < 3; ++i) { road.SetIndent(3, i); road.SetSurrounding(5, i); }
        road.Refresh();
        return road;
    }
}
