#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply final daily-v2 sources over the retained hash-locked base recipe."""
from pathlib import Path
import argparse,subprocess,sys,shutil
p=argparse.ArgumentParser();p.add_argument('archives',type=Path);p.add_argument('build_root',type=Path);a=p.parse_args()
here=Path(__file__).resolve().parents[1]
baseline=here.parent/'compact-cn/firmware'
root=a.build_root.resolve();assert not root.exists()
subprocess.run([sys.executable,str(baseline/'tools/prepare.py'),str(a.archives.resolve()),str(root)],check=True)
# This is a newly created build destination, never an existing installation.
for relative in ['files','package/luci-app-mer-home']:
 target=(root/relative).resolve();assert target.is_relative_to(root) and target!=root
 shutil.rmtree(target)
shutil.copytree(here/'files',root/'files')
shutil.copy2(here/'config.full',root/'.config')
shutil.copy2(here/'board.dts',root/'target/linux/ramips/dts/mt7620a_mercury_mer1200g-v1.dts')
services=root/'local-feeds/services';services.mkdir()
(services/'ddns-scripts').symlink_to(root/'local-feeds/packages-source/net/ddns-scripts',target_is_directory=True)
with (root/'feeds.conf').open('a') as f:f.write('src-link services '+str(services)+'\n')
for directory in ['etc/config']:
 for path in (root/'files'/directory).iterdir():path.chmod(0o600)
for directory in ['etc/uci-defaults','sbin']:
 for path in (root/'files'/directory).iterdir():path.chmod(0o755)
print('Prepared final source only; no device operation.')
