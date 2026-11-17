#!/usr/bin/env python3
"""Compute common prefix and extract tail for delta push of APK to device."""
import sys, os, hashlib
base, new, tail_out = sys.argv[1], sys.argv[2], sys.argv[3]
sa, sb = os.path.getsize(base), os.path.getsize(new)
fa, fb = open(base,'rb'), open(new,'rb')
chunk = 1<<20
i = 0
while True:
    x = fa.read(chunk); y = fb.read(chunk)
    if x == y:
        i += len(x); continue
    for j in range(min(len(x), len(y))):
        if x[j] != y[j]:
            i += j
            break
    break
fa.close()
# write tail from i
fb.seek(i)
h = hashlib.sha256()
with open(tail_out,'wb') as f:
    while True:
        b = fb.read(1<<20)
        if not b: break
        h.update(b); f.write(b)
print(f"PREFIX={i} SIZE={sb} TAIL={tail_out} TAILSHA={h.hexdigest()}")
