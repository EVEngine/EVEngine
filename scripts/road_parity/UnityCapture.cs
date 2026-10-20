using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using EasyRoads3Dv3;

// Reference adapter only. Geometry comes exclusively from EasyRoads APIs.
public static class RoadParityCapture
{
    [Serializable] public class Node { public int id; public Vector3 position; }
    [Serializable] public class Road
    {
        public int id, from, to;
        public float width;
        public float resolution, angleThreshold;
        public int subSegments;
        public bool referenceZeroBumpiness;
        public bool referenceCreatedDefaults;
        public Vector3[] markers;
        public float[] widthScales, rollDegrees, strengths, rollCenters;
        public int[] controls;
    }
    [Serializable] public class Fixture
    {
        public string schema, caseId, sourceFixtureSha256, scope;
        public int schemaVersion, width, height;
        public float fovY, near, far;
        public Vector3 perspectiveEye, topEye, target;
        public Node[] nodes;
        public Road[] roads;
    }

    private static bool materialProfile;
    public static void RunMaterial() { materialProfile=true; Run(); }
    public static void Run()
    {
        string[] args = Environment.GetCommandLineArgs();
        int argument = Array.IndexOf(args, "-roadParityRoot");
        if (argument < 0 || argument + 1 >= args.Length)
            throw new ArgumentException("-roadParityRoot FIXTURES_DIRECTORY is required");
        string root = Path.GetFullPath(args[argument + 1]);
        foreach (string directory in Directory.GetDirectories(root))
        {
            string path = Path.Combine(directory, "unity-fixture.json");
            if (!File.Exists(path)) throw new InvalidDataException("Missing reference fixture: " + path);
            Fixture fixture = JsonUtility.FromJson<Fixture>(File.ReadAllText(path));
            if (fixture.schema != "eve.tests.road-parity" || fixture.schemaVersion != 1 ||
                fixture.scope != "surface-only" || fixture.nodes == null || fixture.roads == null)
                throw new InvalidDataException("Unsupported parity fixture: " + path);
            int only=Array.IndexOf(args,"-roadMaterialCase");
            if(only>=0 && fixture.caseId!=args[only+1])continue;
            Generate(directory, fixture);
        }
        Debug.Log(materialProfile?"ROAD_MATERIAL_REFERENCE_COMPLETE":"ROAD_PARITY_REFERENCE_COMPLETE");
    }

    private static void Generate(string directory, Fixture fixture)
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var network = new ERRoadNetwork();
        var connections = new Dictionary<int, ERConnection>();
        foreach (Node node in fixture.nodes)
        {
            int degree = 0;
            foreach (Road road in fixture.roads) if (road.from == node.id || road.to == node.id) degree++;
            if (degree < 3) continue;
            if (degree != 3 && degree != 4) throw new InvalidDataException("No reference port adapter for degree " + degree);
            string name = degree == 3 ? "Default T Crossing" : "Default X Crossing";
            ERConnection prefab = network.GetConnectionPrefabByName(name);
            if (prefab == null) throw new InvalidDataException("Missing EasyRoads prefab: " + name);
            ERConnection connection = network.InstantiateConnection(prefab, fixture.caseId + ".junction", node.position, Vector3.zero);
            if (connection == null) throw new InvalidDataException("Cannot instantiate EasyRoads connection");
            AlignConnection(connection, node, fixture);
            connections.Add(node.id, connection);
        }
        foreach (Road spec in fixture.roads)
        {
            Debug.Log("ROAD_PARITY_BEGIN case=" + fixture.caseId + " road=" + spec.id);
            var roadType = new ERRoadType { roadTypeName = "parity-" + spec.width, roadWidth = spec.width };
            Vector3[] markers = (Vector3[])spec.markers.Clone();
            int start = -1, end = -1;
            ERConnection startConnection, endConnection;
            if (connections.TryGetValue(spec.from, out startConnection))
            {
                start = startConnection.FindNearestConnectionIndex(markers[1]);
                if (start < 0) throw new InvalidDataException("No free start port");
                markers[0] = startConnection.GetConnectionWorldPosition(start);
            }
            if (connections.TryGetValue(spec.to, out endConnection))
            {
                end = endConnection.FindNearestConnectionIndex(markers[markers.Length - 2]);
                if (end < 0) throw new InvalidDataException("No free end port");
                markers[markers.Length - 1] = endConnection.GetConnectionWorldPosition(end);
            }
            ERRoad road = network.CreateRoad("parity-road-" + spec.id, roadType, markers);
            if (road == null) throw new InvalidDataException("EasyRoads road creation failed");
            road.SetTerrainDeformation(false);
            if(spec.referenceZeroBumpiness) road.SetRandomBumpiness(0f,0f,15f,35f);
            if(spec.referenceCreatedDefaults) {
                if(road.GetResolution()!=spec.resolution || road.roadScript.angleTreshold!=spec.angleThreshold ||
                    road.roadScript.subSegments!=spec.subSegments)
                    throw new InvalidDataException("Actual CreateRoad defaults differ from authored fixture");
            } else {
                road.SetResolution(spec.resolution>0f ? spec.resolution : .375f);
                road.SetAngleThreshold(spec.resolution>0f ? spec.angleThreshold : 4f);
                road.roadScript.subSegments=spec.subSegments>0 ? spec.subSegments : 1;
            }
            if (spec.widthScales != null && spec.widthScales.Length != 0) {
                if (spec.widthScales.Length != markers.Length) throw new InvalidDataException("Width profile count differs");
                for (int i = 0; i < markers.Length; ++i) road.SetWidth(spec.width * spec.widthScales[i], i);
            }
            if (spec.rollDegrees != null && spec.rollDegrees.Length != 0) {
                if (spec.rollDegrees.Length != markers.Length) throw new InvalidDataException("Roll profile count differs");
                for (int i = 0; i < markers.Length; ++i) {
                    if(spec.rollCenters != null && spec.rollCenters.Length != markers.Length)
                        throw new InvalidDataException("Roll center count differs");
                    road.SetMarkerTiltingCenter(spec.rollCenters == null ? .5f : spec.rollCenters[i], i);
                    road.SetMarkerTilting(spec.rollDegrees[i], i);
                }
            }
            if(spec.strengths != null && spec.strengths.Length != 0) {
                if(spec.strengths.Length != markers.Length) throw new InvalidDataException("Strength count differs");
                for(int i=0;i<markers.Length;++i) road.SetSplineStrength(i,spec.strengths[i],false);
            }
            if(spec.controls != null && spec.controls.Length != 0) {
                if(spec.controls.Length != markers.Length) throw new InvalidDataException("Control count differs");
                for(int i=0;i<markers.Length;++i) {
                    ERMarkerControlType mode=spec.controls[i]==0 ? ERMarkerControlType.Spline : spec.controls[i]==1 ? ERMarkerControlType.StraightXZ : spec.controls[i]==2 ? ERMarkerControlType.StraightXZY : ERMarkerControlType.Circular;
                    if(spec.controls[i]<0 || spec.controls[i]>3) throw new InvalidDataException("Unsupported controller");
                    road.SetMarkerControlType(i,mode);
                    if(road.GetMarkerControlType(i)!=mode) throw new InvalidDataException("Controller setter did not apply");
                }
            }
            road.Refresh();
            if (start >= 0 && !road.ConnectToStart(startConnection, start)) throw new InvalidDataException("Start port rejected");
            if (end >= 0 && !road.ConnectToEnd(endConnection, end)) throw new InvalidDataException("End port rejected");
            Debug.Log("ROAD_PARITY_CONNECTED case=" + fixture.caseId + " road=" + spec.id);
        }
        network.HideWhiteSurfaces(true);
        string sceneFolder = materialProfile ? "Assets/RoadMaterialParity/Scenes" : "Assets/RoadParityScenes";
        Directory.CreateDirectory(sceneFolder);
        string materialPath = sceneFolder + "/surface.mat";
        Material material = AssetDatabase.LoadAssetAtPath<Material>(materialPath);
        if (material == null)
        {
            Shader shader = Shader.Find("Unlit/Color");
            if (shader == null) throw new InvalidDataException("Built-in Unlit/Color is required");
            material = new Material(shader) { color = Color.magenta };
            AssetDatabase.CreateAsset(material, materialPath);
        }
        if (materialProfile) {
            if(QualitySettings.activeColorSpace!=ColorSpace.Gamma)throw new InvalidDataException("Shared road material requires Gamma display output");
            var shader=Shader.Find("RoadParity/AsphaltCommonV1");
            if(shader==null || !shader.isSupported)throw new InvalidDataException("Shared road material unavailable");
            material=new Material(shader);
            foreach(string name in new[] {"road-color","road-normal"}) {
                string asset="Assets/RoadMaterialParity/"+name+".jpg";
                var importer=(TextureImporter)AssetImporter.GetAtPath(asset);
                importer.textureType=TextureImporterType.Default;importer.sRGBTexture=false;
                importer.textureCompression=TextureImporterCompression.Uncompressed;importer.mipmapEnabled=false;
                importer.filterMode=FilterMode.Bilinear;importer.wrapMode=TextureWrapMode.Clamp;importer.SaveAndReimport();
                material.SetTexture(name=="road-color"?"roadColor":"roadNormal",AssetDatabase.LoadAssetAtPath<Texture2D>(asset));
            }
        }
        int triangles = 0;
        var referencePositions = new List<Vector3>();
        foreach (MeshRenderer renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
        {
            if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
            // The package's surfaceMaterial belongs to its terrain/intersection
            // surround fill, including underground skirts, rather than asphalt.
            if (Array.Exists(renderer.sharedMaterials, m => m != null && m.name == "surfaceMaterial"))
            {
                renderer.enabled = false;
                continue;
            }
            MeshFilter filter = renderer.GetComponent<MeshFilter>();
            if (filter == null || filter.sharedMesh == null) continue;
            foreach (Vector3 vertex in filter.sharedMesh.vertices) {
                Vector3 world = renderer.transform.TransformPoint(vertex);
                referencePositions.Add(new Vector3(world.x, world.y, -world.z));
            }
            Debug.Log("ROAD_PARITY_MESH case=" + fixture.caseId + " name=" + renderer.name + " bounds=" + renderer.bounds + " materials=" + string.Join(",", Array.ConvertAll(renderer.sharedMaterials, m => m == null ? "null" : m.name)));
            Material[] replacements = new Material[renderer.sharedMaterials.Length];
            for (int i = 0; i < replacements.Length; i++) replacements[i] = material;
            renderer.sharedMaterials = replacements;
            renderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            triangles += filter.sharedMesh.triangles.Length / 3;
        }
        if (triangles == 0) throw new InvalidDataException("Reference produced no visible mesh");
        File.WriteAllText(Path.Combine(directory, "unity-positions.json"), JsonUtility.ToJson(new ReferencePositions {
            schema = "eve.tests.road-reference-positions", schemaVersion = 1,
            fixtureSha256 = fixture.sourceFixtureSha256, coordinates = "EVEngine-RH-Y-up-reflected-Z",
            positions = referencePositions.ToArray()
        }));
        Camera camera = new GameObject("RoadParityCamera").AddComponent<Camera>();
        camera.clearFlags = CameraClearFlags.SolidColor;
        camera.backgroundColor = materialProfile ? new Color(0.3f,0.3f,0.3f,1f) : new Color(0.125f,0.125f,0.125f,1f);
        camera.fieldOfView = fixture.fovY;
        camera.nearClipPlane = fixture.near;
        camera.farClipPlane = fixture.far;
        camera.allowHDR = camera.allowMSAA = false;
        Capture(camera, fixture.perspectiveEye, fixture.target, Vector3.up, fixture, Path.Combine(directory, "unity-perspective.png"));
        Capture(camera, fixture.topEye, fixture.target, new Vector3(0f, 0f, 1f), fixture, Path.Combine(directory, "unity-top.png"));
        camera.transform.position = fixture.perspectiveEye;
        camera.transform.LookAt(fixture.target, Vector3.up);
        EditorSceneManager.SaveScene(UnityEngine.SceneManagement.SceneManager.GetActiveScene(), sceneFolder + "/" + fixture.caseId + ".unity");
        File.WriteAllText(Path.Combine(directory, "unity-evidence.json"), JsonUtility.ToJson(new Evidence {
            schema = "eve.tests.road-parity.evidence", schemaVersion = 1,
            fixtureSha256 = fixture.sourceFixtureSha256, unityVersion = Application.unityVersion,
            triangles = triangles, renderedUtc = DateTime.UtcNow.ToString("O")
        }, true));
        Debug.Log("ROAD_PARITY_REFERENCE case=" + fixture.caseId + " triangles=" + triangles);
    }

    [Serializable] private class Evidence
    {
        public string schema, fixtureSha256, unityVersion, renderedUtc;
        public int schemaVersion, triangles;
    }
    [Serializable] private class ReferencePositions {
        public string schema, fixtureSha256, coordinates;
        public int schemaVersion;
        public Vector3[] positions;
    }

    private static void AlignConnection(ERConnection connection, Node node, Fixture fixture)
    {
        Vector3[] ports = connection.GetConnectionWorldPositions();
        float best = float.PositiveInfinity, yaw = 0f;
        foreach (float angle in new[] { 0f, 90f, 180f, 270f })
        {
            float score = 0f;
            foreach (Road road in fixture.roads)
            {
                if (road.from != node.id && road.to != node.id) continue;
                Vector3 direction = ((road.from == node.id ? road.markers[1] : road.markers[road.markers.Length - 2]) - node.position).normalized;
                float nearest = -1f;
                foreach (Vector3 port in ports)
                    nearest = Mathf.Max(nearest, Vector3.Dot(direction, Quaternion.Euler(0f, angle, 0f) * (port - node.position).normalized));
                score += 1f - nearest;
            }
            if (score < best) { best = score; yaw = angle; }
        }
        if (best > .01f) throw new InvalidDataException("Reference prefab cannot align incident arms");
        connection.SetRotation(new Vector3(0f, yaw, 0f));
        Debug.Log("ROAD_PARITY_PORTS case=" + fixture.caseId + " yaw=" + yaw + " positions=" + string.Join(",", connection.GetConnectionWorldPositions()));
    }

    private static void Capture(Camera camera, Vector3 eye, Vector3 target, Vector3 up, Fixture fixture, string path)
    {
        camera.transform.position = eye;
        camera.transform.LookAt(target, up);
        RenderTexture texture = RenderTexture.GetTemporary(fixture.width, fixture.height, 24, RenderTextureFormat.ARGB32);
        RenderTexture previous = RenderTexture.active;
        var pixels = new Texture2D(fixture.width, fixture.height, TextureFormat.RGB24, false);
        try
        {
            camera.targetTexture = texture;
            camera.Render();
            RenderTexture.active = texture;
            pixels.ReadPixels(new Rect(0, 0, fixture.width, fixture.height), 0, 0);
            pixels.Apply();
            File.WriteAllBytes(path, pixels.EncodeToPNG());
        }
        finally
        {
            camera.targetTexture = null;
            RenderTexture.active = previous;
            UnityEngine.Object.DestroyImmediate(pixels);
            RenderTexture.ReleaseTemporary(texture);
        }
    }
}
