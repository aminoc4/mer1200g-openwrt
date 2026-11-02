# SPDX-License-Identifier: MIT
"""Receive-only progress verifier. Never commands, retries or reboots a device."""
import copy,datetime,json,re,socket,threading,time

def parse(raw):
    if not 1<=len(raw)<=1400:raise ValueError('packet size')
    parts=raw.decode('ascii').split()
    if not parts or parts[0]!='MER1':raise ValueError('marker')
    fields={}
    for part in parts[1:]:
        key,value=part.split('=',1)
        if not re.fullmatch('[a-z][a-z0-9_]*',key) or key in fields or not value:raise ValueError('field')
        fields[key]=value
    return fields

def final_errors(packet,manifest,mode):
    writing=mode=='write';image=manifest['files']['image.bin']['sha256'];boot=manifest['protected']['MER_BOOT_SHA']
    exact={'nonce':manifest['nonce'][mode],'image':image,'profile':'stock-ssh-'+mode+'-v2','phase':'50','at':'50','code':'0','checks':'001fffff' if writing else '0000ffff','preflight':'1','snapshot':'1','jffs':'1','alloc':'30408704'}
    if writing:
        exact.update({'pending_mut':'0','pending_close':'0','erase_ok':'207','block_ok':'207','program_bytes':'13565952','core_error':'0','guard_error':'0','target_error':'0','close_error':'0','target_valid':'1','target_equal':'1','guard_valid':'1','closed':'1','base_boot':boot,'after_boot':boot,'base_desired':image,'readback':image})
    else:exact.update({'boot':boot,'desired':image})
    errors=[key for key,value in exact.items() if packet.get(key)!=value]
    if writing:
        for before,after in [('base_rest','after_rest'),('base_metadata','after_metadata')]:
            if not re.fullmatch('[0-9a-f]{64}',packet.get(before,'')) or packet.get(before)!=packet.get(after):errors.append(after)
        if not packet.get('programs','').isdigit() or int(packet['programs'])<52992:errors.append('programs')
    else:
        for key in ['rest','metadata']:
            if not re.fullmatch('[0-9a-f]{64}',packet.get(key,'')):errors.append(key)
    return errors

class Collector:
    def __init__(self,manifest,mode,output):
        self.manifest,self.mode,self.output=manifest,mode,output
        self.state={'packets':0,'accepted':0,'last_seq':0,'gaps':0,'reordered':0,'finals':0,'complete':False}
        self.lock=threading.Lock()
        self.error=None
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,8*1024*1024)
            self.sock.bind(('192.168.1.2',39001));self.sock.settimeout(.2)
        except BaseException:self.sock.close();raise
    def accept(self,raw,peer):
        with self.lock:return self._accept(raw,peer)
    def snapshot(self):
        with self.lock:return copy.deepcopy(self.state)
    def _accept(self,raw,peer):
        packet=parse(raw)
        if peer!='192.168.1.1' or packet.get('nonce')!=self.manifest['nonce'][self.mode] or packet.get('image')!=self.manifest['files']['image.bin']['sha256'] or packet.get('profile')!='stock-ssh-'+self.mode+'-v2':return False
        seq=int(packet['seq']);assert 1<=seq<2**32
        assert re.fullmatch('-?[0-9]+',packet.get('code',''))
        self.state['accepted']+=1
        if seq>self.state['last_seq']+1:self.state['gaps']+=seq-self.state['last_seq']-1
        if seq<=self.state['last_seq']:self.state['reordered']+=1
        self.state['last_seq']=max(seq,self.state['last_seq']);self.state['latest']=packet
        invalid_final=packet.get('phase')=='50' and final_errors(packet,self.manifest,self.mode)
        if packet['code']!='0' or invalid_final:
            self.state['failure_latched']=True;self.state['finals']=0
        elif packet.get('phase')=='50':self.state['finals']+=1
        else:self.state['finals']=0
        self.state['complete']=not self.state.get('failure_latched',False) and self.state['finals']>=3 and self.state['gaps']==0 and self.state['reordered']==0
        return True
    def run(self,stop):
        try:
            with (self.output/'packets.jsonl').open('x',encoding='utf8',buffering=1024*1024) as log:
                flushed=time.monotonic()
                while not stop.is_set():
                    try:raw,peer=self.sock.recvfrom(2048)
                    except socket.timeout:raw=None
                    if raw is not None:
                        self.state['packets']+=1
                        try:
                            if self.accept(raw,peer[0]):log.write(json.dumps({'time':datetime.datetime.now(datetime.timezone.utc).isoformat(),'raw':raw.decode('ascii')},separators=(',',':'))+'\n')
                        except (ValueError,UnicodeError,KeyError,AssertionError):pass
                    if time.monotonic()-flushed>=1:
                        log.flush();(self.output/'progress.json').write_text(json.dumps(self.snapshot(),indent=2));flushed=time.monotonic()
        except BaseException as error:self.error=str(error)
        finally:
            self.sock.close();(self.output/'progress.json').write_text(json.dumps(self.snapshot(),indent=2))
