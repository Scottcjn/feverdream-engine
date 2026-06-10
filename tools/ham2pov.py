#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""ham2pov.py — convert a HamaPatch / MegaPov .pov export into a clean POV-Ray 3.7
scene our engine can render.

HamaPatch exports MegaPov `bezier_patch { 4,4 accuracy A <16 pts> }`; POV-Ray 3.7
has the same geometry as `bicubic_patch`. We EXTRACT the patches + camera + lights
and emit a clean scene with a default texture, discarding MegaPov-specific
texture/material syntax that 3.7 won't parse.

    ham2pov.py in.pov out.pov [--usteps 3] [--color 0.8,0.8,0.9]
"""
import sys, re

def blocks(text, keyword):
    """Yield brace-balanced `keyword { ... }` blocks (returns inner+full)."""
    out = []
    for m in re.finditer(re.escape(keyword) + r"\s*\{", text):
        i = m.end() - 1            # at the '{'
        depth = 0
        for j in range(i, len(text)):
            if text[j] == '{': depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0:
                    out.append(text[m.start():j+1])
                    break
    return out

def main():
    a = sys.argv
    src, dst = a[1], a[2]
    usteps = int(a[a.index("--usteps")+1]) if "--usteps" in a else 3
    color  = a[a.index("--color")+1] if "--color" in a else "0.80,0.80,0.92"
    text = open(src, encoding="utf-8", errors="replace").read()

    patches = []
    for blk in blocks(text, "bezier_patch"):
        pts = re.findall(r"<[^>]*>", blk)
        if len(pts) >= 16:
            grid = "\n    ".join(" ".join(pts[i*4:i*4+4]) for i in range(4))
            patches.append(f"  bicubic_patch {{ type 0 flatness 0 u_steps {usteps} v_steps {usteps}\n    {grid}\n  }}")

    cams   = blocks(text, "camera")
    lights = blocks(text, "light_source")

    out = []
    out.append("#version 3.7;")
    out.append("global_settings { assumed_gamma 1.0 }")
    out.append('#include "colors.inc"')
    out.append("background { rgb <0.05,0.05,0.10> }")
    out.append(f"#default {{ texture {{ pigment {{ rgb <{color}> }} finish {{ ambient 0.3 diffuse 0.7 phong 0.5 }} }} }}")
    if cams:
        out.append(cams[0])
    else:
        out.append("camera { location <0,0,-12> look_at 0 angle 40 right x*4/3 up y }")
    if lights:
        out.extend(lights)
    else:
        out.append("light_source { <-30,40,-50> rgb 1 }")
    out.append("union {\n" + "\n".join(patches) + "\n}")

    open(dst, "w").write("\n".join(out) + "\n")
    sys.stderr.write(f">> {dst}: {len(patches)} patches, {len(lights)} lights, "
                     f"{'kept' if cams else 'default'} camera\n")

if __name__ == "__main__":
    main()
