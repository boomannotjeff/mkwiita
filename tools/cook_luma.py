#!/usr/bin/env python3
"""Rosalina's Luma for races: TicoModel (USA 807cad0c), common tico.brres, model tico,
looping CHR0 wait. Cooked as the world items are (tools/cook_race_items.py cook_world:
race materials, each material's first sampler as its base texture); the battle cook's
copy (rosalina_luma) uses the default material matching and draws black in a race.
Placement is runtime (include/mkw/luma_motion.hpp), nothing is baked into the mesh.
"""
import json
from cook_race_items import ROOT, cook_world

OUT = ROOT / 'sd-card/3ds/mkw-3ds'


def main():
    record = cook_world(ROOT / 'assets/common/tico.brres', 'tico', 'luma', 'wait', OUT)
    if 'wait' not in [c['name'] for c in record.get('clips', [])]:
        raise ValueError('tico.brres has no wait clip')
    (OUT / 'models/luma.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Luma:', record['path'], record['animation'], record['triangles'], 'triangles')


if __name__ == '__main__':
    main()
