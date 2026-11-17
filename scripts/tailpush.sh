#!/bin/bash
# usage: tailpush.sh <tag> <spec.json> <first_changed_entry> [wait]
set -e
cd /var/home/liu/文档/deepseek-harness/MRFZ
export JAVA_HOME=$PWD/tmp/tools/jdk-17.0.21+9; export PATH=$JAVA_HOME/bin:$PATH
TAG=$1; SPEC=$2; ENTRY=$3; WAIT=${4:-40}; BASE=${BASE:-ak_v2}
python3 tmp/zipedit.py tmp/work/ak_stripped.apk tmp/work/ak_$TAG.apk "$SPEC" >/dev/null
tmp/tools/android-14/apksigner sign --ks tmp/work/test.jks --ks-pass pass:testpass --key-pass pass:testpass \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true tmp/work/ak_$TAG.apk >/dev/null
A=$(python3 - "$ENTRY" <<'PY'
import struct,sys,os
p='tmp/work/ak_base_v2_s.apk'
want=sys.argv[1]
f=open(p,'rb'); size=os.path.getsize(p); f.seek(size-65536); t=f.read()
i=t.rfind(b'PK\x05\x06'); sig,disk,cddisk,nrd,nrec,cdsize,cdoff,clen=struct.unpack('<IHHHHIIH',t[i:i+22])
f.seek(cdoff); cd=f.read(cdsize); pos=0
for _ in range(nrec):
    (vmade,vneed,flags,method,mtime,mdate,crc,csize,usize,nlen,elen,clen2,dstart,iattr,eattr,lho)=struct.unpack_from('<HHHHHHIIIHHHHHII', cd, pos+4)
    nm=cd[pos+46:pos+46+nlen].decode()
    if nm==want: print(lho); sys.exit(0)
    pos+=46+nlen+elen+clen2
sys.exit("not found")
PY
)
tail -c +$((A+1)) tmp/work/ak_$TAG.apk > tmp/work/tail_$TAG.bin
scp -F ~/.ssh/config -q tmp/work/tail_$TAG.bin phone:/data/local/tmp/tp_tail.bin
ssh -F ~/.ssh/config phone "cd /data/local/tmp && head -c $A $BASE.apk > ak_$TAG.apk && cat tp_tail.bin >> ak_$TAG.apk && echo dev=\$(sha256sum ak_$TAG.apk|cut -d' ' -f1) && pm install -r -d -g ak_$TAG.apk 2>&1|tail -1"
echo "loc=$(sha256sum tmp/work/ak_$TAG.apk|cut -d' ' -f1)"
