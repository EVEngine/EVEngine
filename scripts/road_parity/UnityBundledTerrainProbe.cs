using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

/** Read public properties of the original terrain material without saving or changing it. */
public static class UnityBundledTerrainProbe {
    public static void Run() {
        EditorSceneManager.OpenScene("Assets/EasyRoads3D scenes/scene main.unity", OpenSceneMode.Single);
        foreach (var terrain in Terrain.activeTerrains) {
            var material = terrain.materialTemplate;
            Debug.Log("BUNDLED_TERRAIN_SHADER " + material.shader.name);
            for (int i = 0; i < ShaderUtil.GetPropertyCount(material.shader); ++i) {
                string name = ShaderUtil.GetPropertyName(material.shader, i);
                var    type = ShaderUtil.GetPropertyType(material.shader, i);
                if (type == ShaderUtil.ShaderPropertyType.Color)
                    Debug.Log("BUNDLED_TERRAIN_PROPERTY " + name + "=" + material.GetColor(name));
                if (type == ShaderUtil.ShaderPropertyType.Float || type == ShaderUtil.ShaderPropertyType.Range)
                    Debug.Log("BUNDLED_TERRAIN_PROPERTY " + name + "=" + material.GetFloat(name));
            }
            foreach (var layer in terrain.terrainData.terrainLayers)
                Debug.Log("BUNDLED_TERRAIN_LAYER " + layer.name + " diffuseMin=" + layer.diffuseRemapMin +
                          " diffuseMax=" + layer.diffuseRemapMax + " smooth=" + layer.smoothness);
        }
    }
}
