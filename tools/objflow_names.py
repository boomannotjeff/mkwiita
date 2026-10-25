"""Print ObjFlow rows (id, name, resources, collision mode/params) for the given ids.
python objflow_names.py <ObjFlow.bin> id...  (hex or names allowed as hex only)"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
count = struct.unpack('>h', d[:2])[0]
rows = {}
for i in range(count):
    e = 2 + i * 0x74
    oid = struct.unpack('>H', d[e:e + 2])[0]
    name = d[e + 2:e + 34].split(b'\0')[0].decode('latin1')
    res = d[e + 34:e + 98].split(b'\0')[0].decode('latin1')
    mode = struct.unpack('>h', d[e + 0x6a:e + 0x6c])[0]
    params = struct.unpack('>hhh', d[e + 0x6c:e + 0x72])
    rows[oid] = (name, res, mode, params)
for a in sys.argv[2:]:
    oid = int(a, 16)
    print(hex(oid), rows.get(oid, ('<not in ObjFlow>',)))
