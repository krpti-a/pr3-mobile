import struct,sys,json
d=open(__import__('os').environ['PR3_BIN'],'rb').read()
# parse load commands
ncmds=struct.unpack_from('<I',d,16)[0]; off=28
segs=[]; dyld=None
for i in range(ncmds):
    cmd,size=struct.unpack_from('<II',d,off)
    if cmd==1:  # LC_SEGMENT
        name=d[off+8:off+24].split(b'\0')[0].decode(); vmaddr,vmsize,fo,fs=struct.unpack_from('<IIII',d,off+24)
        segs.append((name,vmaddr,vmsize,fo,fs))
    if cmd in (0x22,0x80000022):
        dyld=struct.unpack_from('<10I',d,off+8)
    off+=size
rebase_off,rebase_size,bind_off,bind_size,weak_off,weak_size,lazy_off,lazy_size,exp_off,exp_size=dyld
def uleb(p):
    r=0;s=0
    while True:
        b=d[p];p+=1;r|=(b&0x7f)<<s;s+=7
        if not b&0x80: return r,p
def sleb(p):
    r=0;s=0
    while True:
        b=d[p];p+=1;r|=(b&0x7f)<<s;s+=7
        if not b&0x80:
            if b&0x40: r-=1<<s
            return r,p
def parse(off,size,lazy=False):
    out=[]; p=off; end=off+size
    seg=0;segoff=0;sym='';typ=1;addend=0
    while p<end:
        b=d[p];p+=1; op=b&0xf0; imm=b&0x0f
        if op==0x00: 
            if not lazy: pass
            continue
        elif op==0x10: pass  # set dylib ordinal imm
        elif op==0x20: _,p=uleb(p)
        elif op==0x30: pass
        elif op==0x40:
            e=d.index(b'\0',p); sym=d[p:e].decode(); p=e+1
        elif op==0x50: typ=imm
        elif op==0x60: addend,p=sleb(p)
        elif op==0x70: seg=imm; segoff,p=uleb(p)
        elif op==0x80: v,p=uleb(p); segoff+=v
        elif op==0x90:
            out.append(((segs[seg][1]+segoff)&0xffffffff,sym,typ,addend)); segoff+=4
        elif op==0xa0:
            out.append(((segs[seg][1]+segoff)&0xffffffff,sym,typ,addend)); v,p=uleb(p); segoff+=4+v
        elif op==0xb0:
            out.append(((segs[seg][1]+segoff)&0xffffffff,sym,typ,addend)); segoff+=4+imm*4
        elif op==0xc0:
            cnt,p=uleb(p); skip,p=uleb(p)
            for _ in range(cnt): out.append(((segs[seg][1]+segoff)&0xffffffff,sym,typ,addend)); segoff+=4+skip
        else: raise Exception('op %x'%op)
    return out
b=parse(bind_off,bind_size)
l=parse(lazy_off,lazy_size,True) if lazy_size else []
print(len(b),'binds',len(l),'lazy')
json.dump({'bind':b,'lazy':l},open(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)),'binds.json'),'w'))
import collections
print(collections.Counter(s.split('$')[0] if '$' in s else 'other' for _,s,_,_ in b))
print([s for _,s,_,_ in b][:60])
