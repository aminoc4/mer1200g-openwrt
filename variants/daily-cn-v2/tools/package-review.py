#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline legacy wrapper/range constructor; never writes a device."""
import argparse,hashlib,json,lzma,struct,zlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser();p.add_argument('build_root',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
assert all(p.name in {'kernel.multi-uimage.DO_NOT_FLASH','rootfs.squashfs','firmware-window.review.DO_NOT_FLASH.bin','review.json'} for p in a.output.iterdir())
k=a.build_root/'build_dir/target-mipsel_24kc_musl/linux-ramips_mt7620'
import io
with io.BytesIO((k/'vmlinux.debug').read_bytes()) as f:
 e=ELFFile(f);s=e.get_section_by_name('.symtab');sym=lambda n:int(s.get_symbol_by_name(n)[0]['st_value'])
 entry=sym('__kernel_entry');target=sym('kernel_entry');dtb=sym('__appended_dtb');end=sym('_end');assert target==e.header.e_entry
raw=(k/'vmlinux').read_bytes();compressed=(k/'mercury_mer1200g-v1-kernel.bin').read_bytes()
assert entry==0x80000400 and len(raw)==dtb-0x80000000 and end<0x80c00000
word=struct.unpack_from('<I',raw,0x400)[0];assert word>>26==2 and ((entry+4)&0xf0000000)|((word&0x3ffffff)<<2)==target
d=lzma.LZMADecompressor(format=lzma.FORMAT_ALONE);expanded=d.decompress(compressed)
assert d.eof and not d.unused_data and expanded.startswith(raw) and len(expanded)<0xb00000
assert compressed[:5]==bytes.fromhex('6d00008000')
payload=struct.pack('>II',len(compressed),0)+compressed
h=struct.pack('>7I4B32s',0x27051956,0,1774469393,len(payload),0x80000000,entry,zlib.crc32(payload),5,5,4,3,b'MER1200G daily CN v2')
h=h[:4]+struct.pack('>I',zlib.crc32(h))+h[8:];image=h+payload
rootfs=(a.build_root/'bin/targets/ramips/mt7620/openwrt-ramips-mt7620-mercury_mer1200g-v1-squashfs-review-rootfs.bin').read_bytes()
assert len(image)<=0x300000 and rootfs[:4]==b'hsqs' and len(rootfs)<=0x480000
window=bytearray(b'\xff'*0xcf0000);window[:len(image)]=image;window[0x300000:0x300000+len(rootfs)]=rootfs;window[0x780000:0x780004]=bytes.fromhex('deadc0de')
outputs={'kernel.multi-uimage.DO_NOT_FLASH':image,'rootfs.squashfs':rootfs,'firmware-window.review.DO_NOT_FLASH.bin':window}
for n,b in outputs.items():(a.output/n).write_bytes(b)
report={'scope':'OFFLINE UNBOOTED DAILY V2; partition migration required','range':['0x040000','0xd30000'],'erase_blocks':207,'overlay_bytes':0x570000,'artifacts':{n:{'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for n,b in outputs.items()}}
(a.output/'review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
