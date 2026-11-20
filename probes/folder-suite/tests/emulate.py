"""Execute owned AArch64 fixtures and the real resolver assembly under Unicorn.
The host TLS lookup callback is simulated; no Nintendo code executes.
"""
import json,struct,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root.parent/'test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn import arm64_const as R
uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
uc.reg_write(R.UC_ARM64_REG_CPACR_EL1,3<<20)
def u32(b,o=0):return struct.unpack_from('<I',b,o)[0]
def u64(b,o=0):return struct.unpack_from('<Q',b,o)[0]
def write64(p,v):uc.mem_write(p,struct.pack('<Q',v&((1<<64)-1)))
modules=[]
for i,name in enumerate(('provider','client')):
    raw=(root/'fixture'/f'{name}.nxm').read_bytes();base=0x1000000+i*0x100000;total=len(raw)+u32(raw,0x38);uc.mem_map(base,total);uc.mem_write(base,raw);guest=base+4096;dynamic=u32(raw,0x18c);tags={}
    for j in range(1024):
        tag=u64(raw,4096+dynamic+16*j);value=u64(raw,4096+dynamic+16*j+8)
        if not tag:break
        tags[tag]=value
    count=u32(raw,4096+tags[4]+4);strings=raw[4096+tags[5]:4096+tags[5]+tags[10]];symbols=[]
    for j in range(count):
        at=4096+tags[6]+j*24;off=u32(raw,at);end=strings.index(0,off)
        symbols.append({'name':strings[off:end].decode(),'binding':raw[at+4]>>4,'type':raw[at+4]&15,'section':struct.unpack_from('<H',raw,at+6)[0],'value':u64(raw,at+8)})
    modules.append({'name':name,'raw':raw,'guest':guest,'tags':tags,'symbols':symbols})
definitions={s['name']:(i,s) for i,m in enumerate(modules) for s in m['symbols'] if s['section'] and s['binding']}
# Map the actual resolver assembly linked with a one-instruction callback stub.
elf=(root/'tests/tls-trampoline.elf').read_bytes();ph=u64(elf,32);stride,count=struct.unpack_from('<HH',elf,54);trampoline_base=0x20000000
uc.mem_map(trampoline_base,0x10000)
for i in range(count):
    at=ph+i*stride;kind=u32(elf,at)
    if kind==1:
        file_off=u64(elf,at+8);vaddr=u64(elf,at+16);size=u64(elf,at+32)
        uc.mem_write(trampoline_base+vaddr,elf[file_off:file_off+size])
sh=u64(elf,40);ss,sc=struct.unpack_from('<HH',elf,58);syms={}
for i in range(sc):
    at=sh+i*ss
    if u32(elf,at+4)!=2:continue
    strings_at=sh+u32(elf,at+40)*ss;strings=elf[u64(elf,strings_at+24):u64(elf,strings_at+24)+u64(elf,strings_at+32)]
    off,size,ent=u64(elf,at+24),u64(elf,at+32),u64(elf,at+56)
    for pos in range(off,off+size,ent):
        start=u32(elf,pos);end=strings.index(0,start);name=strings[start:end].decode();syms[name]=trampoline_base+u64(elf,pos+8)
resolver=syms['nx2_tlsdesc_resolver'];callback=syms['nx2_tls_offset']
args_base=0x30000000;uc.mem_map(args_base,0x10000);args_count=0
tls_base=0x40000000;uc.mem_map(tls_base,0x10000)
provider=modules[0];raw=provider['raw'];tls_at,tls_file,tls_size=struct.unpack_from('<III',raw,0x1c0)
for thread in range(3):uc.mem_write(tls_base+thread*4096,raw[4096+tls_at:4096+tls_at+tls_file]+bytes(tls_size-tls_file))
current_thread=0;resolver_calls=0
def lookup(emu,address,size,opaque):
    global resolver_calls
    if address!=callback:return
    resolver_calls+=1;arg=emu.reg_read(R.UC_ARM64_REG_X0);module,_,offset=struct.unpack('<IIQ',bytes(emu.mem_read(arg,16)))
    assert module==0 and offset<tls_size
    result=tls_base+current_thread*4096+offset-emu.reg_read(R.UC_ARM64_REG_TPIDR_EL0);return_pc=emu.reg_read(R.UC_ARM64_REG_X30)
    # Stress the resolver's preservation of caller-saved integer/vector registers.
    for i in range(1,19):emu.reg_write(getattr(R,f'UC_ARM64_REG_X{i}'),0xdead0000+i)
    for i in range(32):emu.reg_write(getattr(R,f'UC_ARM64_REG_Q{i}'),(0xfeed0000+i)<<64|i)
    emu.reg_write(R.UC_ARM64_REG_X0,result&((1<<64)-1));emu.reg_write(R.UC_ARM64_REG_PC,return_pc)
uc.hook_add(UC_HOOK_CODE,lookup)
for owner,m in enumerate(modules):
    raw,tags,guest=m['raw'],m['tags'],m['guest']
    for pointer,size in ((7,8),(23,2)):
        for pos in range(0,tags.get(size,0),24):
            at=4096+tags[pointer]+pos;target,info,addend=struct.unpack_from('<QQq',raw,at);kind=info&0xffffffff;symbol=m['symbols'][info>>32]
            definition=definitions.get(symbol['name']);value=0
            if definition:
                index,defined=definition;value=defined['value']+(0 if defined['type']==6 else modules[index]['guest'])
            elif symbol['binding']!=2 and info>>32:raise AssertionError(symbol)
            if kind==1031:
                address=args_base+args_count*16;args_count+=1;uc.mem_write(address,struct.pack('<IIQ',index,0,value+addend));write64(guest+target,resolver);write64(guest+target+8,address)
            elif kind in (257,1025,1026):write64(guest+target,value+addend)
            elif kind==1027:write64(guest+target,guest+addend)
            else:raise AssertionError(kind)
    cursor=0
    for pos in range(0,tags.get(35,0),8):
        entry=u64(raw,4096+tags[36]+pos)
        offsets=[]
        if not entry&1:offsets=[entry];cursor=entry+8
        else:
            offsets=[cursor+8*(bit-1) for bit in range(1,64) if entry&(1<<bit)];cursor+=63*8
        for offset in offsets:write64(guest+offset,u64(bytes(uc.mem_read(guest+offset,8)))+guest)
stack=0x60000000;stop=0x70000000;uc.mem_map(stack,0x20000);uc.mem_map(stop,4096)
results=[]
def function(name):i,s=definitions[name];return modules[i]['guest']+s['value']
def call(name,arg=0,thread=0,floating=False):
    global current_thread
    current_thread=thread;uc.reg_write(R.UC_ARM64_REG_TPIDR_EL0,0x50000000+thread*4096);uc.reg_write(R.UC_ARM64_REG_SP,stack+0x1f000);uc.reg_write(R.UC_ARM64_REG_X30,stop)
    uc.reg_write(R.UC_ARM64_REG_X0,arg if not floating else 0)
    if floating:uc.reg_write(R.UC_ARM64_REG_D0,u64(struct.pack('<d',arg)))
    uc.emu_start(function(name),stop,count=200000)
    assert uc.reg_read(R.UC_ARM64_REG_PC)==stop,'instruction budget reached'
    return struct.unpack('<d',struct.pack('<Q',uc.reg_read(R.UC_ARM64_REG_D0)))[0] if floating else uc.reg_read(R.UC_ARM64_REG_X0)
def check(name,value,expected):
    assert value==expected,(name,value,expected);results.append({'test':name,'actual':value,'expected':expected})
check('constructor before init',call('nx2_ctor_check'),0)
client=modules[1];init=u64(bytes(uc.mem_read(client['guest']+client['tags'][25],8)))
# Call a raw callback via a temporary named definition.
definitions['callback']=(1,{'value':init-client['guest']});call('callback')
check('constructor after init',call('nx2_ctor_check'),0xcafe)
check('undefined weak',call('nx2_weak_check'),0)
check('BSS and data',call('nx2_bss_check'),1)
check('TLS initial',call('nx2_provider_tls'),7)
check('main first',call('nx2_suite_entry',3),4680)
check('main second',call('nx2_suite_entry',3),4686)
check('SIMD preserved',call('nx2_simd_tls',2.5,floating=True),15.5)
check('thread 1 first',call('nx2_suite_entry',3,thread=1),4680)
check('thread 1 second',call('nx2_suite_entry',3,thread=1),4686)
check('thread 2 first',call('nx2_suite_entry',5,thread=2),4684)
check('thread 2 second',call('nx2_suite_entry',5,thread=2),4694)
check('main TLS isolated',call('nx2_provider_tls'),13)
fini=u64(bytes(uc.mem_read(client['guest']+client['tags'][26],8)));definitions['callback']=(1,{'value':fini-client['guest']});call('callback')
check('destructor',call('nx2_ctor_check'),0)
print(json.dumps({'scope':'Compiled owned AArch64 modules and actual TLS resolver assembly; host TLS lookup callback simulated','checks':results,'resolver_calls':resolver_calls,'passed':len(results)},indent=2))
