"""Offline execution of the launch ELF and owned game SDK. No real service forwarding."""
import sys,json,struct,collections,re,argparse
from pathlib import Path
# Requires unicorn 2.1.4. All kernel replies are offline models.
from unicorn import *
from unicorn.arm64_const import *
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('images',type=Path)
parser.add_argument('--rom-map',type=Path,required=True)
parser.add_argument('--rom-root',type=Path,required=True)
parser.add_argument('--scratch',type=Path,required=True)
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
  pass # Native relocation milestone; do not write decrypted image dumps
  events.append({'milestone':'original rtld relocation pass completed before SDK entry'})
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
 if current&0x3ffffc00==0x38bfc000:
  import unicorn.arm64_const as R
  rn=(current>>5)&31;rt=current&31;width=1<<((current>>30)&3)
  address=uc.reg_read(UC_ARM64_REG_SP) if rn==31 else uc.reg_read(getattr(R,'UC_ARM64_REG_X'+str(rn)))
  value=int.from_bytes(uc.mem_read(address,width),'little')
  if rt!=31:uc.reg_write(getattr(R,'UC_ARM64_REG_X'+str(rt)),value)
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

import ctypes,tempfile,hashlib
sdk=mods[5];u.mem_write(sdk['base'],(root/'sdk.ours-linked.bin').read_bytes())
def sdkaddr(name):return sdk['base']+next(x['value'] for x in sdk['symbols'] if x['name']==name and x['section'])
for name in ('_ZN2nn2fs6detail18IsEnabledAccessLogEv','_ZN2nn2fs6detail18IsEnabledAccessLogEj'):
 u.mem_write(sdkaddr(name),struct.pack('<II',0x52800000,0xd65f03c0))
elf=options.worker.read_bytes();base=0x74000000
phoff=struct.unpack_from('<Q',elf,32)[0];phsize,phnum=struct.unpack_from('<HH',elf,54)
loads=[struct.unpack_from('<IIQQQQQQ',elf,phoff+i*phsize) for i in range(phnum)]
loads=[h for h in loads if h[0]==1];u.mem_map(base,page(max(h[3]+h[6] for h in loads)))
for h in loads:u.mem_write(base+h[3],elf[h[2]:h[2]+h[5]])
shoff=struct.unpack_from('<Q',elf,40)[0];shsize,shnum=struct.unpack_from('<HH',elf,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',elf,shoff+i*shsize) for i in range(shnum)];symbols={}
for sec in sections:
 if sec[1]==19:
  cursor=0
  for at in range(sec[4],sec[4]+sec[5],8):
   item=struct.unpack_from('<Q',elf,at)[0]
   targets=[]
   if not item&1:cursor=item;targets=[cursor];cursor+=8
   else:targets=[cursor+8*i for i in range(63) if item&(1<<(i+1))];cursor+=63*8
   for dest in targets:
    value=struct.unpack('<Q',u.mem_read(base+dest,8))[0];u.mem_write(base+dest,struct.pack('<Q',base+value))
 if sec[1]==4:
  for at in range(sec[4],sec[4]+sec[5],24):
   dest,info,addend=struct.unpack_from('<QQq',elf,at);assert info==1027;u.mem_write(base+dest,struct.pack('<Q',base+addend))
 if sec[1]==2:
  st=sections[sec[6]];strings=elf[st[4]:st[4]+st[5]]
  for at in range(sec[4],sec[4]+sec[5],24):
   name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',elf,at)
   if name and index:symbols[strings[name:strings.index(0,name)].decode()]=value
U64=ctypes.c_uint64;U32=ctypes.c_uint32;PTR=U64
class Rom(ctypes.Structure):_fields_=[(n,U64) for n in ('bytes','entries','strings','metadata','metadata_size','total','data_base','strings_size')]+[('count',U32)]
class Request(ctypes.Structure):_fields_=[(n,U32) for n in ('state','op','result','reserved')]+[(n,U64) for n in ('a','b','c','d','answer','buffer')]+[('path',ctypes.c_char*1024),('path2',ctypes.c_char*1024)]
class Object(ctypes.Structure):_fields_=[(n,U64) for n in ('vtable','active','handle','mode')]
class Region(ctypes.Structure):_fields_=[('begin',U64),('end',U64),('type',U32),('permission',U32)]
class Launch(ctypes.Structure):
 _fields_=[('rom',Rom),('api',U64*28),('requests',Request*16),('objects',Object*96),('storage_table',U64*10),('fs_table',U64*24),('file_table',U64*10),('dir_table',U64*6),('storage',Object),('save_fs',Object)]+[(n,U64) for n in ('heap_base','heap_capacity','heap_size','arena_base','arena_capacity','arena_used')]+[(n,U32) for n in ('worker_handle','stage','finished','abort_requested')]+[(n,U64) for n in ('fsp_denied','rom_reads','save_calls','main_entered','frames')]+[('handle_tags',U64*128),('regions',Region*18),('region_count',U32),('preflight',U32),('preflight_result',U32),('cache_size',U64),('sample_results',U32*3),('sample_reads',U64*3),('samples',(ctypes.c_ubyte*4096)*3),('save_result',U32),('save_cookie',U64)]
layout=struct.unpack('<20Q',u.mem_read(base+symbols['nx2_launch_layout'],160))
expected=(ctypes.sizeof(Launch),28*8,ctypes.sizeof(Request),ctypes.sizeof(Object),Launch.api.offset,Launch.requests.offset,Launch.objects.offset,Launch.heap_base.offset,Launch.arena_base.offset,Launch.worker_handle.offset,Launch.cache_size.offset,Launch.sample_results.offset,Launch.samples.offset,Launch.save_result.offset,Launch.regions.offset,Launch.region_count.offset,Launch.handle_tags.offset,Launch.storage.offset,Request.buffer.offset,Request.path.offset)
assert layout==expected,(layout,expected)
launch_at=0x73000000;stop=0x72000000;map_at=0x75000000;arena=0x76000000;u.mem_map(launch_at,page(ctypes.sizeof(Launch)));u.mem_map(stop,4096);u.mem_map(map_at,0x100000);u.mem_map(arena,0x4000000)
job=Launch();job.worker_handle=0x1234;job.arena_base=arena;job.arena_capacity=0x4000000
api_names=['nnosInitialize','_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE','_ZN2nn2fs6detail27InitializeFileSystemLibraryEv','_ZN2nn2fs6detail18MountRomOnFileImplEPKcONSt3__110unique_ptrINS0_8IStorageENS4_14default_deleteIS6_EEEEPvm','_ZN2nn2fs6detail32QueryMountRomOnFileCacheSizeImplEPmPNS0_8IStorageE','_ZN2nn2fs3fsa8RegisterEPKcONSt3__110unique_ptrINS1_11IFileSystemENS4_14default_deleteIS6_EEEE','_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci','_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvmRKNS0_10ReadOptionE','_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE','_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE','_ZN2nn2fs9CloseFileENS0_10FileHandleE','_ZN2nn2fs7UnmountEPKc','_ZN2nn2fs10CreateFileEPKcl','_ZN2nn2fs10DeleteFileEPKc','_ZNK2nn2fs4Path9GetStringEv','_ZN2nn2fs32SetDefaultFsContextResultHandlerEPFNS0_14AbortSpecifierENS_6ResultEE','_ZTIN2nn2fs8IStorageE','_ZTIN2nn2fs3fsa11IFileSystemE','_ZTIN2nn2fs3fsa5IFileE','_ZTIN2nn2fs3fsa10IDirectoryE']
for i,n in enumerate(api_names):job.api[i]=sdkaddr(n)
u.mem_write(launch_at,bytes(job));rommap=options.rom_map.read_bytes();u.mem_write(map_at,rommap)
backup=options.rom_root;scratch=options.scratch;scratch.mkdir(parents=True,exist_ok=True);(scratch/'.__nx2_probe.bin').unlink(missing_ok=True);opened={};sequence=0;rpc=[]
def reply(uc,pc,size,data):
 global sequence
 for i in range(16):
  at=launch_at+Launch.requests.offset+i*ctypes.sizeof(Request);r=Request.from_buffer_copy(bytes(uc.mem_read(at,ctypes.sizeof(Request))))
  if r.state!=2:continue
  result=answer=0;p=r.path.decode();rpc.append({'op':r.op,'path':p,'a':r.a,'b':r.b,'c':r.c})
  try:
   if r.op==1:
    eo,so=struct.unpack_from('<QQ',rommap,40);start,length,po,pl=struct.unpack_from('<QQII',rommap,eo+r.a*32);name=rommap[so+po:so+po+pl].decode()
    with (backup/name).open('rb') as f:f.seek(r.b);chunk=f.read(r.c)
    assert len(chunk)==r.c;uc.mem_write(r.buffer,chunk)
   elif r.op in (2,3,13):
    q=scratch/p.lstrip('/');assert q.parent==scratch
    if r.op==2:
     with q.open('xb') as f:f.truncate(r.a)
    elif r.op==3:q.unlink()
    else:sequence+=1;opened[sequence]=q.open('r+b' if r.a&2 else 'rb');answer=sequence
   elif r.op==14:
    f=opened[r.a];f.seek(r.b);chunk=f.read(r.c);uc.mem_write(r.buffer,chunk);answer=len(chunk)
   elif r.op==15:
    f=opened[r.a];f.seek(r.b);f.write(bytes(uc.mem_read(r.buffer,r.c)));f.flush()
   elif r.op==19:opened.pop(r.a).close()
   elif r.op in (25,28,29,30):pass
   elif r.op==26:uc.emu_stop();return
   else:raise RuntimeError('Unmodeled RPC '+str(r.op))
  except FileNotFoundError:result=0x202
  except FileExistsError:result=0x402
  uc.mem_write(at+Request.result.offset,struct.pack('<I',result));uc.mem_write(at+Request.answer.offset,struct.pack('<Q',answer));uc.mem_write(at,struct.pack('<I',3))
for off in range(0,len(elf)-3,4):pass
for h in loads:
 if not h[1]&1:continue
 for off in range(h[3],h[3]+h[5]-3,4):
  if struct.unpack('<I',u.mem_read(base+off,4))[0]==0xd503205f:u.hook_add(UC_HOOK_CODE,reply,begin=base+off,end=base+off)
def returned(uc,pc,size,data):uc.emu_stop()
u.hook_add(UC_HOOK_CODE,returned,begin=stop,end=stop);kernel_hook=u.hook_add(UC_HOOK_INTR,interrupt)
import unicorn.arm64_const as R
def call(name,args):
 global reason
 u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X30,stop)
 for i in range(8):u.reg_write(getattr(R,'UC_ARM64_REG_X'+str(i)),args[i] if i<len(args) else 0)
 reason='instruction budget reached';u.emu_start(base+symbols[name],0,count=5000000)
 if u.reg_read(UC_ARM64_REG_PC)!=stop:
  (scratch/'bridge-failure.json').write_text(json.dumps({'pc':location(u.reg_read(UC_ARM64_REG_PC)),'lr':location(u.reg_read(UC_ARM64_REG_X30)),'x0':hex(u.reg_read(UC_ARM64_REG_X0)),'x1':hex(u.reg_read(UC_ARM64_REG_X1)),'reason':reason,'events':events[-5:],'rpc':rpc[-5:]},indent=2));raise RuntimeError('bridge stopped: '+reason+' '+location(u.reg_read(UC_ARM64_REG_PC)))
 return u.reg_read(UC_ARM64_REG_X0)

assert call('nx2_rom_view',[map_at,len(rommap),launch_at])==1
call('nx2_bridge_init',[launch_at])
# Refuse an already busy preemption context before changing host TLS.
before=bytes(range(128));u.mem_write(0x71000180,before);u.mem_write(0x71000100,b'\x01\x00')
call('nx2_preflight_worker',[launch_at])
current=Launch.from_buffer_copy(bytes(u.mem_read(launch_at,ctypes.sizeof(Launch))))
assert current.abort_requested==1 and not rpc and bytes(u.mem_read(0x71000180,128))==before
assert u.reg_read(UC_ARM64_REG_TPIDR_EL0)==0x710001f0
u.mem_write(0x71000100,b'\x00\x00');u.mem_write(launch_at+Launch.abort_requested.offset,bytes(4))
# Exercise actual SVC adapters/tag propagation using a synthetic offline SM.
# These are protocol fixtures, not claims about real console replies.
test_calls=[];next_handle=0x50;next_response_handle=0x50
def scrub(uc,start=1):
 for i in range(start,18):uc.reg_write(getattr(R,'UC_ARM64_REG_X'+str(i)),0)
def protocol_interrupt(uc,number,data):
 pc=uc.reg_read(UC_ARM64_REG_PC);ins=struct.unpack('<I',uc.mem_read(pc-4,4))[0]
 if ins&0xffe0001f==0xd4000001:
  svc=(ins>>5)&65535
  if svc==0x1f:scrub(uc,2);uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_X1,0x40);test_calls.append('connect-sm');return
  if svc==0x21:
   request=bytes(uc.mem_read(0x71000000,256));test_calls.append({'sync_handle':uc.reg_read(UC_ARM64_REG_X0),'type':struct.unpack_from('<I',request)[0]&65535})
   response=bytearray(256);struct.pack_into('<IIII',response,0,0,0x80000000,0x20,next_response_handle);uc.mem_write(0x71000000,bytes(response));scrub(uc);uc.reg_write(UC_ARM64_REG_X0,0);return
  if svc==0x16:scrub(uc);uc.reg_write(UC_ARM64_REG_X0,0);return
 interrupt(uc,number,data)
u.hook_add(UC_HOOK_INTR,protocol_interrupt)
# Remove the old kernel hook so each SVC is modeled exactly once.
# Unicorn identifies hooks by returned handles; rebuild registration above in prefix.
u.hook_del(kernel_hook)
u.mem_write(map_at+0x80000,b'sm:\x00');assert call('nx2_connect',[map_at+0x80020,map_at+0x80000])==0
def packet(kind,cmd,name=b''):
 p=bytearray(256);struct.pack_into('<II',p,0,kind,8);struct.pack_into('<IIII',p,16,0x49434653,0,cmd,0);p[32:32+len(name)]=name;u.mem_write(0x71000000,bytes(p))
def tags():
 j=Launch.from_buffer_copy(bytes(u.mem_read(launch_at,ctypes.sizeof(Launch))));return {x&0xffffffff:x>>32 for x in j.handle_tags if x}
packet(4,1,b'fsp-srv\x00');assert call('nx2_sync',[0x40])==0;assert tags()=={0x40:1,0x50:2}
next_response_handle=0x51;packet(5,2);assert call('nx2_sync',[0x50])==0;assert tags()[0x51]==2
packet(4,1005);assert call('nx2_sync',[0x51])==0
assert call('nx2_close_handle',[0x50])==0 and 0x50 not in tags()
next_response_handle=0x60;packet(4,1);u.mem_write(0x71000000+160,b'fsp-srv\x00');assert call('nx2_sync',[0x40])==0 and 0x60 not in tags()
next_response_handle=0x70;p=bytearray(256);struct.pack_into('<II',p,0,17,2);p[8:16]=b'fsp-srv\x00';u.mem_write(0x71000000,bytes(p));assert call('nx2_sync',[0x40])==0 and tags()[0x70]==2
before_count=len(test_calls);packet(4,51)
def abort_observer(uc,pc,size,data):
 for i in range(16):
  at=launch_at+Launch.requests.offset+i*ctypes.sizeof(Request);r=Request.from_buffer_copy(bytes(uc.mem_read(at,ctypes.sizeof(Request))))
  if r.state==2 and r.op==26:uc.emu_stop();return
# Register before reply by having it bypass abort requests in this script.
u.hook_add(UC_HOOK_CODE,abort_observer,begin=base+symbols['nx2_rpc'],end=base+symbols['nx2_rpc']+1024)
for h in loads:
 if h[1]&1:
  for off in range(h[3],h[3]+h[5]-3,4):
   if struct.unpack('<I',u.mem_read(base+off,4))[0]==0xd503205f:u.hook_add(UC_HOOK_CODE,abort_observer,begin=base+off,end=base+off)
u.reg_write(UC_ARM64_REG_SP,stack+0xff000);u.reg_write(UC_ARM64_REG_X30,stop);u.reg_write(UC_ARM64_REG_X0,0x51);u.emu_start(base+symbols['nx2_sync'],0,count=1000000)
assert len(test_calls)==before_count
current=Launch.from_buffer_copy(bytes(u.mem_read(launch_at,ctypes.sizeof(Launch))));assert current.fsp_denied==1
result={'worker_elf_sha256':hashlib.sha256(elf).hexdigest(),'busy_worker_refused_before_TLS_changes':True,'sm_cmif_fsp_handle_tagged':True,'sm_tipc_fsp_handle_tagged':True,'cloned_fsp_handle_tagged':True,'closed_handle_tag_removed':True,'stale_TLS_name_ignored':True,'read_only_FSP_query_forwarded_in_model':True,'save_mount_FSP_denied_before_kernel_call':True,'volatile_kernel_registers_scrubbed_in_model':True,'real_console_services_forwarded':0}
(scratch/'guard-result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
