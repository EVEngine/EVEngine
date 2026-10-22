using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Open bundled examples without regenerating roads, replacing assets or saving source scenes.
public static class UnityBundledExamplesCapture {
    [Serializable]
    public class Dependency {
        public string path, sha256;
    }
    [Serializable]
    public class MeshFact {
        public string name, meshAsset;
        public int    triangles;
        public Bounds bounds;
        public string[] materials, shaders;
    }
    [Serializable]
    public class Evidence {
        public string  schema = "eve.tests.easyroads-bundled-example", scene, sourceSha256, adapterSha256, unityVersion;
        public int     version = 1, triangles, missingMaterials, unsupportedShaders;
        public string  status, perspectiveSha256, detailSha256;
        public Vector3 cameraEye, cameraTarget;
        public List<MeshFact> meshes = new List<MeshFact>();
        public string[] components;
        public List<Dependency> dependencies = new List<Dependency>();
    }
    public static void Run() {
        string[] args = Environment.GetCommandLineArgs();
        int index     = Array.IndexOf(args, "-bundledRoot");
        if (index < 0)
            throw new ArgumentException("-bundledRoot required");
        string root = args[index + 1];
        Directory.CreateDirectory(root);
        string[] scenes = AssetDatabase.FindAssets("t:Scene", new[] { "Assets/EasyRoads3D scenes" })
                              .Select(AssetDatabase.GUIDToAssetPath)
                              .OrderBy(x => x)
                              .ToArray();
        if (scenes.Length == 0)
            throw new InvalidDataException("No bundled example scenes");
        foreach (string path in scenes) {
            string output = Path.Combine(root, Path.GetFileNameWithoutExtension(path));
            Directory.CreateDirectory(output);
            var evidence =
                new Evidence { scene = path, sourceSha256 = Hash(path), unityVersion = Application.unityVersion,
                               adapterSha256 = Hash("Assets/Editor/UnityBundledExamplesCapture.cs") };
            EditorSceneManager.OpenScene(path, OpenSceneMode.Single);
            evidence.components = UnityEngine.Object.FindObjectsByType<MonoBehaviour>(FindObjectsSortMode.None)
                                      .Where(c => c != null && c.GetType().FullName.StartsWith("EasyRoads3Dv3."))
                                      .GroupBy(c => c.GetType().FullName)
                                      .Select(g => g.Key + " count=" + g.Count())
                                      .OrderBy(x => x)
                                      .ToArray();
            var renderers =
                UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None)
                    .Where(r => r.enabled && r.gameObject.activeInHierarchy && r.GetComponent<MeshFilter>() != null &&
                                r.GetComponent<MeshFilter>().sharedMesh != null)
                    .ToArray();
            foreach (var r in renderers) {
                var mesh = r.GetComponent<MeshFilter>().sharedMesh;
                evidence.triangles += mesh.triangles.Length / 3;
                evidence.missingMaterials += r.sharedMaterials.Count(m => m == null);
                evidence.unsupportedShaders +=
                    r.sharedMaterials.Count(m => m != null && (m.shader == null || !m.shader.isSupported));
                evidence.meshes.Add(new MeshFact {
                    name = r.name, meshAsset = AssetDatabase.GetAssetPath(mesh), triangles = mesh.triangles.Length / 3,
                    bounds    = r.bounds,
                    materials = r.sharedMaterials
                                    .Select(m => m == null ? "MISSING" : AssetDatabase.GetAssetPath(m) + " / " + m.name)
                                    .ToArray(),
                    shaders = r.sharedMaterials.Select(m => m == null || m.shader == null ? "MISSING" : m.shader.name)
                                  .ToArray()
                });
            }
            foreach (string dependency in AssetDatabase.GetDependencies(path, true).OrderBy(x => x))
                if (File.Exists(dependency))
                    evidence.dependencies.Add(new Dependency { path = dependency, sha256 = Hash(dependency) });
            var roads = renderers
                            .Where(r => r.GetComponentInParent<EasyRoads3Dv3.ERModularRoad>() != null &&
                                        r.sharedMaterials.Any(m => m != null && m.shader != null &&
                                                                   m.shader.name == "EasyRoads3D/ER Road"))
                            .ToArray();
            if (roads.Length == 0) {
                evidence.status = "No prebuilt road mesh: runtime example requires its own play workflow; not scored";
            } else {
                Bounds bounds = roads[0].bounds;
                foreach (var r in roads.Skip(1)) bounds.Encapsulate(r.bounds);
                foreach (var camera in UnityEngine.Object.FindObjectsByType<Camera>(FindObjectsSortMode.None))
                    camera.enabled = false;
                var capture           = new GameObject("Bundled example evidence camera").AddComponent<Camera>();
                capture.fieldOfView   = 45;
                capture.nearClipPlane = .3f;
                capture.farClipPlane  = Mathf.Max(5000, bounds.size.magnitude * 5);
                evidence.cameraTarget = bounds.center;
                float distance        = Mathf.Max(30, bounds.size.magnitude * 1.15f);
                evidence.cameraEye    = bounds.center + new Vector3(1, .85f, -1).normalized* distance;
                Capture(capture, evidence.cameraEye, evidence.cameraTarget, Path.Combine(output, "overview.png"));
                var focus = roads.OrderByDescending(r => r.GetComponent<MeshFilter>().sharedMesh.triangles.Length)
                                .First()
                                .bounds;
                Capture(capture, focus.center + new Vector3(1, .8f, -1).normalized* Mathf.Max(20, focus.size.magnitude),
                        focus.center, Path.Combine(output, "detail.png"));
                evidence.perspectiveSha256 = Hash(Path.Combine(output, "overview.png"));
                evidence.detailSha256      = Hash(Path.Combine(output, "detail.png"));
                evidence.status = "Bundled scene preserved; only evidence camera added in memory; not parity-scored";
            }
            if (Hash(path) != evidence.sourceSha256)
                throw new IOException("Bundled source scene changed");
            File.WriteAllText(Path.Combine(output, "evidence.json"), JsonUtility.ToJson(evidence, true));
            Debug.Log("BUNDLED_EXAMPLE " + path + " roads=" + roads.Length + " triangles=" + evidence.triangles);
        }
        Debug.Log("BUNDLED_EXAMPLES_COMPLETE");
    }
    private static void Capture(Camera camera, Vector3 eye, Vector3 target, string path) {
        camera.transform.position = eye;
        camera.transform.LookAt(target);
        camera.aspect = 1280f / 720;
        var rt        = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGB32);
        var previous  = RenderTexture.active;
        var texture   = new Texture2D(1280, 720, TextureFormat.RGB24, false);
        try {
            camera.targetTexture = rt;
            camera.Render();
            RenderTexture.active = rt;
            texture.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0);
            texture.Apply();
            File.WriteAllBytes(path, texture.EncodeToPNG());
        } finally {
            camera.targetTexture = null;
            RenderTexture.active = previous;
            UnityEngine.Object.DestroyImmediate(texture);
            RenderTexture.ReleaseTemporary(rt);
        }
    }
    private static string Hash(string path) {
        using (var hash = SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(File.ReadAllBytes(path)))
            .Replace("-", "")
            .ToLowerInvariant();
    }
}
