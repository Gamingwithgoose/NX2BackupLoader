"""Exercise the linked startup reservation and libounce page allocator offline.

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
u.hook_add(UC_HOOK_INTR,interrupt)
def call(name,*args):
    u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X30,stop)
    for i in range(8):u.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(i)),args[i] if i<len(args) else 0)
    u.emu_start(address(name),0,count=1000000)
    assert u.reg_read(UC_ARM64_REG_PC)==stop,f'{name} did not return'
    return u.reg_read(UC_ARM64_REG_X0)
def startup(begin,size):
    put64(address('g_overrideHeapAddr'),begin);put64(address('g_overrideHeapSize'),size)
    call('__libnx_initheap')
    return struct.unpack('<6Q',u.mem_read(address('nx2_process_heap'),48))

# Must resolve to the strong application override, not the weak library default.
assert symbols['__libnx_initheap'][1]>>4==1
raw=0x46c8493000;size=9501593600
plan=startup(raw,size);ob,os,hb,hs,gb,gs=plan
assert (ob,os,hb)==(raw,size,raw)
assert gs==8426356736 and hs==1075236864 and gb==0x4708600000
assert get64(address('fake_heap_start'))==hb and get64(address('fake_heap_end'))==gb
assert not kernel,'Heap override startup must not kernel-resize the process'
u.mem_map(raw,0x10000)
call('_heapPagesInitialize',hb,gb)
total=call('heapGetTotalSize');free=call('_heapPagesGetFreeSize')
assert total<=hs and total>=hs-0x10000 and free<=total and free>=total-0x10000
assert call('_heapPagesAllocate',gs,0x200000)==0
assert call('_heapPagesAllocate',hs+0x10000,0x200000)==0
allocations=[]
for i in range(64):
    n=0x400000;at=call('_heapPagesAllocate',n,0x200000)
    assert at and not at&0x1fffff and hb<=at and at+n<=gb
    assert all(at+n<=x or x+y<=at for x,y in allocations)
    allocations.append((at,n))
for at,n in reversed(allocations):call('_heapPagesFree',at,n)
assert call('_heapPagesGetFreeSize')==free
assert startup(raw,0x40000000)[3:]==(0x40000000,0,0)
fallback=startup(0,0)
assert fallback==(0x50000000,0x20000000,0x50000000,0x20000000,0,0)
assert kernel==[{'svc':'SetHeapSize','bytes':0x20000000}]
result={'worker_elf_sha256':hashlib.sha256(elf).hexdigest(),
        'observed_hbl_override_base':hex(raw),'observed_hbl_override_bytes':size,
        'planned_host_bytes':hs,'planned_game_base':hex(gb),'planned_game_bytes':gs,
        'strong_startup_override_executed':True,'actual_libounce_allocator_uses_only_host_range':True,
        'host_allocations_checked':len(allocations),'host_allocations_released':True,
        'oversized_host_allocations_rejected':True,'small_override_retains_full_host_range':True,
        'no_override_fallback_checked':True,'game_region_memory_touched':False,
        'scope':'Offline actual linked startup and libounce page allocator, single-thread atomics modeled; no game assets or real kernel/service forwarding.'}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
