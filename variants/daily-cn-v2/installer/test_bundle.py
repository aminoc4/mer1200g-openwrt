#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline host/sanitizer/QEMU tests. All writes use memory-only NOR backends."""
from pathlib import Path
import argparse,subprocess,hashlib,json,os,shutil
from build_bundle import validate_image
p=argparse.ArgumentParser();p.add_argument('--bundle',type=Path,required=True);p.add_argument('--backup',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--qemu',default='qemu-mipsel');a=p.parse_args()
bundle=a.bundle.resolve();source=bundle/'source';tests=bundle/'tests';tests.mkdir(exist_ok=True)
manifest=json.loads((bundle/'manifest.json').read_text())
backup=a.backup.resolve().read_bytes();image=(bundle/'image.bin').read_bytes()
assert hashlib.sha256(backup).hexdigest()==manifest['backup_sha256']
validate_image(image)
# Tests are separately maintained; the compiled production source stays frozen.
for path in (Path(__file__).resolve().parent/'source').glob('test_*.c'):shutil.copy2(path,source/path.name)
pins=manifest['protected'];tail=hashlib.sha256(backup[0xd30000:]).hexdigest();desired=manifest['files']['image.bin']['sha256']
fixture='MER-WRITER-RESULT phase=precheck-complete error=0 offset=00000000 attempted=0 erased=0 verified=0 guard_valid=0 guard_error=0\n'+f"GUARD boot_before={pins['MER_BOOT_SHA']} boot_after= rest_before={tail} rest_after= metadata_before={tail} metadata_after=\n"+f'REGION desired={desired} readback=; allocated=30408704 tail=0; no reboot/rollback stage\n'+'PASS read-only precheck; not permission to write or evidence of device exclusivity\n'
(tests/'snapshot.txt').write_text(fixture)
records=[]
def call(name,cmd,expected=0,timeout=300):
    r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=timeout,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
    (tests/(name+'.stdout')).write_text(r.stdout);(tests/(name+'.stderr')).write_text(r.stderr)
    records.append({'name':name,'exit':r.returncode,'expected':expected})
    (tests/'results.json').write_text(json.dumps(records,indent=2))
    assert r.returncode==expected,(name,r.returncode,r.stdout[-1500:],r.stderr[-2000:])
    print(name+': PASS '+r.stdout[-250:].strip(),flush=True)
base=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(source),'-include',str(source/'stage_policy.h')]
write=['-DMER_PROFILE_WRITE=1','-DMER_WRITE_REVIEW_BUILD=1','-DMER_CORE_EXECUTE=1']
core=['test_core.c','writer_core.c','sha256.c']
protocol=['test_protocol.c','traced_io.c','writer_ledger.c','writer_core.c','snapshot_report.c','sha256.c']
flow=['test_integration.c','writer_flow.c','traced_io.c','writer_ledger.c','writer_core.c','status.c','snapshot_report.c','sha256.c']
specs=[('core',core,write,[a.backup.resolve(),bundle/'image.bin']),('protocol',protocol,write,[]),('integration',flow,write+['-DMER_STATUS_TEST=1'],[a.backup.resolve(),bundle/'image.bin']),('parser',['test_snapshot_report.c','snapshot_report.c'],[],[tests/'snapshot.txt']),('status',['test_status.c','status.c','snapshot_report.c'],['-DMER_STATUS_TEST=1'],[])]
for name,files,flags,args in specs:
    exe=tests/(name+'.host')
    call(name+'-compile',['gcc',*base,*flags,'-o',exe,*[source/f for f in files]])
    call(name+'-host',[exe,*args],timeout=600)
for name,files,flags,args in specs[:3]:
    exe=tests/(name+'.sanitized')
    call(name+'-sanitized-compile',['gcc',*base,*flags,'-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-o',exe,*[source/f for f in files]])
    call(name+'-sanitized',[exe,*args],timeout=600)
for name,files,flags,args in [specs[0],specs[1],specs[2]]:
    exe=tests/(name+'.mipsel')
    call(name+'-mips-compile',[a.cc,*base,*flags,'-static','-mips32r2','-msoft-float','-fno-pie','-no-pie','-Wl,--build-id=none','-o',exe,*[source/f for f in files]])
    nominal=args+['--nominal-only'] if name in ['core','integration'] else args
    call(name+'-qemu',[a.qemu,'-cpu','24KEc',exe,*nominal],timeout=600)
for name in ['preflight.mipsel','writer.mipsel']:
    call(name+'-info',[a.qemu,'-cpu','24KEc',bundle/name,'--build-info'])
    call(name+'-reject',[a.qemu,'-cpu','24KEc','-strace',bundle/name,'--execute-fixed-write'],78)
    trace=(tests/(name+'-reject.stderr')).read_text()
    assert not any(x in trace for x in ['pivot_root(','mount(','ioctl(','pwrite64(','socket('])
call('streaming-image-qemu',[a.qemu,'-cpu','24KEc',bundle/'stock-preflight.mipsel','--check-image',bundle/'image.bin'])
call('snapshot-image-qemu',[a.qemu,'-cpu','24KEc',bundle/'snapshot.mipsel','--offline-image-only','--image',bundle/'image.bin'])
print('PASS offline source, memory NOR, IPC, sanitizers and MIPS checks. No router accessed.',flush=True)
