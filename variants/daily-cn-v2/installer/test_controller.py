#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Collector and preflight-controller tests with fake clients; no sockets."""
from pathlib import Path
import argparse,copy,json,hashlib,importlib.util,re,subprocess,threading
from collector import Collector,parse,final_errors
from install import inspect,activation_rejected
p=argparse.ArgumentParser();p.add_argument('--bundle',type=Path,required=True);p.add_argument('--backup',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--qemu',default='qemu-mipsel');a=p.parse_args()
bundle=a.bundle.resolve();manifest=json.loads((bundle/'manifest.json').read_text());source=bundle/'source';tests=bundle/'tests'
def collector():
 c=Collector.__new__(Collector);c.manifest=manifest;c.mode='write';c.lock=threading.Lock();c.state={'packets':0,'accepted':0,'last_seq':0,'gaps':0,'reordered':0,'finals':0,'complete':False};return c
packet={'nonce':manifest['nonce']['write'],'image':manifest['files']['image.bin']['sha256'],'profile':'stock-ssh-write-v2','phase':'50','at':'50','code':'0','checks':'001fffff','preflight':'1','snapshot':'1','jffs':'1','alloc':'30408704','pending_mut':'0','pending_close':'0','erase_ok':'207','block_ok':'207','program_bytes':'13565952','programs':'52992','core_error':'0','guard_error':'0','target_error':'0','close_error':'0','target_valid':'1','target_equal':'1','guard_valid':'1','closed':'1','base_boot':manifest['protected']['MER_BOOT_SHA'],'after_boot':manifest['protected']['MER_BOOT_SHA'],'base_desired':manifest['files']['image.bin']['sha256'],'readback':manifest['files']['image.bin']['sha256'],'base_rest':'1'*64,'after_rest':'1'*64,'base_metadata':'2'*64,'after_metadata':'2'*64,'seq':'1'}
def raw(p):return ('MER1 '+' '.join(k+'='+v for k,v in p.items())+'\n').encode()
assert not final_errors(packet,manifest,'write');count=1
for key in packet:
 if key=='seq':continue
 broken=packet.copy();broken[key]='wrong'
 assert final_errors(broken,manifest,'write'),key
 count+=1
for seq in [1,2,3]:
 packet['seq']=str(seq)
 if seq==1:c=collector()
 assert c.accept(raw(packet),'192.168.1.1')
assert c.state['complete'];count+=1
for field in ['code','checks','target_equal']:
 c=collector()
 for seq in [1,2,3]:packet['seq']=str(seq);c.accept(raw(packet),'192.168.1.1')
 assert c.snapshot()['complete']
 invalid=packet.copy();invalid['seq']='4';invalid[field]='5'
 c.accept(raw(invalid),'192.168.1.1');assert not c.snapshot()['complete'] and c.snapshot()['failure_latched']
 for seq in [5,6,7]:packet['seq']=str(seq);c.accept(raw(packet),'192.168.1.1')
 assert not c.snapshot()['complete'];count+=1
for order in [[1,1,2,3],[1,3,4,5],[2,3,4]]:
 c=collector()
 for seq in order:packet['seq']=str(seq);c.accept(raw(packet),'192.168.1.1')
 assert not c.state['complete'];count+=1
assert not collector().accept(raw(packet),'192.168.1.9');count+=1
for data in [b'bad',b'MER1 seq=1 seq=2',b'MER1 nonce=\xff',b'X'*1401]:
 try:parse(data)
 except (ValueError,UnicodeError):count+=1
 else:raise AssertionError('Malformed packet accepted')
class Activation:
 def __init__(self,code,ready=True):self.code,self.ready=code,ready
 def exit_status_ready(self):return self.ready
 def recv_exit_status(self):return self.code
for code,ready in [(0,True),(-1,True),(3,False)]:activation_rejected(Activation(code,ready));count+=1
try:activation_rejected(Activation(1))
except RuntimeError:count+=1
else:raise AssertionError('Definite activation failure ignored')
# Inspect uses a fake SSH command stream. No SSH transport or device is opened.
backup=a.backup.read_bytes()
class Channel:
 def recv_exit_status(self):return 0
class Stream:
 def __init__(self,data):self.data=data;self.channel=Channel()
 def read(self):return self.data
class FakeClient:
 def __init__(self,bad=None):self.bad=bad
 def exec_command(self,cmd,timeout=None):
  rows=['dev: size erasesize name']
  from install import LAYOUT
  rows += ['mtd%d: %08x 00010000 "%s"'%(i,size,name) for i,(size,name) in enumerate(LAYOUT)]
  values={'cat /proc/mtd':('\n'.join(rows)+'\n').encode(),'uname -r':b'3.10.14\n','cat /proc/sys/kernel/random/boot_id':b'12345678-1234-1234-1234-123456789abc\n','cat /proc/meminfo':b'MemTotal: 126140 kB\nMemFree: 98304 kB\nCached: 0 kB\nBuffers: 0 kB\nShmem: 0 kB\n','df -k /tmp':b'Filesystem 1K-blocks Used Available Use% Mounted on\ntmpfs 63000 0 63000 0% /tmp\n','cat /dev/mtd9':backup}
  if self.bad=='memory':values['cat /proc/meminfo']=b'MemTotal: 58544 kB\nMemFree: 40000 kB\n'
  if self.bad=='kernel':values['uname -r']=b'6.12.74\n'
  if self.bad=='stock':values['cat /dev/mtd9']=backup[:0x40000]+bytes([backup[0x40000]^1])+backup[0x40001:]
  if self.bad=='mtd':values['cat /proc/mtd']=values['cat /proc/mtd'].replace(b'"firmware"',b'"other"')
  return None,Stream(values[cmd]),Stream(b'')
assert inspect(FakeClient(),manifest)['stock_and_protected_ranges_match'];count+=1
for bad in ['memory','kernel','stock','mtd']:
 try:inspect(FakeClient(bad),manifest)
 except AssertionError:count+=1
 else:raise AssertionError('Inspection accepted '+bad)
# Feed actual C-generated FINAL into the Python verifier, on host and MIPS.
template=Path(__file__).resolve().parent/'source/test_integration.c'
(source/'test_integration.c').write_bytes(template.read_bytes())
files=['test_integration.c','writer_flow.c','traced_io.c','writer_ledger.c','writer_core.c','status.c','snapshot_report.c','sha256.c']
flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(source),'-include',str(source/'stage_policy.h'),'-DMER_PROFILE_WRITE=1','-DMER_WRITE_REVIEW_BUILD=1','-DMER_CORE_EXECUTE=1','-DMER_STATUS_TEST=1']
for kind,cc,extra,runner in [('host','gcc',[],[]),('mips',a.cc,['-static','-mips32r2','-msoft-float','-fno-pie','-no-pie'],[a.qemu,'-cpu','24KEc'])]:
 exe=tests/('collector-contract.'+kind)
 subprocess.run([cc,*flags,*extra,'-o',str(exe),*[str(source/f) for f in files]],check=True)
 result=subprocess.run([*runner,str(exe),str(a.backup.resolve()),str(bundle/'image.bin'),'--nominal-only'],capture_output=True,text=True,check=True,timeout=120)
 (tests/('collector-contract-'+kind+'.stdout')).write_text(result.stdout)
 packets=[parse(line[len('FINAL_PACKET '):].encode()) for line in result.stdout.splitlines() if line.startswith('FINAL_PACKET ')]
 assert packets and all(not final_errors(p,manifest,'write') for p in packets)
 count+=1
(tests/'controller-results.json').write_text(json.dumps({'passed':True,'checks':count,'real_c_final_matches_python_verifier':['host','MIPS_QEMU'],'network_or_router_accessed':False},indent=2))
print('PASS '+str(count)+' controller/collector checks; actual host/MIPS C FINAL accepted; no network or device access')
