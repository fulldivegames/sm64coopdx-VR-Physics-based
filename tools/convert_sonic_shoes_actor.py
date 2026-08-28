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
    white_run = 0
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("v "):
            _, x, y, z = line.split()[:4]
            vertices.append((float(x), float(y), float(z)))
        elif line.startswith("usemtl "):
            material = line.split(maxsplit=1)[1]
            # Each shoe has three separate white material runs: sole trim,
            # instep stripe, then the two-piece ankle cuff.  The pickup uses
            # all three, while the worn mesh intentionally omits only the
            # cuff so Mario's original boot/leg can enter the shoe cleanly.
            if material == "white":
                white_run += 1
                if white_run % 3 == 0:
                    material = "white_cuff"
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
    # Measured directly from the US-ROM Mario meshes:
    # left boot  X[-54,110] Y[-35,65] Z[-41,61]
    # right boot X[-54,110] Y[-35,64] Z[-61,40]
    # Fit the powered shoes around those complete bounds with clearance so the
    # vanilla boots sit inside them like socks. The pickup remains unchanged.
    lateral_offset = 10.0 if center_x < 0.0 else -10.0
    return (
        round((z + 20.94) * 3.34 - 60.0),
        # Mario's foot-local Y axis is inverted relative to the approved OBJ.
        # Pivot around the measured top of the cuffless shoe body so the red
        # upper remains above the boot while the outsole sits below it.  The
        # 5.45 scale already encloses the complete vanilla boot and must not be
        # guessed or resized again.
        round((20.2 - y) * 5.45 - 45.0),
        round((x - center_x) * 3.87 + lateral_offset),
    )


def build_batches(vertices, triangles, transform, reverse_winding=False):
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
                base_material = material.split("_", 1)[0]
                current_vertices.append((transform(vertices[source_index]), COLORS.get(base_material, COLORS["white"])))
            converted.append(current_lookup[key])
        if reverse_winding:
            converted[1], converted[2] = converted[2], converted[1]
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
    lines.append("    gsSPClearGeometryMode(G_LIGHTING | G_TEXTURE_GEN | G_CULL_FRONT | G_CULL_BACK),")
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
    lines.append("    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),")
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

    left = [(material, tri) for material, tri in triangles
            if material != "white_cuff" and sum(vertices[i][0] for i in tri) / 3.0 < 0.0]
    right = [(material, tri) for material, tri in triangles
             if material != "white_cuff" and sum(vertices[i][0] for i in tri) / 3.0 >= 0.0]

    lines = [
        "// Generated from the user-approved low-poly Sonic Shoes concept.",
        "// Keep the pair pickup and the two foot-local meshes visually identical.",
        "",
    ]
    lines += emit_mesh("vr_sonic_shoes_pair_dl", build_batches(vertices, triangles, transform_pair))
    # Preserve the approved OBJ winding. The display lists render both sides,
    # so every exterior surface remains visible after the foot-local transform.
    lines += emit_mesh("vr_sonic_shoe_left_dl", build_batches(vertices, left, lambda vertex: transform_foot(vertex, -16.5)))
    lines += emit_mesh("vr_sonic_shoe_right_dl", build_batches(vertices, right, lambda vertex: transform_foot(vertex, 16.5)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
