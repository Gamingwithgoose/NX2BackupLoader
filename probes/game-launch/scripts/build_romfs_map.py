"""Build a metadata-only RomFS view of the owned loose backup, without copying assets."""
import argparse,struct,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
def align(n,b=4):return (n+b-1)&-b
def prime(n):
 n=max(3,n|1)
 while any(n%d==0 for d in range(3,int(n**0.5)+1,2)):n+=2
 return n
def hname(parent,name):
 h=parent^123456789
 for b in name.encode():h=((h>>5)|(h<<27))&0xffffffff;h^=b
 return h
files=sorted((x.relative_to(a.root).as_posix(),x.stat().st_size) for x in a.root.rglob('*') if x.is_file() and x.name!='.listing')
assert files and len(files)<65536
paths={''}
for n,_ in files:
 assert len(n.encode())<1024 and '\\' not in n and all(s not in ('','..','.') for s in n.split('/'))
 for q in Path(n).parents:
  s=q.as_posix()
  if s!='.':paths.add(s)
dirs=sorted(paths,key=lambda x:(x.count('/')+(bool(x)),x));do={};fo={};dm=bytearray();fm=bytearray();none=0xffffffff
for d in dirs:do[d]=len(dm);dm+=bytes(align(24+len(d.rsplit('/',1)[-1].encode())))
for f,n in files:fo[f]=len(fm);fm+=bytes(align(32+len(f.rsplit('/',1)[-1].encode())))
db=prime(len(dirs));fb=prime(len(files));dh=[none]*db;fh=[none]*fb;data=0;ext=[];strings=bytearray()
children={d:[] for d in dirs};childfiles={d:[] for d in dirs}
for d in dirs:
 if d:children[d.rsplit('/',1)[0] if '/' in d else ''].append(d)
for f,n in files:childfiles[f.rsplit('/',1)[0] if '/' in f else ''].append(f)
for d in dirs:
 parent=d.rsplit('/',1)[0] if '/' in d else '';siblings=children[parent] if d else [];idx=siblings.index(d) if d else -1
 name=d.rsplit('/',1)[-1] if d else '';bucket=hname(do[parent],name)%db;at=do[d]
 struct.pack_into('<6I',dm,at,do[parent],do[siblings[idx+1]] if idx>=0 and idx+1<len(siblings) else none,do[children[d][0]] if children[d] else none,fo[childfiles[d][0]] if childfiles[d] else none,dh[bucket],len(name.encode()));dm[at+24:at+24+len(name.encode())]=name.encode();dh[bucket]=at
for f,n in files:
 parent=f.rsplit('/',1)[0] if '/' in f else '';siblings=childfiles[parent];i=siblings.index(f);name=f.rsplit('/',1)[-1];bucket=hname(do[parent],name)%fb;at=fo[f];data=align(data,16)
 struct.pack_into('<IIQQII',fm,at,do[parent],fo[siblings[i+1]] if i+1<len(siblings) else none,data,n,fh[bucket],len(name.encode()));fm[at+32:at+32+len(name.encode())]=name.encode();fh[bucket]=at
 po=len(strings);strings+=f.encode()+b'\0';ext.append((data,n,po,len(f.encode())));data+=n
meta=bytearray(80);dho=len(meta);meta+=struct.pack('<'+'I'*db,*dh);dmo=len(meta);meta+=dm;fho=len(meta);meta+=struct.pack('<'+'I'*fb,*fh);fmo=len(meta);meta+=fm;data_base=align(len(meta),4096)
struct.pack_into('<10Q',meta,0,80,dho,db*4,dmo,len(dm),fho,fb*4,fmo,len(fm),data_base)
entries=b''.join(struct.pack('<QQIIQ',data_base+off,n,po,pl,0) for off,n,po,pl in ext);so=80+len(entries);mo=align(so+len(strings),16);header=struct.pack('<8sIIQQIIQQQQQ',b'NX2ROM01',1,80,len(meta),align(data_base+data,4096),len(files),32,80,so,len(strings),mo,data_base)
blob=header+entries+strings+bytes(mo-so-len(strings))+meta;(a.out/'romfs.map').write_bytes(blob)
refs=[]
for name,off,size in [('UECommandLine.txt',0,40),('PJ045/Content/Paks/PJ045-Sage.utoc',0,4096),('PJ045/Content/Paks/PJ045-Sage.ucas',0x80010000,4096)]:
 path=a.root/name
 with path.open('rb') as f:f.seek(off);b=f.read(size)
 assert len(b)==size
 refs.append({'path':'rom:/'+name,'offset':off,'length':size,'sha256':hashlib.sha256(b).hexdigest()})
summary={'index_sha256':hashlib.sha256(blob).hexdigest(),'files':len(files),'directories':len(dirs),'metadata_bytes':len(meta),'virtual_bytes':align(data_base+data,4096),'samples':refs}
(a.out/'romfs-reference.json').write_text(json.dumps(summary,indent=2))
(a.out/'romfs_reference.h').write_text('#define NX2_ROMFS_MAP_SHA256 "'+summary['index_sha256']+'"\nstatic const struct {const char *path;uint64_t offset;uint32_t length;const char *sha;} nx2_romfs_samples[]={\n'+''.join('{"'+r['path']+'",'+str(r['offset'])+'ULL,'+str(r['length'])+',"'+r['sha256']+'"},\n' for r in refs)+'};\n')
print(json.dumps(summary,indent=2))
