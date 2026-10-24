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
public static class UnityRoadOptionsCapture {
    [Serializable]
    public class SourceMesh {
        public Vector3[] positions, normals;
        public Vector2[] uvs;
        public int[] indices;
    }
    [Serializable]
    public class Facility {
        public Vector2[] profile;
        public float      x, y, length;
        public bool       capStart, capEnd, automatic;
        public SourceMesh middle, start, end;
    }
    [Serializable]
    public class Road {
        public string caseId, authorSha256;
        public Vector3[] markers;
        public Vector2[] shape;
        public float width;
        public bool  closed;
        public float[] strengths, rolls, widths;
        public Facility[] facilities;
    }
    [Serializable]
    public class Input {
        public string schema, caseId, authorSha256;
        public int    version;
        public Road[] roads;
    }
    [Serializable]
    public class Evidence {
        public string schema  = "eve.tests.road-options-reference";
        public int    version = 1;
        public string caseId, inputSha256, authorSha256, adapterSha256, referenceSha256, unityVersion, error;
        public int    triangles, facilityTriangles;
        public string[] meshFacts;
        public string scenePath, perspectiveSha256, topSha256;
        public string footprintSha256;
        public string[] generatedFacilities;
    }
    public static void Run() {
        string[] args = Environment.GetCommandLineArgs();
        int i         = Array.IndexOf(args, "-optionsRoot");
        if (i < 0 || i + 1 >= args.Length)
            throw new ArgumentException("-optionsRoot required");
        if (QualitySettings.activeColorSpace != ColorSpace.Gamma)
            throw new InvalidDataException("Gamma project required");
        int failures = 0;
        foreach (string directory in Directory.GetDirectories(args[i + 1]).OrderBy(x => x)) {
            string inputPath = Path.Combine(directory, "unity-input.json");
            if (!File.Exists(inputPath))
                continue;
            Input input    = JsonUtility.FromJson<Input>(File.ReadAllText(inputPath));
            var   evidence = new Evidence { caseId        = input.caseId,
                                          inputSha256   = Hash(inputPath),
                                          authorSha256  = input.authorSha256,
                                          adapterSha256 = Hash(
                                              Path.Combine(Application.dataPath, "Editor/UnityRoadOptionsCapture.cs")),
                                          referenceSha256 = Hash(typeof(ERRoad).Assembly.Location),
                                          unityVersion    = Application.unityVersion };
            try {
                if (input.schema != "eve.tests.road-options-input" || input.version != 1)
                    throw new InvalidDataException("Unknown input schema");
                Generate(directory, input, evidence);
            } catch (Exception error) {
                failures++;
                evidence.error = error.ToString();
                Debug.LogError("COMPONENT_REFERENCE_FAILED " + input.caseId + " " + error);
            }
            File.WriteAllText(Path.Combine(directory, "unity-evidence.json"), JsonUtility.ToJson(evidence, true));
        }
        if (failures > 0)
            throw new InvalidDataException("Failed option references: " + failures);
        Debug.Log("OPTIONS_REFERENCE_COMPLETE");
    }
    private static void Generate(string directory, Input spec, Evidence evidence) {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var network            = new ERRoadNetwork();
        var roads              = new List<ERRoad>();
        var expectedFacilities = new List<string>();
        int id                 = 950000;
        foreach (Road input in spec.roads) {
            var templates = new List<SideObject>();
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
                if (facility.middle != null && facility.middle.positions != null &&
                    facility.middle.positions.Length > 0) {
                    template.objectType      = 2;
                    template.sourceObject    = Prefab(facility.middle, "middle-" + id);
                    template.goPath          = AssetDatabase.GetAssetPath(template.sourceObject);
                    template.gameobjectGUID  = AssetDatabase.AssetPathToGUID(template.goPath);
                    template.m_distance      = facility.length;
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
                    template.meshObjects.Add(
                        new ERMesh(template.sourceObject, template, 0f, template.sourceObject.transform, Vector3.one,
                                   sourceMesh, new Material(Shader.Find("Unlit/Color")), 0f, facility.length, false));
                    template.minMiddleZ      = 0;
                    template.maxMiddleZ      = facility.length;
                    template.middleZDistance = facility.length;
                    template.totalZDistance  = facility.length;
                }
                if (template.sourceObject == null) {
                    // The public cache constructor requires a Mesh even for nodeList shapes.
                    // This hidden carrier is never rendered; generated bounds follow the authored profile.
                    var controller                                  = GameObject.CreatePrimitive(PrimitiveType.Cube);
                    controller.name                                 = "shape-input-cache-carrier";
                    controller.GetComponent<MeshRenderer>().enabled = false;
                    template.meshObjects.Add(new ERMesh(controller, template, 0, controller.transform, Vector3.one,
                                                        controller.GetComponent<MeshFilter>().sharedMesh,
                                                        template.material, 0, 1, false));
                }
                FieldInfo owner = network.roadNetwork.GetType()
                                      .GetFields(BindingFlags.Public | BindingFlags.Instance)
                                      .Single(field => field.FieldType == typeof(List<SideObject>));
                ((List<SideObject>)owner.GetValue(network.roadNetwork)).Add(template);
                templates.Add(template);
                expectedFacilities.Add(template.name);
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
            road.SetWidth(input.width);
            if (input.closed) {
                road.roadScript.isCustomRoad    = true;
                road.roadScript.isCustomRoadSet = true;
                road.roadScript.roadShape       = new List<Vector2>(input.shape);
                road.roadScript.roadShapeUVs    = input.shape.Select(p => p.x).ToList();
                road.roadScript.roadShapeUVs2   = input.shape.Select(p => p.x).ToList();
            }
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
            var extensions = templates
                                 .Select(template => {
                                     var ext    = ERSORoadExt.CreateInstance(template);
                                     ext.active = ext.markerActive = true;
                                     ext.sourceObject              = template.sourceObject;
                                     return ext;
                                 })
                                 .ToList();
            foreach (SideObject template in templates) {
                road.SetSideObjects(extensions, template.id);
                road.roadScript.soData.Add(new ERSORoad(template));
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
            roads.Add(road);
        }
        network.BuildRoadNetwork(false, false, false, roads.ToArray());
        network.HideWhiteSurfaces(true);
        var facilities = new HashSet<MeshRenderer>(
            UnityEngine.Object.FindObjectsByType<ERSideObjectInstance>(FindObjectsSortMode.None)
                .SelectMany(s => s.GetComponentsInChildren<MeshRenderer>(true)));
        Material asphalt  = Material("road-color.jpg", "road-normal.jpg");
        Material concrete = Solid(new Color(.95f, .73f, .30f));
        var      facts    = new List<string>();
        string   folder   = "Assets/RoadOptionsParity/" + spec.caseId;
        Directory.CreateDirectory(folder);
        AssetDatabase.Refresh();
        AssetDatabase.CreateAsset(asphalt, AssetDatabase.GenerateUniqueAssetPath(folder + "/asphalt.mat"));
        AssetDatabase.CreateAsset(concrete, AssetDatabase.GenerateUniqueAssetPath(folder + "/facility.mat"));
        var snapshots = new Dictionary<Mesh, Mesh>();
        var footprint = new Footprint();
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
            bool facility = facilities.Contains(renderer);
            if (!snapshots.TryGetValue(mesh.sharedMesh, out Mesh snapshot)) {
                snapshot = UnityEngine.Object.Instantiate(mesh.sharedMesh);
                snapshots.Add(mesh.sharedMesh, snapshot);
                AssetDatabase.CreateAsset(snapshot, AssetDatabase.GenerateUniqueAssetPath(folder + "/mesh.asset"));
            }
            mesh.sharedMesh = snapshot;
            var vertices    = snapshot.vertices;
            var indices     = snapshot.triangles;
            var target      = facility ? footprint.facility : footprint.road;
            for (int t = 0; t < indices.Length; t += 3) {
                var a = renderer.transform.TransformPoint(vertices[indices[t]]);
                var b = renderer.transform.TransformPoint(vertices[indices[t + 1]]);
                var c = renderer.transform.TransformPoint(vertices[indices[t + 2]]);
                target.Add(new Triangle { a = a, b = b, c = c });
            }
            int triangles = mesh.sharedMesh.triangles.Length / 3;
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
        if (evidence.triangles == 0 ||
            (spec.roads.Any(r => r.facilities.Length > 0) && evidence.facilityTriangles == 0))
            throw new InvalidDataException("Missing generated road or facility meshes");
        evidence.meshFacts = facts.ToArray();
        evidence.generatedFacilities =
            facilities
                .Where(r => r.enabled && r.gameObject.activeInHierarchy && r.GetComponent<MeshFilter>() != null &&
                            r.GetComponent<MeshFilter>().sharedMesh != null &&
                            r.GetComponent<MeshFilter>().sharedMesh.triangles.Length > 0)
                .Select(r => r.name)
                .Distinct()
                .OrderBy(n => n)
                .ToArray();
        foreach (string expected in expectedFacilities)
            if (!evidence.generatedFacilities.Contains(expected))
                throw new InvalidDataException("Missing generated facility: " + expected);
        Camera camera          = new GameObject("ComponentParityCamera").AddComponent<Camera>();
        camera.clearFlags      = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color(.3f, .3f, .3f, 1);
        camera.fieldOfView     = 42;
        camera.nearClipPlane   = .1f;
        camera.farClipPlane    = 250;
        camera.allowHDR = camera.allowMSAA = false;
        Capture(camera, new Vector3(48, 36, -48), Vector3.up, Path.Combine(directory, "unity-perspective.png"));
        Capture(camera, new Vector3(0, 92, 0), Vector3.forward, Path.Combine(directory, "unity-top.png"));
        File.WriteAllText(Path.Combine(directory, "unity-footprint.json"), JsonUtility.ToJson(footprint));
        evidence.footprintSha256  = Hash(Path.Combine(directory, "unity-footprint.json"));
        camera.transform.position = new Vector3(48, 36, -48);
        camera.transform.LookAt(new Vector3(0, 2, 0), Vector3.up);
        evidence.perspectiveSha256 = Hash(Path.Combine(directory, "unity-perspective.png"));
        evidence.topSha256         = Hash(Path.Combine(directory, "unity-top.png"));
        evidence.scenePath         = folder + "/reference.unity";
        AssetDatabase.SaveAssets();
        if (!EditorSceneManager.SaveScene(UnityEngine.SceneManagement.SceneManager.GetActiveScene(),
                                          evidence.scenePath))
            throw new IOException("Scene save failed");
        Debug.Log("OPTIONS_REFERENCE_CASE " + spec.caseId + " triangles=" + evidence.triangles +
                  " facilities=" + evidence.facilityTriangles);
    }
    [Serializable]
    public class Triangle {
        public Vector3 a, b, c;
    }
    [Serializable]
    public class Footprint {
        public List<Triangle> road = new List<Triangle>(), facility = new List<Triangle>();
    }
    [Serializable]
    public class ReopenEvidence {
        public string schema  = "eve.tests.road-options-scene-reopen", caseId, adapterSha256, sceneSha256, frameSha256;
        public int    version = 1, triangles;
        public bool   pixelsIdentical;
    }
    public static void VerifySaved() {
        string[] args = Environment.GetCommandLineArgs();
        int index     = Array.IndexOf(args, "-optionsRoot");
        if (index < 0 || index + 1 >= args.Length)
            throw new ArgumentException("-optionsRoot required");
        foreach (string directory in Directory.GetDirectories(args[index + 1]).OrderBy(x => x)) {
            string path = Path.Combine(directory, "unity-evidence.json");
            if (!File.Exists(path))
                continue;
            Evidence evidence = JsonUtility.FromJson<Evidence>(File.ReadAllText(path));
            if (!string.IsNullOrEmpty(evidence.error))
                throw new InvalidDataException(evidence.error);
            EditorSceneManager.OpenScene(evidence.scenePath, OpenSceneMode.Single);
            int triangles = 0;
            foreach (MeshRenderer renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(
                         FindObjectsSortMode.None)) {
                if (!renderer.enabled || !renderer.gameObject.activeInHierarchy)
                    continue;
                MeshFilter filter = renderer.GetComponent<MeshFilter>();
                if (filter == null || filter.sharedMesh == null)
                    continue;
                triangles += filter.sharedMesh.triangles.Length / 3;
            }
            if (triangles != evidence.triangles)
                throw new InvalidDataException("Reopened scene geometry differs: " + evidence.caseId);
            Camera camera = GameObject.Find("ComponentParityCamera").GetComponent<Camera>();
            string frame  = Path.Combine(directory, "unity-reopened-perspective.png");
            Capture(camera, new Vector3(48, 36, -48), Vector3.up, frame);
            string frameHash = Hash(frame);
            if (frameHash != evidence.perspectiveSha256)
                throw new InvalidDataException("Reopened frame differs: " + evidence.caseId);
            File.WriteAllText(
                Path.Combine(directory, "unity-reopen.json"),
                JsonUtility.ToJson(new ReopenEvidence { caseId        = evidence.caseId,
                                                        adapterSha256 = Hash(Path.Combine(
                                                            Application.dataPath, "Editor/UnityRoadOptionsCapture.cs")),
                                                        sceneSha256 = Hash(evidence.scenePath), frameSha256 = frameHash,
                                                        triangles = triangles, pixelsIdentical = true },
                                   true));
            Debug.Log("OPTIONS_REOPEN_CASE " + evidence.caseId + " triangles=" + triangles);
        }
        Debug.Log("OPTIONS_REOPEN_COMPLETE");
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
    private static Material Solid(Color unused) {
        const string folder = "Assets/RoadOptionsParity";
        Directory.CreateDirectory(folder);
        var material = new Material(Shader.Find("RoadParity/AsphaltCommonV1"));
        foreach (bool normal in new[] { false, true }) {
            string path  = folder + (normal ? "/flat.png" : "/rail.png");
            var    image = new Texture2D(4, 4, TextureFormat.RGB24, false, true);
            Color  c     = normal ? new Color(128 / 255f, 128 / 255f, 1) : new Color(242 / 255f, 186 / 255f, 76 / 255f);
            image.SetPixels(Enumerable.Repeat(c, 16).ToArray());
            image.Apply();
            File.WriteAllBytes(path, image.EncodeToPNG());
            UnityEngine.Object.DestroyImmediate(image);
            AssetDatabase.ImportAsset(path);
            var importer                = (TextureImporter)AssetImporter.GetAtPath(path);
            importer.textureType        = TextureImporterType.Default;
            importer.sRGBTexture        = false;
            importer.mipmapEnabled      = false;
            importer.textureCompression = TextureImporterCompression.Uncompressed;
            importer.SaveAndReimport();
            material.SetTexture(normal ? "roadNormal" : "roadColor", AssetDatabase.LoadAssetAtPath<Texture2D>(path));
        }
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
