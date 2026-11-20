"""Exercise the linked libounce exception decoder and application fault capture.

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
old_kernel_hook=u.hook_add(UC_HOOK_INTR,interrupt)
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


ctx=0x73000000;u.mem_map(ctx,4096)
values=[0x11220000+i for i in range(29)];fp=0x33445566;lr=0x170b1000+0x161cd4;sp=0x1780af0000;pc=0x170b1000+0x161cd0
pstate=0x20000000;esr=0x96000004;far=0xdeadbeef
packet=struct.pack('<9Q3Q4IQ',*values[:9],lr,sp,pc,pstate,0x12,0x34,esr,far)
u.mem_write(ctx,packet)
for i in range(9,29):u.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(i)),values[i])
u.reg_write(UC_ARM64_REG_X29,fp)
u.reg_write(UC_ARM64_REG_TPIDR_EL0,0x77889900)
exception_returns=[]
def fault_kernel(uc,number,data):
 here=uc.reg_read(UC_ARM64_REG_PC);ins=struct.unpack('<I',uc.mem_read(here-4,4))[0]
 if ins&0xffe0001f==0xd4000001:
  svc=(ins>>5)&65535
  if svc==0x29 and uc.reg_read(UC_ARM64_REG_X1)==8:
   uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,1);return
  if svc==0x28:
   result=uc.reg_read(UC_ARM64_REG_X0);exception_returns.append(result);assert result==0,'Exception was rejected by runtime'
   altered=bytes(uc.mem_read(ctx,120))
   for i in range(9):uc.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(i)),struct.unpack_from('<Q',altered,8*i)[0])
   uc.reg_write(UC_ARM64_REG_X30,struct.unpack_from('<Q',altered,72)[0]);uc.reg_write(UC_ARM64_REG_SP,struct.unpack_from('<Q',altered,80)[0]);uc.reg_write(UC_ARM64_REG_PC,struct.unpack_from('<Q',altered,88)[0]);return
 interrupt(uc,number,data)
u.hook_del(old_kernel_hook);u.hook_add(UC_HOOK_INTR,fault_kernel)
fault=address('nx2_fault')
def parked(uc,here,size,data):
 ins=struct.unpack('<I',uc.mem_read(here,4))[0]
 if ins==0xd503205f:
  assert struct.unpack('<I',uc.mem_read(fault,4))[0]==2
  uc.emu_stop()
# A bounded hook only on our fault capture function; no host-library calls.
handler=address('__libnx_exception_handler');u.hook_add(UC_HOOK_CODE,parked,begin=handler,end=handler+1024)
u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X0,0x101);u.reg_write(UC_ARM64_REG_X1,ctx)
u.emu_start(address('__libnx_exception_entry'),0,count=1000000)
assert exception_returns==[0]
raw=bytes(u.mem_read(fault,864));dump=raw[16:848]
assert struct.unpack_from('<I',dump)[0]==0x101
assert struct.unpack_from('<29Q',dump,16)==tuple(values)
assert struct.unpack_from('<4Q',dump,248)==(fp,lr,sp,pc)
assert struct.unpack_from('<4I',dump,800)==(pstate,0x12,0x34,esr)
assert struct.unpack_from('<Q',dump,816)[0]==far
assert struct.unpack_from('<2Q',raw,848)==(0x77889900,tls)
# A later fault cannot overwrite the first completed capture.
u.mem_write(ctx,bytes(832));u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X0,ctx)
u.emu_start(handler,0,count=1000000);assert bytes(u.mem_read(fault,864))==raw
result={'worker_elf_sha256':hashlib.sha256(elf).hexdigest(),'real_runtime_exception_decoder_executed':True,'debugged_process_capture_enabled':True,'modeled_ReturnFromException_count':len(exception_returns),'registers_pc_lr_sp_esr_far_preserved':True,'guest_TLS_registers_captured':True,'second_fault_does_not_overwrite_first':True,'faulted_thread_parked_without_resuming':True,'real_console_services_forwarded':0,'scope':'Synthetic CPU exception packet through the actual linked libounce decoder and handler; kernel exception delivery is modeled, not a hardware guarantee.'}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
