using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

// Isolated facility silhouette calibration. Road placement spacing is a separate contract.
public static class UnitySideObjectFrameCapture
{
    public static void Capture(string prefix, List<MeshRenderer> facilities)
    {
        if (facilities.Count == 0) throw new InvalidDataException("No generated facility geometry");
        if (QualitySettings.activeColorSpace != ColorSpace.Gamma)
            throw new InvalidDataException("This calibration requires the explicit Gamma reference project");
        Debug.Log("SIDE_OBJECT_COLOR_SPACE=" + QualitySettings.activeColorSpace);
        foreach (MeshRenderer renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
            renderer.enabled = facilities.Contains(renderer);
        Shader shader = Shader.Find("Unlit/Color");
        if (shader == null) throw new InvalidDataException("Missing Unlit/Color shader");
        var material = new Material(shader) { color = Color.magenta };
        foreach (MeshRenderer renderer in facilities) renderer.sharedMaterial = material;
        Camera camera = new GameObject("SideObjectParityCamera").AddComponent<Camera>();
        camera.clearFlags = CameraClearFlags.SolidColor;
        camera.backgroundColor = new Color(.125f, .125f, .125f, 1);
        camera.fieldOfView = 45;
        camera.nearClipPlane = .1f;
        camera.farClipPlane = 250;
        camera.allowHDR = camera.allowMSAA = false;
        Render(camera, new Vector3(24, 18, 40), Vector3.up, prefix + "-perspective.png");
        Render(camera, new Vector3(0, 50, 15), Vector3.forward, prefix + "-top.png");
        Debug.Log("SIDE_OBJECT_FRAMES_COMPLETE count=" + facilities.Count);
    }

    private static void Render(Camera camera, Vector3 eye, Vector3 up, string path)
    {
        camera.transform.position = eye;
        camera.transform.LookAt(new Vector3(0, 0, 15), up);
        camera.aspect = 640f / 480f;
        RenderTexture texture = RenderTexture.GetTemporary(640, 480, 24, RenderTextureFormat.ARGB32);
        RenderTexture previous = RenderTexture.active;
        var pixels = new Texture2D(640, 480, TextureFormat.RGB24, false);
        try
        {
            camera.targetTexture = texture;
            camera.Render();
            RenderTexture.active = texture;
            pixels.ReadPixels(new Rect(0, 0, 640, 480), 0, 0);
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
