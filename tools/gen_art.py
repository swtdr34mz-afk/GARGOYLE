#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 swtdr34mz (GARGOYLE)
"""GARGOYLE: the pictures, rendered here once and kept in the firmware (art.h): the corridors of the dungeon, one per kind of
stone, seen in first person like the dungeon crawlers of the early 90s; at the far end only darkness (what lives
there is drawn at run time: its eyes, its fire).
Everything is drawn here from code: no picture from anywhere else.

    python3 tools/gen_art.py      -> art.h (committed: the build does not run this), build/art/*.png to look at
"""
import os, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image
import scipy.ndimage as nd

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "art"
OUT.mkdir(parents=True, exist_ok=True)
TMP = Path("/tmp/art")
TMP.mkdir(parents=True, exist_ok=True)

def down(a, f):
    h, w = a.shape[0] // f * f, a.shape[1] // f * f
    a = a[:h, :w]
    return a.reshape(h // f, f, w // f, f, *a.shape[2:]).mean((1, 3))

# ---- the corridor ----
VW, VH, SS = 166, 110, 3
PAL = {   # name: brick, brick variation, mortar, floor, ceiling, moss
    "RED":    ((0.62, 0.30, 0.24), 0.18, (0.20, 0.12, 0.10), (0.30, 0.30, 0.16), (0.40, 0.22, 0.18), 0.10),
    "BLUE":   ((0.38, 0.42, 0.52), 0.14, (0.12, 0.13, 0.17), (0.28, 0.30, 0.36), (0.26, 0.28, 0.34), 0.0),
    "MOSS":   ((0.34, 0.40, 0.26), 0.16, (0.10, 0.13, 0.08), (0.24, 0.30, 0.16), (0.24, 0.28, 0.20), 0.45),
    "SAND":   ((0.66, 0.54, 0.36), 0.14, (0.24, 0.19, 0.12), (0.42, 0.36, 0.24), (0.40, 0.33, 0.22), 0.0),
    "CRYPT":  ((0.34, 0.30, 0.38), 0.12, (0.09, 0.08, 0.11), (0.22, 0.20, 0.24), (0.18, 0.16, 0.21), 0.05),
}
rng = np.random.default_rng(5)
def vnoise(u, v, scale, seed):
    """value noise from a hashed grid, smooth"""
    g = np.random.default_rng(seed).random((64, 64))
    x, y = u * scale, v * scale
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - xi, y - yi
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = g[yi % 64, xi % 64]; b = g[yi % 64, (xi + 1) % 64]
    c = g[(yi + 1) % 64, xi % 64]; d = g[(yi + 1) % 64, (xi + 1) % 64]
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy
def fbm(u, v, seed):
    return sum(vnoise(u, v, 2.0 ** k * 1.5, seed + k) * 0.5 ** k for k in range(5)) / 1.94
def hash2(i, j, seed):
    h = (i * 73856093) ^ (j * 19349663) ^ (seed * 83492791)
    h = (h ^ (h >> 13)) * 1274126177
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0
def bricks(u, v, bw, bh, mortar, seed):
    row = np.floor(v / bh).astype(np.int64)
    uu = u / bw + (row % 2) * 0.5
    col = np.floor(uu).astype(np.int64)
    fu, fv = uu - col, v / bh - row
    du = np.minimum(fu, 1 - fu) * bw; dv = np.minimum(fv, 1 - fv) * bh
    d = np.minimum(du, dv)
    edge = np.clip(d / mortar, 0, 1)
    bev = 1 + 0.28 * np.clip((fv - (1 - 0.18)) / 0.18, 0, 1) - 0.30 * np.clip((0.2 - fv) / 0.2, 0, 1) \
        + 0.10 * np.clip((0.12 - fu) / 0.12, 0, 1) - 0.12 * np.clip((fu - 0.88) / 0.12, 0, 1)
    return edge * bev, hash2(col, row, seed), d

X_W, Y_F, Y_C, Z_END = 1.15, -1.0, 1.05, 4.6
TORCH = [(-X_W + 0.06, 0.30, 1.25), (X_W - 0.06, 0.30, 1.25)]

def corridor(name, seed):
    brick, bvar, mortar, floor, ceil, moss = [np.array(c) if isinstance(c, tuple) else c for c in PAL[name]]
    w, h = VW * SS, VH * SS
    py, px = np.mgrid[0:h, 0:w].astype(np.float64)
    f = 1.05
    sx = (px + 0.5 - w / 2) / (h / 2)
    sy = -(py + 0.5 - h / 2) / (h / 2) + 0.06
    big = 1e9
    tw = np.where(np.abs(sx) > 1e-6, X_W / np.maximum(np.abs(sx), 1e-6), big)
    tf = np.where(sy < 0, -Y_F / np.maximum(-sy, 1e-6), big)
    tc = np.where(sy > 0, Y_C / np.maximum(sy, 1e-6), big)
    t = np.minimum(np.minimum(tw, tf), tc)
    kind = np.where(t == tw, 0, np.where(t == tf, 1, 2))
    back = t > Z_END
    t = np.where(back, Z_END, t)
    X, Y, Z = sx * t, sy * t, t * 1.0
    col = np.zeros((h, w, 3))
    nrm = np.zeros((h, w, 3))
    # walls: bricks along z (u) and y (v)
    e, r, d = bricks(Z * 1.0, Y + 3.0, 0.52, 0.24, 0.035, seed)
    n = fbm(Z * 3, Y * 3, seed + 1)
    wc = brick[None, None, :] * (1 - bvar + 2 * bvar * r[..., None]) * (0.75 + 0.5 * n[..., None])
    mossm = np.clip((fbm(Z * 2.2, Y * 2.2, seed + 9) - 0.55 + moss * 0.25) * 4, 0, 1) * moss * np.clip(0.3 - Y, 0, 1)
    wc = wc * (1 - mossm[..., None]) + np.array([0.18, 0.30, 0.10]) * mossm[..., None]
    wc = wc * e[..., None] + mortar[None, None, :] * (1 - e[..., None])
    wc *= (1 - 0.25 * (hash2(np.floor(Z * 40).astype(np.int64), np.floor(Y * 40).astype(np.int64), seed) > 0.97))[..., None]
    # floor: flagstones
    fe, fr, _ = bricks(X * 1.0 + 2.0, Z * 1.0, 0.7, 0.55, 0.04, seed + 3)
    fn = fbm(X * 3, Z * 3, seed + 4)
    fc = floor[None, None, :] * (0.8 + 0.35 * fr[..., None]) * (0.7 + 0.6 * fn[..., None])
    fc = fc * fe[..., None] + floor[None, None, :] * 0.25 * (1 - fe[..., None])
    # ceiling: big blocks and beams
    ce, cr, _ = bricks(Z * 1.0, X * 1.0 + 2.0, 0.8, 0.6, 0.05, seed + 5)
    cc = ceil[None, None, :] * (0.7 + 0.4 * cr[..., None]) * (0.7 + 0.5 * fbm(Z * 2, X * 2, seed + 6)[..., None])
    cc = cc * ce[..., None] + ceil[None, None, :] * 0.2 * (1 - ce[..., None])
    col = np.where((kind == 0)[..., None], wc, np.where((kind == 1)[..., None], fc, cc))
    # normals for light: walls face +-x, floor up, ceiling down; the mortar bends them a little
    nx = np.where(kind == 0, -np.sign(X), 0.0); ny = np.where(kind == 1, 1.0, np.where(kind == 2, -1.0, 0.0))
    nz = np.zeros_like(nx)
    # the back: darkness, a hint of an arch
    col = np.where(back[..., None], col * 0.0, col)
    # light: two torches, a little cold ambient; falloff
    lit = np.zeros((h, w))
    for (tx, ty, tz) in TORCH:
        lx, ly, lz = tx - X, ty - Y, tz - Z
        dd = np.sqrt(lx * lx + ly * ly + lz * lz) + 1e-6
        lam = np.clip((nx * lx + ny * ly + nz * lz) / dd, 0, 1) * 0.85 + 0.15
        lit += lam * 2.6 / (1 + 2.2 * dd * dd)
    amb = 0.06 * np.exp(-Z * 0.25)
    fog = np.exp(-np.maximum(Z - 0.8, 0) * 0.55)
    img = col * (lit[..., None] * np.array([1.0, 0.80, 0.55]) + amb[..., None]) * fog[..., None]
    # contact shadow where wall meets floor
    img *= np.clip(0.55 + 0.45 * np.clip(np.minimum(np.abs(Y - Y_F), np.abs(np.abs(X) - X_W)) * 6, 0, 1), 0, 1)[..., None] ** 0.5 + 0 * img
    # torch brackets on the wall (iron), drawn into the picture; the flames come at run time
    eyes = None
    gbox = None
    img = down(img, SS)
    img = np.clip(img * 1.15, 0, 1) ** (1 / 1.2)
    m = img.mean(-1, keepdims=True)
    img = np.clip(m + (img - m) * 1.35, 0, 1)        # a little more colour, like the old palettes
    return img, None, None

def rgb565(img):
    d = np.random.default_rng(1).random(img.shape[:2])[..., None] / 32.0
    r = np.clip((img[..., 0] + d[..., 0] * 0.9) * 31, 0, 31).astype(np.uint16)
    g = np.clip((img[..., 1] + d[..., 0] * 0.45) * 63, 0, 63).astype(np.uint16)
    b = np.clip((img[..., 2] + d[..., 0] * 0.9) * 31, 0, 31).astype(np.uint16)
    return (r << 11) | (g << 5) | b

def main():
    out = ["/* generated by tools/gen_art.py: the pictures of GARGOYLE (drawn from code, no outside artwork) */",
           "#pragma once", "#include <stdint.h>", ""]
    # the eight rooms: Tom's pictures (tools/art/scenes.png, four by two), each cut to the view and kept in a palette
    # of its own (pixel art: few colours, one byte a pixel)
    src = Image.open(ROOT / "tools" / "art" / "scenes.png").convert("RGB")
    names = ["ORC", "DRAGON", "ORB", "SKELETON", "SLIME", "BEHOLDER", "TREASURE", "WATER"]
    out.append(f"#define ART_VW {VW}\n#define ART_VH {VH}\n#define ART_NSCN {len(names)}")
    pals, idxs = [], []
    for i, nm in enumerate(names):
        col, row = i % 4, i // 4
        box = (col * 384 + 14, row * 512 + 76, col * 384 + 372, row * 512 + 312)
        im = src.crop(box).resize((VW, VH), Image.LANCZOS)
        q = im.quantize(colors=192, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = np.array(q.getpalette()[:192 * 3], np.float64).reshape(-1, 3) / 255.0
        idx = np.asarray(q, np.uint8)
        q.convert("RGB").resize((VW * 3, VH * 3), Image.NEAREST).save(OUT / f"scene_{nm}.png")
        pals.append(rgb565(pal[None, :, :])[0])
        idxs.append(idx.ravel())
    out.append(f"static const uint16_t ART_SCN_PAL[{len(names)}][192] = {{")
    for pv in pals:
        out.append("  {" + ",".join(f"0x{v:04x}" for v in pv) + "},")
    out.append("};")
    out.append(f"static const uint8_t ART_SCN[{len(names)}][{VW * VH}] = {{")
    for ix in idxs:
        out.append("  {" + ",".join(str(int(v)) for v in ix) + "},")
    out.append("};")
    write_title_and_flames(out)
    (ROOT / "art.h").write_text("\n".join(out) + "\n")
    print("art.h", (ROOT / "art.h").stat().st_size, "bytes of text;", len(names), "rooms", VW, "x", VH)


# ======================= the title and the torches for the start screen =======================
from PIL import ImageDraw, ImageFont

# the letters, drawn here (an own lettering in the spirit of old fantasy box art: angular, blade-like serifs);
# units: cap height 100, y down. Each letter: an outline and the holes in it
LETTERS = {
    "G": ([(64, 6), (44, 0), (18, 6), (4, 26), (0, 52), (6, 80), (22, 96), (46, 100), (64, 92), (68, 56), (36, 56),
           (32, 66), (48, 68), (48, 84), (34, 86), (22, 74), (18, 50), (24, 26), (36, 16), (50, 20), (58, 34), (72, 22)], []),
    "A": ([(-2, 100), (12, 94), (30, 4), (46, 0), (72, 94), (84, 100), (44, 100), (52, 94), (48, 78), (26, 78), (22, 94),
           (30, 100)], [[(29, 64), (45, 64), (37, 30)]]),
    "R": ([(0, 2), (42, 0), (60, 10), (66, 30), (58, 48), (44, 55), (68, 94), (80, 100), (42, 100), (46, 94), (30, 60),
           (24, 60), (24, 92), (32, 100), (-2, 100), (6, 92), (6, 10)],
          [[(24, 14), (38, 14), (46, 24), (44, 40), (34, 47), (24, 47)]]),
    "O": ([(34, 0), (58, 10), (70, 40), (66, 74), (50, 96), (30, 100), (10, 90), (0, 60), (4, 26), (16, 8)],
          [[(34, 17), (46, 30), (48, 62), (40, 84), (28, 82), (20, 60), (22, 32)]]),
    "Y": ([(-2, 0), (32, 0), (26, 6), (38, 38), (50, 6), (44, 0), (76, 0), (64, 8), (46, 52), (46, 92), (54, 100), (20, 100),
           (28, 92), (28, 54), (10, 8)], []),
    "L": ([(-2, 0), (32, 0), (24, 8), (24, 84), (48, 84), (62, 64), (68, 100), (-2, 100), (8, 92), (8, 8)], []),
    "E": ([(-2, 0), (60, 0), (66, 28), (52, 14), (26, 14), (26, 42), (46, 42), (54, 30), (54, 66), (46, 54), (26, 54),
           (26, 86), (52, 86), (68, 68), (64, 100), (-2, 100), (8, 92), (8, 8)], []),
}
ADV = {"G": 74, "A": 82, "R": 78, "O": 72, "Y": 74, "L": 66, "E": 68}

def title_art(text="GARGOYLE", W=232, H=84, S=4):
    """the name on a slab of rough rock, in pixels like the DOS games: gold letters with a bevel and a black edge,
    the rock dark and chipped, the whole picture in a small palette with a little dither"""
    # lay the letters out: the first and the last bigger, the baseline in a slight arch
    n = len(text)
    sizes = [1.18 if i in (0, n - 1) else 1.0 for i in range(n)]
    total = sum(ADV[c] * sz for c, sz in zip(text, sizes)) - 6 * (n - 1)
    sc = (W - 18) * S / total * 1.06
    canvas_h, canvas_w = H * S, W * S
    letters = Image.new("L", (canvas_w, canvas_h), 0)
    holes = Image.new("L", (canvas_w, canvas_h), 0)
    dl, dh = ImageDraw.Draw(letters), ImageDraw.Draw(holes)
    x = 7 * S
    for i, (c, sz) in enumerate(zip(text, sizes)):
        k = sc * sz * 0.92
        u = (i - (n - 1) / 2) / ((n - 1) / 2)
        base = H * S * 0.5 + 50 * sc * 0.92 + 6 * S * u * u - (sz - 1) * 40 * sc
        outline, hs = LETTERS[c]
        rot = (-0.07, 0.05, -0.04, 0.06, -0.05, 0.04, -0.06, 0.07)[i % 8]
        jit = (0, -4, 3, -3, 4, -2, 3, 0)[i % 8] * S
        def tr(p, k=k, rot=rot, jit=jit, x=x, base=base):
            px_, py_ = p[0] - 35, p[1] - 50
            c_, s_ = np.cos(rot), np.sin(rot)
            qx, qy = px_ * c_ - py_ * s_ + 35, px_ * s_ + py_ * c_ + 50
            return (x + qx * k, base - (100 - qy) * k + jit)
        dl.polygon([tr(p) for p in outline], fill=255)
        for hh in hs:
            dh.polygon([tr(p) for p in hh], fill=255)
        x += (ADV[c] - 6) * k / 0.92 * 0.92 * 1.0 + 0 * k
    let = (np.asarray(letters) > 127) & ~(np.asarray(holes) > 127)
    rng2 = np.random.default_rng(4)
    # the slab: around the letters, rough and chipped
    slab = nd.binary_dilation(let, iterations=int(6 * S))
    sd_slab = nd.distance_transform_edt(slab) - nd.distance_transform_edt(~slab)
    sd_slab += nd.gaussian_filter(rng2.standard_normal(slab.shape), 2.5 * S) * 9.0 + nd.gaussian_filter(rng2.standard_normal(slab.shape), 0.8 * S) * 2.0
    slab = sd_slab > 0
    hs_ = np.clip(nd.distance_transform_edt(slab), 0, 5 * S)
    hs_ = hs_ + nd.gaussian_filter(rng2.standard_normal(slab.shape), 1.2 * S) * 3.0
    gy, gx = np.gradient(nd.gaussian_filter(hs_, 0.8 * S))
    nr = np.stack([-gx, -gy, np.full_like(gx, 1.4)], -1); nr /= np.linalg.norm(nr, axis=-1, keepdims=True)
    L = np.array([-0.5, -0.7, 0.55]); L /= np.linalg.norm(L)
    lr = np.clip((nr * L).sum(-1), 0, 1)
    rock = np.array([0.30, 0.27, 0.25])[None, None, :] * (0.25 + 1.1 * lr)[..., None]
    rock *= (0.8 + 0.4 * nd.gaussian_filter(rng2.random(slab.shape), 0.6 * S))[..., None]
    # under the slab a darker, thicker edge (its side, seen from below)
    side = nd.binary_dilation(slab, iterations=int(1.5 * S)) & ~slab
    for dy_ in range(1, int(5 * S)):
        side = side | (np.roll(np.roll(slab, dy_, 0), dy_ // 3, 1) & ~slab)
    # the letters: a bevel, gold
    dt = nd.distance_transform_edt(let)
    hl = nd.gaussian_filter(np.clip(dt, 0, 2.6 * S), 0.6 * S)
    gy, gx = np.gradient(hl)
    nl = np.stack([-gx, -gy, np.full_like(gx, 0.75)], -1); nl /= np.linalg.norm(nl, axis=-1, keepdims=True)
    ll = np.clip((nl * L).sum(-1), 0, 1)
    yy_ = (np.arange(canvas_h) / canvas_h)[:, None] * np.ones(let.shape)
    gold_hi = np.array([1.0, 0.95, 0.55]); gold = np.array([0.98, 0.72, 0.12]); gold_lo = np.array([0.55, 0.30, 0.04])
    t_ = (0.35 + 0.9 * ll - 0.25 * (yy_ - 0.5))[..., None]
    gcol = np.where(t_ > 0.75, gold + (gold_hi - gold) * np.clip((t_ - 0.75) / 0.4, 0, 1),
                    gold_lo + (gold - gold_lo) * np.clip(t_ / 0.75, 0, 1))
    spec = (ll ** 10)[..., None] * 0.5
    gcol = np.clip(gcol + spec, 0, 1)
    edge = nd.binary_dilation(let, iterations=int(1.6 * S)) & ~let
    col = np.zeros(let.shape + (3,))
    sidecol = np.array([0.20, 0.15, 0.11])[None, None, :] * (0.6 + 0.5 * nd.gaussian_filter(rng2.random(slab.shape), 0.7 * S))[..., None]
    col = np.where(side[..., None], sidecol, col)
    col = np.where(slab[..., None], rock, col)
    col = np.where(edge[..., None], np.array([0.06, 0.03, 0.02]), col)
    col = np.where(let[..., None], gcol, col)
    a = (slab | side).astype(np.float32)
    col = down(col, S)
    al = down(a, S)
    col = np.clip(col / np.maximum(al[..., None], 1e-3), 0, 1)
    al = (al > 0.5).astype(np.float32)                 # hard edges, like the old games
    # the DOS look: a small palette, ordered dither
    bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5
    hh, ww = al.shape
    dth = np.tile(bayer, (hh // 4 + 1, ww // 4 + 1))[:hh, :ww][..., None] * 0.06
    img = Image.fromarray((np.clip(col + dth, 0, 1) * 255).astype(np.uint8))
    img = img.quantize(colors=28, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")
    col = np.asarray(img, np.float32) / 255
    Image.fromarray((np.dstack([col * al[..., None], al[..., None]]) * 255).astype(np.uint8), "RGBA").save(OUT / "title.png")
    return col, al

def flame_frames(n=8, w=14, h=26):
    """a torch flame like the DOS games: a little fire simulation on a coarse grid, a palette of a few reds and
    yellows, every frame from the same running fire"""
    pal = np.array([[0, 0, 0], [60, 8, 4], [120, 16, 6], [180, 36, 8], [224, 72, 12], [248, 120, 20],
                    [255, 172, 40], [255, 214, 90], [255, 244, 170], [255, 255, 236]], np.float32) / 255
    rr = np.random.default_rng(9)
    gw, gh = w, h
    fire = np.zeros((gh, gw))
    frames = []
    shape = np.clip(1.0 - (np.abs(np.arange(gw) - (gw - 1) / 2) / (gw / 3.2)) ** 2, 0, 1)
    for step in range(80 + n * 3):
        fire[-1] = shape * (0.75 + 0.25 * rr.random(gw)) * 9.0
        nf = fire.copy()
        for y in range(gh - 1):
            src = fire[y + 1]
            shift = rr.integers(-1, 2, gw)
            idx = np.clip(np.arange(gw) + shift, 0, gw - 1)
            nf[y] = np.maximum(src[idx] - rr.random(gw) * 0.62 - 0.04, 0)
        cone = np.clip(1.0 - (np.abs(np.arange(gw)[None, :] - (gw - 1) / 2) / ((gw / 2) * (0.35 + 0.65 * np.arange(gh)[:, None] / gh))) ** 2, 0, 1)
        fire = nf * (0.55 + 0.45 * cone)
        if step >= 80 and (step - 80) % 3 == 0:
            frames.append(np.clip(np.round(fire), 0, 9).astype(np.uint8))
    rgb = [pal[f] for f in frames]
    strip = np.concatenate(rgb, 1)
    Image.fromarray((strip * 255).astype(np.uint8)).resize((strip.shape[1] * 4, strip.shape[0] * 4), Image.NEAREST).save(OUT / "flames.png")
    return frames, pal

def write_title_and_flames(out):
    col, al = title_art()
    th, tw = al.shape
    px = rgb565(col).ravel()
    out.append(f"#define ART_TW {tw}\n#define ART_TH {th}")
    out.append(f"static const uint16_t ART_TITLE[{tw * th}] = {{")
    for i in range(0, len(px), 24):
        out.append("  " + ",".join(f"0x{v:04x}" for v in px[i:i + 24]) + ",")
    out.append("};")
    a8 = np.clip(np.round(al * 255), 0, 255).astype(np.uint8).ravel()
    out.append(f"static const uint8_t ART_TITLE_A[{tw * th}] = {{")
    for i in range(0, len(a8), 40):
        out.append("  " + ",".join(str(int(v)) for v in a8[i:i + 40]) + ",")
    out.append("};")
    frames, pal = flame_frames()
    fh, fw = frames[0].shape
    out.append(f"#define ART_FN {len(frames)}\n#define ART_FW {fw}\n#define ART_FH {fh}")
    out.append("static const uint8_t ART_FLAME_PAL[10][3] = {" + ",".join("{%d,%d,%d}" % tuple((p * 255).astype(int)) for p in pal) + "};")
    out.append(f"static const uint8_t ART_FLAME[{len(frames)}][{fw * fh}] = {{")
    for f in frames:
        out.append("  {" + ",".join(str(int(v)) for v in f.ravel()) + "},")
    out.append("};")

if __name__ == "__main__":
    main()
