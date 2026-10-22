using System;
using System.IO;
using EasyRoads3Dv3;
using UnityEditor.SceneManagement;
using UnityEngine;

// Observe public build selection/order semantics; no private implementation access.
public static class UnityTerrainOverlapPolicyProbe
{
    [Serializable] public class Sample {
        public string mode;
        public float[] center;
        public float maximumRestoreError;
        public float centerSum;
    }
    [Serializable] public class Report {
        public string schema = "eve.tests.easyroads-overlap-terrain-policy";
        public int version = 1;
        public Sample[] samples;
    }
    public static void Run() {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "-terrainOverlapOutput");
        if (index < 0 || index + 1 >= args.Length) throw new ArgumentException("-terrainOverlapOutput required");
        var report = new Report { samples = new[] { Generate("all-first-created"), Generate("all-second-created"),
            Generate("selected-first-second"), Generate("selected-second-first"), Generate("first-only"), Generate("second-only"), Generate("swap-opacity"), Generate("swap-layers"),
            Generate("swap-names"), Generate("swap-orientation"), Generate("equal-opacity"), Generate("reverse-order-equal") } };
        File.WriteAllText(args[index + 1], JsonUtility.ToJson(report, true));
        Debug.Log("TERRAIN_OVERLAP_POLICY_PROBE_COMPLETE");
    }
    private static ERRoad Road(ERRoadNetwork network, string name, int layer, float opacity, bool horizontal) {
        Vector3 a = horizontal ? new Vector3(-24,106,0) : new Vector3(0,106,-24);
        Vector3 b = horizontal ? new Vector3(24,106,0) : new Vector3(0,106,24);
        var road = network.CreateRoad(name, new ERRoadType { roadTypeName=name, roadWidth=6 },
            new[] { a, new Vector3(0,106,0), b });
        if (road == null) throw new InvalidDataException("Road creation failed");
        road.SetTerrainDeformation(true); road.SetRandomBumpiness(0,0,15,35);
        road.SetSplatmap(true,layer,0,0,opacity);
        for(int i=0;i<3;++i) { road.SetIndent(3,i);road.SetSurrounding(5,i); }
        road.Refresh();
        return road;
    }
    private static Sample Generate(string mode) {
        UnityEngine.Random.InitState(1729);
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        var texture = new Texture2D(2,2); texture.SetPixels(new[] { Color.white,Color.white,Color.white,Color.white });texture.Apply();
        var data = new TerrainData { heightmapResolution=65, alphamapResolution=64, size=new Vector3(64,10,64) };
        data.terrainLayers = new[] { new TerrainLayer { diffuseTexture=texture }, new TerrainLayer { diffuseTexture=texture }, new TerrainLayer { diffuseTexture=texture } };
        var heights=new float[65,65];for(int z=0;z<65;++z)for(int x=0;x<65;++x)heights[z,x]=.5f;data.SetHeights(0,0,heights);
        var initial=new float[64,64,3];for(int z=0;z<64;++z)for(int x=0;x<64;++x)initial[z,x,0]=1;data.SetAlphamaps(0,0,initial);
        var terrain=Terrain.CreateTerrainGameObject(data).GetComponent<Terrain>();terrain.transform.position=new Vector3(-32,100,-32);terrain.Flush();
        var network=new ERRoadNetwork();ERRoad first,second;
        int firstLayer=mode=="swap-layers"?2:1, secondLayer=mode=="swap-layers"?1:2;
        float firstOpacity=mode=="swap-opacity"?.8f:.4f, secondOpacity=mode=="swap-opacity"?.4f:.8f;
        if(mode.Contains("equal"))firstOpacity=secondOpacity=.5f;
        string firstName=mode=="swap-names"?"second":"first", secondName=mode=="swap-names"?"first":"second";
        bool firstHorizontal=mode!="swap-orientation";
        if(mode=="all-second-created" || mode=="reverse-order-equal") {
            second=Road(network,secondName,secondLayer,secondOpacity,!firstHorizontal);
            first=Road(network,firstName,firstLayer,firstOpacity,firstHorizontal);
        } else {
            first=Road(network,firstName,firstLayer,firstOpacity,firstHorizontal);
            second=Road(network,secondName,secondLayer,secondOpacity,!firstHorizontal);
        }
        if(mode.StartsWith("all-") || mode.StartsWith("swap-") || mode.Contains("equal"))network.BuildRoadNetwork(true,false,false);
        else if(mode=="first-only")network.BuildRoadNetwork(true,false,false,new[] {first});
        else if(mode=="second-only")network.BuildRoadNetwork(true,false,false,new[] {second});
        else network.BuildRoadNetwork(true,false,false,mode=="selected-first-second"?new[] {first,second}:new[] {second,first});
        var built=data.GetAlphamaps(0,0,64,64);var sample=new Sample {mode=mode,center=new[] {built[32,32,0],built[32,32,1],built[32,32,2]}};
        sample.centerSum=sample.center[0]+sample.center[1]+sample.center[2];
        Debug.Log("OVERLAP_SAMPLE "+mode+" "+string.Join(",",sample.center)+" sum="+sample.centerSum);
        network.RestoreRoadNetwork();var restored=data.GetAlphamaps(0,0,64,64);
        for(int z=0;z<64;++z)for(int x=0;x<64;++x)for(int c=0;c<3;++c)sample.maximumRestoreError=Mathf.Max(sample.maximumRestoreError,Mathf.Abs(restored[z,x,c]-initial[z,x,c]));
        if(sample.maximumRestoreError>.0001f)throw new InvalidDataException("Texture restore differs");
        return sample;
    }
}
