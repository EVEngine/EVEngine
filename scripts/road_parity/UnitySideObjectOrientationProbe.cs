using System.Collections;
using System.Globalization;
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

// Independent public-SDK orientation experiment; no assembly implementation inspection.
public static class UnitySideObjectOrientationProbe
{
    [Serializable] public class Sample {
        public string name, error, perspectiveFrameSha256, topFrameSha256;
        public float x, y, yaw, measuredLength;
        public float roadWidth=6, resolution, actualResolution, actualWidth;
        public int seed=12345;
        public bool seedApplied;
        public float[] splineStrengths;
        public string[] markerFacts;
        public int position, align;
        public bool relativeToCenter;
        public Vector3 sourceScale=new Vector3(2,3,4);
        public Vector3[] initialCenterPoints, preBuildCenterPoints;
        public Vector3[] markers, actualMarkers, leftPoints, rightPoints, centerPoints, points, rotations, forwards, ups;
    }
    [Serializable] public class Report {
        public string schema="eve.tests.easyroads-side-orientation";
        public int version=1;
        public string probeSha256, assemblySha256, unityVersion, frameAdapterSha256;
        public Sample[] samples;
    }
    public static void Run()
    {
        string[] args=Environment.GetCommandLineArgs();
        int index=Array.IndexOf(args,"-sideOrientationOutput");
        if(index<0 || index+1>=args.Length) throw new ArgumentException("-sideOrientationOutput required");
        var samples=new List<Sample>();
        foreach(string shape in new[]{"grade","curve"})
            for(int align=0;align<=3;++align) samples.Add(new Sample {
                name=shape+"-align"+align, align=align,
                markers=shape=="grade" ? new[]{Vector3.zero,new Vector3(0,3,15),new Vector3(0,6,30)}
                    : new[]{Vector3.zero,new Vector3(6,0,15),new Vector3(0,0,30)}
            });
        int frameIndex=Array.IndexOf(args,"-sideOrientationFramePrefix");
        string prefix=frameIndex<0 ? null : args[frameIndex+1];
        foreach(Sample sample in samples) {
            try { Measure(sample,prefix); }
            catch(Exception error) { sample.error=error.ToString(); Debug.Log("SIDE_OBJECT_ORIENTATION_FAILED "+sample.name+" "+error); }
        }
        File.WriteAllText(args[index+1],JsonUtility.ToJson(new Report {
            probeSha256=Hash(Path.Combine(Application.dataPath,"Editor/UnitySideObjectOrientationProbe.cs")),
            assemblySha256=Hash(typeof(SideObject).Assembly.Location),unityVersion=Application.unityVersion,
            frameAdapterSha256=Hash(Path.Combine(Application.dataPath,"Editor/UnitySideObjectFrameCapture.cs")),
            samples=samples.ToArray()
        },true));
        Debug.Log("SIDE_OBJECT_ORIENTATION_PROBE_COMPLETE samples="+samples.Count);
    }
    public static void RunBoundary()
    {
        string[] args=Environment.GetCommandLineArgs();
        int index=Array.IndexOf(args,"-sideBoundaryOutput");
        if(index<0 || index+1>=args.Length) throw new ArgumentException("-sideBoundaryOutput required");
        var samples=new List<Sample>();
        foreach(float width in new[]{2f,6f,12f})
            foreach(float resolution in new[]{1f,2f,4f}) samples.Add(new Sample {
                name="width"+width+"-resolution"+resolution, align=2, roadWidth=width, resolution=resolution,
                markers=new[]{Vector3.zero,new Vector3(6,0,15),new Vector3(0,0,30)}
            });
        foreach(int seed in new[]{12345,17}) samples.Add(new Sample {
            name="repeat-seed"+seed, align=2, roadWidth=6, resolution=2, seed=seed,
            markers=new[]{Vector3.zero,new Vector3(6,0,15),new Vector3(0,0,30)}
        });
        foreach(float zScale in new[]{1f,8f}) samples.Add(new Sample {
            name="source-z"+zScale, align=2, roadWidth=6, resolution=2,
            sourceScale=new Vector3(2,3,zScale),
            markers=new[]{Vector3.zero,new Vector3(6,0,15),new Vector3(0,0,30)}
        });
        foreach(Sample sample in samples) {
            var previous=UnityEngine.Random.state;
            try { UnityEngine.Random.InitState(sample.seed); sample.seedApplied=true; Measure(sample,null); }
            catch(Exception error) { sample.error=error.ToString(); Debug.Log("SIDE_OBJECT_BOUNDARY_FAILED "+sample.name+" "+error); }
            finally { UnityEngine.Random.state=previous; }
        }
        File.WriteAllText(args[index+1],JsonUtility.ToJson(new Report {
            schema="eve.tests.easyroads-side-boundary",
            probeSha256=Hash(Path.Combine(Application.dataPath,"Editor/UnitySideObjectOrientationProbe.cs")),
            assemblySha256=Hash(typeof(SideObject).Assembly.Location),unityVersion=Application.unityVersion,
            samples=samples.ToArray()
        },true));
        Debug.Log("SIDE_OBJECT_BOUNDARY_PROBE_COMPLETE samples="+samples.Count);
    }
    private static void Measure(Sample sample,string prefix)
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var network = new ERRoadNetwork();
        var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        cube.transform.localScale=sample.sourceScale;
        const string path="Assets/offset-source.prefab";
        GameObject prefab=PrefabUtility.SaveAsPrefabAsset(cube,path);
        UnityEngine.Object.DestroyImmediate(cube);
        var template=ScriptableObject.CreateInstance<SideObject>();
        template.name="offset-cube"; template.id=710033;
        template.sourceObject=prefab; template.gameobjectGUID=AssetDatabase.AssetPathToGUID(path);
        template.goPath=path; template.objectType=0; template.m_distance=5;
        template.averageDistance=true; template.combine=false; template.combineInstantiated=false;
        template.xPosition=sample.x; template.yPosition=sample.y; template.position=sample.position;
        template.relativeToCenter=sample.relativeToCenter; template.yRotation=sample.yaw; template.align=sample.align;
        FieldInfo owner=network.roadNetwork.GetType().GetFields(BindingFlags.Public|BindingFlags.Instance)
            .Single(field=>field.FieldType==typeof(List<SideObject>));
        ((List<SideObject>)owner.GetValue(network.roadNetwork)).Add(template);
        ERRoad road=network.CreateRoad("offset-road",new ERRoadType {roadTypeName="offset-road",roadWidth=sample.roadWidth},
            sample.markers);
        if(sample.resolution>0) { road.SetResolution(sample.resolution,false); road.SetAngleThreshold(45,false); }
        road.SetTerrainDeformation(false); road.SetRandomBumpiness(0,0,15,35);
        sample.initialCenterPoints=road.GetSplinePointsCenter().ToArray();
        ERSORoadExt ext=ERSORoadExt.CreateInstance(template); ext.active=true; ext.markerActive=true;
        road.SetSideObjects(new List<ERSORoadExt>{ext},template.id);
        road.roadScript.soData.Add(new ERSORoad(template));
        network.SynchSideObjects(false); road.SetMarkerSideObjects();
        foreach (ERMarkerExt marker in road.roadScript.markersExt) {
            if(marker.soData==null) marker.soData=new List<ERSOMarkerExt>();
            if(!marker.soData.Any(value=>value.id==template.id)) marker.soData.Add(ERSOMarkerExt.CreateInstance(template,true));
        }
        road.SideObjectSetActive(template,true);
        for(int i=0;i<3;++i) road.SideObjectMarkerSetActive(template,i,true,ERRoadSide.Left,false);
        network.UpdateSideObjects(); network.roadNetwork.buildSOinEditMode=true;
        road.roadScript.rebuildSos=true; road.Refresh(); network.roadNetwork.UpdateSideObjectsInScene();
        sample.preBuildCenterPoints=road.GetSplinePointsCenter().ToArray();
        network.BuildRoadNetwork(false,false,false,new[]{road});
        MeshRenderer[] renderers=UnityEngine.Object.FindObjectsByType<ERSideObjectInstance>(FindObjectsSortMode.None)
            .SelectMany(value=>value.GetComponentsInChildren<MeshRenderer>(true)).OrderBy(value=>value.transform.position.z).ToArray();
        if(renderers.Length<2 || renderers.Length>64) throw new InvalidOperationException(sample.name+" renderer count="+renderers.Length);
        if(sample.align==2 && prefix!=null) {
            string framePrefix=prefix+"-"+sample.name;
            UnitySideObjectFrameCapture.Capture(framePrefix,renderers.ToList());
            sample.perspectiveFrameSha256=Hash(framePrefix+"-perspective.png");
            sample.topFrameSha256=Hash(framePrefix+"-top.png");
        }
        var facts=new List<string>();
        CollectFacts(road.roadScript,"road",facts,0);
        for(int i=0;i<road.roadScript.markersExt.Count;++i) CollectFacts(road.roadScript.markersExt[i],"marker"+i,facts,0);
        sample.markerFacts=facts.ToArray();
        sample.actualWidth=road.GetWidth(); sample.actualResolution=road.GetResolution();
        sample.measuredLength=road.GetLength();
        sample.actualMarkers=road.GetMarkerPositions();
        sample.splineStrengths=Enumerable.Range(0,road.GetMarkerCount()).Select(i=>road.GetSplineStrength(i)).ToArray();
        sample.leftPoints=road.GetSplinePointsLeftSide();
        sample.rightPoints=road.GetSplinePointsRightSide();
        sample.centerPoints=road.GetSplinePointsCenter();
        sample.forwards=renderers.Select(value=>value.transform.forward).ToArray();
        sample.ups=renderers.Select(value=>value.transform.up).ToArray();
        sample.points=renderers.Select(value=>value.transform.position).ToArray();
        sample.rotations=renderers.Select(value=>value.transform.eulerAngles).ToArray();
        Debug.Log("SIDE_OBJECT_ORIENTATION_SAMPLE " + sample.name + " first=" + sample.points[0].ToString("F6"));
    }
    private static void CollectFacts(object value, string prefix, List<string> facts, int depth)
    {
        if (value == null || depth > 4 || facts.Count >= 1500) return;
        if (value is Vector3 point) { facts.Add(prefix + "=" + point.ToString("F9")); return; }
        if (value is IEnumerable nested && !(value is string))
        {
            int index = 0;
            foreach (object element in nested)
            {
                if (index == 8) break;
                CollectFacts(element, prefix + "/" + index, facts, depth + 1);
                ++index;
            }
            return;
        }
        foreach (FieldInfo field in value.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public))
        {
            string name = field.Name.ToLowerInvariant();
            if (depth == 0) facts.Add(prefix + "/fieldType/" + field.Name + "=" + field.FieldType.FullName);
            object candidate = field.GetValue(value);
            if (candidate != null && field.FieldType.Namespace != null &&
                field.FieldType.Namespace.StartsWith("EasyRoads3Dv3") && !(candidate is UnityEngine.Object))
                CollectFacts(candidate, prefix + "/" + field.Name, facts, depth + 1);
            if (!(name.Contains("marker") || name.Contains("direction") || name.Contains("control") ||
                  name.Contains("spline") || name.Contains("circular") || name.Contains("position") || name.Contains("strength") ||
                  name.StartsWith("cp") || name.StartsWith("dp") || name == "pivotp" ||
                  (name.Length == 2 && name[0] == 'p' && char.IsDigit(name[1])) || name.EndsWith("circle") ||
                  name.Contains("random") || name == "tvalues" || name == "oldcp")) continue;
            object item = field.GetValue(value);
            string path = prefix + "/" + field.Name;
            if (item is Vector3 vector) facts.Add(path + "=" + vector.ToString("F9"));
            else if (item is float scalar) facts.Add(path + "=" + scalar.ToString("R", CultureInfo.InvariantCulture));
            else if (item is int || item is bool || (item != null && item.GetType().IsEnum)) facts.Add(path + "=" + item);
            else if (item is IEnumerable array)
            {
                int index = 0;
                foreach (object element in array)
                {
                    if (index == (name=="tvalues" ? 512 : 8)) break;
                    if (element is Vector3 position) facts.Add(path + "/" + index + "=" + position.ToString("F9"));
                    else if (element is float parameter) facts.Add(path + "/" + index + "=" + parameter.ToString("R", CultureInfo.InvariantCulture));
                    else CollectFacts(element, path + "/" + index, facts, depth + 1);
                    ++index;
                }
            }
        }
    }

    private static string Hash(string path) {
        using(var hash=SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(File.ReadAllBytes(path))).Replace("-", "").ToLowerInvariant();
    }
}
