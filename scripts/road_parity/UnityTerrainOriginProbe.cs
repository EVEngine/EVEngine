using System;
using System.IO;
using EasyRoads3Dv3;
using UnityEditor.SceneManagement;
using UnityEngine;

// Public API only: translated copies must deform and restore the same normalized grid.
public static class UnityTerrainOriginProbe
{
    [Serializable] public class Sample
    {
        public float originY;
        public float beforeCenter, builtCenter, restoredCenter;
        public float[] builtHeights, restoredHeights;
        public int changedSamples;
        public float maximumRestoreError;
    }
    [Serializable] public class Report
    {
        public string schema = "eve.tests.easyroads-terrain-origin";
        public int version = 1;
        public string unityVersion;
        public Sample[] samples;
        public float maximumTranslationError;
    }
    public static void Run()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-terrainOriginOutput");
        if (index < 0 || index + 1 >= args.Length) throw new ArgumentException("-terrainOriginOutput required");
        var report = new Report { unityVersion = Application.unityVersion,
            samples = new[] { Generate(0), Generate(100) } };
        for (int i = 0; i < report.samples[0].builtHeights.Length; ++i)
            report.maximumTranslationError = Mathf.Max(report.maximumTranslationError,
                Mathf.Abs(report.samples[0].builtHeights[i] - report.samples[1].builtHeights[i]));
        File.WriteAllText(args[index + 1], JsonUtility.ToJson(report, true));
        foreach (Sample sample in report.samples)
            if (sample.changedSamples == 0 || sample.maximumRestoreError > 0.0001f ||
                Mathf.Abs(sample.beforeCenter - 0.5f) > 0.0001f ||
                Mathf.Abs(sample.builtCenter - 0.6f) > 0.0001f)
                throw new InvalidDataException("Terrain build/restore did not complete at " + sample.originY);
        if (report.maximumTranslationError > 0.0001f)
            throw new InvalidDataException("Terrain translation changes normalized deformation");
        Debug.Log("TERRAIN_ORIGIN_PROBE_COMPLETE");
    }
    private static Sample Generate(float originY)
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var data = new TerrainData { heightmapResolution = 65, size = new Vector3(64, 10, 64) };
        var heights = new float[65, 65];
        for (int z = 0; z < 65; ++z)
            for (int x = 0; x < 65; ++x) heights[z, x] = 0.5f;
        data.SetHeights(0, 0, heights);
        GameObject terrainObject = Terrain.CreateTerrainGameObject(data);
        terrainObject.transform.position = new Vector3(-32, originY, -32);
        var terrain = terrainObject.GetComponent<Terrain>();
        terrain.Flush();
        var network = new ERRoadNetwork();
        var road = network.CreateRoad("origin-probe", new ERRoadType { roadTypeName = "origin-probe", roadWidth = 6 },
            new[] { new Vector3(-24, originY + 6, 0), new Vector3(0, originY + 6, 0), new Vector3(24, originY + 6, 0) });
        if (road == null) throw new InvalidDataException("Road creation failed");
        road.SetTerrainDeformation(true);
        road.SetRandomBumpiness(0, 0, 15, 35);
        for (int i = 0; i < 3; ++i) { road.SetIndent(3, i); road.SetSurrounding(5, i); }
        road.Refresh();
        var sample = new Sample { originY = originY, beforeCenter = data.GetHeights(32, 32, 1, 1)[0, 0] };
        network.BuildRoadNetwork(false, false, false);
        float[,] built = data.GetHeights(0, 0, 65, 65);
        sample.builtCenter = built[32, 32];
        sample.builtHeights = new float[65 * 65];
        for (int z = 0; z < 65; ++z)
            for (int x = 0; x < 65; ++x) {
                sample.builtHeights[z * 65 + x] = built[z, x];
                if (Mathf.Abs(built[z, x] - heights[z, x]) > 0.0001f) ++sample.changedSamples;
            }
        network.RestoreRoadNetwork();
        float[,] restored = data.GetHeights(0, 0, 65, 65);
        sample.restoredCenter = restored[32, 32];
        sample.restoredHeights = new float[65 * 65];
        for (int z = 0; z < 65; ++z)
            for (int x = 0; x < 65; ++x) {
                sample.restoredHeights[z * 65 + x] = restored[z, x];
                sample.maximumRestoreError = Mathf.Max(sample.maximumRestoreError, Mathf.Abs(restored[z, x] - heights[z, x]));
            }
        return sample;
    }
}
