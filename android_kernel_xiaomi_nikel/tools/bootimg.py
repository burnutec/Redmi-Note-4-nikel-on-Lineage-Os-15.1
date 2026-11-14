import struct, sys, os
p=sys.argv[1]; out=sys.argv[2]; os.makedirs(out,exist_ok=True)
d=open(p,'rb').read()
assert d[:8]==b'ANDROID!'
(ks,ka,rs,ra,ss,sa,ta,ps,hv,osv)=struct.unpack_from('<10I',d,8)
name=d[48:64].rstrip(b'\0').decode(errors='replace')
cmd=d[64:64+512].rstrip(b'\0').decode(errors='replace')
extra=d[64+512+32:64+512+32+1024].rstrip(b'\0').decode(errors='replace')
print(f"kernel_size={ks} kernel_addr=0x{ka:08x}\nramdisk_size={rs} ramdisk_addr=0x{ra:08x}\nsecond_size={ss} second_addr=0x{sa:08x}\ntags_addr=0x{ta:08x}\npagesize={ps}\nheader_version={hv}\nos_version=0x{osv:08x}")
if osv:
    a=(osv>>11)&0x1fffff; lvl=osv&0x7ff
    print(f"  os {a>>14}.{(a>>7)&0x7f}.{a&0x7f}  patch {2000+(lvl>>4)}-{lvl&0xf:02d}")
print("name=",repr(name)); print("cmdline=",cmd+extra)
pg=lambda n:(n+ps-1)//ps*ps
off=ps
open(f'{out}/kernel','wb').write(d[off:off+ks]); off+=pg(ks)
open(f'{out}/ramdisk','wb').write(d[off:off+rs]); off+=pg(rs)
if ss: open(f'{out}/second','wb').write(d[off:off+ss])
base=ka-0x00080000
print(f"derived: base=0x{base:08x} kernel_off=0x{ka-base:08x} ramdisk_off=0x{ra-base:08x} tags_off=0x{ta-base:08x}")
