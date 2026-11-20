import hashlib
import json
import struct
from pathlib import Path
root=Path(__file__).resolve().parents[1]
page=lambda n:(n+4095)&~4095
def package(name):
    elf=(root/'fixture'/f'{name}.elf').read_bytes()
    if elf[:6]!=b'\x7fELF\x02\x01':raise ValueError('ELF64 little endian required')
    offset=struct.unpack_from('<Q',elf,32)[0];stride,count=struct.unpack_from('<HH',elf,54)
    headers=[struct.unpack_from('<IIQQQQQQ',elf,offset+i*stride) for i in range(count)]
    loads=[h for h in headers if h[0]==1]
    if len(loads)!=3:raise ValueError('three load segments required')
    text,ro,data=loads
    for h in loads:
        if h[2]+h[5]>len(elf) or h[5]>h[6]:raise ValueError('segment bounds')
    if text[3]!=0 or ro[3]%4096 or data[3]%4096:raise ValueError('segment alignment')
    size=4096+data[3]+page(data[5]);bss=page(data[6])-page(data[5]);out=bytearray(size)
    struct.pack_into('<II',out,0,0xd65f03c0,0x100);out[0x10:0x14]=b'NRO0'
    struct.pack_into('<IIIIIIIIII',out,0x18,size,1,0,4096+ro[3],4096+ro[3],data[3]-ro[3],4096+data[3],page(data[5]),bss,0)
    out[0x40:0x60]=hashlib.sha256(elf).digest();out[0x100:0x104]=b'MOD0'
    for i,magic in enumerate((b'LNY0',b'LNY1',b'LNY2',b'OUN0')):out[0x11c+12*i:0x120+12*i]=magic
    struct.pack_into('<II',out,0x144,2,1);out[0x180:0x188]=b'NX2MOD01';struct.pack_into('<I',out,0x188,1)
    dynamic=next(h[3] for h in headers if h[0]==2);struct.pack_into('<I',out,0x18c,dynamic)
    for i,h in enumerate(loads):
        struct.pack_into('<III',out,0x190+12*i,h[3],h[5],h[6]);out[4096+h[3]:4096+h[3]+h[5]]=elf[h[2]:h[2]+h[5]]
    tls=next((h for h in headers if h[0]==7 and h[6]),None)
    if tls:struct.pack_into('<IIII',out,0x1c0,tls[3],tls[5],tls[6],tls[7])
    path=root/'fixture'/f'{name}.nxm';path.write_bytes(out)
    return {'name':name,'bytes':size,'bss':bss,'sha256':hashlib.sha256(out).hexdigest(),'tls_bytes':tls[6] if tls else 0}
records=[package(n) for n in ('provider','client')]
(root/'fixture/reference.json').write_text(json.dumps(records,indent=2))
(root/'source/fixture_hash.h').write_text('\n'.join(f'#define {r["name"].upper()}_SHA256 "{r["sha256"]}"' for r in records)+'\n')
print(json.dumps(records,indent=2))
