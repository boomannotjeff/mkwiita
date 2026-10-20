#!/usr/bin/env python3
"""Pack race-time models, animation banks (incl. course-object .dobj) and textures into race.pak.

Same MPK1 format and rationale as tools/pack_menu_assets.py: loading a race
opens hundreds of small files (track parts, twelve racers, items and HUD), and
each open/close costs far more than its read. The selection is exactly the
validated package list from tools/package_sd.py, minus what menu.pak holds.

An MPK1 offset is read with a 32-bit seek (src/asset_pack.cpp), so a pack stays
under 2 GiB: more assets (the Retro Rewind edition's 300 tracks) continue in
race2.pak, race3.pak... which the runtime opens after race.pak.
"""
import hashlib, json, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'sd-card/3ds/mkw-3ds'
PART_LIMIT = 1900 << 20  # bytes of data per pack, below the 2 GiB seek range
MAX_PARTS = 5            # race.pak .. race5.pak (src/asset_pack.cpp)


def menu_pak_names():
    path = ASSETS / 'menu.pak'
    if not path.is_file():
        return set()
    data = path.read_bytes()
    count, names_bytes = struct.unpack_from('<II', data, 4)
    blob = data[12 + 12 * count:12 + 12 * count + names_bytes]
    return {n.decode() for n in blob.split(b'\0') if n}


def part_path(index):
    return ASSETS / ('race.pak' if index == 0 else f'race{index + 1}.pak')


def write_pack(output, names, listing):
    blob = b''.join(n.encode() + b'\0' for n in names)
    header = 12 + 12 * len(names) + len(blob)
    offset = (header + 31) & ~31
    entries = []
    with output.open('wb') as out:
        sizes = [Path(listing[n]).stat().st_size for n in names]
        name_offset = 0
        for n, size in zip(names, sizes):
            entries.append((name_offset, offset, size))
            name_offset += len(n.encode()) + 1
            offset += (size + 31) & ~31
        if offset >= 1 << 31:
            raise SystemExit(f'{output.name} exceeds the 2 GiB seek range')
        out.write(b'MPK1' + struct.pack('<II', len(names), len(blob)))
        for entry in entries:
            out.write(struct.pack('<III', *entry))
        out.write(blob)
        out.write(b'\0' * (((header + 31) & ~31) - header))
        for n, size in zip(names, sizes):
            data = Path(listing[n]).read_bytes()
            out.write(data + b'\0' * (((size + 31) & ~31) - size))


def main():
    listing = json.loads(subprocess.check_output([sys.executable, str(ROOT / 'tools/package_sd.py'), '--list'], text=True))
    packed_menu = menu_pak_names()
    names = sorted(rel for rel in listing
                   if (rel.startswith('models/') and rel.endswith(('.gmdl', '.dskm', '.dobj')))
                   or (rel.startswith('textures/') and rel.endswith(('.t3x', '.bin'))))
    names = [n for n in names if n not in packed_menu]
    # The other models/ and textures/ files (track lists, effect records, receipts)
    # follow in parts of their own, so adding them leaves the model parts unchanged.
    # The runtime opens them through the pack (platform/vita/src/ctru_file.cpp).
    extra = sorted(rel for rel in listing if rel.startswith(('models/', 'textures/')) and rel not in names
                   and rel not in packed_menu and not rel.endswith('-pak.json'))
    # Each part takes names in order until its data would pass PART_LIMIT.
    parts = []
    for group in (names, extra):
        current, size = [], 0
        for n in group:
            padded = (Path(listing[n]).stat().st_size + 31) & ~31
            if current and size + padded > PART_LIMIT:
                parts.append(current); current, size = [], 0
            current.append(n); size += padded
        if current or not parts: parts.append(current)
    names += extra
    if len(parts) > MAX_PARTS:
        raise SystemExit(f'race assets need {len(parts)} packs; the runtime opens {MAX_PARTS}')
    for index, part in enumerate(parts):
        write_pack(part_path(index), part, listing)
    for index in range(len(parts), MAX_PARTS):
        part_path(index).unlink(missing_ok=True)
    receipt = {'version': 1, 'files': len(names),
               'parts': [{'pack': part_path(i).name, 'files': len(p), 'bytes': part_path(i).stat().st_size,
                          'sha256': hashlib.sha256(part_path(i).read_bytes()).hexdigest()} for i, p in enumerate(parts)]}
    if len(parts) == 1:  # the single-pack receipt fields, as before
        receipt.update(bytes=receipt['parts'][0]['bytes'], sha256=receipt['parts'][0]['sha256'])
    (ASSETS / 'models/race-pak.json').write_text(json.dumps(receipt, indent=1) + '\n')
    print(json.dumps(receipt, indent=1))


if __name__ == '__main__':
    main()
