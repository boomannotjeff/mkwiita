#!/usr/bin/env python3
"""Builds the Vita game files from your own Mario Kart Wii (USA) disc.

    .venv/Scripts/python tools/vita/build_assets.py <disc.iso|disc.wbfs>
    .venv/Scripts/python tools/vita/build_assets.py --mipmaps --force-cook   (experimental mipmaps)

Runs the CIA builder's disc and cook stages (tools/release/build_cia.py),
then stages the result in build/vita/data/mkw. Copy that folder's contents
to ux0:data/mkw on the Vita. Only tex3ds is needed from devkitPro (the
Vita layer converts its .t3x textures when the game loads them).
"""
import argparse, os, shutil, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/release')]
import build_cia
from devkitpro import tool

OUT = ROOT / 'build/vita/data/mkw'


def check_tools():
    if not tool('tex3ds').exists():
        raise SystemExit(f'Missing {tool("tex3ds")}: install devkitPro tex3ds')
    if not shutil.which('node'):
        raise SystemExit('Missing Node.js (https://nodejs.org), used by the model exporters')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('disc', nargs='?', type=Path, help='Mario Kart Wii (USA) ISO or WBFS (not needed once cooked)')
    p.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    p.add_argument('--force-cook', action='store_true')
    p.add_argument('--mipmaps', action='store_true',
                   help='experimental: mip chains for the textures that had them on the Wii '
                        '(use --force-cook when switching this on or off)')
    a = p.parse_args()
    # The cook steps inherit it (tools/cook_model.py).
    os.environ['MKW_MIPMAPS'] = '1' if a.mipmaps else '0'
    check_tools()
    build_cia.stage_sources()
    build_cia.stage_disc(a.disc)
    build_cia.check_game_code(a.disc)
    build_cia.stage_cook(a.disc, a.jobs, a.force_cook)
    build_cia.run([sys.executable, 'tools/package_sd.py', '--no-zip'])
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(ROOT / 'build/playable/3ds/mkw-3ds'), str(OUT))
    shutil.rmtree(ROOT / 'build/playable', ignore_errors=True)
    (OUT / 'content.txt').write_text(build_cia.content_fingerprint() + '\n')
    files = sum(1 for _ in OUT.rglob('*') if _.is_file())
    size = sum(f.stat().st_size for f in OUT.rglob('*') if f.is_file())
    build_cia.log(f'done: {files} files, {size / 2**20:.0f} MiB in {OUT}')
    build_cia.log('Copy the contents of that folder to ux0:data/mkw on your Vita.')


if __name__ == '__main__':
    main()
