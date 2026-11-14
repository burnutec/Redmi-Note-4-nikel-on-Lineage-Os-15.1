# Minimal FDT -> DTS decompiler (no dtc available). Phandles shown as numbers.
import struct, sys, string
def strz(b,o):
    e=b.index(b'\0',o); return b[o:e].decode(errors='replace')
def fmt(name,v):
    if not v: return f"{name};"
    if v[-1]==0 and all(32<=c<127 or c==0 for c in v[:-1]) and v[0]!=0 and b'\0\0' not in v:
        parts=v[:-1].split(b'\0'); return f"{name} = "+", ".join('"%s"'%p.decode() for p in parts)+";"
    if len(v)%4==0:
        return f"{name} = <"+" ".join("0x%x"%x for x in struct.unpack('>%dI'%(len(v)//4),v))+">;"
    return f"{name} = ["+" ".join("%02x"%c for c in v)+"];"
def dec(b):
    mg,ts,ost,szs,_,_,_,_,_,_=struct.unpack_from('>10I',b,0)
    assert mg==0xd00dfeed
    p=ost; dep=0; out=[]
    while True:
        tok=struct.unpack_from('>I',b,p)[0]; p+=4
        if tok==1:
            n=strz(b,p); p+=(len(n.encode())+4)//4*4
            out.append("\t"*dep+(n or "/")+" {"); dep+=1
        elif tok==2: dep-=1; out.append("\t"*dep+"};")
        elif tok==3:
            l,no=struct.unpack_from('>II',b,p); p+=8; v=b[p:p+l]; p+=(l+3)//4*4
            out.append("\t"*dep+fmt(strz(b,szs+no),v))
        elif tok==4: pass
        elif tok==9: break
    return "/dts-v1/;\n"+"\n".join(out)+"\n"
if __name__=="__main__":
    b=open(sys.argv[1],'rb').read(); off=int(sys.argv[2]) if len(sys.argv)>2 else 0
    sz=struct.unpack_from('>I',b,off+4)[0]
    open(sys.argv[3] if len(sys.argv)>3 else '/dev/stdout','w').write(dec(b[off:off+sz]))
