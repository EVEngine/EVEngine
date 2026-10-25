using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using EasyRoads3Dv3;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Independent reference generation from author inputs; never imports native baked meshes.
public static class UnityComponentCapture {
    [Serializable]
    public class SourceMesh {
        public Vector3[] positions, normals;
        public Vector2[] uvs;
        public int[] indices;
    }
    [Serializable]
    public class Facility {
        public Vector2[] profile;
        public float      x, y;
        public bool       capStart, capEnd, automatic;
        public SourceMesh middle, start, end;
    }
    [Serializable]
    public class Input {
        public string caseId, authorSha256;
        public Vector3[] markers;
        public Vector2[] shape;
        public float width;
        public bool  closed;
        public float[] strengths, rolls, widths;
        public Facility[] facilities;
    }
    [Serializable]
    public class Evidence {
        public string caseId, inputSha256, authorSha256, adapterSha256, referenceSha256, unityVersion, error;
        public int    triangles, facilityTriangles;
        public string[] meshFacts;
    }
    public static void Run() {
        string[] args = Environment.GetCommandLineArgs();
        int i         = Array.IndexOf(args, "-componentRoot");
        if (i < 0 || i + 1 >= args.Length)
            throw new ArgumentException("-componentRoot required");
        if (QualitySettings.activeColorSpace != ColorSpace.Gamma)
            throw new InvalidDataException("Gamma project required");
        foreach (string directory in Directory.GetDirectories(args[i + 1]).OrderBy(x => x)) {
            string inputPath = Path.Combine(directory, "unity-input.json");
            if (!File.Exists(inputPath))
                continue;
            Input input    = JsonUtility.FromJson<Input>(File.ReadAllText(inputPath));
            var   evidence = new Evidence { caseId        = input.caseId,
                                          inputSha256   = Hash(inputPath),
                                          authorSha256  = input.authorSha256,
                                          adapterSha256 = Hash(
                                              Path.Combine(Application.dataPath, "Editor/UnityComponentCapture.cs")),
                                          referenceSha256 = Hash(typeof(ERRoad).Assembly.Location),
                                          unityVersion    = Application.unityVersion };
            try {
                Generate(directory, input, evidence);
            } catch (Exception error) {
                evidence.error = error.ToString();
                Debug.LogError("COMPONENT_REFERENCE_FAILED " + input.caseId + " " + error);
            }
            File.WriteAllText(Path.Combine(directory, "unity-evidence.json"), JsonUtility.ToJson(evidence, true));
        }
        Debug.Log("COMPONENT_REFERENCE_COMPLETE");
    }
    private static void Generate(string directory, Input input, Evidence evidence) {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var network   = new ERRoadNetwork();
        var templates = new List<SideObject>();
        int id        = 910000;
        foreach (Facility facility in input.facilities) {
            var template                 = ScriptableObject.CreateInstance<SideObject>();
            template.id                  = ++id;
            template.name                = "component-" + id;
            template.position            = 0;
            template.relativeToCenter    = true;
            template.xPosition           = facility.x;
            template.yPosition           = facility.y;
            template.align               = 2;
            template.combine             = false;
            template.combineInstantiated = false;
            template.snapToTerrain       = false;
            template.material            = new Material(Shader.Find("Unlit/Color"));
            template.startEndCaps        = facility.capStart || facility.capEnd;
            template.autoGenerate        = false;
            template.minSectionLength    = 0;
            template.maxVertices         = 65000;
            if (facility.middle != null && facility.middle.positions != null && facility.middle.positions.Length > 0) {
                template.objectType      = 2;
                template.sourceObject    = Prefab(facility.middle, "middle-" + id);
                template.goPath          = AssetDatabase.GetAssetPath(template.sourceObject);
                template.gameobjectGUID  = AssetDatabase.AssetPathToGUID(template.goPath);
                template.m_distance      = 1;
                template.averageDistance = facility.automatic;
                if (facility.start != null && facility.start.positions.Length > 0) {
                    template.startObject         = Prefab(facility.start, "start-" + id);
                    template.startPath           = AssetDatabase.GetAssetPath(template.startObject);
                    template.gameobjectStartGUID = AssetDatabase.AssetPathToGUID(template.startPath);
                    template.startSection        = true;
                }
                if (facility.end != null && facility.end.positions.Length > 0) {
                    template.endObject         = Prefab(facility.end, "end-" + id);
                    template.endPath           = AssetDatabase.GetAssetPath(template.endObject);
                    template.gameobjectEndGUID = AssetDatabase.AssetPathToGUID(template.endPath);
                    template.endSection        = true;
                }
            } else {
                template.objectType       = 1;
                template.nodeList         = new List<Vector2>(facility.profile);
                template.nodeListMirrored = new List<Vector2>(facility.profile);
                template.hardEdge         = facility.profile
                                        .Select(
                                            _ => true)
                                        .ToList();
                template.hardEdgeMirrored = new List<bool>(template.hardEdge);
                template.uvs              = facility.profile.Select(p => p.x).ToList();
                template.uvsMirrored      = new List<float>(template.uvs);
                template.uvDistances =
                    facility.profile
                        .Select((p, index) => index == 0 ? 0f : Vector2.Distance(p, facility.profile[index - 1]))
                        .ToList();
                template.totalDistance = template.uvDistances.Sum();
                template.snapList      = facility.profile
                                        .Select(
                                            _ => false)
                                        .ToList();
                template.snapWeightList = facility.profile
                                              .Select(
                                                  _ => 0f)
                                              .ToList();
                template.colorList = facility.profile
                                         .Select(
                                             _ => Color.white)
                                         .ToList();
                template.startCapTris         = new List<int> { 0, 1, 2, 0, 2, 3 };
                template.startCapTrisMirrored = new List<int>(template.startCapTris);
                template.startCapUVs          = facility.profile.ToList();
                template.endCapUVs            = facility.profile.ToList();
            }
            if (template.sourceObject != null) {
                var sourceMesh = template.sourceObject.GetComponent<MeshFilter>().sharedMesh;
                template.meshObjects.Add(new ERMesh(template.sourceObject, template, 0f,
                                                    template.sourceObject.transform, Vector3.one, sourceMesh,
                                                    new Material(Shader.Find("Unlit/Color")), 0f, 1f, false));
                template.minMiddleZ      = 0;
                template.maxMiddleZ      = 1;
                template.middleZDistance = 1;
                template.totalZDistance  = 1;
            }
            if (template.sourceObject == null) {
                // The public cache constructor requires a Mesh even for nodeList shapes.
                // This hidden carrier is never rendered; generated bounds follow the authored profile.
                var controller                                  = GameObject.CreatePrimitive(PrimitiveType.Cube);
                controller.name                                 = "shape-input-cache-carrier";
                controller.GetComponent<MeshRenderer>().enabled = false;
                template.meshObjects.Add(new ERMesh(controller, template, 0, controller.transform, Vector3.one,
                                                    controller.GetComponent<MeshFilter>().sharedMesh, template.material,
                                                    0, 1, false));
            }
            FieldInfo owner = network.roadNetwork.GetType()
                                  .GetFields(BindingFlags.Public | BindingFlags.Instance)
                                  .Single(field => field.FieldType == typeof(List<SideObject>));
            ((List<SideObject>)owner.GetValue(network.roadNetwork)).Add(template);
            templates.Add(template);
        }
        ERRoad road = network.CreateRoad(input.caseId,
                                         new ERRoadType { roadTypeName = "component", roadWidth = input.width,
                                                          roadShape     = new List<Vector2>(input.shape),
                                                          roadShapeUVs  = input.shape.Select(p => p.x).ToList(),
                                                          roadShapeUVs2 = input.shape.Select(p => p.x).ToList(),
                                                          hardEdge      = input.shape
                                                                         .Select(
                                                                             _ => true)
                                                                         .ToList() },
                                         input.markers);
        road.SetTerrainDeformation(false);
        road.SetRandomBumpiness(0, 0, 15, 35);
        road.SetResolution(.375f, false);
        road.SetAngleThreshold(4, false);
        for (int m = 0; m < input.markers.Length; ++m) {
            road.SetSplineStrength(m, input.strengths[m], false);
            road.SetWidth(input.width * input.widths[m], m);
            road.SetMarkerTilting(input.rolls[m], m);
            road.SetRoadShapeNodes(m, input.shape);
        }
        road.Refresh();
        foreach (SideObject template in templates) {
            ERSORoadExt ext  = ERSORoadExt.CreateInstance(template);
            ext.active       = true;
            ext.markerActive = true;
            road.SetSideObjects(new List<ERSORoadExt> { ext }, template.id);
            road.roadScript.soData.Add(new ERSORoad(template));
            ext.sourceObject = template.sourceObject;
            network.SynchSideObjects(false);
            road.SetMarkerSideObjects();
            foreach (ERMarkerExt marker in road.roadScript.markersExt) {
                if (marker.soData == null)
                    marker.soData = new List<ERSOMarkerExt>();
                if (!marker.soData.Any(s => s.id == template.id))
                    marker.soData.Add(ERSOMarkerExt.CreateInstance(template, true));
                marker.soData.First(s => s.id == template.id).nodeList = new List<Vector2>(template.nodeList);
            }
            bool activationChanged = road.SideObjectSetActive(template, true);
            Debug.Log("COMPONENT_ACTIVATION changed=" + activationChanged +
                      " active=" + road.GetSideObjectActiveState(template));
            if (!road.GetSideObjectActiveState(template))
                throw new InvalidDataException("Facility remained inactive");
            for (int m = 0; m < input.markers.Length; ++m) {
                bool markerChanged = road.SideObjectMarkerSetActive(template, m, true, ERRoadSide.Left, false);
                Debug.Log("COMPONENT_MARKER changed=" + markerChanged +
                          " active=" + road.GetSideObjectMarkerActiveState(template, m));
                if (!road.GetSideObjectMarkerActiveState(template, m))
                    throw new InvalidDataException("Marker remained inactive");
            }
        }
        network.UpdateSideObjects();
        network.roadNetwork.buildSOinEditMode = true;
        road.roadScript.rebuildSos            = true;
        road.Refresh();
        network.roadNetwork.UpdateSideObjectsInScene();
        network.BuildRoadNetwork(false, false, false, new[] { road });
        network.HideWhiteSurfaces(true);
        var facilities = new HashSet<MeshRenderer>(
            UnityEngine.Object.FindObjectsByType<ERSideObjectInstance>(FindObjectsSortMode.None)
                .SelectMany(s => s.GetComponentsInChildren<MeshRenderer>(true)));
        Material asphalt  = Material("road-color.jpg", "road-normal.jpg");
        Material concrete = Solid(new Color(.95f, .73f, .30f));
        var      facts    = new List<string>();
        foreach (MeshRenderer renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(
                     FindObjectsSortMode.None)) {
            if (!renderer.enabled || !renderer.gameObject.activeInHierarchy)
                continue;
            if (renderer.sharedMaterials.Any(m => m != null && m.name == "surfaceMaterial")) {
                renderer.enabled = false;
                continue;
            }
            MeshFilter mesh = renderer.GetComponent<MeshFilter>();
            if (mesh == null || mesh.sharedMesh == null)
                continue;
            bool facility  = facilities.Contains(renderer);
            int  triangles = mesh.sharedMesh.triangles.Length / 3;
            evidence.triangles += triangles;
            if (facility)
                evidence.facilityTriangles += triangles;
            renderer.sharedMaterials =
                Enumerable.Repeat(facility ? concrete : asphalt, Math.Max(1, renderer.sharedMaterials.Length))
                    .ToArray();
            renderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            facts.Add(renderer.name + " triangles=" + triangles + " facility=" + facility +
                      " bounds=" + renderer.bounds);
        }
        if (evidence.triangles == 0 || (input.facilities.Length > 0 && evidence.facilityTriangles == 0))
            throw new InvalidDataException("Missing generated road or facility meshes");
        evidence.meshFacts     = facts.ToArray();
        Camera camera          = new GameObject("ComponentParityCamera").AddComponent<Camera>();
        camera.clearFlags      = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color(.3f, .3f, .3f, 1);
        camera.fieldOfView     = 42;
        camera.nearClipPlane   = .1f;
        camera.farClipPlane    = 250;
        camera.allowHDR = camera.allowMSAA = false;
        Capture(camera, new Vector3(32, 18, -32), Vector3.up, Path.Combine(directory, "unity-perspective.png"));
        Capture(camera, new Vector3(0, 64, 0), Vector3.forward, Path.Combine(directory, "unity-top.png"));
        foreach (MeshRenderer renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
            renderer.enabled = facilities.Contains(renderer);
        if (facilities.Count > 0) {
            Capture(camera, new Vector3(32, 18, -32), Vector3.up,
                    Path.Combine(directory, "unity-facility-perspective.png"));
            Capture(camera, new Vector3(0, 64, 0), Vector3.forward, Path.Combine(directory, "unity-facility-top.png"));
        }
        Debug.Log("COMPONENT_REFERENCE_CASE " + input.caseId + " triangles=" + evidence.triangles +
                  " facility=" + evidence.facilityTriangles);
    }
    private static GameObject Prefab(SourceMesh input, string name) {
        const string folder = "Assets/ComponentParitySources";
        Directory.CreateDirectory(folder);
        var mesh =
            new Mesh { vertices = input.positions, normals = input.normals, uv = input.uvs, triangles = input.indices };
        mesh.RecalculateBounds();
        AssetDatabase.CreateAsset(mesh, AssetDatabase.GenerateUniqueAssetPath(folder + "/" + name + ".asset"));
        var source                                   = new GameObject(name);
        source.AddComponent<MeshFilter>().sharedMesh = mesh;
        source.AddComponent<MeshRenderer>();
        GameObject prefab = PrefabUtility.SaveAsPrefabAsset(source, folder + "/" + name + ".prefab");
        UnityEngine.Object.DestroyImmediate(source);
        return prefab;
    }
    private static Material Material(string color, string normal) {
        Shader shader = Shader.Find("RoadParity/AsphaltCommonV1");
        if (shader == null)
            throw new InvalidDataException("Missing shared shader");
        var material = new Material(shader);
        material.SetTexture("roadColor", Texture(color));
        material.SetTexture("roadNormal", Texture(normal));
        return material;
    }
    private static Texture2D Texture(string name) {
        string path     = "Assets/RoadMaterialParity/" + name;
        var    importer = (TextureImporter)AssetImporter.GetAtPath(path);
        if (importer == null)
            throw new InvalidDataException("Missing texture " + path);
        importer.textureType        = TextureImporterType.Default;
        importer.sRGBTexture        = false;
        importer.mipmapEnabled      = false;
        importer.textureCompression = TextureImporterCompression.Uncompressed;
        importer.filterMode         = FilterMode.Bilinear;
        importer.wrapMode           = TextureWrapMode.Clamp;
        importer.SaveAndReimport();
        return AssetDatabase.LoadAssetAtPath<Texture2D>(path);
    }
    private static Material Solid(Color color) {
        var material = new Material(Shader.Find("RoadParity/AsphaltCommonV1"));
        var albedo   = new Texture2D(4, 4, TextureFormat.RGB24, false, true);
        albedo.SetPixels(Enumerable.Repeat(color, 16).ToArray());
        albedo.Apply();
        var normal = new Texture2D(4, 4, TextureFormat.RGB24, false, true);
        normal.SetPixels(Enumerable.Repeat(new Color(128 / 255f, 128 / 255f, 1), 16).ToArray());
        normal.Apply();
        material.SetTexture("roadColor", albedo);
        material.SetTexture("roadNormal", normal);
        return material;
    }
    private static void Capture(Camera camera, Vector3 eye, Vector3 up, string path) {
        camera.transform.position = eye;
        camera.transform.LookAt(new Vector3(0, 2, 0), up);
        camera.aspect          = 1280f / 720;
        RenderTexture rt       = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGB32);
        RenderTexture previous = RenderTexture.active;
        var           image    = new Texture2D(1280, 720, TextureFormat.RGB24, false);
        try {
            camera.targetTexture = rt;
            camera.Render();
            RenderTexture.active = rt;
            image.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0);
            image.Apply();
            File.WriteAllBytes(path, image.EncodeToPNG());
        } finally {
            camera.targetTexture = null;
            RenderTexture.active = previous;
            UnityEngine.Object.DestroyImmediate(image);
            RenderTexture.ReleaseTemporary(rt);
        }
    }
    private static string Hash(string path) {
        using (var h = SHA256.Create()) return BitConverter.ToString(h.ComputeHash(File.ReadAllBytes(path)))
            .Replace("-", "")
            .ToLowerInvariant();
    }
}
