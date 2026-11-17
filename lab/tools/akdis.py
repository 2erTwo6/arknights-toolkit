#!/usr/bin/env python3
import capstone, sys
so = 'tmp/apk/arm64/libil2cpp.so'
addr = int(sys.argv[1], 16)
ln = int(sys.argv[2], 16)
data = open(so, 'rb').read()
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
md.detail = False
code = data[addr:addr+ln]
for ins in md.disasm(code, addr):
    print(f'{ins.address:X}: {ins.mnemonic:8s} {ins.op_str}')
