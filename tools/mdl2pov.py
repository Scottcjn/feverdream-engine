#!/usr/bin/env python3
"""mdl2pov.py — native Hash Animation:Master .mdl -> POV-Ray 3.7 converter (*nix).

No Wine, no HamaPatch. Reads A:M's text .mdl directly:
  [MESH]   splines of control points (in order). Each CP = 4 lines:
             "<flags> <isRef> <cpID>"
             then position "x y z" (isRef=0) OR a shared-CP id (isRef=1)
             then two tangent lines.
  [PATCHES] each patch = a flag line then "cp1 cp2 cp3 cp4 ...": 4 corner CP ids.

SMOOTHING: rather than trust A:M's proprietary tangent encoding, we derive
Catmull-Rom tangents from each spline's CP sequence (what A:M's auto-tangents
approximate) and build every patch as a Coons bicubic from its 4 boundary Bezier
curves. Shared edges reuse the same spline tangents => adjacent patches meet
smoothly. `--flat` falls back to the bilinear first cut.

    mdl2pov.py model.mdl out.pov [--flat] [--color r,g,b]
"""
import sys, re

def vadd(a,b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def vsub(a,b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def vmul(a,s): return (a[0]*s, a[1]*s, a[2]*s)
def lerp(p,q,t): return (p[0]+(q[0]-p[0])*t, p[1]+(q[1]-p[1])*t, p[2]+(q[2]-p[2])*t)

def main():
    a = sys.argv
    src, dst = a[1], a[2]
    flat  = "--flat" in a
    color = a[a.index("--color")+1] if "--color" in a else "0.80,0.78,0.92"
    lines = [l.rstrip("\r\n") for l in open(src, encoding="utf-8", errors="replace")]

    def find(tag):
        for i,l in enumerate(lines):
            if l.strip()==tag: return i
        return -1
    mesh,endmesh = find("[MESH]"), find("[ENDMESH]")
    pat,endpat   = find("[PATCHES]"), find("[ENDPATCHES]")

    # --- parse CPs (positions + refs) AND spline membership in order ---
    pos, ref, splines = {}, {}, []
    hdr = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s*$")
    cpcount = re.compile(r"^\s*CPs=(\d+)")
    # robust line-scan: each CP header consumes exactly its 3 data lines; a new
    # spline begins at each "CPs=" marker. Never assumes a fixed record stride.
    cur = None
    i = mesh+1
    while i < endmesh:
        line = lines[i]
        if cpcount.match(line):
            if cur is not None: splines.append(cur)
            cur = []
        else:
            m = hdr.match(line)
            if m:
                cpid, isref = int(m.group(3)), int(m.group(2))
                b = lines[i+1].strip() if i+1 < endmesh else ""
                if isref == 0:
                    p = b.split()
                    if len(p)==3:
                        try: pos[cpid]=(float(p[0]),float(p[1]),float(p[2]))
                        except ValueError: pass
                else:
                    try: ref[cpid]=int(b.split()[0])
                    except (ValueError,IndexError): pass
                if cur is not None: cur.append(cpid)
                i += 3   # skip this CP's 3 data lines
        i += 1
    if cur is not None: splines.append(cur)
    def resolve(c, seen=None):
        seen = seen or set()
        if c in pos: return pos[c]
        if c in ref and c not in seen:
            seen.add(c); return resolve(ref[c], seen)
        return None
    for c in list(ref):
        r = resolve(c)
        if r: pos[c] = r

    # --- Catmull-Rom -> cubic Bezier for every consecutive CP pair (both dirs) ---
    # edge[(a,b)] = [Pa, c1, c2, Pb]  (the boundary curve from a to b)
    edge = {}
    for sp in splines:
        P = [pos.get(c) for c in sp]
        L = len(sp)
        for k in range(L-1):
            a0,a1 = sp[k], sp[k+1]
            p0,p1 = P[k], P[k+1]
            if p0 is None or p1 is None: continue
            pm = P[k-1] if k-1>=0 else p0          # neighbor before
            pn = P[k+2] if k+2<L else p1            # neighbor after
            if pm is None: pm = p0
            if pn is None: pn = p1
            c1 = vadd(p0, vmul(vsub(p1,pm), 1/6))
            c2 = vsub(p1, vmul(vsub(pn,p0), 1/6))
            edge[(a0,a1)] = [p0,c1,c2,p1]
            edge[(a1,a0)] = [p1,c2,c1,p0]          # reverse

    def bezier_edge(A,B):
        if (A,B) in edge: return edge[(A,B)]
        pa,pb = pos.get(A), pos.get(B)
        if pa is None or pb is None: return None
        return [pa, lerp(pa,pb,1/3), lerp(pa,pb,2/3), pb]   # straight fallback

    # --- parse patches (4 corner CP ids) ---
    patch_ids = []
    for j in range(pat+1, endpat):
        parts = lines[j].split()
        if len(parts)>=5 and all(x.lstrip("-").isdigit() for x in parts[:4]):
            patch_ids.append([int(x) for x in parts[:4]])

    # --- build the 16 control points for one patch ---
    def flat16(c):
        P00,P03,P33,P30 = c
        g=[]
        for ui in range(4):
            top,bot = lerp(P00,P30,ui/3.0), lerp(P03,P33,ui/3.0)
            for vi in range(4): g.append(lerp(top,bot,vi/3.0))
        return g
    def coons16(ids):
        A,B,C,D = ids   # CP ids (edge lookup needs ids, not positions)
        eAB,eBC,eCD,eDA = bezier_edge(A,B),bezier_edge(B,C),bezier_edge(C,D),bezier_edge(D,A)
        if not all([eAB,eBC,eCD,eDA]): return None
        g = [[None]*4 for _ in range(4)]
        # corners
        g[0][0],g[0][3],g[3][3],g[3][0] = eAB[0],eAB[3],eCD[0],eCD[3]
        # edges around the quad
        g[0][1],g[0][2] = eAB[1],eAB[2]            # A->B  (row 0)
        g[1][3],g[2][3] = eBC[1],eBC[2]            # B->C  (col 3)
        g[3][2],g[3][1] = eCD[1],eCD[2]            # C->D  (row 3)
        g[2][0],g[1][0] = eDA[1],eDA[2]            # D->A  (col 0)
        # interior: zero-twist Coons estimate
        g[1][1]=vsub(vadd(g[0][1],g[1][0]),g[0][0])
        g[1][2]=vsub(vadd(g[0][2],g[1][3]),g[0][3])
        g[2][1]=vsub(vadd(g[3][1],g[2][0]),g[3][0])
        g[2][2]=vsub(vadd(g[3][2],g[2][3]),g[3][3])
        return [g[r][c] for r in range(4) for c in range(4)]

    patches=[]
    for ids in patch_ids:
        corners=[pos.get(x) for x in ids]
        if any(c is None for c in corners): continue
        g = flat16(corners) if flat else coons16(ids)
        if g: patches.append(g)

    # --- emit ---
    ptype = 0 if flat else 1
    o=["#version 3.7;","global_settings { assumed_gamma 1.0 }",'#include "colors.inc"',
       "background { rgb <0.05,0.05,0.10> }",
       f"#default {{ texture {{ pigment {{ rgb <{color}> }} finish {{ ambient 0.3 diffuse 0.75 phong 0.5 }} }} }}",
       "light_source { <-40,60,-80> rgb 1 }",
       "light_source { <40,20,-40> rgb <0.3,0.3,0.4> shadowless }"]
    if not patches:
        sys.exit("no patches built — check .mdl [MESH]/[PATCHES] sections")
    allp=[p for g in patches for p in g]
    xs=[p[0] for p in allp]; ys=[p[1] for p in allp]; zs=[p[2] for p in allp]
    cx,cy,cz=(sum(xs)/len(xs),sum(ys)/len(ys),sum(zs)/len(zs))
    span=max(max(xs)-min(xs),max(ys)-min(ys),max(zs)-min(zs))
    o.append(f"camera {{ location <{cx},{cy},{cz-span*1.8}> look_at <{cx},{cy},{cz}> angle 40 right x*4/3 up y }}")
    o.append("union {")
    for g in patches:
        rows="\n    ".join(" ".join(f"<{p[0]:.4f},{p[1]:.4f},{p[2]:.4f}>" for p in g[r*4:r*4+4]) for r in range(4))
        o.append(f"  bicubic_patch {{ type {ptype} flatness 0 u_steps 3 v_steps 3\n    {rows}\n  }}")
    o.append("}")
    open(dst,"w").write("\n".join(o)+"\n")
    sys.stderr.write(f">> {dst}: {len(pos)} CPs, {len(splines)} splines, "
                     f"{len(patches)} patches ({'flat' if flat else 'smooth Coons'})\n")

if __name__=="__main__":
    main()
