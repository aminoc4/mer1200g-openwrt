/* Fixed-target, transmit-only UDP. No receive, DNS, shell or remote-command API. */
#define _GNU_SOURCE 1
#include "status.h"
#include "bundle_nonce.h"
#include "stage_policy.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#if MER_PROFILE_WRITE
#include "writer_ledger.h"
#endif
#ifdef MER_STATUS_TEST
int mer_test_socket(int,int,int);
ssize_t mer_test_sendto(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
int mer_test_net_close(int);
#define NET_SOCKET mer_test_socket
#define NET_SENDTO mer_test_sendto
#define NET_CLOSE mer_test_net_close
#else
#define NET_SOCKET socket
#define NET_SENDTO sendto
#define NET_CLOSE close
#endif
static struct {
 int fd,events[2],acks[2],child;
 unsigned sequence,timer1,checks,last_phase,preflight;
 int last_error;
 unsigned snapshot;
 struct mer_snapshot_report report;
 struct sockaddr_in destination;
#if MER_PROFILE_WRITE
 struct mer_writer_ledger ledger;
 struct timespec pending_since;
#endif
} channel={.fd=-1,.events={-1,-1},.acks={-1,-1},.last_phase=MER_PH_READY,.timer1=UINT_MAX};
static int failed(int error){errno=error;return -1;}
static int valid_phase(unsigned p){
 switch(p){
 case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 10:case 11:case 20:case 21:case 22:case 23:case 24:case 25:case 26:case 27:case 28:case 29:
 case 30:case 31:case 32:case 33:case 34:case 35:case 36:case 37:case 38:case 39:case 40:case 41:
 case 50:case 60:case 61:case 62:case 63:case 64:case 65:case 66:case 67:case 68:case 69:case 70:case 71:case 72:case 80:case 81:case 82:case 83:case 84:case 85:case 86:case 87:case 88:case 89:case 90:case 91:case 93:return 1;default:return 0;
 }
}
int mer_status_open(unsigned timer1_control){
 if(channel.fd>=0)return failed(EALREADY);
 struct sigaction ignore;memset(&ignore,0,sizeof(ignore));ignore.sa_handler=SIG_IGN;sigemptyset(&ignore.sa_mask);
 if(sigaction(SIGPIPE,&ignore,NULL))return -1;
 memset(&channel.destination,0,sizeof(channel.destination));channel.destination.sin_family=AF_INET;
 channel.destination.sin_addr.s_addr=htonl(0xc0a80102u);channel.destination.sin_port=htons(39001);
 channel.timer1=timer1_control;channel.sequence=0;channel.last_phase=MER_PH_READY;channel.last_error=0;channel.checks=0;channel.child=0;channel.preflight=0;
 channel.snapshot=0;memset(&channel.report,0,sizeof(channel.report));
#if MER_PROFILE_WRITE
 memset(&channel.ledger,0,sizeof(channel.ledger));
 memset(&channel.pending_since,0,sizeof(channel.pending_since));
#endif
 channel.fd=NET_SOCKET(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
 return channel.fd<0?-1:0;
}
int mer_status_emit(unsigned phase,int error,unsigned checks){
 if(channel.fd<0 || !valid_phase(phase) || channel.sequence==UINT_MAX)return failed(EINVAL);
 char packet[1400];unsigned seq=++channel.sequence;
 int compact=MER_PROFILE_WRITE && (phase==81 || phase==89 || phase==50 || phase==60 || phase==61);
 int length=snprintf(packet,sizeof(packet),"MER1 nonce=%s image=%s seq=%u phase=%u at=%u code=%d checks=%08x timer1=%08x preflight=%u snapshot=%u jffs=%u alloc=%u profile=%s boot=%s rest=%s metadata=%s desired=%s\n",MER_BUNDLE_NONCE,MER_BUNDLE_IMAGE_SHA,seq,phase,channel.last_phase,error,checks,channel.timer1,channel.preflight,channel.snapshot,!!(checks&0x2000u),channel.snapshot?MER_SNAPSHOT_ALLOC:0u,MER_PROFILE_NAME,(channel.snapshot && !compact)?channel.report.boot:"-",(channel.snapshot && !compact)?channel.report.rest:"-",(channel.snapshot && !compact)?channel.report.metadata:"-",(channel.snapshot && !compact)?channel.report.desired:"-");
 if(length<0 || (size_t)length>=sizeof(packet))return failed(EOVERFLOW);
#if MER_PROFILE_WRITE
 const struct mer_writer_ledger *l=&channel.ledger;const struct mer_writer_payload *p=&l->last_io,*r=&l->report;
 if(length>0 && packet[length-1]=='\n')length--;
 int extra=snprintf(packet+length,sizeof(packet)-(size_t)length," pending_mut=%u pending_close=%u op=%u opid=%u off=%08x len=%u io_ret=%d io_error=%d erase_ok=%u block_ok=%u programs=%u program_bytes=%u core_error=%d guard_error=%d target_error=%d close_error=%d target_valid=%u target_equal=%u guard_valid=%u closed=%u",l->pending,l->close_pending,p->op,p->id,p->offset,p->bytes,p->result,p->io_error,l->erase_ok,l->verified,l->program_calls,l->program_bytes,r->core_error,r->guard_error,r->target_error,r->close_error,r->target_valid,r->target_equal,r->guard_valid,l->closed);
 if(extra<0 || (size_t)extra>=sizeof(packet)-(size_t)length)return failed(EOVERFLOW);
 length+=extra;
 if(phase==81 || phase==89 || phase==50 || phase==60 || phase==61){
  extra=snprintf(packet+length,sizeof(packet)-(size_t)length," base_boot=%s base_rest=%s base_metadata=%s base_desired=%s after_boot=%s after_rest=%s after_metadata=%s readback=%s",l->baseline?l->before.boot:"-",l->baseline?l->before.rest:"-",l->baseline?l->before.metadata:"-",l->baseline?l->before.desired:"-",r->guard_valid?l->after.boot:"-",r->guard_valid?l->after.rest:"-",r->guard_valid?l->after.metadata:"-",r->target_valid?l->after.desired:"-");
  if(extra<0 || (size_t)extra>=sizeof(packet)-(size_t)length)return failed(EOVERFLOW);
  length+=extra;
 }
 if((size_t)length+1>=sizeof(packet))return failed(EOVERFLOW);
 packet[length++]='\n';
#endif

 unsigned interrupts=0;
 for(;;){
  ssize_t sent=NET_SENDTO(channel.fd,packet,(size_t)length,MSG_DONTWAIT|MSG_NOSIGNAL,(const struct sockaddr *)&channel.destination,sizeof(channel.destination));
  if(sent==length)return 0;
  if(sent<0 && errno==EINTR && ++interrupts<=32)continue;
  if(sent>=0)errno=EIO;
  return -1;
 }
}
int mer_status_setup_pipes(void){
 if(pipe2(channel.events,O_CLOEXEC|O_NONBLOCK))return -1;
 if(pipe2(channel.acks,O_CLOEXEC|O_NONBLOCK)){close(channel.events[0]);close(channel.events[1]);channel.events[0]=channel.events[1]=-1;return -1;}
 return 0;
}
int mer_status_after_fork(int child){
 channel.child=!!child;
 if(child){
  close(channel.events[0]);channel.events[0]=-1;close(channel.acks[1]);channel.acks[1]=-1;
  int flags=fcntl(channel.acks[0],F_GETFL);if(flags<0 || fcntl(channel.acks[0],F_SETFL,flags&~O_NONBLOCK))return -1;
 }else{close(channel.events[1]);channel.events[1]=-1;close(channel.acks[0]);channel.acks[0]=-1;}
 return 0;
}
static int submit(struct mer_status_event *event){
 unsigned interrupts=0;
 for(;;){
  ssize_t n=write(channel.events[1],event,sizeof(*event));
  if(n==(ssize_t)sizeof(*event))break;
  if(n<0 && errno==EINTR && ++interrupts<=32)continue;
  return failed(EIO);
 }
 struct mer_status_ack ack;interrupts=0;
 for(;;){
  ssize_t n=read(channel.acks[0],&ack,sizeof(ack));
  if(n==(ssize_t)sizeof(ack)){if(ack.error)channel.last_error=ack.error;return ack.permit?0:failed(ack.error?ack.error:ECANCELED);}
  if(n<0 && errno==EINTR && ++interrupts<=32)continue;
  return failed(EPIPE);
 }
}
int mer_status_stage(unsigned phase,unsigned checks){
 if(!valid_phase(phase) || phase==MER_PH_SNAPSHOT_RESULT || phase==MER_PH_WAIT)return failed(EINVAL);
 if(phase==MER_PH_FINAL && (channel.last_phase!=MER_FINAL_PHASE_BEFORE || checks!=MER_STAGE_FINAL_CHECKS || !channel.preflight || !channel.snapshot))return failed(EPROTO);
 if(phase==MER_PH_PREFLIGHT_DONE)channel.preflight=1;
 channel.last_phase=phase;channel.checks=checks;
 if(!channel.child)return mer_status_emit(phase,0,checks);
 struct mer_status_event event={.magic=0x4d535033u,.phase=phase,.checks=checks,.ack=1,.at=phase};
 return submit(&event);
}
int mer_status_snapshot(const struct mer_snapshot_report *report,unsigned checks){
 if(channel.last_phase!=MER_PH_SNAPSHOT_BEFORE || mer_snapshot_valid(report) || (checks&0x6000u)!=0x6000u)return failed(EPROTO);
 channel.snapshot=1;channel.report=*report;channel.last_phase=MER_PH_SNAPSHOT_RESULT;channel.checks=checks;
 if(!channel.child)return mer_status_emit(MER_PH_SNAPSHOT_RESULT,0,checks);
 struct mer_status_event event={.magic=0x4d535033u,.phase=MER_PH_SNAPSHOT_RESULT,.checks=checks,.ack=1,.at=MER_PH_SNAPSHOT_RESULT,.snapshot=1,.report=*report};
 return submit(&event);
}
unsigned mer_status_current_phase(void){return channel.last_phase;}
void mer_status_note_error(int reason){if(reason && !channel.last_error)channel.last_error=reason;}
int mer_status_wait_report(int reason){
 mer_status_note_error(reason);
 return mer_status_emit(channel.last_error?MER_PH_HOLD:MER_PH_WAIT,channel.last_error,channel.checks);
}
int mer_status_pump(int allow_advance){
 if(channel.events[0]<0)return 0;
 struct mer_status_event event;
 ssize_t n=read(channel.events[0],&event,sizeof(event));
 if(n<0 && (errno==EAGAIN || errno==EINTR))return 0;
 if(!n)return 0;
 if(n!=(ssize_t)sizeof(event) || event.magic!=0x4d535033u || !valid_phase(event.phase) || !valid_phase(event.at) || event.ack>1 || event.snapshot>1)return failed(EPROTO);

#if MER_PROFILE_WRITE
 if(event.kind){
  int observer=event.kind==MW_IO_RETURN || event.kind==MW_CLOSE_BEFORE || event.kind==MW_CLOSE_RETURN || event.kind==MW_AUDIT_BEGIN || event.kind==MW_REPORT;
  if(!observer && (!allow_advance || channel.last_error)){
   mer_status_note_error(ECANCELED);struct mer_status_ack ack={0,channel.last_error};
   if(write(channel.acks[1],&ack,sizeof(ack))!=(ssize_t)sizeof(ack))return failed(EPIPE);
   return failed(channel.last_error);
  }
  if(mer_writer_ledger_apply(&channel.ledger,event.kind,event.phase,&event.writer,&event.report))return -1;
  if(event.kind==MW_IO_BEFORE && clock_gettime(CLOCK_MONOTONIC,&channel.pending_since))return -1;
  channel.last_phase=event.at;channel.checks=event.checks;
  if(event.writer.io_error)mer_status_note_error(event.writer.io_error);
  if(event.writer.core_error)mer_status_note_error(event.writer.core_error);
  int sent=mer_status_emit(event.phase,channel.last_error,event.checks),saved=errno;
  if(sent)mer_status_note_error(saved?saved:EIO);
  int cleanup=event.kind==MW_CLOSE_BEFORE || event.kind==MW_CLOSE_RETURN || event.kind==MW_AUDIT_BEGIN || event.kind==MW_REPORT;
  struct mer_status_ack ack={cleanup || (!channel.last_error && allow_advance),channel.last_error};
  if(write(channel.acks[1],&ack,sizeof(ack))!=(ssize_t)sizeof(ack))return failed(EPIPE);
  if(channel.last_error)return failed(channel.last_error);
  return 1;
 }
 if(event.phase==MER_PH_FINAL && mer_writer_ledger_final(&channel.ledger,1))return -1;
#endif
 if(!allow_advance && event.ack){
  if(!channel.last_error)channel.last_error=ECANCELED;
  int sent=mer_status_emit(MER_PH_HOLD,channel.last_error,channel.checks);
  struct mer_status_ack ack={0,channel.last_error};if(write(channel.acks[1],&ack,sizeof(ack))!=(ssize_t)sizeof(ack))return failed(EPIPE);
  return sent?-1:failed(ECANCELED);
 }
 if(event.phase==MER_PH_SNAPSHOT_RESULT && !event.snapshot)return failed(EPROTO);
 if(event.snapshot && (channel.last_phase!=MER_PH_SNAPSHOT_BEFORE || event.phase!=MER_PH_SNAPSHOT_RESULT || (event.checks&0x6000u)!=0x6000u || mer_snapshot_valid(&event.report)))return failed(EPROTO);
 if(event.phase==MER_PH_FINAL && (channel.last_phase!=MER_FINAL_PHASE_BEFORE || event.checks!=MER_STAGE_FINAL_CHECKS || !channel.preflight || !channel.snapshot))return failed(EPROTO);
 if(event.snapshot){channel.snapshot=1;channel.report=event.report;}
 channel.last_phase=event.at;if(event.error && !channel.last_error)channel.last_error=event.error;channel.checks=event.checks;
 int sent=mer_status_emit(event.phase,channel.last_error,event.checks);int saved=errno;
 if(sent && !channel.last_error)channel.last_error=saved?saved:EIO;
 if(event.ack){
  struct mer_status_ack ack={!(sent || !allow_advance || channel.last_error),channel.last_error};
  if(write(channel.acks[1],&ack,sizeof(ack))!=(ssize_t)sizeof(ack))return failed(EPIPE);
 }
 if(sent){if(!channel.last_error)channel.last_error=saved;errno=channel.last_error;return -1;}
 if(channel.last_error && event.ack)return failed(channel.last_error);
 if(!allow_advance && event.ack)return failed(ECANCELED);
 if(event.phase==MER_PH_FAILURE)return failed(event.error?event.error:EIO);
 return 1;
}
void mer_status_child_failure(int error){
 if(error<=0)error=EIO;
 channel.last_error=error;
 if(channel.child && channel.events[1]>=0){
  struct mer_status_event event={.magic=0x4d535033u,.phase=MER_PH_FAILURE,.error=error,.checks=channel.checks,.at=channel.last_phase};
  ssize_t ignored=write(channel.events[1],&event,sizeof(event));(void)ignored;
 }
}
int mer_status_hold_repeat(int reason){
 if(reason && !channel.last_error)channel.last_error=reason;
 if(channel.fd<0){
  if(channel.destination.sin_family!=AF_INET){
   channel.destination.sin_family=AF_INET;channel.destination.sin_addr.s_addr=htonl(0xc0a80102u);channel.destination.sin_port=htons(39001);
  }
  channel.fd=NET_SOCKET(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  if(channel.fd<0)return -1;
 }
 unsigned phase=(!channel.last_error && channel.last_phase==MER_PH_FINAL)?MER_PH_FINAL:MER_PH_HOLD;
 return mer_status_emit(phase,channel.last_error,channel.checks);
}
int mer_status_export_exec(int *fd,unsigned *sequence){
 if(channel.fd<3)return failed(EINVAL);
 int flags=fcntl(channel.fd,F_GETFD);if(flags<0 || fcntl(channel.fd,F_SETFD,flags&~FD_CLOEXEC))return -1;
 *fd=channel.fd;*sequence=channel.sequence;return 0;
}
int mer_status_import_exec(int fd,unsigned sequence,unsigned timer1_control){
#ifdef MER_STATUS_TEST
 (void)fd;(void)sequence;(void)timer1_control;return failed(ENOTSUP);
#else
 int type=0;socklen_t length=sizeof(type);
 if(fd<3 || sequence==UINT_MAX || getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&length) || type!=SOCK_DGRAM)return failed(EINVAL);
 int flags=fcntl(fd,F_GETFL);if(flags<0 || !(flags&O_NONBLOCK))return failed(EINVAL);
 flags=fcntl(fd,F_GETFD);if(flags<0 || fcntl(fd,F_SETFD,flags|FD_CLOEXEC))return -1;
 channel.fd=fd;channel.sequence=sequence;channel.timer1=timer1_control;channel.child=0;
 channel.last_phase=MER_PH_READY;channel.last_error=0;channel.checks=0;
 memset(&channel.destination,0,sizeof(channel.destination));channel.destination.sin_family=AF_INET;
 channel.destination.sin_addr.s_addr=htonl(0xc0a80102u);channel.destination.sin_port=htons(39001);return 0;
#endif
}
void mer_status_close(void){
 if(channel.fd>=0)NET_CLOSE(channel.fd);
 channel.fd=-1;
 for(unsigned i=0;i<2;i++){if(channel.events[i]>=0)close(channel.events[i]);if(channel.acks[i]>=0)close(channel.acks[i]);channel.events[i]=channel.acks[i]=-1;}
}

int mer_status_wait_events(unsigned milliseconds){
 if(channel.events[0]<0)return failed(EINVAL);
 struct pollfd p={channel.events[0],POLLIN,0};return poll(&p,1,(int)milliseconds);
}
int mer_status_operation_stalled(void){
#if MER_PROFILE_WRITE
 if(channel.ledger.pending){struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return -1;
  time_t budget=channel.ledger.last_io.op==MW_ERASE?600:60;
  if(now.tv_sec-channel.pending_since.tv_sec>=budget)return failed(ETIMEDOUT);
 }
#endif
 return 0;
}
#if MER_PROFILE_WRITE
int mer_status_writer_event(unsigned kind,unsigned phase,unsigned checks,const struct mer_writer_payload *payload,const struct mer_snapshot_report *hashes,int *stop){
 if(!channel.child || !payload || !stop || !valid_phase(phase))return failed(EINVAL);
 struct mer_status_event event={.magic=0x4d535033u,.phase=phase,.checks=checks,.ack=1,.at=phase,.kind=kind,.writer=*payload};
 if(hashes)event.report=*hashes;
 channel.last_phase=phase;channel.checks=checks;
 int result=submit(&event);*stop=channel.last_error;return result;
}
#endif
