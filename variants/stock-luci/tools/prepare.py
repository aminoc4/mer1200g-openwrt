#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare a fresh local build from three hash-locked source archives."""
import argparse,hashlib,json,os,shutil,tarfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('archives',type=Path);p.add_argument('build_root',type=Path);a=p.parse_args()
here=Path(__file__).resolve().parents[1];root=a.build_root.resolve()
assert not root.exists(),'Use a fresh build directory'
for source in json.loads((here/'sources.lock.json').read_text()):
 archive=a.archives/source['file']
 assert hashlib.sha256(archive.read_bytes()).hexdigest()==source['sha256'],source['name']
 with tarfile.open(archive) as t:
  members=t.getmembers();prefix=members[0].name.split('/')[0]
  dest=root if source['name']=='openwrt' else root/'local-feeds'/('luci' if source['name']=='luci' else 'packages-source')
  dest.mkdir(parents=True,exist_ok=True)
  links=[]
  for m in members:
   parts=Path(m.name).parts
   assert parts[0]==prefix and '..' not in parts and not Path(m.name).is_absolute()
   if len(parts)<2:continue
   m.name=str(Path(*parts[1:]))
   if m.issym():
    q=dest/m.name;target=(q.parent/m.linkname).resolve()
    assert not Path(m.linkname).is_absolute() and target.is_relative_to(dest.resolve())
    links.append((q,m.linkname))
   else:
    assert not m.islnk(),'Unexpected hard link'
    t.extract(m,dest,filter='data')
  for q,target in links:q.parent.mkdir(parents=True,exist_ok=True);os.symlink(target,q)
webdeps=root/'local-feeds/webdeps';webdeps.mkdir()
for src,name in [('net/cgi-io','cgi-io'),('lang/lua/luasrcdiet','luasrcdiet')]:
 shutil.copytree(root/'local-feeds/packages-source'/src,webdeps/name)
(root/'feeds.conf').write_text(f'src-link luci {root}/local-feeds/luci\nsrc-link webdeps {webdeps}\n')
shutil.copy2(here/'config.full',root/'.config')
shutil.copytree(here/'files',root/'files')
for name in ('mtd','sysupgrade'):(root/'files/sbin'/name).chmod(0o755)
shutil.copy2(here/'board/mt7620a_mercury_mer1200g-v1.dts',root/'target/linux/ramips/dts')
shutil.copy2(here/'board/02_network',root/'target/linux/ramips/mt7620/base-files/etc/board.d/02_network')
shutil.copy2(here/'board/999-mer1200g-dtb-only.patch',root/'target/linux/ramips/patches-6.12')
f=root/'config/Config-kernel.in'
with f.open('a') as o:o.write('\nconfig KERNEL_MER1200G_DTB_ONLY\n\tbool "MER1200G DT-only early boot"\n\tdepends on TARGET_ramips_mt7620_DEVICE_mercury_mer1200g-v1 && !TARGET_MULTI_PROFILE\n\tdefault n\n')
f=root/'target/linux/ramips/image/mt7620.mk'
with f.open('a') as o:o.write('''
define Device/mercury_mer1200g-v1
  SOC := mt7620a
  DEVICE_VENDOR := Mercury
  DEVICE_MODEL := MER1200G
  DEVICE_VARIANT := v1 experimental
  DEVICE_DTS := mt7620a_mercury_mer1200g-v1
  DEVICE_PACKAGES := kmod-switch-rtl8367b
  IMAGE_SIZE := 13248k
  IMAGES := review-rootfs.bin
  KERNEL := $$(KERNEL_DTB)
  IMAGE/review-rootfs.bin := append-rootfs
  KERNEL_INITRAMFS := $$(KERNEL_DTB)
endef
TARGET_DEVICES += mercury_mer1200g-v1
''')
with (root/'target/linux/ramips/mt7620/config-6.12').open('a') as o:o.write('\n# CONFIG_MTD_SPLIT_SQUASHFS_ROOT is not set\n')
f=root/'package/base-files/files/etc/init.d/boot';s=f.read_text();needle='\t[ -f /proc/jffs2_bbc ] && echo "S" > /proc/jffs2_bbc\n';assert s.count(needle)==1
f.write_text(s.replace(needle,'\t# MER1200G: do not issue vendor JFFS2 block check command.\n'))
print('Prepared offline source; run build.sh on a Linux build host. No device actions.')
