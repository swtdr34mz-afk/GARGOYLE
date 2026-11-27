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
    names = list(PAL)
    eyes = None
    out.append(f"#define ART_VW {VW}\n#define ART_VH {VH}\n#define ART_NPAL {len(names)}")
    out.append(f"static const uint16_t ART_CORR[{len(names)}][{VW * VH}] = {{")
    for i, nm in enumerate(names):
        img, eyes, gbox = corridor(nm, 11 + i * 7)
        Image.fromarray((img * 255).astype(np.uint8)).resize((VW * 3, VH * 3), Image.NEAREST).save(OUT / f"corr_{nm}.png")
        px = rgb565(img).ravel()
        out.append("  {" + ",".join(f"0x{v:04x}" for v in px) + "},")
    out.append("};")
    tx = []
    for (x, y, z) in TORCH:
        sx = x / z * (VH * SS / 2) + VW * SS / 2
        sy = (VH * SS / 2) - (y / z - 0.06) * (VH * SS / 2)
        tx.append((sx / SS, sy / SS))
    out.append(f"static const float ART_TORCH[2][2] = {{{{{tx[0][0]:.2f}f, {tx[0][1]:.2f}f}}, {{{tx[1][0]:.2f}f, {tx[1][1]:.2f}f}}}};")
    (ROOT / "art.h").write_text("\n".join(out) + "\n")
    print("art.h", (ROOT / "art.h").stat().st_size, "bytes of text;", len(names), "corridors", VW, "x", VH)

if __name__ == "__main__":
    main()
