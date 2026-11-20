"""Offline execution of the supplied original rtld. No host service forwarding."""
import sys,json,struct,collections,re,argparse
from pathlib import Path
# Requires unicorn 2.1.4; install into your Python environment.
from unicorn import *
from unicorn.arm64_const import *
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('images',type=Path)
parser.add_argument('--process-report',type=Path,help='Replay current-process GetInfo observations from a Suite 0.3 report; entropy stays synthetic')
parser.add_argument('--process-info-json',type=Path,help='Replay a retained JSON transcription of process GetInfo observations')
options=parser.parse_args()
if options.process_report and options.process_info_json:parser.error('Choose one observation source')
process_info={}
if options.process_report:
 report=options.process_report.read_text(encoding='utf-8-sig')
 if not report.startswith('NX2 Folder Execution Suite 0.3'):raise SystemExit('Expected a Suite 0.3 report')
 for line in report.splitlines():
  match=re.fullmatch(r'GET_INFO type=(\d+) handle=current-process sub=0 result=([0-9a-fA-F]{8}) value=([0-9a-fA-F]{16})',line)
  if match:
   kind=int(match[1])
   if kind in process_info:raise SystemExit('Duplicate GetInfo observation')
   result=int(match[2],16)
   # Failed calls do not produce a valid value. Some wrappers leave the info
   # type in the output register, which must never become a process attribute.
   process_info[kind]=(result,int(match[3],16) if result==0 else 0)
 if set(process_info)!=set(range(39)):raise SystemExit('Incomplete process capability report')
if options.process_info_json:
 capture=json.loads(options.process_info_json.read_text())
 for row in capture['queries']:
  kind=int(row['type']);status=int(row['result'],16)
  if kind in process_info:raise SystemExit('Duplicate GetInfo observation')
  process_info[kind]=(status,int(row['value'],16) if status==0 else 0)
 if set(process_info)!=set(range(39)):raise SystemExit('Incomplete process capability capture')
root=options.images;mods=json.loads((root/'images.json').read_text());u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);regions=[]
def page(x):return (x+4095)&~4095
for m in mods:
 b=m['base'];n=page(m['mapped_bytes']);u.mem_map(b,n);u.mem_write(b,(root/(m['name']+'.image.bin')).read_bytes())
 for i,s in enumerate(m['segments']):
  start=b+s['address'];end=b+(m['segments'][i+1]['address'] if i<2 else n)
  regions.append((start,end,3 if i<2 else 4,[5,1,3][i],m['name']))
# Use a simulated 1 MiB mapping inside the observed stack reservation. The
# actual stack mapping was not recorded, so this remains an explicit model.
stack=(process_info[14][1]+0x200000) if process_info and process_info[14][0]==0 else 0x70000000
regions.append((stack,stack+0x100000,0x0b,3,'modeled stack'))
regions.append((0x71000000,0x71010000,0x0c,3,'modeled TLS'))
u.mem_map(stack,0x100000);u.reg_write(UC_ARM64_REG_SP,stack+0xff000)
u.reg_write(UC_ARM64_REG_X0,0);u.reg_write(UC_ARM64_REG_X1,0x1234);u.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20)
# A synthetic, zero-initialized kernel TLS region. Its contents are not claimed
# to reproduce the console; stop if unsupported kernel behavior is requested.
u.mem_map(0x71000000,0x10000);u.reg_write(UC_ARM64_REG_TPIDRRO_EL0,0x71000000);u.reg_write(UC_ARM64_REG_TPIDR_EL0,0x710001f0)
recent=collections.deque(maxlen=30);events=[];steps=0;reason='instruction budget reached'
def location(pc):
 for m in mods:
  if m['base']<=pc<m['base']+page(m['mapped_bytes']):return m['name']+'+'+hex(pc-m['base'])
 return hex(pc)
def code(uc,pc,size,data):
 global steps,reason
 steps+=1
 if pc==mods[0]['base']+0xffc:
  for m in mods:(root/(m['name']+'.native-linked.bin')).write_bytes(bytes(uc.mem_read(m['base'],page(m['mapped_bytes']))))
  events.append({'milestone':'original rtld relocation pass completed; snapshots captured before SDK entry'})
 if pc==mods[0]['base']+0x2140:
  dst=uc.reg_read(UC_ARM64_REG_X0);byte=uc.reg_read(UC_ARM64_REG_X1)&255;length=uc.reg_read(UC_ARM64_REG_X2)
  if length>256*1024*1024:reason='oversized memset';uc.emu_stop();return
  if length:uc.mem_write(dst,bytes([byte])*length)
  uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
 # ARMv8.3 PAC return instructions may not be implemented by Unicorn. Model
 # only the return target; signatures are NOT validated in this experiment.
 ins=struct.unpack('<I',uc.mem_read(pc,4))[0]
 if ins in (0xd65f0fff,0xd65f0bff):uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
def interrupt(uc,number,data):
 global reason
 pc=uc.reg_read(UC_ARM64_REG_PC)
 current=struct.unpack('<I',uc.mem_read(pc,4))[0]
 if current in (0xd65f0fff,0xd65f0bff):
  uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
 ins=struct.unpack('<I',uc.mem_read(pc-4,4))[0]
 if ins&0xffe0001f!=0xd4000001:
  reason='non-SVC exception '+str(number);uc.emu_stop();return
 svc=(ins>>5)&65535;args=[uc.reg_read(r) for r in (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3)]
 event={'svc':hex(svc),'pc':location(pc-4),'args':[hex(x) for x in args]};events.append(event)
 if svc==6:
  # QueryMemory writes MemoryInfo through x0, queried address in x2.
  target=args[2];r=next((r for r in regions if r[0]<=target<r[1]),None)
  if r:start,end,state,perm,name=r
  else:
   start=max([0]+[r[1] for r in regions if r[1]<=target]);end=min([1<<39]+[r[0] for r in regions if r[0]>target]);state=perm=0;name='free'
  uc.mem_write(args[0],struct.pack('<QQIIIIII',start,end-start,state,0,perm,0,0,0));uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,0);event['model']={'base':hex(start),'size':end-start,'state':state,'permission':perm,'module':name}
 elif svc==0x29 and args[2]==0xffff8001 and args[3]==0 and args[1] in process_info:
  status,value=process_info[args[1]];uc.reg_write(UC_ARM64_REG_X0,status);uc.reg_write(UC_ARM64_REG_X1,value)
  event['observed_process_info']={'result':hex(status),'value':hex(value) if status==0 else None}
 elif svc==0x29 and args[1]==11 and args[2]==0 and args[3]<4:
  # Synthetic entropy for offline startup exploration; never a console value.
  value=0x9e3779b97f4a7c15 ^ args[3];uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,value);event['synthetic_entropy']=hex(value)
 elif svc==0x29 and args[1] in (0,1,2,3,4,5,6,7,8,12,13,14,15,16,17,21,22,23,24) and args[3]==0:
  value={0:15,1:0x3fffffffffffffff,2:0x40000000,3:0x10000000,4:0x20000000,5:0x20000000,6:0x100000000,7:0x10000000,8:0,12:0x8000000,13:0x7ff8000000,14:stack,15:0x100000,16:0x200000,17:0x100000,21:0x100000000,22:0x10000000,23:1,24:64}[args[1]];uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,value);event['synthetic_process_info']=hex(value)
 elif svc==0x27:
  # OutputDebugString is captured locally, never forwarded to the console.
  if args[1]>4096:reason='oversized debug string';uc.emu_stop();return
  event['text']=bytes(uc.mem_read(args[0],args[1])).decode('utf-8','replace');uc.reg_write(UC_ARM64_REG_X0,0)
 else:
  reason='stopped before unsupported SVC '+hex(svc);uc.emu_stop()
# Hook only small memory helper and PAC returns, rather than every instruction.
u.hook_add(UC_HOOK_CODE,code,begin=mods[0]['base']+0x2140,end=mods[0]['base']+0x2140)
for off in range(0,len((root/'rtld.text.bin').read_bytes()),4):
 if struct.unpack_from('<I',(root/'rtld.text.bin').read_bytes(),off)[0] in (0xd65f0fff,0xd65f0bff):
  addr=mods[0]['base']+off;u.hook_add(UC_HOOK_CODE,code,begin=addr,end=addr)
u.hook_add(UC_HOOK_CODE,code,begin=mods[0]['base']+0xffc,end=mods[0]['base']+0xffc)
u.hook_add(UC_HOOK_INTR,interrupt)
try:u.emu_start(mods[0]['base'],0,count=100000000)
except UcError as e:reason=str(e)
initial_reason=reason;initial_pc=location(u.reg_read(UC_ARM64_REG_PC))
# Exercise the original lazy resolver for each main PLT import. This changes
# emulator memory only; none of its kernel/service calls are forwarded.
main=mods[1];rtld=mods[0];head=rtld['base']+0x10030
node=struct.unpack('<Q',u.mem_read(head+8,8))[0];main_node=None
for _ in range(16):
 if node==head:break
 if struct.unpack('<Q',u.mem_read(node+40,8))[0]==main['base']:main_node=node;break
 node=struct.unpack('<Q',u.mem_read(node+8,8))[0]
assert main_node is not None,'native main module object absent'
stop=0x72000000;u.mem_map(stop,4096)
def return_hook(uc,pc,size,data):uc.emu_stop()
u.hook_add(UC_HOOK_CODE,return_hook,begin=stop,end=stop)
# Discover PLT count from the original, unchanged dynamic table.
raw=(root/'main.image.bin').read_bytes();tags={}
for pos in range(main['dynamic'],main['dynamic']+4096*16,16):
 tag,value=struct.unpack_from('<QQ',raw,pos)
 if not tag:break
 tags[tag]=value
bindings=[]
for index in range(tags[2]//24):
 u.reg_write(UC_ARM64_REG_SP,stack+0xfe000);u.reg_write(UC_ARM64_REG_X30,stop);u.reg_write(UC_ARM64_REG_X0,main_node);u.reg_write(UC_ARM64_REG_X1,index)
 u.emu_start(rtld['base']+0x17a0,0,count=100000)
 assert u.reg_read(UC_ARM64_REG_PC)==stop,'lazy resolver failed'
 offset=struct.unpack_from('<Q',raw,tags[23]+index*24)[0]
 target=u.reg_read(UC_ARM64_REG_X0)
 assert struct.unpack('<Q',u.mem_read(main['base']+offset,8))[0]==target,'binder did not install resolved address'
 bindings.append({'index':index,'target':location(target)})
(root/'main.native-linked.bin').write_bytes(bytes(u.mem_read(main['base'],page(main['mapped_bytes']))))
result={'scope':'Original rtld with modeled QueryMemory/stack/TLS, deterministic entropy and accelerated memset. Process GetInfo replies use the stated source. No SDK service, storage, save or network calls forwarded. PAC return target modeled without signature validation.','stop':initial_reason,'pc':initial_pc,'process_info_source':'Suite 0.3 hardware observations' if process_info else 'synthetic defaults','modeled_helper_and_return_visits':steps,'events':events,'main_lazy_imports_resolved':len(bindings),'bindings':bindings}
(root/'startup-trace.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k not in ('events','bindings')},indent=2))