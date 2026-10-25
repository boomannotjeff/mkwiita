#!/usr/bin/env python3
"""Builds the Vita game files from your own Mario Kart Wii (USA) disc.

    .venv/Scripts/python tools/vita/build_assets.py <disc.iso|disc.wbfs>

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
    p.add_argument('--rr', type=Path, help='Retro Rewind folder: the Retro Rewind edition (ux0:data/mkwrr)')
    p.add_argument('--rr-cups', default='all')
    p.add_argument('--rr-skins', default='all')
    a = p.parse_args()
    # The cook steps inherit these (tools/course_catalog.py).
    if a.rr:
        if not (a.rr / 'Binaries/ConfigRT.pul').is_file():
            raise SystemExit(f'{a.rr} is not a Retro Rewind folder (no Binaries/ConfigRT.pul)')
        os.environ.update(MKW_RR=str(a.rr.resolve()), MKW_RR_CUPS=a.rr_cups, MKW_RR_SKINS=a.rr_skins)
    data = 'mkwrr' if a.rr else 'mkw'
    OUT = ROOT / 'build/vita/data' / data
    # One folder cooks one edition: the cook's stamps and outputs (data/, assets/,
    # sd-card/, build/) would otherwise mix the two. Folders from before this
    # marker: an RR catalog means Retro Rewind, any finished step the retail game.
    marker = ROOT / 'build/edition.txt'
    if marker.is_file():
        edition = marker.read_text().strip()
    elif (ROOT / 'data/extracted/rr-catalog.json').is_file():
        edition = 'rr'
    else:
        edition = 'retail' if any((ROOT / 'build/release/cook').glob('*.done')) else None
    wanted = 'rr' if a.rr else 'retail'
    if edition and edition != wanted:
        names = {'rr': 'Retro Rewind', 'retail': 'Mario Kart Wii'}
        raise SystemExit(f'This folder already holds the {names[edition]} build. Build {names[wanted]} '
                         f'from a separate copy of the source (unzip or clone it into another folder).')
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(wanted + '\n')
    check_tools()
    build_cia.stage_sources()
    build_cia.stage_disc(a.disc)
    build_cia.check_game_code(a.disc)
    # Staged files are hard links to the cooked ones (tools/package_sd.py), and cooking
    # rewrites those in place: drop the old output first rather than leave it half updated.
    if OUT.is_dir() and any(f.stat().st_nlink > 1 for f in OUT.glob('*.pak')):
        try:
            shutil.rmtree(OUT)
        except PermissionError as e:
            raise SystemExit(f'{e.filename} is in use. Finish or stop any copy from {OUT} (an FTP upload?), '
                             'then run the build again.')
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
    build_cia.log(f'Copy the contents of that folder to ux0:data/{data} on your Vita.')


if __name__ == '__main__':
    main()
