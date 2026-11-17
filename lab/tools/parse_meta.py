#!/usr/bin/env python3
"""离线解析 il2cpp v29 metadata（已按 XOR 规律解码的副本）：导出 类型/方法/字段 名表。"""
import struct, sys, collections

path = sys.argv[1] if len(sys.argv) > 1 else 'tmp/re/meta_v29.dat'
data = open(path, 'rb').read()

TYPEDEF_SIZE = 92
METHOD_SIZE = 32
FIELD_SIZE = 12

S = {
    'string': (1716496, 8271112),
    'methods': (10938344, 9851744),
    'fields': (30196192, 4485480),
    'typeDefinitions': (37456824, 4650416),
    'images': (42107240, 3560),
    'assemblies': (42110800, 5696),
}

soff, ssize = S['string']

def getstr(idx):
    if idx < 0 or idx >= ssize: return None
    end = data.find(b'\x00', soff + idx)
    if end < 0: return None
    return data[soff + idx:end].decode('utf-8', 'replace')

toff, tsize = S['typeDefinitions']
ntypes = tsize // TYPEDEF_SIZE
types = []
for i in range(ntypes):
    o = toff + i * TYPEDEF_SIZE
    nameIdx, nsIdx = struct.unpack_from('<2i', data, o)
    parent = struct.unpack_from('<i', data, o + 20)[0]
    flags, fieldStart, methodStart, eventStart, propStart, nestedStart, ifaceStart, vtStart, ifaceOffStart = struct.unpack_from('<9I', data, o + 28)
    mcount, pcount, fcount, ecount, ncount, vcount, icount, iocount = struct.unpack_from('<8H', data, o + 68)
    bitfield, token = struct.unpack_from('<2I', data, o + 84)
    types.append((getstr(nsIdx) or '', getstr(nameIdx) or '', methodStart, mcount, fieldStart, fcount, parent))

moff, msize = S['methods']
nmethods = msize // METHOD_SIZE
methods = []
for i in range(nmethods):
    o = moff + i * METHOD_SIZE
    nameIdx, declType, retType, paramStart, gen, tok = struct.unpack_from('<6I', data, o)
    fl, ifl, slot, pcount = struct.unpack_from('<4H', data, o + 24)
    methods.append((getstr(nameIdx) or '', declType, pcount, fl, tok))

foff, fsize = S['fields']
nfields = fsize // FIELD_SIZE
fields = []
for i in range(nfields):
    o = foff + i * FIELD_SIZE
    nameIdx, typeIdx, tok = struct.unpack_from('<2I', data, o) + (0,)
    fields.append((getstr(nameIdx) or '', typeIdx))

print(f'types={ntypes} methods={nmethods} fields={nfields}', file=sys.stderr)

out = open('tmp/lab/dump.txt', 'w')
for ti, (ns, name, mstart, mcount, fstart, fcount, parent) in enumerate(types):
    full = f'{ns}.{name}' if ns else name
    out.write(f'C {full} parent={parent} methods={mcount} fields={fcount}\n')
    for mi in range(mstart, min(mstart + mcount, nmethods)):
        mn, dt, pc, fl, tok = methods[mi]
        if dt != ti: continue
        out.write(f'  M {mn}/{pc} flags=0x{fl:x} token=0x{tok:x}\n')
    for fi in range(fstart, min(fstart + fcount, nfields)):
        out.write(f'  F {fields[fi][0]}\n')
out.close()
print('wrote tmp/lab/dump.txt', file=sys.stderr)
