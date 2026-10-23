using System;
using System.IO;
using EasyRoads3Dv3;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class UnityTerrainChannelsProbe
{
    [Serializable] public class Sample
    {
        public bool splats, trees, details;
        public float opacity;
        public float initialTargetWeight;
        public float[] builtCenterLayers;
        public float centerWeight, farWeight, maximumRestoreError;
        public int treesBefore, treesBuilt, treesRestored, detailBefore, detailBuilt, detailRestored;
        public int changedSplatSamples;
        public int detailRestoreMismatches, treeRestoreMismatches;
        public string[] treeBeforeFacts, treeRestoredFacts;
    }
    [Serializable] public class Report
    {
        public string schema = "eve.tests.easyroads-terrain-channels";
        public int version = 1;
        public Sample[] samples;
        public bool writeChecksPassed = true, strictRestorePassed = true;
    }
    public static void Run()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-terrainChannelsOutput");
        if (index < 0 || index + 1 >= args.Length) throw new ArgumentException("-terrainChannelsOutput required");
        var report = new Report { samples = new[] {
            Generate(false, false, false, 1), Generate(true, false, false, 1),
            Generate(false, true, false, 1), Generate(false, false, true, 1),
            Generate(true, true, true, 1), Generate(true, false, false, 0.5f),
            Generate(true, false, false, 0.5f, 0.3f), Generate(true, false, false, 0.5f, 0.8f),
            Generate(true, false, false, 0.5f, 0.3f, 0.5f) } };
        foreach (Sample sample in report.samples) {
            if (sample.maximumRestoreError > 0.0001f || sample.treesRestored != sample.treesBefore ||
                sample.detailRestored != sample.detailBefore || sample.detailRestoreMismatches != 0 ||
                sample.treeRestoreMismatches != 0)
                report.strictRestorePassed = false;
            if (!sample.splats && sample.changedSplatSamples != 0 ||
                !sample.trees && sample.treesBuilt != sample.treesBefore ||
                !sample.details && sample.detailBuilt != sample.detailBefore)
                report.writeChecksPassed = false;
            if (sample.splats && (sample.changedSplatSamples == 0 && sample.initialTargetWeight < sample.opacity ||
                Mathf.Abs(sample.centerWeight - Mathf.Max(sample.initialTargetWeight, sample.opacity)) > 1f / 255f) ||
                sample.trees && sample.treesBuilt != 1 || sample.details && sample.detailBuilt >= sample.detailBefore ||
                Mathf.Abs(sample.farWeight - sample.initialTargetWeight) > 1f / 255f)
                report.writeChecksPassed = false;
        }
        File.WriteAllText(args[index + 1], JsonUtility.ToJson(report, true));
        Debug.Log("TERRAIN_CHANNELS_MEASUREMENTS_COMPLETE");
        if (!report.writeChecksPassed) throw new InvalidDataException("Channel write checks failed");
        if (!report.strictRestorePassed) throw new InvalidDataException("Channel restore failed");
        Debug.Log("TERRAIN_CHANNELS_PROBE_COMPLETE");
    }
    private static Sample Generate(bool splats, bool trees, bool details, float opacity, float initialTargetWeight = 0,
                                  float initialThirdWeight = 0)
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var texture = new Texture2D(2, 2);
        texture.SetPixels(new[] { Color.white, Color.white, Color.white, Color.white }); texture.Apply();
        var data = new TerrainData { heightmapResolution = 65, alphamapResolution = 64,
            size = new Vector3(64, 10, 64) };
        data.terrainLayers = new[] { new TerrainLayer { diffuseTexture = texture }, new TerrainLayer { diffuseTexture = texture },
            new TerrainLayer { diffuseTexture = texture } };
        var heights = new float[65, 65];
        for (int z = 0; z < 65; ++z) for (int x = 0; x < 65; ++x) heights[z, x] = .5f;
        data.SetHeights(0, 0, heights);
        var weights = new float[64, 64, 3];
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) {
            weights[z, x, 0] = 1 - initialTargetWeight - initialThirdWeight;
            weights[z, x, 1] = initialTargetWeight; weights[z, x, 2] = initialThirdWeight;
        }
        data.SetAlphamaps(0, 0, weights);
        weights = data.GetAlphamaps(0, 0, 64, 64);
        data.SetDetailResolution(64, 8);
        data.detailPrototypes = new[] { new DetailPrototype { prototypeTexture = texture,
            renderMode = DetailRenderMode.GrassBillboard, minWidth = 1, maxWidth = 1, minHeight = 1, maxHeight = 1 } };
        var density = new int[64, 64];
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) density[z, x] = 4;
        data.SetDetailLayer(0, 0, 0, density);
        GameObject source = GameObject.CreatePrimitive(PrimitiveType.Cube);
        GameObject prefab = PrefabUtility.SaveAsPrefabAsset(source, "Assets/terrain-channel-tree.prefab");
        UnityEngine.Object.DestroyImmediate(source);
        data.treePrototypes = new[] { new TreePrototype { prefab = prefab } };
        data.SetTreeInstances(new[] {
            new TreeInstance { position = new Vector3(.5f, .5f, .5f), widthScale = 1, heightScale = 1, color = Color.white, lightmapColor = Color.white },
            new TreeInstance { position = new Vector3(.5f, .5f, .9f), widthScale = 1, heightScale = 1, color = Color.white, lightmapColor = Color.white } }, false);
        var terrain = Terrain.CreateTerrainGameObject(data).GetComponent<Terrain>();
        terrain.transform.position = new Vector3(-32, 100, -32); terrain.Flush();
        var network = new ERRoadNetwork();
        var road = network.CreateRoad("channel-probe", new ERRoadType { roadTypeName = "channel-probe", roadWidth = 6 },
            new[] { new Vector3(-24, 106, 0), new Vector3(0, 106, 0), new Vector3(24, 106, 0) });
        if (road == null) throw new InvalidDataException("Road creation failed");
        road.SetTerrainDeformation(true); road.SetRandomBumpiness(0, 0, 15, 35);
        road.SetSplatmap(true, 1, 0, 0, opacity);
        for (int i = 0; i < 3; ++i) { road.SetIndent(3, i); road.SetSurrounding(5, i); }
        road.Refresh();
        var sample = new Sample { splats = splats, trees = trees, details = details, opacity = opacity,
            initialTargetWeight = weights[32, 32, 1],
            treesBefore = data.treeInstanceCount, detailBefore = Sum(data.GetDetailLayer(0, 0, 64, 64, 0)) };
        TreeInstance[] originalTrees = data.treeInstances;
        int[,] originalDetails = data.GetDetailLayer(0, 0, 64, 64, 0);
        network.BuildRoadNetwork(splats, trees, details);
        float[,,] built = data.GetAlphamaps(0, 0, 64, 64);
        sample.centerWeight = built[32, 32, 1]; sample.farWeight = built[60, 32, 1];
        sample.builtCenterLayers = new[] { built[32, 32, 0], built[32, 32, 1], built[32, 32, 2] };
        sample.treesBuilt = data.treeInstanceCount; sample.detailBuilt = Sum(data.GetDetailLayer(0, 0, 64, 64, 0));
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x)
            if (Mathf.Abs(built[z, x, 1] - weights[z, x, 1]) > .0001f) ++sample.changedSplatSamples;
        network.RestoreRoadNetwork();
        sample.treesRestored = data.treeInstanceCount; sample.detailRestored = Sum(data.GetDetailLayer(0, 0, 64, 64, 0));
        TreeInstance[] restoredTrees = data.treeInstances;
        sample.treeBeforeFacts = Array.ConvertAll(originalTrees, TreeFacts);
        sample.treeRestoredFacts = Array.ConvertAll(restoredTrees, TreeFacts);
        for (int i = 0; i < Math.Min(originalTrees.Length, restoredTrees.Length); ++i) {
            TreeInstance a = originalTrees[i], b = restoredTrees[i];
            if (a.position != b.position || a.prototypeIndex != b.prototypeIndex || a.rotation != b.rotation ||
                a.widthScale != b.widthScale || a.heightScale != b.heightScale ||
                !a.color.Equals(b.color) || !a.lightmapColor.Equals(b.lightmapColor)) ++sample.treeRestoreMismatches;
        }
        int[,] restoredDetails = data.GetDetailLayer(0, 0, 64, 64, 0);
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x)
            if (originalDetails[z, x] != restoredDetails[z, x]) ++sample.detailRestoreMismatches;
        float[,,] restored = data.GetAlphamaps(0, 0, 64, 64);
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) for (int c = 0; c < 3; ++c)
            sample.maximumRestoreError = Mathf.Max(sample.maximumRestoreError, Mathf.Abs(restored[z, x, c] - weights[z, x, c]));
        return sample;
    }
    private static int Sum(int[,] values) { int sum = 0; foreach (int value in values) sum += value; return sum; }
    private static string TreeFacts(TreeInstance t)
    {
        return string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "position={0:R},{1:R},{2:R};prototype={3};rotation={4:R};scale={5:R},{6:R};color={7};lightmap={8}",
            t.position.x, t.position.y, t.position.z, t.prototypeIndex, t.rotation, t.widthScale, t.heightScale,
            t.color, t.lightmapColor);
    }
}
