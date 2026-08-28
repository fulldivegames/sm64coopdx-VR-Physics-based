#!/usr/bin/env python3
"""Convert the approved Sonic Shoes OBJ into compact SM64 display lists."""

from __future__ import annotations

import argparse
from pathlib import Path


COLORS = {
    "red": (248, 38, 45, 255),
    "white": (250, 249, 242, 255),
    "gold": (235, 183, 31, 255),
    "outsole": (55, 51, 49, 255),
}


def load_obj(path: Path):
    vertices = []
    triangles = []
    material = "white"
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("v "):
            _, x, y, z = line.split()[:4]
            vertices.append((float(x), float(y), float(z)))
        elif line.startswith("usemtl "):
            material = line.split(maxsplit=1)[1]
        elif line.startswith("f "):
            indices = [int(piece.split("/")[0]) - 1 for piece in line.split()[1:]]
            for index in range(1, len(indices) - 1):
                triangles.append((material, (indices[0], indices[index], indices[index + 1])))
    return vertices, triangles


def transform_pair(vertex):
    x, y, z = vertex
    return round(x * 1.55), round(y * 1.55), round(z * 1.55)


def transform_foot(vertex, center_x):
    x, y, z = vertex
    # Mario's foot joint points along local +X. Put the heel at that joint,
    # keep the sole below it, and preserve each shoe's outward-facing buckle.
    return (
        round((z + 18.0) * 1.55),
        round((y - 18.0) * 1.55),
        round((x - center_x) * 1.55),
    )


def build_batches(vertices, triangles, transform):
    batches = []
    current_vertices = []
    current_lookup = {}
    current_triangles = []

    def flush():
        nonlocal current_vertices, current_lookup, current_triangles
        if current_triangles:
            batches.append((current_vertices, current_triangles))
        current_vertices = []
        current_lookup = {}
        current_triangles = []

    for material, triangle in triangles:
        keys = [(index, material) for index in triangle]
        missing = sum(key not in current_lookup for key in keys)
        if current_triangles and len(current_vertices) + missing > 32:
            flush()
        converted = []
        for key, source_index in zip(keys, triangle):
            if key not in current_lookup:
                current_lookup[key] = len(current_vertices)
                current_vertices.append((transform(vertices[source_index]), COLORS.get(material, COLORS["white"])))
            converted.append(current_lookup[key])
        current_triangles.append(tuple(converted))
    flush()
    return batches


def emit_mesh(name, batches):
    lines = []
    for batch_index, (vertices, _) in enumerate(batches):
        lines.append(f"static const Vtx {name}_vtx_{batch_index}[] = {{")
        for (x, y, z), (r, g, b, a) in vertices:
            lines.append(
                "    {{{%4d, %4d, %4d}, 0, {0, 0}, {%d, %d, %d, %d}}},"
                % (x, y, z, r, g, b, a)
            )
        lines.append("};")
        lines.append("")

    lines.append(f"const Gfx {name}[] = {{")
    lines.append("    gsDPPipeSync(),")
    lines.append("    gsSPClearGeometryMode(G_LIGHTING | G_TEXTURE_GEN),")
    lines.append("    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),")
    for batch_index, (vertices, triangles) in enumerate(batches):
        lines.append(f"    gsSPVertex({name}_vtx_{batch_index}, {len(vertices)}, 0),")
        index = 0
        while index + 1 < len(triangles):
            a = triangles[index]
            b = triangles[index + 1]
            lines.append(
                "    gsSP2Triangles(%d, %d, %d, 0, %d, %d, %d, 0)," % (*a, *b)
            )
            index += 2
        if index < len(triangles):
            a = triangles[index]
            lines.append("    gsSP1Triangle(%d, %d, %d, 0)," % a)
    lines.append("    gsSPSetGeometryMode(G_LIGHTING),")
    lines.append("    gsSPEndDisplayList(),")
    lines.append("};")
    lines.append("")
    return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("obj", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    vertices, triangles = load_obj(args.obj)

    left = [(material, tri) for material, tri in triangles if sum(vertices[i][0] for i in tri) / 3.0 < 0.0]
    right = [(material, tri) for material, tri in triangles if sum(vertices[i][0] for i in tri) / 3.0 >= 0.0]

    lines = [
        "// Generated from the user-approved low-poly Sonic Shoes concept.",
        "// Keep the pair pickup and the two foot-local meshes visually identical.",
        "",
    ]
    lines += emit_mesh("vr_sonic_shoes_pair_dl", build_batches(vertices, triangles, transform_pair))
    lines += emit_mesh("vr_sonic_shoe_left_dl", build_batches(vertices, left, lambda vertex: transform_foot(vertex, -16.5)))
    lines += emit_mesh("vr_sonic_shoe_right_dl", build_batches(vertices, right, lambda vertex: transform_foot(vertex, 16.5)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
