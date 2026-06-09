#!/usr/bin/env python3
"""mdl2pov.py — native Hash Animation:Master .mdl -> POV-Ray 3.7 converter (*nix).

No Wine, no HamaPatch. Reads A:M's text .mdl directly:
  [MESH]   splines of control points. Each CP = 4 lines:
             "<flags> <isRef> <cpID>"
             then a position "x y z" (isRef=0) OR a shared-CP id (isRef=1)
             then two tangent lines ("0" or "x y z").
  [PATCHES] each patch = a flag line then "cp1 cp2 cp3 cp4 ...": 4 corner CP ids
            (cp1==cp4 => 3-point/triangle patch).

FIRST CUT: builds each patch as a flat bilinear bicubic_patch from its 4 corners
(silhouette/faceted). Tangent-based smoothing is the next pass.

    mdl2pov.py model.mdl out.pov [--color r,g,b]
"""
import sys, re

def main():
    a = sys.argv
    src, dst = a[1], a[2]
    color = a[a.index("--color")+1] if "--color" in a else "0.80,0.78,0.92"
    lines = [l.rstrip("\r\n") for l in open(src, encoding="utf-8", errors="replace")]
    n = len(lines)

    # locate sections
    def find(tag):
        for i, l in enumerate(lines):
            if l.strip() == tag: return i
        return -1
    mesh, endmesh = find("[MESH]"), find("[ENDMESH]")
    pat, endpat   = find("[PATCHES]"), find("[ENDPATCHES]")

    # --- parse CPs: id -> position (resolve isRef references) ---
    pos, ref = {}, {}
    i = mesh + 1
    hdr = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s*$")   # flags isRef cpID
    while i < endmesh:
        m = hdr.match(lines[i])
        if not m:
            i += 1; continue
        flags, isref, cpid = int(m.group(1)), int(m.group(2)), int(m.group(3))
        b = lines[i+1].strip()
        if isref == 0:
            p = b.split()
            if len(p) == 3:
                try: pos[cpid] = (float(p[0]), float(p[1]), float(p[2]))
                except ValueError: pass
        else:
            try: ref[cpid] = int(b.split()[0])
            except (ValueError, IndexError): pass
        i += 4   # each CP record is 4 lines
    # resolve references (shared CPs) to a concrete position
    def resolve(cid, seen=None):
        seen = seen or set()
        if cid in pos: return pos[cid]
        if cid in ref and cid not in seen:
            seen.add(cid); return resolve(ref[cid], seen)
        return None
    for cid in list(ref):
        r = resolve(cid)
        if r: pos[cid] = r

    # --- parse patches: lines of "cp1 cp2 cp3 cp4 ..." ---
    patches = []
    for j in range(pat+1, endpat):
        parts = lines[j].split()
        if len(parts) >= 5:                      # corner ids + index fields
            ids = parts[:4]
            if all(x.lstrip("-").isdigit() for x in ids):
                corners = [pos.get(int(x)) for x in ids]
                if all(c is not None for c in corners):
                    patches.append(corners)

    # --- emit: flat bilinear bicubic_patch per patch (first cut) ---
    def lerp(p, q, t): return tuple(p[k] + (q[k]-p[k])*t for k in range(3))
    def grid16(c):
        P00, P03, P33, P30 = c[0], c[1], c[2], c[3]   # quad-cyclic corners
        out = []
        for ui in range(4):
            u = ui/3.0
            top, bot = lerp(P00, P30, u), lerp(P03, P33, u)
            for vi in range(4):
                v = vi/3.0
                out.append(lerp(top, bot, v))
        return out

    o = ["#version 3.7;", "global_settings { assumed_gamma 1.0 }", '#include "colors.inc"',
         "background { rgb <0.05,0.05,0.10> }",
         f"#default {{ texture {{ pigment {{ rgb <{color}> }} finish {{ ambient 0.3 diffuse 0.7 phong 0.5 }} }} }}",
         "light_source { <-40,60,-80> rgb 1 }",
         "light_source { <40,20,-40> rgb <0.3,0.3,0.4> shadowless }"]
    # auto camera from bounding box
    allp = [p for c in patches for p in c]
    xs=[p[0] for p in allp]; ys=[p[1] for p in allp]; zs=[p[2] for p in allp]
    cx,cy,cz = (sum(xs)/len(xs), sum(ys)/len(ys), sum(zs)/len(zs))
    span = max(max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs))
    o.append(f"camera {{ location <{cx},{cy},{cz - span*1.8}> look_at <{cx},{cy},{cz}> angle 40 right x*4/3 up y }}")
    o.append("union {")
    for c in patches:
        g = grid16(c)
        rows = "\n    ".join(" ".join(f"<{p[0]:.4f},{p[1]:.4f},{p[2]:.4f}>" for p in g[r*4:r*4+4]) for r in range(4))
        o.append(f"  bicubic_patch {{ type 0 flatness 0 u_steps 2 v_steps 2\n    {rows}\n  }}")
    o.append("}")
    open(dst, "w").write("\n".join(o) + "\n")
    sys.stderr.write(f">> {dst}: {len(pos)} CPs, {len(patches)} patches "
                     f"(bbox span {span:.1f})\n")

if __name__ == "__main__":
    main()
