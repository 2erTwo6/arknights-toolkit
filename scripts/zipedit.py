#!/usr/bin/env python3
"""Edit a non-zip64 APK: replace or add entries, preserving other entries raw.
Usage: zipedit.py SRC DST SPEC_JSON
SPEC_JSON: {"replace": {"entry": "/path/to/newdata"}, "add": {"/path/in/zip": "/path/to/data"}}
"""
import struct, sys, zlib, os, json

def parse_cd(path):
    f = open(path, 'rb'); size = os.path.getsize(path)
    f.seek(size - 65536); tail = f.read()
    i = tail.rfind(b'PK\x05\x06'); assert i >= 0, "no EOCD"
    sig, disk, cddisk, nrd, nrec, cdsize, cdoff, clen = struct.unpack('<IHHHHIIH', tail[i:i+22])
    assert sig == 0x06054b50 and tail.rfind(b'PK\x06\x07') == -1
    f.seek(cdoff); cd = f.read(cdsize)
    pos = 0; entries = []
    for _ in range(nrec):
        assert cd[pos:pos+4] == b'PK\x01\x02'
        (vmade, vneed, flags, method, mtime, mdate, crc, csize, usize, nlen, elen, clen2,
         dstart, iattr, eattr, lho) = struct.unpack_from('<HHHHHHIIIHHHHHII', cd, pos+4)
        name = cd[pos+46:pos+46+nlen].decode('utf-8','replace')
        extra = cd[pos+46+nlen:pos+46+nlen+elen]
        comment = cd[pos+46+nlen+elen:pos+46+nlen+elen+clen2]
        d = dict(name=name, vmade=vmade, vneed=vneed, flags=flags, method=method, mtime=mtime,
                 mdate=mdate, crc=crc, csize=csize, usize=usize, extra=extra, comment=comment,
                 dstart=dstart, iattr=iattr, eattr=eattr, lho=lho)
        entries.append(d); pos += 46 + nlen + elen + clen2
    assert pos == cdsize
    return f, entries

def main(src, dst, spec_path):
    spec = json.load(open(spec_path))
    repl = spec.get('replace', {}); add = spec.get('add', {}); drop = set(spec.get('drop', []))
    f, entries = parse_cd(src)
    names = {e['name'] for e in entries}
    for n in list(repl) + list(add):
        assert not (n in add and n in names), f"exists: {n}"
    out = open(dst, 'wb'); written = {}
    for e in entries:
        f.seek(e['lho']); lh = f.read(30)
        (lsig, lver, lflags, lmeth, ltime, ldate, lcrc, lcsize, lusize, lnlen, lelen) = struct.unpack('<IHHHHHIIIHH', lh)
        assert lsig == 0x04034b50
        lname = f.read(lnlen); lextra = f.read(lelen)
        f.seek(e['lho'] + 30 + lnlen + lelen)
        raw = f.read(e['csize'])
        desc = b''
        if e['flags'] & 0x08:
            desc = f.read(16)
            if desc[:4] != b'PK\x07\x08': desc = desc[:12]
        crc, csize, usize, flags = e['crc'], e['csize'], e['usize'], e['flags']
        if e['name'] in repl:
            data = open(repl[e['name']], 'rb').read()
            crc = zlib.crc32(data) & 0xffffffff; usize = len(data)
            co = zlib.compressobj(9, zlib.DEFLATED, -15)
            raw = co.compress(data) + co.flush(); csize = len(raw)
            # choose extra length so that the entry's record length is unchanged when possible,
            # otherwise changed by a multiple of 4 (keeps resources.arsc 4-byte aligned)
            desc_len = 16 if (flags & 0x08) else 0
            L_old = 30 + len(lname) + len(lextra) + e['csize'] + desc_len
            base_len = 30 + len(lname) + csize + desc_len
            ideal = L_old - base_len  # extra_len that keeps record length identical
            if ideal >= 4 or ideal == 0:
                extra_len = ideal
            else:
                extra_len = ideal % 4
                if extra_len == 0:
                    extra_len = 0
                elif extra_len < 4:
                    extra_len += 4
            if extra_len:
                new_extra = struct.pack('<HH', 0xFFFF, extra_len - 4) + b'\x00' * (extra_len - 4)
            else:
                new_extra = b''
            lextra = new_extra
            lelen = len(lextra)
            if flags & 0x08:
                desc = struct.pack('<IIII', 0x08074b50, crc, csize, usize)
            print(f"replace {e['name']}: {e['csize']} -> {csize}, extra_len {extra_len}, delta {base_len+extra_len-L_old}")
        if e['name'] in drop:
            continue
        new_off = out.tell(); written[e['name']] = (new_off, crc, csize, usize, flags, lname, lextra, e)
        out.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, lver, flags, lmeth, ltime, ldate, crc, csize, usize, lnlen, lelen))
        out.write(lname); out.write(lextra); out.write(raw); out.write(desc)
    for name, path in add.items():
        data = open(path, 'rb').read()
        crc = zlib.crc32(data) & 0xffffffff; usize = len(data)
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        raw = co.compress(data) + co.flush()
        n = name.encode()
        fixed = spec.get('fixed_record_len', {}).get(name)
        extra = b''
        if fixed:
            base_len = 30 + len(n) + len(raw)
            pad = fixed - base_len
            assert pad >= 0, f"fixed_record_len too small: need {base_len}"
            if pad:
                assert pad >= 4
                chunks = []
                left = pad
                while left > 0:
                    fld = min(left, 65535)
                    if fld < 4:
                        raise SystemExit("bad pad chunk")
                    chunks.append(struct.pack('<HH', 0xFFFF, fld - 4) + b'\x00' * (fld - 4))
                    left -= fld
                extra = b''.join(chunks)
                assert len(extra) == pad
        new_off = out.tell()
        e = dict(vmade=0x14, vneed=0x14, method=8, mtime=0, mdate=0x21, extra=extra, comment=b'', dstart=0, iattr=0, eattr=0)
        written[name] = (new_off, crc, len(raw), usize, 0, n, extra, e)
        out.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 0x14, 0, 8, 0, 0x21, crc, len(raw), usize, len(n), len(extra)))
        out.write(n); out.write(extra); out.write(raw)
        print(f"add {name}: {len(data)} bytes, extra {len(extra)}, record {30+len(n)+len(extra)+len(raw)}")
    cd_start = out.tell()
    for name, (new_off, crc, csize, usize, flags, lname, lextra, e) in written.items():
        out.write(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, e['vmade'], e['vneed'], flags,
            e['method'], e['mtime'], e['mdate'], crc, csize, usize, len(lname),
            len(lextra), len(e['comment']), e['dstart'], e['iattr'], e['eattr'], new_off))
        out.write(lname); out.write(lextra); out.write(e['comment'])
    cd_size = out.tell() - cd_start
    out.write(struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(written), len(written), cd_size, cd_start, 0))
    out.close(); f.close()
    print(f"wrote {dst}: {os.path.getsize(dst)} bytes, {len(written)} entries")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
