"""List KMP objects that Kinoko builds as ObjectNoImpl, per course.
python kmp_noimpl.py <rr repo> <objflow names json or ''>"""
import json, re, struct, sys
from collections import Counter, defaultdict
from pathlib import Path

repo = Path(sys.argv[1])
kin = repo / 'vendor/kinoko/source/game/field'
ids = {}
for name, val in re.findall(r'^\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)', (kin / 'obj/ObjectId.hh').read_text(), re.M):
    ids[int(val, 0)] = name
director = (kin / 'ObjectDirector.cc').read_text()
# The factory switch: every `case ObjectId::X:` before the ObjectNoImpl default.
factory = director[director.find('createObject'):]
implemented = set(re.findall(r'case ObjectId::(\w+)', factory))

names = {}
cat = json.loads((repo / 'data/extracted/rr-catalog.json').read_text())
for p in cat['packs']:
    for cup in p['cups']:
        for t in cup['tracks']:
            names[t['file']] = t['name']


def gobj(path):
    d = path.read_bytes()
    if d[:4] != b'RKMD':
        return []
    count, header = struct.unpack('>HH', d[8:12])
    offs = struct.unpack('>%dI' % count, d[16:16 + 4 * count])
    out = []
    for o in offs:
        s = header + o
        if d[s:s + 4] == b'GOBJ':
            n = struct.unpack('>H', d[s + 4:s + 6])[0]
            for i in range(n):
                e = s + 8 + i * 0x3c
                out.append(struct.unpack('>H', d[e:e + 2])[0])
    return out


per = defaultdict(Counter)
total = Counter()
for kmp in sorted((repo / 'assets/courses').glob('*/course.kmp')):
    for oid in gobj(kmp):
        name = ids.get(oid, hex(oid))
        if name not in implemented:
            per[kmp.parent.name][name] += 1
            total[name] += 1
print('object kinds Kinoko does not build (courses using them):')
users = Counter()
for c, cnt in per.items():
    for n in cnt:
        users[n] += 1
for n, k in users.most_common(60):
    print(f'{k:4d}  {n}')
print()
for key in ['3DS Bowser', 'Mushroom', 'SW2', 'Wii U Wild', 'Wii U Animal', 'Wii U Super Bell', 'Wii U Big Blue']:
    for c, cnt in per.items():
        nm = names.get(c, c)
        if key in nm:
            print(nm, dict(cnt))
