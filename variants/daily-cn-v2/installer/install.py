#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stock SSH controller: inspect, stage, rehearse, flash. Never auto-reboots."""
from pathlib import Path
import argparse,base64,datetime,getpass,hashlib,json,os,re,secrets,socket,struct,subprocess,threading,time
from collector import Collector

if not __debug__:
    raise RuntimeError('Run Python without -O: installer validation requires assertions')

FILES={'stock-preflight.mipsel':'/tmp/mer_stock_preflight_v2','snapshot.mipsel':'/tmp/mer_snapshot_v2','preflight.mipsel':'/tmp/mer_stock_preflight_v2.supervisor','writer.mipsel':'/tmp/mer_stock_writer_v2','image.bin':'/tmp/mer1200g-v2-window.bin'}
LAYOUT=[(0x40000,'bootloader'),(0x150000,'kernel'),(0xba0000,'rootfs'),(0x10000,'device-info'),(0x10000,'firmware-info'),(0x10000,'tddp'),(0x20000,'log'),(0x200000,'rootfs_data'),(0x10000,'art'),(0x1000000,'firmware')]
def sha(data):return hashlib.sha256(data).hexdigest()
def activation_rejected(channel):
    if channel.exit_status_ready():
        code=channel.recv_exit_status()
        # -1 is an uncertain SSH disconnect during PID1 handover, never success.
        if code>0:raise RuntimeError('Original ubus activation returned exit '+str(code)+'; no retry issued')
def windows_index():
    if os.name!='nt':return None
    text=subprocess.check_output(['powershell.exe','-NoProfile','-Command','Get-NetIPAddress -AddressFamily IPv4 -IPAddress 192.168.1.2 | Select-Object -ExpandProperty InterfaceIndex']).decode().strip()
    assert text.isdigit(),'192.168.1.2 must identify one router adapter'
    return int(text)
def connect(bundle):
    import paramiko
    client=paramiko.SSHClient();known=bundle/'known_hosts'
    if known.exists():client.load_host_keys(str(known))
    class FirstConnection(paramiko.MissingHostKeyPolicy):
        def missing_host_key(self,ssh,hostname,key):
            fingerprint=base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
            print('Stock SSH host fingerprint: SHA256:'+fingerprint)
            if input('Trust this directly connected router? Type yes: ').strip()!='yes':raise RuntimeError('Host key not accepted')
            ssh.get_host_keys().add(hostname,key.get_name(),key);ssh.save_host_keys(str(known))
    client.set_missing_host_key_policy(FirstConnection())
    password=getpass.getpass('Stock root SSH password: ')
    sock=socket.socket();sock.settimeout(15);index=windows_index()
    if index is not None:sock.setsockopt(socket.IPPROTO_IP,31,struct.pack('!I',index))
    try:
        sock.bind(('192.168.1.2',0));sock.connect(('192.168.1.1',33400))
        client.connect('192.168.1.1',port=33400,username='root',password=password,sock=sock,allow_agent=False,look_for_keys=False,timeout=15,banner_timeout=15,auth_timeout=20)
    except BaseException:sock.close();client.close();raise
    finally:password=None
    return client
def command(client,text,timeout=120):
    _,out,err=client.exec_command(text,timeout=timeout)
    data=out.read();error=err.read();code=out.channel.recv_exit_status()
    if code:raise RuntimeError('Router command failed: '+text+'; '+error.decode('utf8','replace')[:300])
    return data
def upload(client,path,data,mode):
    temporary='/tmp/mer-v2-upload-'+secrets.token_hex(16)
    inp,out,err=client.exec_command('umask 077; set -C; cat > '+temporary,timeout=120)
    inp.channel.sendall(data);inp.channel.shutdown_write()
    assert not out.read() and not err.read() and out.channel.recv_exit_status()==0
    assert sha(command(client,'cat '+temporary))==sha(data),'Upload readback differs'
    command(client,'chmod '+mode+' '+temporary+' && test ! -d '+path+' && mv -f '+temporary+' '+path)
def inspect(client,manifest):
    text=command(client,'cat /proc/mtd').decode();rows=re.findall(r'^mtd(\d+): ([0-9a-f]+) ([0-9a-f]+) "([^"]+)"$',text,re.M)
    assert len(rows)==10
    for number,(index,size,erase,name) in enumerate(rows):assert int(index)==number and (int(size,16),name)==LAYOUT[number] and int(erase,16)==65536,'Not the matching original stock layout'
    release=command(client,'uname -r').decode().strip();assert release.startswith('3.10.')
    boot=command(client,'cat /proc/sys/kernel/random/boot_id').decode().strip()
    assert re.fullmatch('[0-9a-f-]{36}',boot)
    memory=command(client,'cat /proc/meminfo').decode()
    values={k:int(v) for k,v in re.findall(r'^(\w+):\s+(\d+) kB',memory,re.M)}
    assert values['MemTotal']>=120000,'128MiB stock environment required'
    available=values.get('MemAvailable',values['MemFree']+values.get('Cached',0)+values.get('Buffers',0)-values.get('Shmem',0))
    assert available>=64*1024,'Need at least 64MiB estimated available before staging'
    space=command(client,'df -k /tmp').decode().splitlines()[-1].split()
    assert int(space[3])>=18*1024,'Free /tmp backup copies after downloading them'
    raw=command(client,'cat /dev/mtd9',180);assert len(raw)==0x1000000
    assert sha(raw[0x40000:0xd30000])==manifest['stock_region_sha256'],'Original kernel/rootfs differ from the supplied backup'
    for key,start,end in [('MER_BOOT_SHA',0,0x40000),('MER_DEVICE_INFO_SHA',0xd30000,0xd40000),('MER_FIRMWARE_INFO_SHA',0xd40000,0xd50000),('MER_TDDP_SHA',0xd50000,0xd60000),('MER_ART_SHA',0xff0000,0x1000000)]:
        assert sha(raw[start:end])==manifest['protected'][key],'Protected identity differs from supplied backup'
    return {'boot_id':boot,'kernel':release,'estimated_available_kib':available,'stock_and_protected_ranges_match':True}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['inspect','stage','rehearse','flash']);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--seconds',type=int,default=10800);a=p.parse_args()
    assert 30<=a.seconds<=14400
    bundle=a.bundle.resolve();manifest_bytes=(bundle/'manifest.json').read_bytes();manifest=json.loads(manifest_bytes)
    assert manifest['format']=='mer1200g-stock-ssh-v2' and set(manifest['files'])==set(FILES)
    for name,entry in manifest['files'].items():
        data=(bundle/name).read_bytes();assert len(data)==entry['bytes'] and sha(data)==entry['sha256'],name
    assert manifest['files']['image.bin']['bytes']==0xcf0000
    output=bundle/('run-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'));output.mkdir(mode=0o700)
    client=connect(bundle);collector=None;stop=threading.Event();thread=None
    result={'action':a.action,'manifest_sha256':sha(manifest_bytes),'activated':False,'automatic_reboot':False,'completed':False}
    try:
        result['inspection']=inspect(client,manifest)
        result['host_key_sha256']=sha(client.get_transport().get_remote_server_key().asbytes())
        print('PASS original stock layout, 128MiB memory budget, backup identity and protected regions.',flush=True)
        if a.action=='inspect':result['completed']=True;return
        if a.action=='flash':
            rehearsal=json.loads((bundle/'rehearsal.json').read_text())
            assert rehearsal['completed'] and rehearsal['manifest_sha256']==result['manifest_sha256']
            assert rehearsal['host_key_sha256']==result['host_key_sha256']
            assert rehearsal['inspection']['boot_id']!=result['inspection']['boot_id'],'Power-cycle back to stock after rehearsal first'
        command(client,'test ! -e /tmp/upgraded && test ! -e /tmp/mer-stock-preflight-v2 && test ! -e /tmp/mer-stock-write-v2 && test ! -e /tmp/mer-stock-preflight-v2.contract && test ! -e /tmp/mer-stock-write-v2.contract')
        for name,path in FILES.items():upload(client,path,(bundle/name).read_bytes(),'400' if name=='image.bin' else '500')
        command(client,'/tmp/mer_stock_preflight_v2 --image /tmp/mer1200g-v2-window.bin --mtd /dev/mtd9',120)
        print('PASS upload readback and streaming stock/image preflight.',flush=True)
        if a.action=='stage':result['completed']=True;return
        mode='write' if a.action=='flash' else 'preflight'
        collector=Collector(manifest,mode,output);thread=threading.Thread(target=collector.run,args=(stop,),daemon=True);thread.start()
        deadline=time.monotonic()+5
        while not (output/'packets.jsonl').exists() and time.monotonic()<deadline:
            assert thread.is_alive() and not collector.error;time.sleep(.05)
        assert (output/'packets.jsonl').exists() and thread.is_alive() and not collector.error
        executable='writer.mipsel' if mode=='write' else 'preflight.mipsel'
        contract='/tmp/mer-stock-'+mode+'-v2.contract'
        marker='MER_STOCK_WRITE_V2' if mode=='write' else 'MER_STOCK_PREFLIGHT_V2'
        ack='\nack=INSTALL_MER1200G_V2' if mode=='write' else ''
        text=(marker+'\nnonce='+manifest['nonce'][mode]+ack+'\nhelper_sha256='+manifest['files'][executable]['sha256']+'\n').encode()
        command(client,'cp '+FILES[executable]+' /tmp/upgraded && chmod 500 /tmp/upgraded')
        assert sha(command(client,'cat /tmp/upgraded'))==manifest['files'][executable]['sha256']
        upload(client,contract,text,'444')
        assert thread.is_alive() and not collector.error
        result['activated']=True;(output/'result.json').write_text(json.dumps(result,indent=2))
        # One activation only. Loss of SSH acknowledgement never causes retry.
        channel=client.get_transport().open_session(timeout=15)
        try:channel.exec_command('ubus call system nandupgrade '+"'{\"path\":\""+contract+"\"}'")
        except Exception:result['ssh_acknowledgement_lost']=True
        print('RAM handover activated. Progress directory: '+str(output),flush=True)
        deadline=time.monotonic()+a.seconds;previous=None
        while time.monotonic()<deadline:
            assert thread.is_alive() and not collector.error,'Progress receiver stopped; reboot state is not inferred'
            activation_rejected(channel)
            snapshot=collector.snapshot();packet=snapshot.get('latest',{});progress=(packet.get('phase'),packet.get('block_ok'),packet.get('code'))
            if progress!=previous:print(json.dumps({'phase':progress[0],'verified_blocks':progress[1],'code':progress[2],'gaps':snapshot['gaps']}),flush=True);previous=progress
            if snapshot.get('failure_latched'):raise RuntimeError('Installer reported an error or invalid final record; no reboot/retry issued')
            if packet.get('phase') in ['60','61']:raise RuntimeError('Installer entered HOLD; inspect progress without repeating activation')
            if snapshot['complete']:
                stop.set();thread.join(3)
                final=collector.snapshot()
                assert not thread.is_alive() and not collector.error and final['complete']
                result['completed']=True;result['progress']=final
                if mode=='preflight':(bundle/'rehearsal.json').write_text(json.dumps(result,indent=2))
                print(('PASS read-only snapshot and RAM handover.' if mode=='preflight' else 'PASS final readback and preserved regions.')+' Power-cycle the router to '+('return to original stock.' if mode=='preflight' else 'boot OpenWrt.'),flush=True)
                break
            time.sleep(.1)
        assert result['completed'],'Observation deadline reached; no reboot or retry issued'
    finally:
        stop.set()
        if thread:thread.join(3)
        if collector:result['progress']=collector.snapshot()
        (output/'result.json').write_text(json.dumps(result,indent=2));client.close()
if __name__=='__main__':main()
