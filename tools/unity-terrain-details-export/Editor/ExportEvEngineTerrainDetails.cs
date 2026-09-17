// Place this file under an Editor folder in the source Unity project.
// It uses only public UnityEditor/UnityEngine APIs and writes no project assets.
#if UNITY_EDITOR && UNITY_2021_2_OR_NEWER
using System;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

public static class ExportEvEngineTerrainDetails
{
    [MenuItem("Tools/EVEngine/Export Selected Terrain Details")]
    private static void ExportSelected()
    {
        Terrain terrain = Selection.activeGameObject != null
            ? Selection.activeGameObject.GetComponent<Terrain>()
            : null;
        if (terrain == null || terrain.terrainData == null)
            throw new InvalidOperationException("Select a GameObject with a Terrain component.");

        string sourcePath = AssetDatabase.GetAssetPath(terrain.terrainData);
        string destination = EditorUtility.SaveFilePanel(
            "Export EVEngine terrain details", Path.GetDirectoryName(sourcePath),
            Path.GetFileName(sourcePath) + ".eve-details", "json");
        if (string.IsNullOrEmpty(destination)) return;
        File.WriteAllText(destination, Build(terrain.terrainData), new UTF8Encoding(false));
        Debug.Log("EVEngine terrain detail sidecar written: " + destination);
    }

    /// <summary>Build a deterministic sidecar without mutating the TerrainData.</summary>
    public static string Build(TerrainData data)
    {
        var text = new StringBuilder(1024 * 1024);
        text.Append("{\"schema\":\"eve.unity-terrain-details\",\"schemaVersion\":3,\"wavingGrass\":{");
        text.Append("\"amount\":"); Number(text, data.wavingGrassAmount);
        text.Append(",\"speed\":"); Number(text, data.wavingGrassSpeed);
        text.Append(",\"strength\":"); Number(text, data.wavingGrassStrength);
        text.Append(",\"tint\":"); ColorValue(text, data.wavingGrassTint);
        text.Append("},\"prototypes\":[");
        DetailPrototype[] prototypes = data.detailPrototypes;
        for (int layer = 0; layer < prototypes.Length; ++layer)
        {
            if (layer != 0) text.Append(',');
            DetailPrototype prototype = prototypes[layer];
            string id = PrototypeId(prototype);
            text.Append("{\"prototype\":"); String(text, id);
            text.Append(",\"renderMode\":"); String(text, prototype.renderMode.ToString());
            text.Append(",\"usePrototypeMesh\":").Append(prototype.usePrototypeMesh ? "true" : "false");
            text.Append(",\"useInstancing\":").Append(prototype.useInstancing ? "true" : "false");
            text.Append(",\"minWidth\":"); Number(text, prototype.minWidth);
            text.Append(",\"maxWidth\":"); Number(text, prototype.maxWidth);
            text.Append(",\"minHeight\":"); Number(text, prototype.minHeight);
            text.Append(",\"maxHeight\":"); Number(text, prototype.maxHeight);
            text.Append(",\"noiseSeed\":").Append(prototype.noiseSeed);
            text.Append(",\"noiseSpread\":"); Number(text, prototype.noiseSpread);
            text.Append(",\"density\":"); Number(text, prototype.density);
            text.Append(",\"alignToGround\":"); Number(text, prototype.alignToGround);
            text.Append(",\"positionJitter\":"); Number(text, prototype.positionJitter);
            text.Append(",\"bendFactor\":"); Number(text, prototype.bendFactor);
            text.Append(",\"healthyColor\":"); ColorValue(text, prototype.healthyColor);
            text.Append(",\"dryColor\":"); ColorValue(text, prototype.dryColor);
#if UNITY_2022_2_OR_NEWER
            text.Append(",\"holeEdgePadding\":"); Number(text, prototype.holeEdgePadding);
            text.Append(",\"useDensityScaling\":").Append(prototype.useDensityScaling ? "true" : "false");
#else
            text.Append(",\"holeEdgePadding\":0,\"useDensityScaling\":true");
#endif
            text.Append('}');
        }
        text.Append("],\"instances\":[");
        bool first = true;
        int patches = data.detailPatchCount;
        for (int layer = 0; layer < prototypes.Length; ++layer)
        {
            string id = PrototypeId(prototypes[layer]);
            for (int patchY = 0; patchY < patches; ++patchY)
            for (int patchX = 0; patchX < patches; ++patchX)
            {
                Bounds ignored;
                DetailInstanceTransform[] instances =
                    data.ComputeDetailInstanceTransforms(patchX, patchY, layer, 1.0f, out ignored);
                foreach (DetailInstanceTransform instance in instances)
                {
                    if (!first) text.Append(',');
                    first = false;
                    float halfYaw = -instance.rotationY * 0.5f;
                    text.Append("{\"prototype\":"); String(text, id);
                    text.Append(",\"position\":["); Number(text, instance.posX); text.Append(',');
                    Number(text, instance.posY); text.Append(','); Number(text, -instance.posZ);
                    text.Append("],\"rotation\":[0,"); Number(text, Mathf.Sin(halfYaw));
                    text.Append(",0,"); Number(text, Mathf.Cos(halfYaw));
                    text.Append("],\"scale\":["); Number(text, instance.scaleXZ); text.Append(',');
                    Number(text, instance.scaleY); text.Append(','); Number(text, instance.scaleXZ);
                    text.Append("]}");
                }
            }
        }
        return text.Append("]}").ToString();
    }

    private static string PrototypeId(DetailPrototype prototype)
    {
        UnityEngine.Object source = prototype.prototype != null
            ? (UnityEngine.Object)prototype.prototype : prototype.prototypeTexture;
        string path = source != null ? AssetDatabase.GetAssetPath(source) : string.Empty;
        string guid = string.IsNullOrEmpty(path) ? string.Empty : AssetDatabase.AssetPathToGUID(path);
        if (string.IsNullOrEmpty(guid))
            throw new InvalidOperationException("Every Terrain detail prototype must reference a project asset.");
        return (prototype.prototype != null ? "unity-guid:" : "unity-texture-guid:") + guid.ToLowerInvariant();
    }

    private static void Number(StringBuilder text, float value)
    {
        if (float.IsNaN(value) || float.IsInfinity(value))
            throw new InvalidOperationException("Terrain detail transform contains a non-finite value.");
        text.Append(value.ToString("R", CultureInfo.InvariantCulture));
    }

    private static void ColorValue(StringBuilder text, Color value)
    {
        text.Append('['); Number(text, value.r); text.Append(','); Number(text, value.g);
        text.Append(','); Number(text, value.b); text.Append(','); Number(text, value.a); text.Append(']');
    }

    private static void String(StringBuilder text, string value)
    {
        text.Append('"');
        foreach (char character in value)
        {
            switch (character)
            {
                case '\\': text.Append("\\\\"); break;
                case '"': text.Append("\\\""); break;
                case '\n': text.Append("\\n"); break;
                case '\r': text.Append("\\r"); break;
                case '\t': text.Append("\\t"); break;
                default:
                    if (character < 32) text.Append("\\u").Append(((int)character).ToString("x4"));
                    else text.Append(character);
                    break;
            }
        }
        text.Append('"');
    }
}
#endif
