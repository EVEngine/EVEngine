using System;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Run the supplied runtimeScript through Unity's real Play lifecycle, on cloned terrain data.
[InitializeOnLoad]
public static class UnityBundledRuntimeCapture {
    const string Key = "EVRoadBundledRuntime";
    static UnityBundledRuntimeCapture() {
        if (SessionState.GetBool(Key, false))
            EditorApplication.update += Tick;
    }
    public static void Run() {
        var args      = Environment.GetCommandLineArgs();
        int rootIndex = Array.IndexOf(args, "-exportRoot"), sceneIndex = Array.IndexOf(args, "-exampleName");
        if (rootIndex < 0 || sceneIndex < 0)
            throw new ArgumentException("-exportRoot and -exampleName required");
        string name = args[sceneIndex + 1];
        if (name != "scene runtime - existing road network" && name != "scene runtime - new road network")
            throw new ArgumentException("Not a bundled runtime example");
        string path = "Assets/EasyRoads3D scenes/" + name + ".unity";
        EditorSceneManager.OpenScene(path, OpenSceneMode.Single);
        foreach (var terrain in Terrain.activeTerrains) {
            terrain.terrainData = UnityEngine.Object.Instantiate(terrain.terrainData);
            var collider        = terrain.GetComponent<TerrainCollider>();
            if (collider != null)
                collider.terrainData = terrain.terrainData;
        }
        SessionState.SetString(Key + "Path", path);
        SessionState.SetString(Key + "Name", name);
        SessionState.SetString(Key + "Root", args[rootIndex + 1]);
        SessionState.SetBool(Key, true);
        EditorApplication.EnterPlaymode();
    }
    static void Tick() {
        if (!EditorApplication.isPlaying || Time.frameCount < 30)
            return;
        SessionState.SetBool(Key, false);
        EditorApplication.update -= Tick;
        try {
            Time.timeScale = 0;
            string name    = SessionState.GetString(Key + "Name", "");
            string output  = Path.Combine(SessionState.GetString(Key + "Root", ""), name + " (play)");
            if (UnityEngine.Object.FindObjectsByType<EasyRoads3Dv3.ERModularRoad>(FindObjectsSortMode.None).Length == 0)
                throw new InvalidDataException("Official runtime script did not generate roads");
            UnityBundledSceneExport.ExportCurrent(name, SessionState.GetString(Key + "Path", ""), output);
            File.WriteAllText(Path.Combine(output, "workflow.json"),
                              "{\"schema\":\"eve.tests.bundled-play-workflow\",\"version\":1,\"frame\":" +
                                  Time.frameCount + ",\"officialRuntimeScript\":true,\"terrainDataCloned\":true}");
            Debug.Log("BUNDLED_RUNTIME_COMPLETE " + name);
            EditorApplication.Exit(0);
        } catch (Exception error) {
            Debug.LogException(error);
            EditorApplication.Exit(1);
        }
    }
}
