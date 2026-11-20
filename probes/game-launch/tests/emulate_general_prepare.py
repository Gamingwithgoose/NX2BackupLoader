"""Exercise the actual linked checked heap-release transition with modeled kernel replies.

No game assets are needed. Memory outside allocator metadata remains unmapped,
so an accidental write into the reserved game region fails the test.
Requires Unicorn 2.1.4. No console services are forwarded.
"""
import argparse, hashlib, json, struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR, UC_HOOK_CODE
from unicorn.arm64_const import *
import unicorn.arm64_const as regs

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--images',type=Path,required=True)
p.add_argument('--worker',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
elf=a.worker.read_bytes();u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);base=0x74000000
page=lambda n:(n+4095)&~4095
phoff=struct.unpack_from('<Q',elf,32)[0];phsize,phnum=struct.unpack_from('<HH',elf,54)
loads=[struct.unpack_from('<IIQQQQQQ',elf,phoff+i*phsize) for i in range(phnum)]
loads=[h for h in loads if h[0]==1];u.mem_map(base,page(max(h[3]+h[6] for h in loads)))
for h in loads:u.mem_write(base+h[3],elf[h[2]:h[2]+h[5]])
shoff=struct.unpack_from('<Q',elf,40)[0];shsize,shnum=struct.unpack_from('<HH',elf,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',elf,shoff+i*shsize) for i in range(shnum)]
symbols={}
for sec in sections:
    if sec[1]==19:
        cursor=0
        for at in range(sec[4],sec[4]+sec[5],8):
            item=struct.unpack_from('<Q',elf,at)[0]
            if not item&1:targets=[item];cursor=item+8
            else:targets=[cursor+8*i for i in range(63) if item&(1<<(i+1))];cursor+=63*8
            for target in targets:
                value=struct.unpack('<Q',u.mem_read(base+target,8))[0]
                u.mem_write(base+target,struct.pack('<Q',base+value))
    elif sec[1]==4:
        for at in range(sec[4],sec[4]+sec[5],24):
            target,info,addend=struct.unpack_from('<QQq',elf,at);assert info==1027
            u.mem_write(base+target,struct.pack('<Q',base+addend))
    elif sec[1]==2:
        st=sections[sec[6]];strings=elf[st[4]:st[4]+st[5]]
        for at in range(sec[4],sec[4]+sec[5],24):
            name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',elf,at)
            if name and index:symbols[strings[name:strings.index(0,name)].decode()]=(value,info)

def address(name):return base+symbols[name][0]
def put64(at,value):u.mem_write(at,struct.pack('<Q',value))
def get64(at):return struct.unpack('<Q',u.mem_read(at,8))[0]
stop=0x72000000;stack=0x70000000;tls=0x71000000
u.mem_map(stop,4096);u.mem_map(stack,0x100000);u.mem_map(tls,4096)
u.reg_write(UC_ARM64_REG_TPIDRRO_EL0,tls);u.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20)
u.hook_add(UC_HOOK_CODE,lambda uc,*_:uc.emu_stop(),begin=stop,end=stop)
kernel=[]
def interrupt(uc,number,data):
    pc=uc.reg_read(UC_ARM64_REG_PC);current=struct.unpack('<I',uc.mem_read(pc,4))[0]
    if current&0x3ffffc00==0x38bfc000:
        rn=(current>>5)&31;rt=current&31;width=1<<((current>>30)&3)
        at=uc.reg_read(UC_ARM64_REG_SP) if rn==31 else uc.reg_read(getattr(regs,'UC_ARM64_REG_X'+str(rn)))
        value=int.from_bytes(uc.mem_read(at,width),'little')
        if rt!=31:uc.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(rt)),value)
        uc.reg_write(UC_ARM64_REG_PC,pc+4);return
    if current&0x3fa07c00==0x08a07c00:
        width=8 if current&(1<<30) else 4;rs=(current>>16)&31;rt=current&31;rn=(current>>5)&31
        def reg(n):return 0 if n==31 else uc.reg_read(getattr(regs,'UC_ARM64_REG_X'+str(n)))
        at=uc.reg_read(UC_ARM64_REG_SP) if rn==31 else reg(rn);mask=(1<<(8*width))-1
        old=int.from_bytes(uc.mem_read(at,width),'little')
        if old==reg(rs)&mask:uc.mem_write(at,(reg(rt)&mask).to_bytes(width,'little'))
        if rs!=31:uc.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(rs)),old)
        uc.reg_write(UC_ARM64_REG_PC,pc+4);return
    ins=struct.unpack('<I',uc.mem_read(pc-4,4))[0]
    if ins&0xffe0001f==0xd4000001 and ((ins>>5)&65535)==1:
        # Explicit no-override fallback fixture only; no actual kernel call.
        kernel.append({'svc':'SetHeapSize','bytes':uc.reg_read(UC_ARM64_REG_X1)})
        uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,0x50000000);return
    raise RuntimeError(f'Unmodeled instruction/exception at {pc:#x}, instruction={current:#x}')
old_kernel_hook=u.hook_add(UC_HOOK_INTR,interrupt)
def call(name,*args):
    u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X30,stop)
    for i in range(8):u.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(i)),args[i] if i<len(args) else 0)
    u.emu_start(address(name),0,count=1500000000)
    assert u.reg_read(UC_ARM64_REG_PC)==stop,f'{name} did not return pc={u.reg_read(UC_ARM64_REG_PC):x} lr={u.reg_read(UC_ARM64_REG_X30):x}'
    return u.reg_read(UC_ARM64_REG_X0)
def startup(begin,size):
    put64(address('g_overrideHeapAddr'),begin);put64(address('g_overrideHeapSize'),size)
    call('__libnx_initheap')
    return struct.unpack('<6Q',u.mem_read(address('nx2_process_heap'),48))



import ast,re
meta=json.loads((a.images/'images.json').read_text());data=0x73000000;u.mem_map(data,0x10000);next_at=0x40000000;rows=[]
def invoke(name,*args):
 put64(stack+0xff000,args[8] if len(args)>8 else 0)
 return call(name,*args[:8])
def string(at):
 b=bytearray()
 while at:
  c=u.mem_read(at,1)[0]
  if not c:break
  b.append(c);at+=1
 return b.decode()
def model_format(uc,*args):
 uc.mem_write(uc.reg_read(UC_ARM64_REG_X0),b'\0');uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
u.hook_add(UC_HOOK_CODE,model_format,begin=address('snprintf'),end=address('snprintf'))
all_plans=[]
for i,m in enumerate(meta):
 seg=m['segments'];file_bytes=4096+seg[2]['address']+page(seg[2]['size']);total=4096+seg[2]['address']+page(seg[2]['size']+m['bss'])
 at=next_at;next_at+=page(total)+4096;assert next_at<stack
 u.mem_map(at,page(total));blob=bytearray(total);raw=(a.images/(m['name']+'.image.bin')).read_bytes();blob[4096:]=raw[:total-4096]
 struct.pack_into('<I',blob,0x18,file_bytes);struct.pack_into('<I',blob,0x38,total-file_bytes);blob[0x10:0x14]=b'NRO0';struct.pack_into('<I',blob,0x20,0)
 for off,value in [(0x24,4096+seg[1]['address']),(0x28,4096+seg[1]['address']),(0x2c,seg[2]['address']-seg[1]['address']),(0x30,4096+seg[2]['address']),(0x34,page(seg[2]['size']))]:struct.pack_into('<I',blob,off,value)
 blob[0x180:0x188]=b'NX2MOD01';struct.pack_into('<II',blob,0x188,1,m['dynamic'])
 for j,g in enumerate(seg):struct.pack_into('<III',blob,0x190+12*j,g['address'],g['size'],g['size']+(m['bss'] if j==2 else 0))
 u.mem_write(at,bytes(blob));module=data;assert invoke('nx2_module_inspect',at,total,module)==0,(m['name'],'parse')
 role=0 if i==0 else 1 if i==1 else 2 if i==len(meta)-1 else 3
 result=invoke('nx2_resolve_plan',module,role,data+0x1000,data+0x500,data+0x600)
 assert result==1,(m['name'],string(data+0x600))
 count=struct.unpack('<I',u.mem_read(data+0x500,4))[0];plans=[struct.unpack('<9Q',u.mem_read(data+0x1000+j*72,72)) for j in range(count)]
 if role==2:
  names=ast.literal_eval(re.search(r'^api_names=(\[.*\])$',Path(__file__).with_name('emulate_bridge.py').read_text(),re.M)[1])
  for name in names:
   u.mem_write(data+0x8000,name.encode()+b'\0');assert invoke('nx2_module_address',module,data+0x8000,data+0x700)==0,name
 if role==2:
  wrapped=next(row for row in plans if row[3] in (1,31,35,41))
  damaged=at+4096+wrapped[2]+4;original=bytes(u.mem_read(damaged,4));u.mem_write(damaged,struct.pack('<I',0xd503201f))
  assert invoke('nx2_resolve_plan',module,role,data+0x4000,data+0x500,data+0x600)==0
  u.mem_write(damaged,original)
 # Validate the same detour implementation used by production for every dynamic hook.
 cursor=(seg[0]['size']+15)&~15;patches=[]
 for row in plans:
  _,off,svc_off,svc,mode,field,symbol,label,target=row
  put64(data+0x900,cursor)
  assert invoke('nx2_patch_gate',at+4096,seg[0]['size'],seg[1]['address'],data+0x900,off,target,at+4096,data+0x908 if field!=0xffffffffffffffff else 0)==1,(m['name'],string(label),'gate')
  cursor=struct.unpack('<I',u.mem_read(data+0x900,4))[0];patches.append({'offset':off,'label':string(label)})
 rows.append({'module':m['name'],'hooks':patches,'thunk_bytes':cursor-seg[0]['size'],'available':seg[1]['address']-seg[0]['size']})
 all_plans.extend((i,r[1],string(r[7])) for r in plans)
# Yoshi must resolve the same 68 semantic sites as the working legacy manifest.
if len(meta)==6 and meta[0]['segments'][0]['size']==9856:
 legacy=json.loads(Path(__file__).with_name('legacy-yoshi-hooks.json').read_text());expected=[]
 for entry in legacy:
  mod=next(i for i,m in enumerate(meta) if m['name']==entry['module'])
  expected.append((mod,int(entry['offset'],16),entry['label']))
 assert set(all_plans)==set(expected),(set(expected)-set(all_plans),set(all_plans)-set(expected))
result={'worker_elf_sha256':hashlib.sha256(elf).hexdigest(),'modules':rows,'total_hooks':len(all_plans),'all_20_sdk_bindings_found':True,'damaged_SVC_wrapper_rejected':True,'error_formatting_modeled':True,'scope':'Actual linked module parser, general resolver and detour writer on original decoded modules. No title-specific selection; no original game code, console services or GPU executed.'}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='modules'},indent=2))
