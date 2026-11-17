#!/usr/bin/env python3
"""Apply string patches to split14: args = name=url pairs, keep aligned length."""
import struct, sys, re
src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src,'rb').read())
urls = []
for m in re.finditer(rb'https?://', data):
    s = m.start()
    if s < 4: continue
    ln = struct.unpack_from('<I', data, s-4)[0]
    if 5 <= ln <= 400 and s+ln <= len(data):
        t = data[s:s+ln]
        if all(32 <= c < 127 for c in t):
            urls.append((s, ln, t.decode()))
idx = {u[2]: u for u in urls}
for spec in sys.argv[3:]:
    old, new = spec.split('=>')
    assert old in idx, f"not found: {old}"
    s, ln, txt = idx[old]
    old_al = (ln + 3)//4*4
    newb = new.encode()
    new_al = (len(newb)+3)//4*4
    assert len(newb) <= old_al and new_al == old_al, f"align mismatch old {ln}/{old_al} new {len(newb)}/{new_al}"
    struct.pack_into('<I', data, s-4, len(newb))
    data[s:s+old_al] = newb + b'\x00'*(old_al-len(newb))
    print(f"patched {txt[:50]} -> {new} ({len(newb)} bytes, slot {old_al})")
open(dst,'wb').write(data)
