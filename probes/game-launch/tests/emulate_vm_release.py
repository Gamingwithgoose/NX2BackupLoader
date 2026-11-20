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
    u.emu_start(address(name),0,count=1000000)
    assert u.reg_read(UC_ARM64_REG_PC)==stop,f'{name} did not return'
    return u.reg_read(UC_ARM64_REG_X0)
def startup(begin,size):
    put64(address('g_overrideHeapAddr'),begin);put64(address('g_overrideHeapSize'),size)
    call('__libnx_initheap')
    return struct.unpack('<6Q',u.mem_read(address('nx2_process_heap'),48))


raw=0x2ed569e000;original_size=9321193472;heap=0x2ed1600000;cut=0x2f15800000;old_end=0x3101000000
initial_total=9462349824;initial_used=9458319360;alias=0x4e28400000;alias_size=0x2000000000
u.mem_map(raw,0x10000);data=0x73000000;u.mem_map(data,0x10000)
# Only the report formatting is suppressed. Every range check, kernel wrapper,
# query sequence, state transition and output check is the actual compiled code.
def ignore_log(uc,*args):uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
for n in ['vfprintf','fflush']:u.hook_add(UC_HOOK_CODE,ignore_log,begin=address(n),end=address(n))
def model_allocator_registration(uc,*args):
 uc.reg_write(UC_ARM64_REG_X0,1);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
for n in ['mi_option_set','mi_manage_os_memory']:
 u.hook_add(UC_HOOK_CODE,model_allocator_registration,begin=address(n),end=address(n))
scenario='success';shrunk=False;resize_calls=[]
def modeled_kernel(uc,number,unused):
 global shrunk
 pc=uc.reg_read(UC_ARM64_REG_PC);ins=struct.unpack('<I',uc.mem_read(pc-4,4))[0]
 if ins&0xffe0001f!=0xd4000001:return interrupt(uc,number,unused)
 svc=(ins>>5)&65535;x0=uc.reg_read(UC_ARM64_REG_X0);x1=uc.reg_read(UC_ARM64_REG_X1);x2=uc.reg_read(UC_ARM64_REG_X2)
 if svc==0x29:
  values={4:heap,5:0x300000000,16:0x4000000,2:alias,3:alias_size,6:initial_total,7:initial_used-(old_end-cut if shrunk else 0)}
  if scenario=='no_resources':values[16]=0
  if scenario=='small_alias':values[3]=0x100000
  if scenario=='low_after' and shrunk:values[7]=initial_total-0x100000
  value=values[x1]
  for i in range(2,18):uc.reg_write(getattr(regs,'UC_ARM64_REG_X'+str(i)),0)
  uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,value);return
 if svc==6:
  target=x2
  if target==cut and shrunk:
   state=5 if scenario=='tail_still_mapped' else 0;perm=3 if state else 0;begin=cut;n=heap+0x300000000-cut
  elif target==cut:
   state=4 if scenario=='bad_tail_type' else 5;perm=3;begin=0x2ef2c71000;n=old_end-begin
  else:assert target==cut-1;state=5;perm=3;begin=heap;n=cut-heap
  attr=1 if scenario=='locked_tail' else 0
  uc.mem_write(x0,struct.pack('<QQIIIIII',begin,n,state,attr,perm,0,0,0));uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,0);return
 if svc==1:
  resize_calls.append(x1);assert x1==0x44200000
  shrunk=scenario!='resize_failure'
  uc.reg_write(UC_ARM64_REG_X0,0xd401 if scenario=='resize_failure' else 0)
  uc.reg_write(UC_ARM64_REG_X1,heap+0x200000 if scenario=='wrong_returned_base' else heap);return
 return interrupt(uc,number,unused)
u.hook_del(old_kernel_hook);u.hook_add(UC_HOOK_INTR,modeled_kernel)
results=[]
for scenario in ['success','allocator_initialized','bad_compatibility_cursor','no_resources','small_alias','bad_tail_type','locked_tail','allocator_crosses_cut','resize_failure','wrong_returned_base','tail_still_mapped','low_after']:
 shrunk=False;resize_calls=[];u.mem_write(address('nx2_native_vm_active'),bytes(4));startup(raw,original_size)
 if scenario in ['allocator_initialized','bad_compatibility_cursor']:
  # Execute the actual full allocator initializer, including its final cursor
  # mutation. Only mimalloc option/arena registration is modeled below.
  call('_mallocInitialize')
  assert get64(address('fake_heap_start'))==get64(address('fake_heap_end'))==cut
 else:call('_heapPagesInitialize',raw,cut)
 if scenario=='bad_compatibility_cursor':put64(address('fake_heap_start'),cut-4096)
 if scenario=='allocator_crosses_cut':put64(address('fake_heap_end'),cut+4096)
 result=call('nx2_release_heap_for_vm',1,address('nx2_process_heap'),0x1400000,0xf80000000,0x100000000,data)
 active=struct.unpack('<I',u.mem_read(address('nx2_native_vm_active'),4))[0]
 expected=scenario in ['success','allocator_initialized'];assert bool(result)==expected and active==int(expected),(scenario,result,active)
 if expected:
  assert resize_calls==[0x44200000] and get64(data)==8250032128
  assert get64(address('fake_heap_end'))==cut
  assert call('_heapPagesAllocate',0x400000,0x200000)<cut
 if scenario in ['bad_compatibility_cursor','no_resources','small_alias','bad_tail_type','locked_tail','allocator_crosses_cut']:assert not resize_calls
 results.append({'case':scenario,'success':bool(result),'resize_calls':len(resize_calls),'native_mode_enabled':active})
result={'worker_elf_sha256':hashlib.sha256(elf).hexdigest(),'cases':results,'observed_MKW_layout_used':True,'actual_malloc_initializer_cursor_mutation_executed':True,'mimalloc_option_and_arena_registration_modeled':True,'retained_kernel_heap_bytes':0x44200000,'released_tail_bytes':old_end-cut,'modeled_available_physical_bytes':8250032128,'scope':'Actual linked transition and host allocator with modeled QueryMemory/GetInfo/SetHeapSize, no real console calls or game-tail memory accesses. Failed post-transition validation must terminate the process in the caller.'}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
