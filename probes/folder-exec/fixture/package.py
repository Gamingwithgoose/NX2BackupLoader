"""Package this project's own ELF fixture in an NRO mapping envelope.
The original ELF virtual layout is preserved after a 4 KiB prefix. Not a game launcher.
"""
import hashlib
import json
import struct
from pathlib import Path

root=Path(__file__).resolve().parents[1]
elf=(root/'fixture/module.elf').read_bytes()
assert elf[:6]==b'\x7fELF\x02\x01'
headers=struct.unpack_from('<Q',elf,32)[0]
stride,count=struct.unpack_from('<HH',elf,54)
segments=[]
for i in range(count):
    kind,flags,offset,vaddr,paddr,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',elf,headers+i*stride)
    if kind==1:
        assert offset+filesz<=len(elf) and filesz<=memsz
        segments.append((vaddr,elf[offset:offset+filesz],memsz,flags))
assert len(segments)==3
text,ro,data=segments
assert text[0]==0 and ro[0]%4096==0 and data[0]%4096==0
round_page=lambda n:(n+4095)&~4095
prefix=4096
data_size=round_page(len(data[1]))
image_size=prefix+data[0]+data_size
bss_size=round_page(data[2])-data_size
image=bytearray(image_size)
struct.pack_into('<I',image,0,0xd65f03c0) # RET: prefix is not a module entrypoint.
struct.pack_into('<I',image,4,0x100)
image[0x10:0x14]=b'NRO0'
struct.pack_into('<IIIIIIIIII',image,0x18,image_size,1,0,prefix+ro[0],prefix+ro[0],data[0]-ro[0],prefix+data[0],data_size,bss_size,0)
image[0x40:0x60]=hashlib.sha256(elf).digest()
image[0x100:0x104]=b'MOD0'
for i,magic in enumerate((b'LNY0',b'LNY1',b'LNY2',b'OUN0')):
    image[0x11c+12*i:0x120+12*i]=magic
struct.pack_into('<II',image,0x144,2,1)
image[0x180:0x188]=b'NX2MOD01'
struct.pack_into('<I',image,0x188,1)
# Original virtual segments and dynamic section address for the bounded loader.
for i,(address,payload,memory,flags) in enumerate(segments):
    struct.pack_into('<III',image,0x190+i*12,address,len(payload),memory)
    image[prefix+address:prefix+address+len(payload)]=payload
dynamic=next(struct.unpack_from('<Q',elf,headers+i*stride+16)[0] for i in range(count) if struct.unpack_from('<I',elf,headers+i*stride)[0]==2)
struct.pack_into('<I',image,0x18c,dynamic)
target=root/'fixture/module.nxm'
target.write_bytes(image)
digest=hashlib.sha256(image).hexdigest()
(root/'source/fixture_hash.h').write_text('#define FIXTURE_SHA256 "'+digest+'"\n')
(root/'fixture/reference.json').write_text(json.dumps({'bytes':len(image),'sha256':digest,'bss':bss_size,'input':41,'expected_returns':[4702,4703],'scope':'Own test module, not Yoshi'},indent=2))
print('Packaged owned fixture:',len(image),'bytes, SHA256',digest)
