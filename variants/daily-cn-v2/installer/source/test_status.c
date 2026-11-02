#define _GNU_SOURCE 1
#include "status.h"
#include "bundle_nonce.h"
#include "stage_policy.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
/* Only the network functions are replaced; real C protocol and local pipes run. */
static int mode,calls;static char last[1024];
static struct mer_snapshot_report report(void){struct mer_snapshot_report r;strcpy(r.boot,MER_SNAPSHOT_BOOT);memset(r.rest,'1',64);r.rest[64]=0;memset(r.metadata,'2',64);r.metadata[64]=0;strcpy(r.desired,MER_SNAPSHOT_DESIRED);return r;}
int mer_test_socket(int family,int type,int protocol){
 assert(family==AF_INET && (type&SOCK_DGRAM) && (type&SOCK_NONBLOCK) && (type&SOCK_CLOEXEC) && !protocol);
 if(mode==5){errno=EACCES;return -1;}return 200;
}
int mer_test_net_close(int fd){assert(fd==200);return 0;}
ssize_t mer_test_sendto(int fd,const void *p,size_t n,int flags,const struct sockaddr *dest,socklen_t size){
 assert(fd==200 && size==sizeof(struct sockaddr_in));const struct sockaddr_in *a=(const void *)dest;
 assert(a->sin_family==AF_INET && ntohl(a->sin_addr.s_addr)==0xc0a80102u && ntohs(a->sin_port)==39001);
 assert((flags&MSG_DONTWAIT) && (flags&MSG_NOSIGNAL));calls++;
 if(mode==1){errno=ENETUNREACH;return -1;}
 if(mode==2)return (ssize_t)n-1;
 if(mode==3 && calls==1){errno=EINTR;return -1;}
 if(mode==4){errno=EINTR;return -1;}
 assert(n<sizeof(last));memcpy(last,p,n);last[n]=0;return (ssize_t)n;
}
static void ipc_case(int fail){
 mode=fail?1:0;calls=0;assert(!mer_status_open(0));assert(!mer_status_setup_pipes());
 pid_t child=fork();assert(child>=0);
 if(!child){
  if(mer_status_after_fork(1))_exit(90);
  int result=mer_status_stage(MER_PH_QUIESCE_BEFORE,3);
  _exit(((result==0)==(!fail))?0:91);
 }
 assert(!mer_status_after_fork(0));int result=0;
 for(unsigned i=0;i<300;i++){result=mer_status_pump(1);if(result)break;usleep(1000);}
 assert(fail?result<0:result>0);int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 mer_status_close();
}
static void snapshot_ipc_case(int denied){
 mode=0;calls=0;assert(!mer_status_open(0));assert(!mer_status_stage(MER_PH_PREFLIGHT_DONE,0));assert(!mer_status_stage(MER_PH_SNAPSHOT_BEFORE,0x3fff));assert(!mer_status_setup_pipes());
 struct mer_snapshot_report r=report();pid_t child=fork();assert(child>=0);
 if(!child){if(mer_status_after_fork(1))_exit(90);int result=mer_status_snapshot(&r,0x7fff);_exit(((result==0)==(!denied))?0:91);}
 assert(!mer_status_after_fork(0));if(denied)mer_status_note_error(ETIMEDOUT);int result=0;
 for(unsigned i=0;i<300;i++){result=mer_status_pump(!denied);if(result)break;usleep(1000);}
 assert(denied?result<0:result>0);int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
 if(denied){char expected[96];snprintf(expected,sizeof(expected),"phase=61 at=68 code=%d checks=00003fff",ETIMEDOUT);assert(strstr(last,expected) && strstr(last,"snapshot=0") && strstr(last,"desired=-"));}
 else {assert(strstr(last,"phase=69 at=69 code=0 checks=00007fff") && strstr(last,"snapshot=1") && strstr(last,MER_SNAPSHOT_DESIRED));assert(!mer_status_stage(MER_PH_POST_SNAPSHOT_REFS,0xffff));assert(!mer_status_stage(MER_PH_FINAL,0xffff));assert(!mer_status_hold_repeat(0));assert(strstr(last,"phase=50 at=50 code=0 checks=0000ffff") && strstr(last,"snapshot=1 jffs=1 alloc=30408704"));}
 mer_status_close();
}
int main(void){
 for(mode=0;mode<=5;mode++){
  calls=0;int opened=mer_status_open(0);
  if(mode==5){assert(opened<0);mer_status_close();continue;}
  assert(!opened);int sent=mer_status_stage(MER_PH_READY,1);
  assert((sent==0)==(mode==0 || mode==3));if(mode==4)assert(calls==33);
  if(!sent){assert(strstr(last,"nonce=" MER_BUNDLE_NONCE));assert(strstr(last,"image=" MER_BUNDLE_IMAGE_SHA));assert(strstr(last,"profile=stock-ssh-preflight-v2"));puts(last);}
  mer_status_close();
 }
 mode=0;assert(!mer_status_open(0));assert(!mer_status_stage(MER_PH_PREFLIGHT_DONE,0));
 unsigned before=(unsigned)calls;assert(mer_status_stage(MER_PH_FINAL,0xffff)<0 && (unsigned)calls==before);
 assert(!mer_status_stage(MER_PH_SNAPSHOT_BEFORE,0x3fff));struct mer_snapshot_report r=report();assert(!mer_status_snapshot(&r,0x7fff));assert(!mer_status_stage(MER_PH_POST_SNAPSHOT_REFS,0xffff));
 assert(!mer_status_stage(MER_PH_FINAL,0xffff));assert(!mer_status_hold_repeat(0));
 assert(strstr(last,"phase=50 at=50 code=0 checks=0000ffff") && strstr(last,"preflight=1 snapshot=1"));
 assert(!mer_status_hold_repeat(EIO));assert(strstr(last,"phase=61 at=50 code=5"));mer_status_close();
 ipc_case(0);ipc_case(1);
 snapshot_ipc_case(0);snapshot_ipc_case(1);
 printf("ABI_ERRNO ETIMEDOUT=%d ECANCELED=%d ENETUNREACH=%d\n",ETIMEDOUT,ECANCELED,ENETUNREACH);
 puts("PASS fixed target/profile, send faults, actual atomic local-pipe snapshot ACK, final cached hashes, premature FINAL rejection and late snapshot denied after timeout; NETWORK MOCKED, no device actions");return 0;
}
