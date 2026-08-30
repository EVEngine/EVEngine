#!/usr/bin/env python3
"""Cross-module dependency report for src/modules.

Scans #include directives and reports the module dependency graph, its
topological layering and any dependency cycles. See
docs/dev/模块编排与裁剪架构.md for how the output is interpreted.

    python3 scripts/module_depgraph.py            # graph + layers
    python3 scripts/module_depgraph.py --cycles   # cycles only
    python3 scripts/module_depgraph.py --check    # exit 1 on unlisted back-edge
    python3 scripts/module_depgraph.py --check-layers
                                                  # exit 1 when an include edge
                                                  # climbs above the LAYER the
                                                  # module declares in the
                                                  # module manifest

Only sub-path includes ("map/Map.h") count as a cross-module dependency;
a bare <map> / <thread> / <filesystem> is a standard library header.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

import check_module_manifest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULES_DIR = os.path.join(REPO, "src", "modules")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
SOURCE_EXT = (".cpp", ".cc", ".h", ".hpp")

# Upward edges that break the layering. Each entry is a known debt item; --check
# fails on anything outside this list so no new back-edge slips in, and reports
# entries that are no longer needed so the list shrinks as debt is paid off.
#
# All three original back-edges are fixed:
#   window -> graphics: the window queries the IWindowSurfaceHost capability
#     (common/WindowSurfaceHost.h) instead of holding a Graphics pointer.
#   window -> image:   Window::setIconRGBA takes raw RGBA8 pixels; the image
#     module does not cross the window boundary.
#   scene  -> graphics: the Camera3D-based picking entry points are implemented
#     in graphics/ScenePicking.cpp (graphics -> scene is a legal downward edge).
KNOWN_BACK_EDGES = set()

def declared_layers():
    """Return {module: declared LAYER} parsed from cmake/module_manifest.cmake.

    The manifest is the single source of truth for the *declared* module
    layers. The computed layers printed by the default report are derived from
    the include graph instead; this check exists to keep the two in step.
    """
    result = {}
    for declaration in check_module_manifest.parse_manifest():
        if declaration.name and declaration.layer is not None:
            result[declaration.name] = declaration.layer
    return result


def scan():
    """Return (modules, edges, header_edges).

    edges[a][b] is the set of b's headers included anywhere in a;
    header_edges[a][b] narrows that to includes in a's own headers, which
    leak into a's public API and are harder to decouple.
    """
    modules = sorted(
        d for d in os.listdir(MODULES_DIR) if os.path.isdir(os.path.join(MODULES_DIR, d))
    )
    known = set(modules)
    edges = defaultdict(lambda: defaultdict(set))
    header_edges = defaultdict(lambda: defaultdict(set))

    for module in modules:
        for dirpath, _, files in os.walk(os.path.join(MODULES_DIR, module)):
            for name in files:
                if not name.endswith(SOURCE_EXT):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, encoding="utf-8", errors="ignore") as handle:
                    text = handle.read()
                for include in INCLUDE_RE.findall(text):
                    head, _, _ = include.partition("/")
                    if not _:
                        continue
                    if head not in known or head == module:
                        continue
                    edges[module][head].add(include)
                    if name.endswith((".h", ".hpp")):
                        header_edges[module][head].add(include)
    return modules, edges, header_edges


def layers(modules, edges, ignore):
    """Longest-path depth per module once `ignore` edges are dropped."""
    graph = {a: {b for b in deps if (a, b) not in ignore} for a, deps in edges.items()}
    depth = {}

    def visit(node, stack):
        if node in depth:
            return depth[node]
        if node in stack:
            return 0
        depth[node] = 0
        depth[node] = max((visit(d, stack | {node}) + 1 for d in graph.get(node, ())), default=0)
        return depth[node]

    for module in modules:
        visit(module, set())
    return depth


def cycles(edges, ignore=frozenset()):
    graph = {a: {b for b in deps if (a, b) not in ignore} for a, deps in edges.items()}
    found = []

    def walk(start, node, path, seen):
        for nxt in sorted(graph.get(node, ())):
            if nxt == start and len(path) > 1:
                found.append(list(path))
            # Only extend through nodes ordered after `start` so each cycle is
            # reported once, from its alphabetically first member.
            elif nxt not in seen and nxt > start:
                walk(start, nxt, path + [nxt], seen | {nxt})

    for start in sorted(graph):
        walk(start, start, [start], {start})
    return found


def check_declared_layers(edges, header_edges):
    """Exit 1 when an include edge climbs above the declared module LAYER.

    The manifest declares each module's layer (cmake/module_manifest.cmake).
    A module may only include headers from modules declared at the same or a
    lower layer; an edge toward a *strictly higher* layer is a layering
    violation and is reported (KNOWN_BACK_EDGES whitelists accepted debt).
    Same-layer edges (e.g. window -> image) are allowed, matching the DEPS the
    manifest itself declares.
    """
    layers_map = declared_layers()
    missing = sorted(m for m in set(edges) | {b for deps in edges.values() for b in deps}
                     if m not in layers_map)
    if missing:
        print("warning: modules missing a LAYER declaration in the manifest: "
              + ", ".join(missing), file=sys.stderr)

    actual = {(a, b) for a, deps in edges.items() for b in deps}
    offenders = sorted(
        (a, b) for (a, b) in actual
        if a in layers_map and b in layers_map
        and layers_map[b] > layers_map[a]
        and (a, b) not in KNOWN_BACK_EDGES
    )
    stale = sorted(
        (a, b) for (a, b) in KNOWN_BACK_EDGES
        if (a, b) in actual
        and (a not in layers_map or b not in layers_map
             or layers_map[b] <= layers_map[a])
    )

    for a, b in offenders:
        marker = "*" if (a, b) in header_edges else " "
        print(f"error: layer violation {a}(L{layers_map[a]}) -> {b}(L{layers_map[b]}){marker}",
              file=sys.stderr)
    for a, b in stale:
        print(f"note: {a} -> {b} no longer violates the declared layers; "
              "drop it from KNOWN_BACK_EDGES", file=sys.stderr)

    if offenders:
        return 1
    print(f"ok: no declared-layer violations "
          f"({len(KNOWN_BACK_EDGES)} known back-edges, {len(stale)} now stale)")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cycles", action="store_true", help="report dependency cycles only")
    parser.add_argument("--check", action="store_true",
                        help="fail when a back-edge outside KNOWN_BACK_EDGES appears")
    parser.add_argument("--check-layers", action="store_true",
                        help="fail when an include edge climbs above the module's "
                             "declared manifest LAYER")
    args = parser.parse_args()

    modules, edges, header_edges = scan()

    if args.cycles:
        found = cycles(edges)
        print(f"dependency cycles: {len(found)}")
        for cycle in sorted(found, key=len):
            print("  " + " -> ".join(cycle) + " -> " + cycle[0])
        remaining = cycles(edges, KNOWN_BACK_EDGES)
        print(f"\nafter removing {len(KNOWN_BACK_EDGES)} known back-edges: {len(remaining)}")
        return 0

    depth = layers(modules, edges, KNOWN_BACK_EDGES)

    if args.check:
        actual = {(a, b) for a, deps in edges.items() for b in deps}
        # A back-edge points from a lower layer up to a higher one.
        offenders = sorted(
            (a, b) for (a, b) in actual
            if depth[a] <= depth[b] and (a, b) not in KNOWN_BACK_EDGES
        )
        stale = sorted(KNOWN_BACK_EDGES - actual)
        for a, b in offenders:
            print(f"error: layering violation {a}(L{depth[a]}) -> {b}(L{depth[b]})", file=sys.stderr)
        for a, b in stale:
            print(f"note: {a} -> {b} is fixed; drop it from KNOWN_BACK_EDGES", file=sys.stderr)
        if offenders:
            return 1
        print(f"ok: no new back-edges ({len(KNOWN_BACK_EDGES)} known, {len(stale)} now stale)")
        return 0

    if args.check_layers:
        return check_declared_layers(edges, header_edges)

    by_layer = defaultdict(list)
    for module in modules:
        by_layer[depth[module]].append(module)
    print("=== layers (back-edges removed) ===")
    for layer in sorted(by_layer):
        print(f"L{layer} ({len(by_layer[layer])}): " + ", ".join(sorted(by_layer[layer])))

    print("\n=== dependencies (* = leaks into the module's own headers) ===")
    for module in sorted(modules, key=lambda m: (-len(edges[m]), m)):
        if not edges[module]:
            continue
        deps = [b + ("*" if b in header_edges[module] else "") for b in sorted(edges[module])]
        print(f"{module:16s} L{depth[module]} ({len(deps)}): " + ", ".join(deps))

    reverse = defaultdict(set)
    for a, deps in edges.items():
        for b in deps:
            reverse[b].add(a)
    print("\n=== dependents ===")
    for module in sorted(modules, key=lambda m: (-len(reverse[m]), m)):
        if reverse[module]:
            print(f"{module:16s} ({len(reverse[module])}): " + ", ".join(sorted(reverse[module])))

    isolated = [m for m in modules if not edges[m] and not reverse[m]]
    print("\n=== isolated (trimmable with no graph impact) ===")
    print(", ".join(isolated) if isolated else "(none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
