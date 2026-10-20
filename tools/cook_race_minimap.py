#!/usr/bin/env python3
"""The experimental Wii-style race map (ux0:data/mkw/map.txt on the Vita).

Each course's own map model (map_model.brres: its drivable surfaces in world
units) seen from above and baked into a texture: white surfaces, grey lines
where one level meets another, a dark rim around the whole. The Wii draws the
same model in its map; the start line strip (start_line.brlyt's
tt_start_line_22) is cooked beside it.

Writes build/race-map/textures/race_minimap_NN.t3x with race_minimap_NN.bin
(little endian: x0, z0, world units per texel, content width and height, padded
width and height) and race_minimap_start.t3x. tools/package_sd.py packs them.

Every catalog entry is cooked, the ten battle arenas (32..41) included: an
arena's only map is this one (src/race_bottom_screen.cpp, arena mode).
"""
import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'vendor/gclib')]
from course_catalog import RR_FIRST_ID, courses
import race_map_bake as rb
from cook_race_items import cook_image_sprite, cook_sprite

OUT = ROOT / 'build/race-map'
WORK = ROOT / 'build/race-map/minimap-src'
SIZE = 256        # the content's longer side, texels
SUPERSAMPLE = 4   # rasterized at 4x, then averaged down
LEVEL_STEP = 400  # height difference (world units) drawn as a line between levels
FILL, LINE, RIM = (255, 255, 255, 205), (110, 110, 110, 225), (20, 20, 20, 150)


def triangles(model_json):
    data = json.loads(model_json.read_text())
    tris = []
    for model in data['models']:
        for shape in model['shapes']:
            v = shape['vertices']
            for i in range(0, len(v) - 2, 3):
                tris.append((v[i][:3], v[i + 1][:3], v[i + 2][:3]))
    return tris


ROAD_TYPES = [0, 1, 5, 6, 7, 8, 9]  # KCL road, slippery road, boost pads and ramps, jump pads, item road


def road_triangles(kcl):
    """The course's drivable road surfaces (world space), seen from above."""
    a, b, c, kind, face = rb.kcl_triangles(kcl)
    keep = np.isin(kind, ROAD_TYPES) & (face[:, 1] > 0.3)
    return [(tuple(p), tuple(q), tuple(r)) for p, q, r in zip(a[keep], b[keep], c[keep])]


def on_road(tris, road, n=256):
    """Share of the map drawing that lies on the road: 1.0 for every retail map."""
    pts = np.array([p for t in tris + road for p in t])
    x0, z0 = pts[:, 0].min(), pts[:, 2].min()
    s = n / max(pts[:, 0].max() - x0, pts[:, 2].max() - z0, 1)
    def mask(ts):
        im = Image.new('L', (n, n))
        draw = ImageDraw.Draw(im)
        for t in ts:
            draw.polygon([((p[0] - x0) * s, (p[2] - z0) * s) for p in t], fill=1)
        return np.array(im, bool)
    m = mask(tris)
    return (m & mask(road)).sum() / max(m.sum(), 1)


def dilate(mask, radius):
    out = mask.copy()
    h, w = mask.shape
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy > radius * radius or (dx == 0 and dy == 0):
                continue
            shifted = np.zeros_like(mask)
            shifted[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = \
                mask[max(-dy, 0):h + min(-dy, 0), max(-dx, 0):w + min(-dx, 0)]
            out |= shifted
    return out


def bake(tris):
    xs = [p[0] for t in tris for p in t]
    zs = [p[2] for t in tris for p in t]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    # A rim's width of padding on every side.
    upt = max(x1 - x0, z1 - z0) / (SIZE - 6)
    x0 -= 3 * upt
    z0 -= 3 * upt
    w = min(SIZE, int(np.ceil((x1 - x0) / upt)) + 3)
    h = min(SIZE, int(np.ceil((z1 - z0) / upt)) + 3)
    W, H, s = w * SUPERSAMPLE, h * SUPERSAMPLE, SUPERSAMPLE / upt
    heights = Image.new('I', (W, H), -(1 << 30))
    cover = Image.new('L', (W, H), 0)
    dh, dc = ImageDraw.Draw(heights), ImageDraw.Draw(cover)
    # Lowest first: where levels overlap, the upper surface shows.
    for t in sorted(tris, key=lambda t: sum(p[1] for p in t)):
        poly = [((p[0] - x0) * s, (p[2] - z0) * s) for p in t]
        dh.polygon(poly, fill=int(sum(p[1] for p in t) / 3))
        dc.polygon(poly, fill=255)
    hmap = np.array(heights, dtype=np.int64)
    mask = np.array(cover) > 0
    level = np.zeros_like(mask)
    for axis in (0, 1):
        d = np.abs(np.diff(hmap, axis=axis)) > LEVEL_STEP
        both = mask[1:, :] & mask[:-1, :] if axis == 0 else mask[:, 1:] & mask[:, :-1]
        d &= both
        if axis == 0:
            level[1:, :] |= d
        else:
            level[:, 1:] |= d
    level = dilate(level, SUPERSAMPLE // 2) & mask
    rim = dilate(mask, int(SUPERSAMPLE * 1.5)) & ~mask
    rgba = np.zeros((H, W, 4), dtype=np.float64)
    for region, color in ((mask, FILL), (level, LINE), (rim, RIM)):
        rgba[region] = color
    # Average down with premultiplied alpha, so edges do not darken.
    a = rgba[..., 3:4] / 255.0
    pre = np.concatenate([rgba[..., :3] * a, a], axis=-1)
    pre = pre.reshape(h, SUPERSAMPLE, w, SUPERSAMPLE, 4).mean(axis=(1, 3))
    alpha = pre[..., 3:4]
    rgb = np.where(alpha > 0, pre[..., :3] / np.maximum(alpha, 1e-6), 0)
    image = np.concatenate([rgb, alpha * 255.0], axis=-1).clip(0, 255).astype(np.uint8)
    return Image.fromarray(image, 'RGBA'), (x0, z0, upt, w, h)


def main():
    (OUT / 'textures').mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    ui = ROOT / 'assets/race-ui/Race/game_image'
    cook_sprite(ui / 'timg/tt_start_line_22.tpl', 'race_minimap_start', OUT)
    def one(course):
        index = course['index']  # the menu's course number (race_bottom_screen.cpp)
        source = ROOT / 'assets/courses' / course['key'] / 'map_model.brres'
        if not source.is_file():
            print('no map model:', course['name'])
            return
        work = WORK / course['key']
        subprocess.run(['node', str(ROOT / 'build/export_brres.mjs'), str(source), str(work)],
                       check=True, stdout=subprocess.DEVNULL)
        tris = triangles(work / 'model.json')
        if not tris:
            print('empty map model:', course['name'])
            return
        # Some custom tracks' map models are not in course space (moved or scaled):
        # racers would miss the drawing, so draw the course's own road instead.
        if course['id'] >= RR_FIRST_ID:
            road = road_triangles(ROOT / 'assets/courses' / course['key'] / 'course.kcl')
            if road and on_road(tris, road) < 0.9:
                print(f"{course['name']}: map model off the course, drawn from its road")
                tris = road
        image, (x0, z0, upt, w, h) = bake(tris)
        name = f'race_minimap_{index:02d}'
        record = cook_image_sprite(image, name, OUT)
        pw, ph = record['padded']
        (OUT / 'textures' / (name + '.bin')).write_bytes(struct.pack('<3f4H', x0, z0, upt, w, h, pw, ph))
        print(f'{course["name"]}: {len(tris)} triangles, {w}x{h}')

    # Each course is an independent Node export and bake: all of them at once.
    import os
    from concurrent.futures import ThreadPoolExecutor
    def guarded(course):
        try:
            one(course)
        except Exception as error:
            if course['id'] < RR_FIRST_ID: raise
            print(f"{course['name']}: minimap left out ({str(error)[:120]})", flush=True)
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        list(pool.map(guarded, courses()))

if __name__ == '__main__':
    main()
