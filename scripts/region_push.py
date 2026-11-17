#!/usr/bin/env python3
"""Prepare a region-based push for one changed zip entry + tail."""
import struct, sys, os, json

def parse_cd(path):
    f = open(path, 'rb'); size = os.path.getsize(path)
    f.seek(max(0,size-65536)); tail = f.read()
    i = tail.rfind(b'PK\x05\x06'); assert i >= 0
    sig, disk, cddisk, nrd, nrec, cdsize, cdoff, clen = struct.unpack('<IHHHHIIH', tail[i:i+22])
    f.seek(cdoff); cd = f.read(cdsize)
    pos = 0; entries = {}
    for _ in range(nrec):
        (vmade, vneed, flags, method, mtime, mdate, crc, csize, usize, nlen, elen, clen2,
         dstart, iattr, eattr, lho) = struct.unpack_from('<HHHHHHIIIHHHHHII', cd, pos+4)
        name = cd[pos+46:pos+46+nlen].decode('utf-8','replace')
        entries[name] = dict(flags=flags, csize=csize, lho=lho, nlen=nlen, elen=elen)
        pos += 46+nlen+elen+clen2
    return f, entries, cdoff, size

def record_len(f, e):
    f.seek(e['lho'])
    hdr = f.read(30)
    (lsig, lver, lflags, lmeth, ltime, ldate, lcrc, lcsize, lusize, lnlen, lelen) = struct.unpack('<IHHHHHIIIHH', hdr)
    assert lsig == 0x04034b50
    dl = 0
    if lflags & 0x08:
        # read descriptor
        f.seek(e['lho'] + 30 + lnlen + lelen + lcsize)
        d = f.read(16)
        dl = 16 if d[:4] == b'PK\x07\x08' else 12
    return 30 + lnlen + lelen + lcsize + dl, e['lho']

def sig_block_start(path, cdoff):
    f=open(path,'rb'); f.seek(cdoff-24); b=f.read(24); f.close()
    if len(b)==24 and b[8:24]==b'APK Sig Block 42':
        size=struct.unpack('<Q', b[0:8])[0]
        return cdoff - size - 8
    return cdoff

def verify(base, new, A, L_old, L_new, suffix_len, tail_start):
    b = open(base,'rb'); v = open(new,'rb')
    v.seek(A); region = v.read(L_new)
    b.seek(A+L_old); suffix = b.read(suffix_len)
    v.seek(tail_start); tail = v.read()
    b.seek(0); v.seek(0)
    hdr = b.read(A) + region + suffix + tail
    vv = v.read()
    if len(hdr) != len(vv):
        return False
    return hdr == vv

def main(base, new, entry, outdir):
    fb, eb, cdob, _ = parse_cd(base)
    fn, en, cdon, szn = parse_cd(new)
    assert entry in eb and entry in en
    L_old, A = record_len(fb, eb[entry])
    L_new, A2 = record_len(fn, en[entry])
    assert A == A2, (A, A2)
    reg = os.path.join(outdir, 'region.bin')
    fb.seek(A); fn.seek(A)
    with open(reg,'wb') as o: o.write(fn.read(L_new))
    tail = os.path.join(outdir, 'tail.bin')
    sgn = sig_block_start(new, cdon)
    sgb = sig_block_start(base, cdob)
    # base entries after our changed entry: up to the end of the last real entry (exclude apksig padding)
    last_end = 0
    for nm, e in eb.items():
        rl, _lho = record_len(fb, e)
        if _lho + rl > last_end:
            last_end = _lho + rl
    suffix_len = max(0, min(sgb, last_end) - (A + L_old))
    tail_start = A + L_new + suffix_len
    assert tail_start <= sgn, (tail_start, sgn)
    fn.seek(tail_start); 
    with open(tail,'wb') as o: o.write(fn.read(szn-tail_start))
    # suffix from base between entry end and old CD
    # verify reconstruction locally
    ok = verify(base, new, A, L_old, L_new, suffix_len, tail_start)
    info = dict(verify=ok,A=A, L_old=L_old, L_new=L_new, cd_old=cdob, cd_new=cdon, size=szn,
                suffix_start=A+L_old, suffix_len=suffix_len, sig_start_new=sgn, sig_start_old=sgb, tail_start=tail_start)
    print(json.dumps(info))
    fb.close(); fn.close()

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
