#!/usr/bin/env python3
"""Staff and expert staff ghosts as runs the game can race and watch.

The disc's ghosts (Race/TimeAttack/ghost1, ghost2) hold only inputs. The
port's Time Trial ghosts also keep the kart's pose every tick (see
include/mkw/time_trial_ghost.hpp), so each RKG is replayed through Kinoko on
this PC (tools/staff_ghost_run.cc) and written as the same .mkg run:

    ghosts/staff/course_NN_staff.mkg, course_NN_expert.mkg (menu course order)

plus include/mkw/staff_ghost_info.hpp: each ghost's name and Mii data.

The replay tool is built here with the host's C++ compiler (clang++ or g++;
on Windows llvm-mingw: winget install MartinStorsjo.LLVM-MinGW.UCRT). Without
one the step reports it and leaves the staff ghosts out.
"""
import json, os, re, shutil, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXTRACTED = ROOT / 'data/extracted'
GHOSTS = EXTRACTED / 'Race/TimeAttack'
KINOKO = ROOT / 'vendor/kinoko'
WORK = ROOT / 'build/host-tools'
TOOL = WORK / ('staff_ghost_run.exe' if os.name == 'nt' else 'staff_ghost_run')
OUT = ROOT / 'sd-card/3ds/mkw-3ds/ghosts/staff'
HEADER = ROOT / 'include/mkw/staff_ghost_info.hpp'
MAGIC, VERSION = 0x4d4b4748, 2  # TimeTrialGhost "MKGH" v2


def compiler():
    found = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not found and os.name == 'nt':
        winget = Path(os.environ.get('LOCALAPPDATA', '')) / 'Microsoft/WinGet/Packages'
        found = next((str(p) for p in sorted(winget.glob('MartinStorsjo.LLVM-MinGW*/*/bin/clang++.exe'))), None)
    return found


def build_tool(cxx):
    sources = [p for p in (KINOKO / 'source').rglob('*.cc') if p.name != 'main.cc']
    newest = max(p.stat().st_mtime for p in sources + [ROOT / 'tools/staff_ghost_run.cc', Path(__file__)])
    if TOOL.is_file() and TOOL.stat().st_mtime > newest:
        return
    objects = WORK / 'kinoko'
    objects.mkdir(parents=True, exist_ok=True)
    # Kinoko's headers lean on GCC's transitive standard includes; name them for libc++.
    flags = ['-std=c++23', '-O2', '-DREVOLUTION', '-fno-exceptions', '-fno-rtti', '-fshort-wchar', '-w',
             '-I', str(KINOKO / 'include'), '-I', str(KINOKO / 'source'), '-I', str(ROOT / 'include')]
    for header in ('bit', 'algorithm', 'cstring', 'cmath', 'array', 'sstream'):
        flags += ['-include', header]

    def compile_one(p):
        o = objects / (p.relative_to(KINOKO).as_posix().replace('/', '_') + '.o')
        if not o.is_file() or o.stat().st_mtime < p.stat().st_mtime:
            subprocess.run([cxx, *flags, '-c', str(p), '-o', str(o)], check=True)
        return str(o)
    with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        built = list(pool.map(compile_one, sources))
    subprocess.run([cxx, *flags, '-static', str(ROOT / 'tools/staff_ghost_run.cc'), *built, '-o', str(TOOL)], check=True)


def garage_choice(garage, character, vehicle):
    for c in garage['choices']:
        if c['character'] == character and c['vehicle'] == vehicle:
            return c['driverIndex'] * 12 + c['slot']
    raise ValueError(f'no garage choice for character {character} vehicle {vehicle}')


def main():
    cxx = compiler()
    if not cxx:
        print('staff ghost runs: no C++ compiler (clang++/g++); staff ghosts are left out')
        write_header([('', b'')] * 64)
        return
    if not GHOSTS.is_dir():
        raise SystemExit('Race/TimeAttack is not extracted (tools/extract_disc.py --prefix Race/TimeAttack/)')
    build_tool(cxx)
    garage = json.loads((ROOT / 'sd-card/3ds/mkw-3ds/models/garage.json').read_text())
    # Menu order to course id, as tools/cook_staff_ghosts.py.
    ids = [int(n) for n in re.findall(r'models/course_(\d+)\.list', (ROOT / 'include/mkw/course_catalog.hpp').read_text())[:32]]
    menu = {course: i for i, course in enumerate(ids)}
    OUT.mkdir(parents=True, exist_ok=True)
    info = {}
    for folder, kind in (('ghost1', 'staff'), ('ghost2', 'expert')):
        for i in range(32):
            rkg = GHOSTS / folder / f'{folder}_comp_{i:02}.rkg'
            raw = WORK / f'{folder}_{i:02}.bin'
            subprocess.run([str(TOOL), str(rkg.relative_to(EXTRACTED)), str(raw)], cwd=EXTRACTED, check=True)
            data = raw.read_bytes()
            frames, ms, course, character, vehicle, auto = struct.unpack_from('<6I', data)
            mii = data[24:24 + 0x4a]
            moves = data[24 + 0x4a:24 + 0x4a + frames * 4]
            poses = data[24 + 0x4a + frames * 4:]
            assert len(poses) == frames * 44, rkg
            slot = menu[course]
            header = struct.pack('<7I', MAGIC, VERSION, slot, garage_choice(garage, character, vehicle), auto, ms, frames)
            (OUT / f'course_{slot:02}_{kind}.mkg').write_bytes(header + moves + poses)
            name = mii[2:22].decode('utf-16-be').split('\0')[0]
            info.setdefault(slot, {})[kind] = (name, mii)
    write_header([info[slot][kind] for slot in range(32) for kind in ('staff', 'expert')])
    print(f'staff ghost runs: {OUT.relative_to(ROOT)} (64), {HEADER.relative_to(ROOT)}')


def write_header(ghosts):
    rows = ['    {%s,{%s}}' % (json.dumps(name), ','.join(str(b) for b in mii) or '0') for name, mii in ghosts]
    HEADER.write_text(
        '// Generated by tools/cook_staff_ghost_runs.py from the disc (RKG Mii data).\n'
        '#pragma once\n#include <cstdint>\nnamespace mkw {\n'
        '// Per course in menu order: the staff ghost, then the expert staff ghost.\n'
        '// name: the Mii name (UTF-8); mii: the RKG\'s 0x4A bytes of Mii data.\n'
        'struct StaffGhostInfo {const char* name;uint8_t mii[0x4a];};\n'
        'inline constexpr StaffGhostInfo StaffGhostInfos[64]={\n' + ',\n'.join(rows) + '\n};\n}\n', encoding='utf-8')


if __name__ == '__main__':
    main()
