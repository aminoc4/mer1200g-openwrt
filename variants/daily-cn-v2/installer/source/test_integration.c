/* Same flow/core/trace/status as the review ELF; memory NOR, mock UDP only. */
#define _GNU_SOURCE 1
#include "writer_flow.h"
#include "status.h"
#include "stage_policy.h"
#include "sha256.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
enum fault {OK,DENY_ERASE,RETURN_NET,INFLIGHT_HOLD,ERASE_INTERRUPT,PROGRAM_PARTIAL_ERROR,GUARD_ERROR,CLOSE_ERROR,SHORT_PROGRAM,GUARD_READ_ERROR,INFLIGHT_IO_ERROR,BASELINE_NET,READY_NET};
struct shared {unsigned erases,programs,closes,inflight,reads_after_close,max_packet,reports;int result,core_error,guard_error,target_error,target_valid,target_equal,guard_valid,close_error;unsigned char data[MER_FLASH];};
static struct shared *shared;static enum fault fault;static char last[1400];static int failed_net;
int mer_test_socket(int a,int b,int c){assert(a==AF_INET && (b&SOCK_DGRAM) && !c);return 200;}
int mer_test_net_close(int fd){assert(fd==200);return 0;}
ssize_t mer_test_sendto(int fd,const void *p,size_t n,int flags,const struct sockaddr *addr,socklen_t bytes){
 const struct sockaddr_in *a=(const void *)addr;assert(fd==200 && bytes==sizeof(*a) && ntohl(a->sin_addr.s_addr)==0xc0a80102u && ntohs(a->sin_port)==39001 && (flags&MSG_DONTWAIT));
 assert(n<sizeof(last));memcpy(last,p,n);last[n]=0;if(n>shared->max_packet)shared->max_packet=(unsigned)n;
 if(strstr(last,"phase=89 "))shared->reports++;
 if(strstr(last,"phase=50 "))printf("FINAL_PACKET %s",last);
 if(!failed_net && ((fault==DENY_ERASE && strstr(last,"phase=83 ") && strstr(last,"op=1 ")) || (fault==RETURN_NET && strstr(last,"phase=84 ") && strstr(last,"op=2 ")) || (fault==BASELINE_NET && strstr(last,"phase=81 ")) || (fault==READY_NET && strstr(last,"phase=82 ")))){failed_net=1;errno=ENETUNREACH;return -1;}
 return (ssize_t)n;
}
struct fake {struct mer_traced_io trace;struct mer_writer_flow flow;int opened;};
static ssize_t read_memory(void *ctx,void *p,size_t n,uint64_t at){(void)ctx;assert(at<=MER_FLASH && n<=MER_FLASH-at);if(shared->closes){shared->reads_after_close++;if(fault==GUARD_READ_ERROR && at>=MER_END){errno=EIO;return -1;}}memcpy(p,shared->data+(size_t)at,n);return (ssize_t)n;}
static int geometry(void *ctx,struct mer_geometry *g){(void)ctx;*g=(struct mer_geometry){MER_FLASH,MER_ERASE,1,0,3};return 0;}
static int begin(void *ctx){struct fake *f=ctx;if(mer_status_stage(82,*f->flow.checks))return -1;f->opened=1;return 0;}
static int raw_erase(void *ctx,uint64_t at,size_t n){(void)ctx;assert(!mer_write_range(at,n,1));shared->erases++;shared->inflight=1;if(fault==INFLIGHT_HOLD || fault==INFLIGHT_IO_ERROR)usleep(300000);memset(shared->data+(size_t)at,255,n);shared->inflight=0;if(fault==ERASE_INTERRUPT || fault==INFLIGHT_IO_ERROR){errno=EINTR;return -1;}return 0;}
static ssize_t raw_program(void *ctx,const void *p,size_t n,uint64_t at){(void)ctx;assert(!mer_write_range(at,n,0));shared->programs++;if(fault==SHORT_PROGRAM && n>63)n=63;if(fault==PROGRAM_PARTIAL_ERROR){memcpy(shared->data+(size_t)at,p,7);errno=ENOSPC;return -1;}memcpy(shared->data+(size_t)at,p,n);return (ssize_t)n;}
static int raw_close(void *ctx){struct fake *f=ctx;assert(f->opened);f->opened=0;shared->closes++;if(fault==GUARD_ERROR)shared->data[MER_METADATA+5]^=1;if(fault==CLOSE_ERROR){errno=EIO;return -1;}return 0;}
static int erase(void *ctx,uint64_t at,size_t n){return mer_traced_erase(&((struct fake *)ctx)->trace,at,n);}
static ssize_t program(void *ctx,const void *p,size_t n,uint64_t at){return mer_traced_program(&((struct fake *)ctx)->trace,p,n,at);}
static int end(void *ctx){struct fake *f=ctx;int result=mer_traced_close(&f->trace),saved=errno;struct mer_writer_payload p={0};int stop=0;(void)mer_writer_notify(&f->flow,MW_AUDIT_BEGIN,&p,&stop);errno=saved;return result;}
static int gate(void *ctx,enum mer_event event,uint64_t at){return mer_writer_gate(&((struct fake *)ctx)->flow,event,at);}
static unsigned char *load(const char *path,size_t n){FILE *f=fopen(path,"rb");assert(f);unsigned char *p=malloc(n);assert(p && fread(p,1,n,f)==n && fgetc(f)==EOF && !fclose(f));return p;}
static void run(enum fault mode,const unsigned char *original,const unsigned char *image){
 struct timespec start,finish;assert(!clock_gettime(CLOCK_MONOTONIC,&start));
 fault=mode;failed_net=0;memset(shared,0,sizeof(*shared));memcpy(shared->data,original,MER_FLASH);
 assert(!mer_status_open(0));assert(!mer_status_stage(5,0));assert(!mer_status_stage(68,0x3fff));
 struct mer_snapshot_report snap;strcpy(snap.boot,MER_SNAPSHOT_BOOT);mer_sha256(original+MER_END,MER_FLASH-MER_END,snap.rest);mer_sha256(original+MER_METADATA,MER_FLASH-MER_METADATA,snap.metadata);strcpy(snap.desired,MER_SNAPSHOT_DESIRED);
 assert(!mer_status_snapshot(&snap,0x7fff));assert(!mer_status_stage(71,0xffff));assert(!mer_status_setup_pipes());
 pid_t child=fork();assert(child>=0);
 if(!child){
  assert(!mer_status_after_fork(1));unsigned checks=0xffff;struct mer_report r={0};struct fake f={0};
  f.flow=(struct mer_writer_flow){&r,&f.trace,&checks,0};f.trace=(struct mer_traced_io){.raw_ctx=&f,.notify_ctx=&f.flow,.erase=raw_erase,.program=raw_program,.close_rw=raw_close,.notify=mer_writer_notify};
  struct mer_workspace w={malloc(MER_FLASH),malloc(MER_REGION),malloc(MER_ERASE),MER_FLASH,MER_REGION,MER_ERASE,0};assert(w.snapshot && w.desired && w.block);memcpy(w.desired,image,MER_IMAGE);
  struct mer_backend b={&f,read_memory,geometry,begin,erase,program,gate,end};int result=mer_writer_transaction(&b,&w,&r,&f.flow);
  shared->result=result;shared->core_error=r.error;shared->guard_error=r.guard_error;shared->target_error=r.target_error;shared->target_valid=r.target_valid;shared->target_equal=r.target_equal;shared->guard_valid=r.guard_after_valid;shared->close_error=r.close_error;
  if(!result){checks|=MER_WR_REFS_BIT;assert(!mer_status_stage(90,checks));assert(!mer_status_stage(50,checks));}
  free(w.snapshot);free(w.desired);free(w.block);mer_status_close();_exit(0);
 }
 assert(!mer_status_after_fork(0));int hold=0,status=0;unsigned spins=0;
 for(;;){
  if((mode==INFLIGHT_HOLD || mode==INFLIGHT_IO_ERROR) && shared->inflight && !hold){hold=ETIMEDOUT;mer_status_note_error(hold);}
  int result=mer_status_pump(!hold);if(result<0 && !hold)hold=errno;
  pid_t done=waitpid(child,&status,WNOHANG);assert(done>=0);if(done==child)break;
  (void)mer_status_wait_events(100);assert(++spins<1000000);
 }
 assert(WIFEXITED(status) && !WEXITSTATUS(status));assert(shared->max_packet<=1400 && shared->reports==1);
 if(mode==OK || mode==SHORT_PROGRAM){assert(!hold && !shared->result && shared->erases==207 && shared->closes==1 && shared->programs==(mode==OK?52992u:264960u));assert(shared->target_valid && shared->target_equal && shared->guard_valid && !shared->guard_error);assert(!memcmp(shared->data+MER_START,image,MER_IMAGE));assert(strstr(last,"phase=50 at=50 code=0 checks=001fffff") && strstr(last,"pending_mut=0 pending_close=0") && strstr(last,"program_bytes=13565952"));}
 else {assert(shared->result<0 && shared->core_error);assert(!mer_status_hold_repeat(hold));assert(strstr(last,"phase=61 "));
  if(mode==DENY_ERASE)assert(!shared->erases && !shared->programs && shared->core_error==ENETUNREACH && strstr(last,"pending_mut=1"));
  if(mode==RETURN_NET)assert(shared->erases==1 && shared->programs==1 && shared->core_error==ENETUNREACH && strstr(last,"pending_mut=0"));
  if(mode==INFLIGHT_HOLD)assert(shared->erases==1 && !shared->programs && shared->core_error==ETIMEDOUT && strstr(last,"pending_mut=0"));
  if(mode==ERASE_INTERRUPT)assert(shared->erases==1 && !shared->programs && shared->core_error==EINTR);
  if(mode==INFLIGHT_IO_ERROR){char io[40],first[40];snprintf(io,sizeof(io),"io_error=%d",EINTR);snprintf(first,sizeof(first),"code=%d",ETIMEDOUT);assert(shared->erases==1 && !shared->programs && shared->core_error==EINTR && strstr(last,io) && strstr(last,first) && strstr(last,"pending_mut=0"));}
  if(mode==PROGRAM_PARTIAL_ERROR)assert(shared->erases==1 && shared->programs==1 && shared->core_error==ENOSPC);
  if(mode==GUARD_ERROR)assert(shared->target_equal && shared->guard_valid && shared->guard_error==EIO);
  if(mode==GUARD_READ_ERROR)assert(shared->target_equal && !shared->guard_valid && shared->guard_error==EIO && strstr(last,"after_rest=-"));
  if(mode==CLOSE_ERROR)assert(shared->close_error==EIO && shared->target_equal);
  if(mode==BASELINE_NET || mode==READY_NET)assert(!shared->erases && !shared->programs && !shared->closes && shared->core_error==ENETUNREACH);
  else assert(shared->closes==1 && shared->reads_after_close);
  assert(shared->target_valid);
 }
 assert(!memcmp(shared->data,original,MER_START));if(mode!=GUARD_ERROR)assert(!memcmp(shared->data+MER_END,original+MER_END,MER_FLASH-MER_END));
 assert(!clock_gettime(CLOCK_MONOTONIC,&finish));
 printf("PASS integration fault=%u raw_erases=%u raw_programs=%u closes=%u core=%d guard=%d target=%d close=%d max_udp=%u elapsed=%.6f parent_wait=poll-pipe-immediate\n",mode,shared->erases,shared->programs,shared->closes,shared->core_error,shared->guard_error,shared->target_error,shared->close_error,shared->max_packet,(double)(finish.tv_sec-start.tv_sec)+(double)(finish.tv_nsec-start.tv_nsec)/1e9);fflush(stdout);
 mer_status_close();
}
int main(int argc,char **argv){assert(argc==3 || argc==4);shared=mmap(NULL,sizeof(*shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(shared!=MAP_FAILED);unsigned char *original=load(argv[1],MER_FLASH),*image=load(argv[2],MER_IMAGE);for(unsigned i=0;i<=(argc==4?0u:READY_NET);i++)run((enum fault)i,original,image);free(original);free(image);assert(!munmap(shared,sizeof(*shared)));puts("PASS real fork/atomic pipe ACK, mock fixed UDP, actual same-C flow/trace/core; memory NOR only");return 0;}
