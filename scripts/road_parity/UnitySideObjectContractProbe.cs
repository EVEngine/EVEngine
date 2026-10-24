using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Globalization;
using System.Security.Cryptography;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using EasyRoads3Dv3;

// Public metadata/API observations only. Never reads IL or private method bodies.
public static class UnitySideObjectContractProbe
{
    [Serializable] public class Report
    {
        public string schema = "eve.tests.easyroads-side-object-contracts";
        public int schemaVersion = 1;
        public string sourcePose;
        public string probeSha256, frameAdapterSha256, referenceAssemblySha256, unityVersion;
        public string perspectiveFrameSha256, topFrameSha256;
        public float requestedSpacing, requestedLength, measuredRoadLength, measuredRoadDistance;
        public bool averageDistance;
        public Vector3[] centerPoints;
        public string[] publicApi;
        public string[] defaults;
        public string[] availableSideObjects;
        public string[] collectionObservations;
        public string[] generationObservations;
    }

    public static void Run()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-sideObjectContractOutput");
        if (index < 0 || index + 1 >= args.Length)
            throw new ArgumentException("-sideObjectContractOutput OUTPUT_JSON is required");
        int poseIndex = Array.IndexOf(args, "-sideObjectSourcePose");
        string sourcePose = poseIndex < 0 ? "combined" : args[poseIndex + 1];
        if (!new[] { "identity", "translated", "rotated", "scaled", "combined", "child" }.Contains(sourcePose))
            throw new ArgumentException("Unknown source pose " + sourcePose);
        int spacingIndex = Array.IndexOf(args, "-sideObjectSpacing");
        float requestedSpacing = spacingIndex < 0 ? 5 : float.Parse(args[spacingIndex + 1], CultureInfo.InvariantCulture);
        int lengthIndex = Array.IndexOf(args, "-sideObjectRoadLength");
        float requestedLength = lengthIndex < 0 ? 30 : float.Parse(args[lengthIndex + 1], CultureInfo.InvariantCulture);
        int averageIndex = Array.IndexOf(args, "-sideObjectAverageDistance");
        bool averageDistance = averageIndex < 0 || bool.Parse(args[averageIndex + 1]);
        if (!(requestedSpacing >= .25f && requestedSpacing <= 100 && requestedLength >= 1 && requestedLength <= 100))
            throw new ArgumentException("Spacing/length outside probe budget");
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var network = new ERRoadNetwork();
        var api = new List<string>();
        var defaults = new List<string>();
        foreach (Type type in new Type[] {typeof(SideObject), typeof(ERRoad), typeof(ERRoadNetwork),
                                          typeof(ERSideObjectInstance), typeof(ERSORoadExt),
                                          typeof(ERModularRoad), typeof(ERMarkerExt), typeof(ERSOMarkerExt),
                                          typeof(ERSORoad), network.roadNetwork.GetType()})
        {
            foreach (ConstructorInfo ctor in type.GetConstructors()) api.Add(type.FullName + " " + ctor);
            foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
                api.Add(type.FullName + " field " + field.FieldType.FullName + " " + field.Name);
            foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly))
                if (method.Name.All(c => c < 128) && !method.IsSpecialName)
                    api.Add(type.FullName + " method " + method);
        }
        object side = ScriptableObject.CreateInstance<SideObject>();
        foreach (FieldInfo field in typeof(SideObject).GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            object value = field.GetValue(side);
            if (value == null || value is string || value is ValueType)
                defaults.Add(field.Name + "=" + (value == null ? "null" : Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture)));
        }
        var available = new List<string>();
        var collections = new List<string>();
        foreach (FieldInfo field in network.roadNetwork.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            if (field.FieldType != typeof(List<SideObject>)) continue;
            var values = (List<SideObject>)field.GetValue(network.roadNetwork);
            collections.Add(field.Name + " count=" + (values == null ? "null" : values.Count.ToString()));
            // Identify the public data owner through the SDK getter, without invoking
            // undocumented methods or interpreting assembly implementation details.
            var candidate = ScriptableObject.CreateInstance<SideObject>();
            candidate.name = "contract-probe";
            candidate.id = 710031;
            if (values == null) field.SetValue(network.roadNetwork, values = new List<SideObject>());
            values.Add(candidate);
            SideObject[] observed = network.GetSideObjects();
            collections.Add(field.Name + " getterContainsSentinel=" +
                (observed != null && observed.Any(value => ReferenceEquals(value, candidate))));
            values.Remove(candidate);
            UnityEngine.Object.DestroyImmediate(candidate);
        }
        SideObject[] sides = network.GetSideObjects();
        if (sides != null)
            foreach (SideObject value in sides)
                available.Add(value.name + " id=" + value.id + " type=" + value.objectType);
        var generation = new List<string>();
        string perspectiveFrameHash = null, topFrameHash = null;
        float measuredRoadLength = 0, measuredRoadDistance = 0;
        Vector3[] centerPoints = null;
        try
        {
            var source = GameObject.CreatePrimitive(PrimitiveType.Cube);
            source.name = "contract-source";
            source.transform.position = new Vector3(2, 3, 4);
            source.transform.rotation = Quaternion.Euler(10, 20, 30);
            source.transform.localScale = new Vector3(2, 3, 4);
            if (sourcePose != "combined" && sourcePose != "child")
            {
                if (sourcePose != "translated") source.transform.position = Vector3.zero;
                if (sourcePose != "rotated") source.transform.rotation = Quaternion.identity;
                if (sourcePose != "scaled") source.transform.localScale = Vector3.one;
            }
            if (sourcePose == "child")
            {
                GameObject child = GameObject.CreatePrimitive(PrimitiveType.Cube);
                child.name = "contract-child";
                child.transform.SetParent(source.transform, false);
                child.transform.localPosition = new Vector3(1, .5f, 2);
                child.transform.localRotation = Quaternion.Euler(0, 35, 0);
                child.transform.localScale = new Vector3(.5f, .75f, 1.25f);
            }
            const string prefabPath = "Assets/contract-source.prefab";
            GameObject prefab = PrefabUtility.SaveAsPrefabAsset(source, prefabPath);
            UnityEngine.Object.DestroyImmediate(source);
            var template = ScriptableObject.CreateInstance<SideObject>();
            template.name = "contract-cube";
            template.id = 710032;
            template.sourceObject = prefab;
            template.gameobjectGUID = AssetDatabase.AssetPathToGUID(prefabPath);
            template.goPath = prefabPath;
            template.objectType = 0;
            template.m_distance = requestedSpacing;
            template.averageDistance = averageDistance;
            template.combine = false;
            template.combineInstantiated = false;
            FieldInfo owner = network.roadNetwork.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance)
                .Single(field => field.FieldType == typeof(List<SideObject>));
            ((List<SideObject>)owner.GetValue(network.roadNetwork)).Add(template);
            ERRoad road = network.CreateRoad("contract-road", new ERRoadType {
                roadTypeName = "contract-road", roadWidth = 6 }, new Vector3[] {
                    new Vector3(0, 0, 0), new Vector3(0, 0, requestedLength / 2), new Vector3(0, 0, requestedLength) });
            road.SetTerrainDeformation(false);
            road.SetRandomBumpiness(0, 0, 15, 35);
            ERSORoadExt roadSide = ERSORoadExt.CreateInstance(template);
            roadSide.active = true;
            roadSide.markerActive = true;
            road.SetSideObjects(new List<ERSORoadExt> { roadSide }, template.id);
            road.roadScript.soData.Add(new ERSORoad(template));
            network.SynchSideObjects(false);
            road.SetMarkerSideObjects();
            foreach (ERMarkerExt marker in road.roadScript.markersExt)
            {
                if (marker.soData == null) marker.soData = new List<ERSOMarkerExt>();
                if (!marker.soData.Any(value => value.id == template.id))
                    marker.soData.Add(ERSOMarkerExt.CreateInstance(template, true));
            }
            generation.Add("activate=" + road.SideObjectSetActive(template, true));
            for (int marker = 0; marker < 3; ++marker)
                generation.Add("marker" + marker + "=" + road.SideObjectMarkerSetActive(
                    template, marker, true, ERRoadSide.Left, false));
            network.UpdateSideObjects();
            network.roadNetwork.buildSOinEditMode = true;
            road.roadScript.rebuildSos = true;
            road.Refresh();
            network.roadNetwork.UpdateSideObjectsInScene();
            generation.Add("activeState=" + road.GetSideObjectActiveState(template));
            for (int marker = 0; marker < 3; ++marker)
                generation.Add("markerState" + marker + "=" + road.GetSideObjectMarkerActiveState(template, marker));
            generation.Add("templateId=" + template.id + " roadId=" + road.roadScript.soData[0].id +
                " extId=" + road.roadScript.soDataExt[0].id);
            network.BuildRoadNetwork(false, false, false, new ERRoad[] { road });
            measuredRoadLength = road.GetLength();
            measuredRoadDistance = road.GetDistance();
            centerPoints = road.GetSplinePointsCenter();
            var generatedRenderers = new List<MeshRenderer>();
            foreach (ERSideObjectInstance generated in UnityEngine.Object.FindObjectsByType<ERSideObjectInstance>(FindObjectsSortMode.None))
            {
                generation.Add("generatedComponent=" + generated.name + " id=" + generated.id);
                foreach (MeshRenderer renderer in generated.GetComponentsInChildren<MeshRenderer>(true))
                {
                    generatedRenderers.Add(renderer);
                    Transform pose = renderer.transform;
                    generation.Add("renderer=" + pose.name + " position=" + pose.position.ToString("F6") +
                        " localPosition=" + pose.localPosition.ToString("F6") +
                        " rotation=" + pose.eulerAngles.ToString("F6") +
                        " scale=" + pose.lossyScale.ToString("F6") +
                        " boundsCenter=" + renderer.bounds.center.ToString("F6") +
                        " boundsSize=" + renderer.bounds.size.ToString("F6"));
                }
            }
            int frameIndex = Array.IndexOf(args, "-sideObjectFramePrefix");
            if (frameIndex >= 0)
            {
                UnitySideObjectFrameCapture.Capture(args[frameIndex + 1], generatedRenderers);
                perspectiveFrameHash = Hash(args[frameIndex + 1] + "-perspective.png");
                topFrameHash = Hash(args[frameIndex + 1] + "-top.png");
            }
            generation.Add("sceneRenderers=" + UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None).Length);
            foreach (FieldInfo field in typeof(ERModularRoad).GetFields(BindingFlags.Public | BindingFlags.Instance))
                if (field.Name.StartsWith("soData", StringComparison.Ordinal))
                {
                    object data = field.GetValue(road.roadScript);
                    var list = data as ICollection;
                    generation.Add("road." + field.Name + "=" + (list == null ? Convert.ToString(data) : "count:" + list.Count));
                }
            foreach (ERMarkerExt data in road.roadScript.markersExt)
            {
                generation.Add("marker.soData=" + (data == null || data.soData == null ? "null" : data.soData.Count.ToString()));
            }
            GameObject[] instances = road.GetSideObjectInstances(template);
            Vector3[] points = road.GetSideObjectInstancePoints(template);
            generation.Add("instances=" + (instances == null ? "null" : instances.Length.ToString()));
            generation.Add("points=" + (points == null ? "null" : points.Length.ToString()));
            if (instances != null)
                foreach (GameObject instance in instances)
                    generation.Add(instance == null ? "null-instance" : instance.name +
                        " position=" + instance.transform.position.ToString("F6") +
                        " rotation=" + instance.transform.eulerAngles.ToString("F6") +
                        " scale=" + instance.transform.lossyScale.ToString("F6"));
        }
        catch (Exception error)
        {
            generation.Add("FAILED " + error);
        }
        File.WriteAllText(args[index + 1], JsonUtility.ToJson(new Report {
            sourcePose = sourcePose,
            requestedSpacing = requestedSpacing, requestedLength = requestedLength, averageDistance = averageDistance,
            measuredRoadLength = measuredRoadLength, measuredRoadDistance = measuredRoadDistance, centerPoints = centerPoints,
            probeSha256 = Hash(Path.Combine(Application.dataPath, "Editor/UnitySideObjectContractProbe.cs")),
            frameAdapterSha256 = Hash(Path.Combine(Application.dataPath, "Editor/UnitySideObjectFrameCapture.cs")),
            referenceAssemblySha256 = Hash(typeof(SideObject).Assembly.Location),
            unityVersion = Application.unityVersion,
            perspectiveFrameSha256 = perspectiveFrameHash,
            topFrameSha256 = topFrameHash,
            publicApi = api.OrderBy(v => v, StringComparer.Ordinal).ToArray(),
            defaults = defaults.OrderBy(v => v, StringComparer.Ordinal).ToArray(),
            availableSideObjects = available.ToArray(),
            collectionObservations = collections.ToArray(),
            generationObservations = generation.ToArray()
        }, true));
        Debug.Log("SIDE_OBJECT_CONTRACT_PROBE_COMPLETE");
    }

    private static string Hash(string path)
    {
        using (var hash = SHA256.Create())
            return BitConverter.ToString(hash.ComputeHash(File.ReadAllBytes(path))).Replace("-", "").ToLowerInvariant();
    }
}
