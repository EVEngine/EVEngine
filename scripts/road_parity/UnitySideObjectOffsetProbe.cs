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

// Independent public-API experiment; preserves the existing spacing/frame adapter identity.
public static class UnitySideObjectOffsetProbe
{
    [Serializable] public class Sample {
        public string name;
        public float x, y, yaw;
        public int position, align;
        public bool relativeToCenter;
        public Vector3[] points, rotations;
    }
    [Serializable] public class Report {
        public string schema = "eve.tests.easyroads-side-offsets";
        public int version = 1;
        public string probeSha256, assemblySha256, unityVersion;
        public string frameAdapterSha256, perspectiveFrameSha256, topFrameSha256;
        public Sample[] samples;
    }
    public static void Run()
    {
        string[] args = Environment.GetCommandLineArgs();
        int output = Array.IndexOf(args, "-sideOffsetOutput");
        if (output < 0 || output + 1 >= args.Length) throw new ArgumentException("-sideOffsetOutput required");
        var samples = new[] {
            new Sample { name="baseline" }, new Sample { name="x2", x=2 },
            new Sample { name="y1", y=1 }, new Sample { name="position1", position=1 },
            new Sample { name="position2", position=2 },
            new Sample { name="center-x2", relativeToCenter=true, x=2 },
            new Sample { name="yaw45", yaw=45 }, new Sample { name="align1", align=1 }
        };
        int frameIndex=Array.IndexOf(args,"-sideOffsetFramePrefix");
        string prefix=frameIndex < 0 ? null : args[frameIndex+1];
        foreach (Sample sample in samples) Measure(sample, prefix);
        File.WriteAllText(args[output+1], JsonUtility.ToJson(new Report {
            probeSha256=Hash(Path.Combine(Application.dataPath,"Editor/UnitySideObjectOffsetProbe.cs")),
            assemblySha256=Hash(typeof(SideObject).Assembly.Location), unityVersion=Application.unityVersion,
            samples=samples,
            frameAdapterSha256=Hash(Path.Combine(Application.dataPath,"Editor/UnitySideObjectFrameCapture.cs")),
            perspectiveFrameSha256=prefix==null ? null : Hash(prefix+"-perspective.png"),
            topFrameSha256=prefix==null ? null : Hash(prefix+"-top.png")
        }, true));
        Debug.Log("SIDE_OBJECT_OFFSET_PROBE_COMPLETE samples=" + samples.Length);
    }
    private static void Measure(Sample sample, string prefix)
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var network = new ERRoadNetwork();
        var cube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        cube.transform.localScale=new Vector3(2,3,4);
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
        ERRoad road=network.CreateRoad("offset-road",new ERRoadType {roadTypeName="offset-road",roadWidth=6},
            new[] {Vector3.zero,new Vector3(0,0,15),new Vector3(0,0,30)});
        road.SetTerrainDeformation(false); road.SetRandomBumpiness(0,0,15,35);
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
        network.BuildRoadNetwork(false,false,false,new[]{road});
        MeshRenderer[] renderers=UnityEngine.Object.FindObjectsByType<ERSideObjectInstance>(FindObjectsSortMode.None)
            .SelectMany(value=>value.GetComponentsInChildren<MeshRenderer>(true)).OrderBy(value=>value.transform.position.z).ToArray();
        if(renderers.Length!=7) throw new InvalidOperationException(sample.name+" renderer count="+renderers.Length);
        if (sample.name=="yaw45" && prefix!=null)
            UnitySideObjectFrameCapture.Capture(prefix,renderers.ToList());
        sample.points=renderers.Select(value=>value.transform.position).ToArray();
        sample.rotations=renderers.Select(value=>value.transform.eulerAngles).ToArray();
        Debug.Log("SIDE_OBJECT_OFFSET_SAMPLE " + sample.name + " first=" + sample.points[0].ToString("F6"));
    }
    private static string Hash(string path) {
        using(var hash=SHA256.Create()) return BitConverter.ToString(hash.ComputeHash(File.ReadAllBytes(path))).Replace("-", "").ToLowerInvariant();
    }
}
