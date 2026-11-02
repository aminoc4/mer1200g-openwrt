#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a private, per-backup 207-block stock SSH bundle. No router access."""
from pathlib import Path
import argparse,hashlib,json,lzma,re,secrets,shutil,struct,subprocess,zlib

if not __debug__:
    raise RuntimeError('Run Python without -O: installer validation requires assertions')

START,END,ERASE,FLASH=0x40000,0xd30000,0x10000,0x1000000
def sha(data):return hashlib.sha256(data).hexdigest()
def validate_image(image):
    assert len(image)==END-START,'Expected exactly the 207-block firmware window'
    h=list(struct.unpack('>7I4B32s',image[:64]));saved=h[1];h[1]=0
    assert h[0]==0x27051956 and zlib.crc32(struct.pack('>7I4B32s',*h))==saved
    assert h[4:6]==[0x80000000,0x80000400] and h[7:11]==[5,5,4,3]
    assert 8<h[3]<=0x300000-64
    payload=image[64:64+h[3]];assert zlib.crc32(payload)==h[6]
    size,zero=struct.unpack('>II',payload[:8]);assert zero==0 and size==len(payload)-8
    assert payload[8:13]==bytes.fromhex('6d00008000')
    decoder=lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
    raw=decoder.decompress(payload[8:],max_length=0xb00001)
    assert decoder.eof and not decoder.unused_data and len(raw)<=0xb00000
    instruction=struct.unpack_from('<I',raw,0x400)[0]
    jump=((0x80000404)&0xf0000000)|((instruction&0x3ffffff)<<2)
    assert instruction>>26==2 and 0x80000000<=jump<0x80000000+len(raw) and jump!=0x80000400
    offset=raw.rfind(bytes.fromhex('d00dfeed'));assert offset>=0
    dtb=raw[offset:];magic,total,off_struct,off_strings=struct.unpack('>4I',dtb[:16])
    assert 40<=total<=len(dtb) and off_struct<total and off_strings<total
    strings=dtb[off_strings:total];pos=off_struct;stack=[];nodes={}
    while True:
        token=struct.unpack_from('>I',dtb,pos)[0];pos+=4
        if token==1:
            end=dtb.index(0,pos);name=dtb[pos:end].decode();pos=(end+4)&~3
            stack.append(name);nodes['/'+'/'.join(x for x in stack if x)]={}
        elif token==2:stack.pop()
        elif token==3:
            length,nameoff=struct.unpack_from('>II',dtb,pos);pos+=8
            end=strings.index(0,nameoff);name=strings[nameoff:end].decode()
            assert pos+length<=total
            nodes['/'+'/'.join(x for x in stack if x)][name]=dtb[pos:pos+length]
            pos=(pos+length+3)&~3
        elif token==4:pass
        elif token==9:break
        else:raise ValueError('Invalid FDT token')
    assert b'mercury,mer1200g-v1' in nodes['/']['compatible'].split(b'\0')
    memory=next(v for k,v in nodes.items() if k.startswith('/memory@'))
    assert struct.unpack('>II',memory['reg'])==(0,0x8000000)
    parts=sorted((struct.unpack('>II',v['reg']),v) for k,v in nodes.items() if '/partitions/partition@' in k and '/nvmem-' not in k)
    expected=[(0,0x40000),(0x40000,0x300000),(0x340000,0x480000),(0x7c0000,0x570000),(0xd30000,0x10000),(0xd40000,0x10000),(0xd50000,0x10000),(0xd60000,0x20000),(0xd80000,0x70000),(0xdf0000,0x200000),(0xff0000,0x10000)]
    assert [p[0] for p in parts]==expected
    assert all(('read-only' in v)==(reg[0]!=0x7c0000) for reg,v in parts)
    assert image[0x300000:0x300004]==b'hsqs'
    used=struct.unpack_from('<Q',image,0x300000+40)[0]
    assert 96<=used<=0x480000
    assert set(image[64+h[3]:0x300000])=={255}
    assert set(image[0x300000+used:0x780000])=={255}
    assert image[0x780000:0x780004]==bytes.fromhex('deadc0de')
    assert set(image[0x780004:])=={255},'Fresh native overlay initialization only'
    return {'kernel_bytes':64+h[3],'rootfs_bytes':used,'blocks':207,'overlay':'native-firstboot-marker'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--backup',required=True,type=Path);p.add_argument('--image',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path);p.add_argument('--cc',required=True)
    a=p.parse_args();here=Path(__file__).resolve().parent
    backup=a.backup.read_bytes();image=a.image.read_bytes()
    assert len(backup)==FLASH and len(set(backup[:START]))>16
    metadata=validate_image(image)
    out=a.output.resolve();assert not out.exists(),'Use a new private output directory'
    out.mkdir(mode=0o700,parents=True);source=out/'source';shutil.copytree(here/'source',source)
    pins={'MER_BACKUP_SHA':sha(backup),'MER_IMAGE_SHA':sha(image),'MER_STOCK_SHA':sha(backup[START:END]),'MER_BOOT_SHA':sha(backup[:START]),'MER_DEVICE_INFO_SHA':sha(backup[0xd30000:0xd40000]),'MER_FIRMWARE_INFO_SHA':sha(backup[0xd40000:0xd50000]),'MER_TDDP_SHA':sha(backup[0xd50000:0xd60000]),'MER_ART_SHA':sha(backup[0xff0000:]),'MER_PREFLIGHT_NONCE':secrets.token_hex(16),'MER_WRITE_NONCE':secrets.token_hex(16)}
    def header():
        lines=['#ifndef MER_INSTALL_POLICY_H','#define MER_INSTALL_POLICY_H']
        lines += ['#define '+k+' '+(json.dumps(v) if isinstance(v,str) else str(v)+'u') for k,v in pins.items()]
        lines += ['#define MER_BUNDLE_IMAGE_SHA MER_IMAGE_SHA','#define MER_SNAPSHOT_DESIRED MER_IMAGE_SHA','#define MER_SNAPSHOT_BOOT MER_BOOT_SHA','#define MER_SNAPSHOT_ALLOC 30408704u','#endif']
        (source/'install_policy.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-static','-mips32r2','-msoft-float','-fno-pie','-no-pie','-Wl,--build-id=none']
    def compile(name,files,extra=[]):
        subprocess.run([a.cc,*flags,*extra,'-I',str(source),'-o',str(out/name),*[str(source/f) for f in files]],check=True)
    header()
    compile('stock-preflight.mipsel',['stock_preflight.c','sha256.c'])
    compile('snapshot.mipsel',['snapshot_helper.c','writer_core.c','sha256.c'])
    for name,key in [('stock-preflight.mipsel','PREFLIGHT'),('snapshot.mipsel','SNAPSHOT_HELPER')]:
        data=(out/name).read_bytes();pins['MER_'+key+'_SHA']=sha(data);pins['MER_'+key+'_BYTES']=len(data)
    header()
    # Helper metadata is not used in helper code; independently confirm no cycle.
    compile('snapshot-rebuilt.mipsel',['snapshot_helper.c','writer_core.c','sha256.c'])
    assert (out/'snapshot-rebuilt.mipsel').read_bytes()==(out/'snapshot.mipsel').read_bytes()
    (out/'snapshot-rebuilt.mipsel').unlink()
    common=['enabled_entry.c','handover_ops.c','mount_graph.c','status.c','sha256.c','live_guard.c','cleanup_jffs2.c','snapshot_report.c','capture_readonly.c']
    commonflags=['-include',str(source/'stage_policy.h'),'-DMER_BUILD_RAM_PREFLIGHT_STAGE=1','-DMER_REVIEW_REAL_OPS=1']
    compile('preflight.mipsel',common,commonflags)
    compile('writer.mipsel',common+['writer_core.c','writer_flow.c','writer_runtime.c','traced_io.c','writer_ledger.c'],commonflags+['-DMER_PROFILE_WRITE=1','-DMER_WRITE_REVIEW_BUILD=1','-DMER_CORE_EXECUTE=1'])
    (out/'image.bin').write_bytes(image)
    files={name:{'sha256':sha((out/name).read_bytes()),'bytes':(out/name).stat().st_size} for name in ['stock-preflight.mipsel','snapshot.mipsel','preflight.mipsel','writer.mipsel','image.bin']}
    manifest={'format':'mer1200g-stock-ssh-v2','private_per_device':True,'backup_sha256':sha(backup),'stock_region_sha256':pins['MER_STOCK_SHA'],'protected':{k:v for k,v in pins.items() if k in ['MER_BOOT_SHA','MER_DEVICE_INFO_SHA','MER_FIRMWARE_INFO_SHA','MER_TDDP_SHA','MER_ART_SHA']},'nonce':{'preflight':pins['MER_PREFLIGHT_NONCE'],'write':pins['MER_WRITE_NONCE']},'files':files,'image':metadata,'geometry':{'flash':FLASH,'start':START,'end':END,'erase':ERASE,'blocks':207},'hardware_stock_to_v2_tested':False}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'private_output':str(out),'files_built':list(files),'image_validation':metadata,'router_accessed':False},indent=2))
if __name__=='__main__':main()
