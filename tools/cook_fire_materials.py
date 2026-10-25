#!/usr/bin/env python3
"""Give the course fire effects the parts of their original GX materials the shared
exporter leaves out, by patching the cooked course-object submeshes in place.

- 2 x texture alpha (the Fire Snake and fireball glow planes): alpha mode 7 in
  src/renderer.cpp. The exporter gives up on the doubled alpha, so the planes drew solid.
- ef_flamePoleMat (the flame pillar core): lerp(C0, raster, texture), the renderer's
  bit-25 colour lerp. The exporter matched no pattern and drew the bare texture.
  The env-mapped glow stage (+ C2 * firePoleGlow) and the indirect warp stay omitted.

Runs after every step that writes course objects (tools/release/build_cia.py), so a
re-cooked object is patched again. Patching is idempotent.
"""
import json, struct
from pathlib import Path
from gx_material import GMDL_SUBMESH_BYTES

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / 'sd-card/3ds/mkw-3ds/models'
DOUBLE_ALPHA = {'m_snakeFirePl', 'm_koopaBallPl'}
COLOR_LERP = {'ef_flamePoleMat'}
RECORD = GMDL_SUBMESH_BYTES  # GMDL submesh bytes by version


def packed(c):
    return round(c['r'] * 255) | round(c['g'] * 255) << 8 | round(c['b'] * 255) << 16


def patch(path, materials):
    data = bytearray(path.read_bytes())
    magic, version, _, _, count, _ = struct.unpack_from('<4s5I', data)
    if magic != b'GMDL' or version not in RECORD:
        raise ValueError('Not a GMDL model: ' + str(path))
    changed = 0
    for i in range(count):
        at = 24 + i * RECORD[version]
        texture, flags, secondary, constant = struct.unpack_from('<iIiI', data, at + 8)
        matches = [m for m in materials if m['texture'] == texture and m['secondary'] == secondary]
        targets = [m for m in matches if m['material'] in DOUBLE_ALPHA | COLOR_LERP]
        if not targets:
            continue
        if len({m['material'] for m in matches}) > 1:
            raise ValueError(f'{path.name}: submesh {i} matches several materials')
        m = targets[0]
        if m['material'] in DOUBLE_ALPHA:
            flags = flags & ~(7 << 20) | 7 << 20
        else:
            c0 = m['sourceMaterial']['colorRegisters'][1]
            flags = flags & ~(3 << 12) | 1 << 25
            constant = constant & 0xff000000 | packed(c0)
        struct.pack_into('<I', data, at + 12, flags)
        struct.pack_into('<I', data, at + 20, constant)
        changed += 1
    if changed:
        path.write_bytes(bytes(data))
    return changed


def main():
    report = {'version': 1, 'models': {}}
    for receipt in sorted(MODELS.glob('obj*-receipt.json')):
        if receipt.name.startswith('._'):
            continue
        r = json.loads(receipt.read_text())
        materials = [m for m in r.get('materials', []) if 'sourceMaterial' in m]
        if not any(m['material'] in DOUBLE_ALPHA | COLOR_LERP for m in materials):
            continue
        for part in r['parts']:
            n = patch(MODELS / (part['name'] + '.gmdl'), materials)
            if n:
                report['models'][part['name']] = n
    out = ROOT / 'build/fire-materials.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=1) + '\n')
    print(f"Patched fire materials in {len(report['models'])} course-object models")


if __name__ == '__main__':
    main()
