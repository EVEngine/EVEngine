using System;
using System.Collections.Generic;
using System.Linq;
using EasyRoads3Dv3;
using UnityEngine;

// Reference-only adapter pinned by the SDK assembly SHA in UnityJunctionCapture.
// Calls the supplied public SDK generators; contains no copied mesh algorithm.
public static class UnityJunctionAuthoring {
    public static ERConnection Ring(ERRoadNetwork network, ERConnection connection, float radius, float width) {
        var ring = connection.prefabScript.roundaboutScript;
        if (ring == null || ring.connections.Count != 4)
            throw new InvalidOperationException("Expected four-arm ring");
        ring.roundAboutRadius = radius;
        ring.roundaboutWidth  = width;
        ring.OODODCOQOD();
        int segments = ring.mainCenterPoints.Count - 1;
        if (segments < 8 || segments % 8 != 0)
            throw new InvalidOperationException("Ring cannot place four exact diagonal ports");
        for (int i = 0; i < 4; ++i) {
            ring.connections[i].centerInt     = segments / 8 + i * segments / 4;
            ring.connections[i].prevCenterInt = ring.connections[i].centerInt;
        }
        network.Refresh();
        foreach (var point in ring.mainCenterPoints)
            if (Mathf.Abs(new Vector2(point.x, point.z).magnitude - radius) > .001f)
                throw new InvalidOperationException("Ring radius not generated");
        foreach (var point in ring.mainLeftPoints)
            if (Mathf.Abs(new Vector2(point.x, point.z).magnitude - radius - width / 2) > .001f)
                throw new InvalidOperationException("Ring width not generated");
        foreach (var point in ring.mainRightPoints)
            if (Mathf.Abs(new Vector2(point.x, point.z).magnitude - radius + width / 2) > .001f)
                throw new InvalidOperationException("Ring inner radius not generated");
        Debug.Log("JUNCTION_RING_GENERATED radius=" + radius + " width=" + width + " segments=" + segments);
        return new ERConnection(connection.gameObject, connection.name);
    }
    public static void Walk(ERRoadNetwork network, ERRoad road, UnityJunctionCapture.Road spec) {
        float width = spec.curbWidth + spec.sidewalkWidth;
        if (width <= 0)
            return;
        var sw =
            ERSideWalk.Upgrade(network.roadNetwork.sidewalks, width, spec.curbHeight, spec.curbWidth, false, 0, 0,
                               false, new List<float> { 0, 0, .2f, .2f, 1 }, new Material(Shader.Find("Unlit/Color")));
        sw.shape            = new List<Vector2> { new Vector2(0, 0), new Vector2(0, spec.curbHeight),
                                                  new Vector2(spec.curbWidth, spec.curbHeight),
                                                  new Vector2(spec.curbWidth, spec.sidewalkHeight),
                                                  new Vector2(width, spec.sidewalkHeight) };
        sw.shapePercentages = sw.shape.Select(p => p.x / width).ToList();
        sw.doConnectionTri  = sw.shape
                                 .Select(
                                     _ => true)
                                 .ToList();
        road.SetSidewalk(sw, ERRoadSide.Both, true);
        road.roadScript.rt.roadWidth = spec.width;
        road.SetWidth(spec.width);
        road.Refresh();
        var left  = new List<Vector3>();
        var right = new List<Vector3>();
        if (road.roadScript.soSplinePointsLeft.Count != road.roadScript.soSplinePointsRight.Count)
            throw new InvalidOperationException("Mismatched SDK edge samples");
        for (int i = 0; i < road.roadScript.soSplinePointsLeft.Count; ++i) {
            Vector3 l = road.roadScript.soSplinePointsLeft[i], r = road.roadScript.soSplinePointsRight[i];
            Vector3 c = (l + r) * .5f, axis = (l - r).normalized;
            left.Add(c + axis * spec.width * .5f);
            right.Add(c - axis * spec.width * .5f);
        }
        int middle = left.Count / 2;
        for (int side = -1; side <= 1; side += 2) {
            var instance  = new ERSideWalkInstance(sw, 0, road.roadScript.totalDistance, Vector3.zero, Vector3.zero,
                                                   road.roadScript, side == -1 ? "Left" : "Right");
            var positions = new List<Vector3>();
            var perps     = new List<Vector3>();
            var indexes   = new List<int>();
            var distances = new List<float>();
            ERSideWalkVecs.ODCOOOCCQO(road.roadScript, sw, sw.shape, sw.doConnectionTri, sw.sidewalkUVs,
                                      side < 0 ? left : right, side < 0 ? right : left, side, instance.swObject, 0,
                                      false, false, ref positions, ref perps, ref indexes, ref distances);
            var mesh = instance.swObject.GetComponent<MeshFilter>().sharedMesh;
            if (mesh == null || mesh.triangles.Length == 0)
                throw new InvalidOperationException("Empty SDK sidewalk");
            Vector3 center =
                (road.roadScript.soSplinePointsLeft[middle] + road.roadScript.soSplinePointsRight[middle]) * .5f;
            Vector3 lateral =
                (road.roadScript.soSplinePointsLeft[middle] - road.roadScript.soSplinePointsRight[middle]).normalized;
            Vector3 forward       = Vector3.Cross(lateral, Vector3.up).normalized;
            var     middleSection = mesh.triangles.Distinct()
                                    .Select(i => instance.swObject.transform.TransformPoint(mesh.vertices[i]))
                                    .Where(p => Mathf.Abs(Vector3.Dot(p - center, forward)) < .5f)
                                    .ToArray();
            if (middleSection.Length == 0)
                throw new InvalidOperationException("No generated sidewalk middle section");
            float inner = middleSection.Min(p => Mathf.Abs(Vector3.Dot(p - center, lateral)));
            float outer = middleSection.Max(p => Mathf.Abs(Vector3.Dot(p - center, lateral)));
            if (Mathf.Abs(inner - spec.width * .5f) > .005f || Mathf.Abs(outer - spec.width * .5f - width) > .005f)
                throw new InvalidOperationException(
                    "Generated sidewalk does not meet authored road edge or width: " + inner + "," + outer +
                    " expected=" + spec.width * .5f + "," + (spec.width * .5f + width) + " center=" + center +
                    " basis=" + lateral);
            Debug.Log("JUNCTION_WALK_GENERATED road=" + spec.id + " side=" + side +
                      " triangles=" + mesh.triangles.Length / 3 + " bounds=" + mesh.bounds);
        }
    }
}
