"""Execute the compiled SDK worker and verified supplied SDK; no service forwarding."""
import sys,json,struct,collections,re,argparse
from pathlib import Path
# Requires unicorn 2.1.4; install into your Python environment.
from unicorn import *
from unicorn.arm64_const import *
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('images',type=Path)
parser.add_argument('--process-report',type=Path,help='Replay current-process GetInfo observations from a Suite 0.3 report; entropy stays synthetic')
parser.add_argument('--process-info-json',type=Path,help='Replay a retained JSON transcription of process GetInfo observations')
parser.add_argument('--worker',type=Path,required=True)
parser.add_argument('--case',choices=['normal','busy','bad-thread','tls-failure'],default='normal')
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
 if current&0x3fa07c00==0x08a07c00:
  # Single-thread LSE CAS model; ordering/coherence is not tested.
  import unicorn.arm64_const as registers
  width=8 if current&(1<<30) else 4;rs=(current>>16)&31;rt=current&31;rn=(current>>5)&31
  def reg(index):return 0 if index==31 else uc.reg_read(getattr(registers,'UC_ARM64_REG_X'+str(index)))
  address=uc.reg_read(UC_ARM64_REG_SP) if rn==31 else reg(rn);mask=(1<<(8*width))-1
  old=int.from_bytes(uc.mem_read(address,width),'little')
  if old==reg(rs)&mask:uc.mem_write(address,(reg(rt)&mask).to_bytes(width,'little'))
  if rs!=31:uc.reg_write(getattr(registers,'UC_ARM64_REG_X'+str(rs)),old)
  uc.reg_write(UC_ARM64_REG_PC,pc+4);return
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
 elif svc==0xc and args[1]==0x1234:
  uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,44);event['synthetic_thread_priority']=44
 elif svc==0xe and args[2]==0x1234:
  uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,0);uc.reg_write(UC_ARM64_REG_X2,63);event['synthetic_thread_affinity']=63
 elif svc==0x25 and args[1]==0x1234:
  uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,0x4321);event['synthetic_thread_id']=0x4321
 elif svc==0x27:
  # OutputDebugString is captured locally, never forwarded to the console.
  if args[1]>4096:reason='oversized debug string';uc.emu_stop();return
  event['text']=bytes(uc.mem_read(args[0],args[1])).decode('utf-8','replace');uc.reg_write(UC_ARM64_REG_X0,0)
 else:
  reason='stopped before unsupported SVC '+hex(svc);uc.emu_stop()
import ctypes
sdk=mods[5];u.mem_write(sdk['base'],(root/'sdk.ours-linked.bin').read_bytes())
elf=options.worker.read_bytes();worker_base=0x74000000
phoff=struct.unpack_from('<Q',elf,32)[0];phentsize,phnum=struct.unpack_from('<HH',elf,54)
loads=[]
for i in range(phnum):
 h=struct.unpack_from('<IIQQQQQQ',elf,phoff+i*phentsize)
 if h[0]==1:loads.append(h)
u.mem_map(worker_base,page(max(h[3]+h[6] for h in loads)))
for h in loads:u.mem_write(worker_base+h[3],elf[h[2]:h[2]+h[5]])
shoff=struct.unpack_from('<Q',elf,40)[0];shsize,shnum=struct.unpack_from('<HH',elf,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',elf,shoff+i*shsize) for i in range(shnum)];symbols={}
for section in sections:
 if section[1]!=2:continue
 strings=sections[section[6]];names=elf[strings[4]:strings[4]+strings[5]]
 for off in range(section[4],section[4]+section[5],24):
  name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',elf,off)
  if name and index:symbols[names[name:names.index(0,name)].decode()]=value
class Job(ctypes.Structure):
 _fields_=[('api',ctypes.c_uint64*11)]+[(n,ctypes.c_uint64) for n in ('handle','data_begin','data_end','stage','trace_count')]+[('trace',ctypes.c_uint64*32)]+[(n,ctypes.c_uint64) for n in ('initialized','thread','thread_id','thread_valid','tls_result','tls_slot','tls_initial','tls_value','tls_cleared','tls_freed','mutex_first','mutex_second','mutex_third','mutex_finalized','tail_restored','tp_restored','preemption_same','context_fault')]+[('mutex',ctypes.c_ubyte*64)]
layout=struct.unpack('<6Q',u.mem_read(worker_base+symbols['nx2_sdk_os_job_layout'],48))
assert layout==(ctypes.sizeof(Job),Job.handle.offset,Job.data_begin.offset,Job.stage.offset,Job.initialized.offset,Job.tail_restored.offset),layout
names=['nnosInitialize','_ZN2nn2os16GetCurrentThreadEv','_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE','_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE','_ZN2nn2os11GetTlsValueENS0_7TlsSlotE','_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm','_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE','_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi','_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE','_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE','_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE']
job=Job();job.handle=0x1234;job.data_begin=sdk['base']+sdk['segments'][2]['address'];job.data_end=sdk['base']+sdk['mapped_bytes']
for i,name in enumerate(names):job.api[i]=sdk['base']+next(s['value'] for s in sdk['symbols'] if s['name']==name and s['section'])
job_at=0x73000000;stop=0x72000000;u.mem_map(job_at,4096);u.mem_map(stop,4096)
if options.case in ('bad-thread','tls-failure'):
 stub=stop+16
 # mov w0,#0 or #0x1234; ret -- simulated failing callee, not retail code.
 u.mem_write(stub,struct.pack('<II',0x52800000|((0x1234 if options.case=='tls-failure' else 0)<<5),0xd65f03c0))
 job.api[1 if options.case=='bad-thread' else 3]=stub
u.mem_write(job_at,bytes(job))
host_tail=bytes((i*7+3)&255 for i in range(128));u.mem_write(0x71000180,host_tail)
original_tp=0x71000800;u.reg_write(UC_ARM64_REG_TPIDR_EL0,original_tp)
u.mem_write(0x71000100,struct.pack('<H',1 if options.case=='busy' else 0))
def returned(uc,pc,size,data):uc.emu_stop()
u.hook_add(UC_HOOK_CODE,returned,begin=stop,end=stop);u.hook_add(UC_HOOK_INTR,interrupt)
u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X30,stop);u.reg_write(UC_ARM64_REG_X0,job_at)
u.emu_start(worker_base+symbols['nx2_sdk_os_run'],0,count=10000000)
assert u.reg_read(UC_ARM64_REG_PC)==stop,reason
job=Job.from_buffer_copy(bytes(u.mem_read(job_at,ctypes.sizeof(Job))))
assert bytes(u.mem_read(0x71000180,128))==host_tail,'host TLR tail corrupted'
assert u.reg_read(UC_ARM64_REG_TPIDR_EL0)==original_tp,'host TP changed'
assert set(e['svc'] for e in events)<={'0x29','0x6','0xc','0x25'},events
if options.case=='busy':assert job.context_fault and not job.initialized and not events
else:
 assert job.initialized and job.tail_restored and job.tp_restored and job.preemption_same and not job.context_fault
 if options.case=='bad-thread':assert not job.thread_valid and not job.tls_freed and not job.mutex_finalized
 else:
  assert job.thread_valid and job.thread_id==0x4321
  assert (job.mutex_first,job.mutex_second,job.mutex_third,job.mutex_finalized)==(1,0,1,1)
  if options.case=='tls-failure':assert job.tls_result==0x1234 and not job.tls_freed
  else:assert not job.tls_result and job.tls_initial==0 and job.tls_value==0x1122334455667788 and job.tls_cleared and job.tls_freed
print(json.dumps({'case':options.case,'scope':'Actual compiled SDK worker and C-linked supplied SDK. Synthetic kernel replies, single-thread CAS model and unsigned PAC returns. No host service forwarding.','returned':True,'host_TLR_preserved':True,'host_TP_preserved':True,'trace':list(job.trace)[:job.trace_count],'SDK_initialized':bool(job.initialized),'thread_valid':bool(job.thread_valid),'tls_result':hex(job.tls_result),'mutex_finalized':bool(job.mutex_finalized),'context_fault':bool(job.context_fault),'kernel_calls':events},indent=2))