using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using EasyRoads3Dv3;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// Authoring input and resource interchange, not an implementation of the SDK generators.
public static class UnityBundledSceneExport {
    [Serializable]
    public class TextureInfo {
        public string  property, path;
        public Vector2 scale, offset;
        public bool    srgb, mipmaps;
        public int     anisotropy, filter;
    }
    [Serializable]
    public class MaterialInfo {
        public string            id, path, shader;
        public Color             color;
        public int               queue;
        public float             metallic, smoothness, normalScale, occlusion;
        public float             offsetFactor, offsetUnits;
        public List<TextureInfo> textures = new List<TextureInfo>();
    }
    [Serializable]
    public class Part {
        public string name, role, material, road;
        public float[] positions, normals, uvs, tangents;
        public int[] indices;
    }
    [Serializable]
    public class Marker {
        public Vector3 position;
        public float   strength, width, roll, pivot;
        public int     control;
        public Vector2[] shape;
    }
    [Serializable]
    public class Road {
        public string       name, id;
        public float        width, resolution, uvTiling;
        public bool         closed, flipUV;
        public List<Marker> markers = new List<Marker>();
        public Vector2[] shape;
        public float[] shapeUV;
        public int[] shapeMaterials;
    }
    [Serializable]
    public class LightInfo {
        public Vector3 forward;
        public Color   color;
        public float   intensity;
        public int     type, shadows;
    }
    [Serializable]
    public class View {
        public string  name;
        public Vector3 eye, target;
        public float   fov, near, far;
        public Color   background;
    }
    [Serializable]
    public class TerrainLayerInfo {
        public string  diffuse, normal;
        public Vector2 tileSize, tileOffset;
    }
    [Serializable]
    public class TerrainInfo {
        public string name, material, shaderName;
        public int    alphaWidth, alphaHeight;
        public float[] weights;
        public Vector3                size, origin;
        public List<TerrainLayerInfo> layers = new List<TerrainLayerInfo>();
    }
    [Serializable]
    public class Export {
        public string schema = "eve.tests.easyroads-bundled-interchange", scene, sceneSha256, adapterSha256,
                      referenceSha256;
        public int                version = 1, msaaSamples;
        public string             colorSpace, ambientMode;
        public Color              ambient, ambientIrradiance;
        public List<MaterialInfo> materials = new List<MaterialInfo>();
        public List<Part>         parts     = new List<Part>();
        public List<Road>         roads     = new List<Road>();
        public List<LightInfo>    lights    = new List<LightInfo>();
        public List<View>         views     = new List<View>();
        public List<TerrainInfo>  terrains  = new List<TerrainInfo>();
    }
    public static void Run() {
        var args  = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-exportRoot");
        if (index < 0)
            throw new ArgumentException("-exportRoot required");
        string root = args[index + 1];
        Directory.CreateDirectory(root);
        foreach (string scene in new[] { "scene main", "scene runtime - existing road network" }) {
            string path = "Assets/EasyRoads3D scenes/" + scene + ".unity";
            string dir  = Path.Combine(root, scene);
            Directory.CreateDirectory(dir);
            EditorSceneManager.OpenScene(path, OpenSceneMode.Single);
            ExportCurrent(scene, path, dir);
        }
    }
    public static void ExportCurrent(string scene, string path, string dir) {
        Directory.CreateDirectory(dir);
        var                    data     = new Export { scene           = path,
                                sceneSha256     = Hash(path),
                                adapterSha256   = Hash("Assets/Editor/UnityBundledSceneExport.cs"),
                                referenceSha256 = Hash(typeof(ERRoad).Assembly.Location),
                                colorSpace      = QualitySettings.activeColorSpace.ToString(),
                                msaaSamples     = QualitySettings.antiAliasing,
                                ambientMode     = RenderSettings.ambientMode.ToString(),
                                ambient         = RenderSettings.ambientLight };
        var                    matIds   = new Dictionary<Material, string>();
        Func<Material, string> material = m => {
            if (m == null)
                throw new InvalidDataException("Missing original material");
            if (matIds.TryGetValue(m, out var id))
                return id;
            id = "material-" + matIds.Count;
            matIds.Add(m, id);
            var info = new MaterialInfo {
                id           = id,
                path         = AssetDatabase.GetAssetPath(m),
                shader       = m.shader.name,
                queue        = m.renderQueue,
                offsetFactor = m.HasProperty("_OffsetFactor") ? m.GetFloat("_OffsetFactor") : 0,
                offsetUnits  = m.HasProperty("_OffsetUnit") ? m.GetFloat("_OffsetUnit") : 0,
                color        = m.HasProperty("_Color") ? m.color : Color.white,
                metallic     = m.HasProperty("_MainMetallicPower") ? m.GetFloat("_MainMetallicPower")
                               : m.HasProperty("_Metallic") && m.shader.name == "Standard" ? m.GetFloat("_Metallic")
                                                                                           : 0,
                smoothness   = m.HasProperty("_MainSmoothnessPower") ? m.GetFloat("_MainSmoothnessPower")
                               : m.HasProperty("_Glossiness")        ? m.GetFloat("_Glossiness")
                                                                     : .5f,
                normalScale  = m.HasProperty("_BumpScale") ? m.GetFloat("_BumpScale") : 1,
                occlusion    = m.HasProperty("_OcclusionStrength") ? m.GetFloat("_OcclusionStrength") : 1
            };
            foreach (string property in m.GetTexturePropertyNames()) {
                var tex = m.GetTexture(property);
                if (tex == null)
                    continue;
                string texturePath = AssetDatabase.GetAssetPath(tex);
                var    importer    = AssetImporter.GetAtPath(texturePath) as TextureImporter;
                info.textures.Add(new TextureInfo { property = property, path = texturePath,
                                                    scale      = m.GetTextureScale(property),
                                                    offset     = m.GetTextureOffset(property),
                                                    srgb       = importer != null && importer.sRGBTexture,
                                                    mipmaps    = importer != null && importer.mipmapEnabled,
                                                    anisotropy = tex.anisoLevel, filter = (int)tex.filterMode });
            }
            data.materials.Add(info);
            return id;
        };
        foreach (var raw in UnityEngine.Object.FindObjectsByType<ERModularRoad>(FindObjectsSortMode.None)
                     .OrderBy(r => r.name)) {
            var sdk  = new ERRoad(raw);
            var road = new Road { name           = raw.name,
                                  id             = Key(raw.transform),
                                  width          = sdk.GetWidth(),
                                  resolution     = sdk.GetResolution(),
                                  uvTiling       = raw.uvTiling,
                                  closed         = raw.closedTrack,
                                  flipUV         = raw.flipRoadUVs,
                                  shape          = raw.roadShape.ToArray(),
                                  shapeUV        = raw.roadShapeUVs.ToArray(),
                                  shapeMaterials = raw.roadShapeMaterialInts.ToArray() };
            for (int m = 0; m < sdk.GetMarkerCount(); ++m)
                road.markers.Add(new Marker { position = sdk.GetMarkerPosition(m), strength = sdk.GetSplineStrength(m),
                                              width = raw.markersExt[m].roadMarkerWidth,
                                              roll = raw.markersExt[m].rotation, pivot = sdk.GetMarkerTiltingCenter(m),
                                              control = (int)sdk.GetMarkerControlType(m),
                                              shape   = sdk.GetRoadShapeNodes(m) });
            data.roads.Add(road);
        }
        foreach (var renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None)
                     .OrderBy(r => r.name)) {
            if (!renderer.enabled || !renderer.gameObject.activeInHierarchy)
                continue;
            var filter = renderer.GetComponent<MeshFilter>();
            if (filter == null || filter.sharedMesh == null)
                continue;
            var    mesh     = filter.sharedMesh;
            var    verts    = mesh.vertices;
            var    normals  = mesh.normals;
            var    uv       = mesh.uv;
            var    tangents = mesh.tangents;
            var    road     = renderer.GetComponentInParent<ERModularRoad>();
            string role =
                road != null && renderer.sharedMaterials.Any(m => m != null && m.shader.name == "EasyRoads3D/ER Road")
                    ? "road-output"
                    : "shared-context";
            for (int sub = 0; sub < mesh.subMeshCount; ++sub) {
                var p           = new List<float>();
                var n           = new List<float>();
                var u           = new List<float>();
                var tangentData = new List<float>();
                for (int v = 0; v < verts.Length; ++v) {
                    Vector3 point = renderer.transform.TransformPoint(verts[v]);
                    Vector3 normal =
                        normals.Length > v
                            ? renderer.transform.worldToLocalMatrix.transpose.MultiplyVector(normals[v]).normalized
                            : Vector3.up;
                    p.AddRange(new[] { point.x, point.y, -point.z });
                    n.AddRange(new[] { normal.x, normal.y, -normal.z });
                    u.AddRange(v < uv.Length ? new[] { uv[v].x, uv[v].y } : new[] { 0f, 0f });
                    if (tangents.Length == verts.Length) {
                        Vector3 tangent =
                            renderer.transform.TransformVector(new Vector3(tangents[v].x, tangents[v].y, tangents[v].z))
                                .normalized;
                        float sign = renderer.transform.localToWorldMatrix.determinant < 0 ? -1 : 1;
                        tangentData.AddRange(new[] { tangent.x, tangent.y, -tangent.z, -tangents[v].w * sign });
                    }
                }
                var indices = mesh.GetTriangles(sub);
                for (int t = 0; t < indices.Length; t += 3) {
                    int temp       = indices[t + 1];
                    indices[t + 1] = indices[t + 2];
                    indices[t + 2] = temp;
                }
                data.parts.Add(new Part {
                    name = Key(renderer.transform) + "/" + sub, role = role,
                    road      = road == null ? "" : Key(road.transform),
                    material  = material(renderer.sharedMaterials[Math.Min(sub, renderer.sharedMaterials.Length - 1)]),
                    positions = p.ToArray(), normals = n.ToArray(), uvs = u.ToArray(), tangents = tangentData.ToArray(),
                    indices = indices
                });
            }
        }
        foreach (var terrain in Terrain.activeTerrains) {
            var td         = terrain.terrainData;
            int resolution = td.heightmapResolution;
            var ti =
                new TerrainInfo { name        = terrain.name,
                                  material    = "terrain-" + data.terrains.Count,
                                  shaderName  = terrain.materialTemplate == null ? "defaultTerrain"
                                                                                 : terrain.materialTemplate.shader.name,
                                  size        = td.size,
                                  origin      = terrain.transform.position,
                                  alphaWidth  = td.alphamapWidth,
                                  alphaHeight = td.alphamapHeight };
            foreach (var layer in td.terrainLayers)
                ti.layers.Add(new TerrainLayerInfo { diffuse  = AssetDatabase.GetAssetPath(layer.diffuseTexture),
                                                     normal   = AssetDatabase.GetAssetPath(layer.normalMapTexture),
                                                     tileSize = layer.tileSize, tileOffset = layer.tileOffset });
            var alpha   = td.GetAlphamaps(0, 0, td.alphamapWidth, td.alphamapHeight);
            var weights = new List<float>();
            for (int z = 0; z < td.alphamapHeight; ++z)
                for (int x = 0; x < td.alphamapWidth; ++x)
                    for (int l = 0; l < td.alphamapLayers; ++l) weights.Add(alpha[z, x, l]);
            ti.weights  = weights.ToArray();
            var p       = new List<float>();
            var n       = new List<float>();
            var u       = new List<float>();
            var indices = new List<int>();
            for (int z = 0; z < resolution; ++z)
                for (int x = 0; x < resolution; ++x) {
                    float   fx = x / (float)(resolution - 1), fz = z / (float)(resolution - 1);
                    Vector3 point =
                        terrain.transform.position + new Vector3(fx * td.size.x, td.GetHeight(x, z), fz * td.size.z);
                    Vector3 normal = td.GetInterpolatedNormal(fx, fz);
                    p.AddRange(new[] { point.x, point.y, -point.z });
                    n.AddRange(new[] { normal.x, normal.y, -normal.z });
                    u.AddRange(new[] { fx, fz });
                    if (x < resolution - 1 && z < resolution - 1) {
                        int a = z * resolution + x;
                        indices.AddRange(new[] { a, a + 1, a + resolution, a + 1, a + resolution + 1, a + resolution });
                    }
                }
            data.parts.Add(new Part { name = terrain.name, role = "terrain-resource", road = "", material = ti.material,
                                      positions = p.ToArray(), normals = n.ToArray(), uvs = u.ToArray(),
                                      indices = indices.ToArray() });
            data.terrains.Add(ti);
        }
        foreach (var light in UnityEngine.Object.FindObjectsByType<Light>(FindObjectsSortMode.None)
                     .Where(l => l.enabled))
            data.lights.Add(new LightInfo { forward = light.transform.forward, color = light.color,
                                            intensity = light.intensity, type = (int)light.type,
                                            shadows = (int)light.shadows });
        var    roads = data.parts.Where(p => p.role == "road-output").ToArray();
        Bounds all   = BoundsOf(roads);
        var    detail =
            BoundsOf(roads.Where(p => p.road == data.roads.OrderByDescending(r => r.markers.Count).First().id));
        AddView(data, "overview",
                all.center + new Vector3(1, .85f, -1).normalized* Mathf.Max(30, all.size.magnitude * 1.15f), all.center,
                all);
        AddView(data, "detail",
                detail.center + new Vector3(1, .8f, -1).normalized* Mathf.Max(20, detail.size.magnitude), detail.center,
                all);
        // The interchange uses canonical reflected coordinates; camera values are Unity world coordinates.
        foreach (var view in data.views) {
            var camera                = new GameObject("Original example comparison camera").AddComponent<Camera>();
            camera.fieldOfView        = view.fov;
            camera.nearClipPlane      = view.near;
            camera.farClipPlane       = view.far;
            camera.backgroundColor    = view.background;
            camera.transform.position = view.eye;
            camera.transform.LookAt(view.target);
            Capture(camera, Path.Combine(dir, "unity-" + view.name + ".png"));
            var allRenderers = UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None);
            var oldMaterials = allRenderers.Select(r => r.sharedMaterials).ToArray();
            var oldEnabled   = allRenderers.Select(r => r.enabled).ToArray();
            var terrains     = Terrain.activeTerrains;
            var white        = new Material(Shader.Find("Unlit/Color"));
            white.color      = Color.white;
            try {
                foreach (var terrain in terrains) terrain.enabled = false;
                for (int r = 0; r < allRenderers.Length; ++r) {
                    bool isRoad = allRenderers[r].GetComponentInParent<ERModularRoad>() != null &&
                                  oldMaterials[r].Any(m => m != null && m.shader.name == "EasyRoads3D/ER Road");
                    allRenderers[r].enabled = oldEnabled[r] && isRoad;
                    if (isRoad)
                        allRenderers[r].sharedMaterials = Enumerable.Repeat(white, oldMaterials[r].Length).ToArray();
                }
                camera.clearFlags      = CameraClearFlags.SolidColor;
                camera.backgroundColor = Color.black;
                Capture(camera, Path.Combine(dir, "unity-" + view.name + "-road-mask.png"));
            } finally {
                for (int r = 0; r < allRenderers.Length; ++r) {
                    allRenderers[r].enabled         = oldEnabled[r];
                    allRenderers[r].sharedMaterials = oldMaterials[r];
                }
                foreach (var terrain in terrains) terrain.enabled = true;
                UnityEngine.Object.DestroyImmediate(white);
            }
            UnityEngine.Object.DestroyImmediate(camera.gameObject);
        }
        var irradiance = new Color[1];
        RenderSettings.ambientProbe.Evaluate(new[] { Vector3.up }, irradiance);
        data.ambientIrradiance = irradiance[0];
        Debug.Log("BUNDLED_AMBIENT " + scene + " author=" + data.ambient + " evaluated=" + data.ambientIrradiance);
        if (Hash(path) != data.sceneSha256)
            throw new IOException("Source changed");
        File.WriteAllText(Path.Combine(dir, "scene.json"), JsonUtility.ToJson(data));
        Debug.Log("BUNDLED_EXPORT " + scene + " roads=" + data.roads.Count + " parts=" + data.parts.Count);
    }
    private static string Key(Transform transform) {
        string result = transform.name + "[" + transform.GetSiblingIndex() + "]";
        return transform.parent == null ? result : Key(transform.parent) + "/" + result;
    }
    private static Bounds BoundsOf(IEnumerable<Part> parts) {
        bool first  = true;
        var  result = new Bounds();
        foreach (var p in parts)
            for (int i = 0; i < p.positions.Length; i += 3) {
                var v = new Vector3(p.positions[i], p.positions[i + 1], -p.positions[i + 2]);
                if (first) {
                    result = new Bounds(v, Vector3.zero);
                    first  = false;
                } else
                    result.Encapsulate(v);
            }
        if (first)
            throw new InvalidDataException("No road bounds");
        return result;
    }
    private static void AddView(Export e, string name, Vector3 eye, Vector3 target, Bounds bounds) {
        e.views.Add(new View { name = name, eye = eye, target = target, fov = 45, near = .3f,
                               far        = Mathf.Max(5000, bounds.size.magnitude * 5),
                               background = new Color(.1921569f, .3019608f, .4745098f, 0) });
    }
    private static void Capture(Camera c, string path) {
        c.aspect     = 1280f / 720;
        var rt       = RenderTexture.GetTemporary(1280, 720, 24, RenderTextureFormat.ARGB32);
        var previous = RenderTexture.active;
        var image    = new Texture2D(1280, 720, TextureFormat.RGB24, false);
        try {
            c.targetTexture = rt;
            c.Render();
            RenderTexture.active = rt;
            image.ReadPixels(new Rect(0, 0, 1280, 720), 0, 0);
            image.Apply();
            File.WriteAllBytes(path, image.EncodeToPNG());
        } finally {
            c.targetTexture      = null;
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
