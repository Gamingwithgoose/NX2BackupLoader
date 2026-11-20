'use strict';
const fs=require('node:fs'),path=require('node:path');
if(process.argv.length!==5)throw Error('Usage: node export-startup.cjs DECODER_EXEFS_CJS EXEFS_DIR OUTPUT_DIR');
const {decodeNso}=require(path.resolve(process.argv[2]));
const root=path.resolve(process.argv[3]);
const names=['rtld','main','subsdk0','subsdk1','subsdk2','sdk'];
function check(ok,why){if(!ok)throw Error(why);}
const modules=[];
const expected={"rtld":"b01d53da100a51eb40d408d7011cd5b09d7f4ee2578ef536d089855b51ee5e0f","main":"ddaf0958eacebb54996c43dae90346ca96e1df032111a1f1f56291b21bba3732","subsdk0":"86fa5ec126d41f9c89bf332d84032476b033168f133cb8cdcc83ad12e33cbca4","subsdk1":"0c02d2050f52a26462e3859b15395749d0867fe418624538631dd8e7026dd2d8","subsdk2":"8f72c818479fa3410020a9c7bffe8dd2f6224d16f366bd0fe73adeb0201e0dee","sdk":"4617761033cab45951a19fa7797fa79ad691c46f324048f832484877d3df034c"};
for(const name of names){
 const original=fs.readFileSync(path.join(root,name));
 if(require('node:crypto').createHash('sha256').update(original).digest('hex')!==expected[name])throw Error('This experiment supports only the verified supplied Yoshi build: '+name);
 const nso=decodeNso(original);
 function read(at,size){check(Number.isSafeInteger(at)&&at>=0&&size>=0,'invalid virtual read');const s=nso.segments.find(s=>s.address<=at&&at+size<=s.address+s.size);check(s,'virtual read outside module '+name+' at '+at.toString(16));return s.data.subarray(at-s.address,at-s.address+size);}
 const mod=read(4,4).readUInt32LE();check(read(mod,4).toString()==='MOD0','missing declared MOD0 '+name);
 const dynamic=mod+read(mod+4,4).readInt32LE();const tags=new Map();let terminated=false;
 for(let i=0;i<1024;i++){const b=read(dynamic+i*16,16),tag=Number(b.readBigInt64LE()),value=Number(b.readBigUInt64LE(8));if(!tag){terminated=true;break;}tags.set(tag,value);}
 check(terminated,'unterminated dynamic '+name);
 const stringTable=tags.has(5)?read(tags.get(5),tags.get(10)):Buffer.alloc(0);
 const string=at=>{check(at<stringTable.length,'bad string index');const end=stringTable.indexOf(0,at);check(end>=at,'unterminated symbol');return stringTable.toString('utf8',at,end);};
 let symbolCount=0;if(tags.has(4))symbolCount=read(tags.get(4)+4,4).readUInt32LE();
 if(!symbolCount&&tags.has(0x6ffffef5)){
  const at=tags.get(0x6ffffef5),h=read(at,16),buckets=h.readUInt32LE(),first=h.readUInt32LE(4),bloom=h.readUInt32LE(8);
  check(buckets<1000000&&bloom<1000000,'GNU hash bounds');const bucketAt=at+16+bloom*8,chainAt=bucketAt+buckets*4;let highest=0;
  for(let i=0;i<buckets;i++)highest=Math.max(highest,read(bucketAt+i*4,4).readUInt32LE());
  symbolCount=first;
  if(highest){check(highest>=first,'GNU hash bucket index');let index=highest;for(;;index++){check(index<1000000,'GNU hash chain limit');if(read(chainAt+(index-first)*4,4).readUInt32LE()&1){symbolCount=index+1;break;}}}
 }
 check(!tags.has(6)||symbolCount>0,'symbol count unavailable '+name);
 check(symbolCount<1000000,'symbol count limit');const symbols=[];
 for(let i=0;i<symbolCount;i++){const b=read(tags.get(6)+i*(tags.get(11)||24),24);symbols.push({name:b.readUInt32LE()?string(b.readUInt32LE()):'',binding:b[4]>>4,type:b[4]&15,section:b.readUInt16LE(6),value:Number(b.readBigUInt64LE(8)),size:Number(b.readBigUInt64LE(16))});}
 const relocations={};let count=0;
 for(const [pointer,size] of [[7,8],[23,2]])if(tags.has(pointer)&&tags.get(size)){
  check((tags.get(size)%24)===0,'non-RELA relocation table '+name);
  for(let at=0;at<tags.get(size);at+=24){const b=read(tags.get(pointer)+at,24),type=b.readUInt32LE(8),symbol=b.readUInt32LE(12);check(symbol<symbols.length||symbol===0,'relocation symbol bound');relocations[type]=(relocations[type]||0)+1;count++;if(type===1031)console.error(JSON.stringify({module:name,type,offset:Number(b.readBigUInt64LE(0)),symbol:symbols[symbol],addend:Number(b.readBigInt64LE(16))}));}
 }
 const memoryEnd=Math.max(...nso.segments.map(s=>s.address+s.size));
 modules.push({name,nso,read,symbols,summary:{name,segments:nso.segments.map(s=>({address:s.address,size:s.size,compressed:s.originalCompressed,zbic:s.originalZbic,sha256:s.originalHash})),bss:nso.header.readUInt32LE(0x3c),mapped_bytes:memoryEnd+nso.header.readUInt32LE(0x3c),mod0:mod,dynamic,symbol_count:symbolCount,relocation_count:count,relocation_types:relocations,dynamic_tags:[...tags.keys()].map(n=>'0x'+n.toString(16))}});
 console.error('Decompressed and parsed '+name);
}
const out=path.resolve(process.argv[4]);fs.mkdirSync(out,{recursive:true});
const bases=[0x80b1000,0x82b1000,0x122b1000,0x15901000,0x16d81000,0x16fc1000];
const result=[];
for(let i=0;i<modules.length;i++){
 const m=modules[i];const memory=Buffer.alloc(m.summary.mapped_bytes);
 for(const s of m.nso.segments)s.data.copy(memory,s.address);
 fs.writeFileSync(out+'/'+m.name+'.image.bin',memory);
 if(m.name==='rtld')fs.writeFileSync(out+'/rtld.text.bin',m.nso.segments[0].data);
 result.push({...m.summary,base:bases[i],symbols:m.symbols});
}
fs.writeFileSync(out+'/images.json',JSON.stringify(result));