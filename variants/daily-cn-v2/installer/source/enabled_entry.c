/* Independent compile-time profiles. Default has no firmware write backend;
 * explicit WRITE_REVIEW adds the fixed transaction after normal JFFS2 teardown.
 * Global PID1 + exact contract/nonce/immutable assets gate the runtime path.
 */
#ifndef MER_BUILD_RAM_PREFLIGHT_STAGE
#error "Use the separately reviewed RAM-stage build"
#endif
#define _GNU_SOURCE 1
#include "handover_ops.h"
#include "status.h"
#include "bundle_nonce.h"
#include "stage_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CONTRACT MER_CONTRACT
#define RAM_ROOT MER_RAM_ROOT
extern char **environ;
static struct mer_watchdog watchdog={.fd=-1,.mode=MER_WD_UNKNOWN,.timer1_control=UINT_MAX};
static _Noreturn void fail(int reason){
 int saved=reason?reason:EIO;
 (void)mer_status_open(watchdog.validated?watchdog.timer1_control:UINT_MAX);
 (void)mer_status_emit(MER_PH_FAILURE,saved,0);
 mer_pid1_failure_hold(watchdog.validated?&watchdog:NULL,saved);
}
static int decimal(const char *s,unsigned long maximum,unsigned long *value){
 if(!s || !*s)return -1;
 for(const char *p=s;*p;p++)if(*p<'0' || *p>'9')return -1;
 char *end;errno=0;unsigned long n=strtoul(s,&end,10);if(errno || *end || n>maximum)return -1;*value=n;return 0;
}
static int inherited_watchdog(void){
 const char *s=getenv("WDTFD");if(!s)return -1;unsigned long fd;
 if(decimal(s,1024,&fd) || fd<3)return -2;
 return (int)fd;
}
static int contract_read(const char *path,char helper_hash[65]){
 if(strcmp(path,CONTRACT)){errno=EINVAL;return -1;}
 int pin=open(path,O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;
 struct stat st;struct statfs fs;
 if(fstat(pin,&st)){int saved=errno;close(pin);errno=saved;return -1;}
 if(!S_ISREG(st.st_mode) || st.st_uid!=0 || st.st_nlink!=1 || (st.st_mode&0222) || st.st_size<1 || st.st_size>511){close(pin);errno=EINVAL;return -1;}
 char fdpath[64];snprintf(fdpath,sizeof(fdpath),"/proc/self/fd/%d",pin);int fd=open(fdpath,O_RDONLY|O_CLOEXEC);int saved=errno;close(pin);errno=saved;if(fd<0)return -1;
 struct stat reopened;
 if(fstat(fd,&reopened) || fstatfs(fd,&fs)){saved=errno;close(fd);errno=saved;return -1;}
 if(reopened.st_dev!=st.st_dev || reopened.st_ino!=st.st_ino || fs.f_type!=0x01021994){close(fd);errno=EINVAL;return -1;}
 char text[512];size_t done=0;while(done<(size_t)st.st_size){ssize_t n=read(fd,text+done,(size_t)st.st_size-done);if(n<=0){close(fd);errno=EIO;return -1;}done+=(size_t)n;}
 close(fd);
 const char prefix[]=MER_CONTRACT_MARKER "\nnonce=" MER_BUNDLE_NONCE MER_CONTRACT_ACK "\nhelper_sha256=";
 size_t offset=sizeof(prefix)-1;
 if(done!=offset+65 || memcmp(text,prefix,offset) || text[done-1]!='\n'){errno=EINVAL;return -1;}
 for(unsigned i=0;i<64;i++)if(!((text[offset+i]>='0' && text[offset+i]<='9') || (text[offset+i]>='a' && text[offset+i]<='f'))){errno=EINVAL;return -1;}
 memcpy(helper_hash,text+offset,64);helper_hash[64]=0;return 0;
}
static void preflight(void){
 if(mer_status_stage(MER_PH_PREFLIGHT_BEFORE,0))fail(errno);
 const char *program=RAM_ROOT"/sbin/mer-stock-preflight-v2";
 if(mer_require_ram_static_elf(program))fail(errno);
 int fd=open(program,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)fail(errno);
 pid_t child=fork();if(child<0){close(fd);fail(errno);}
 if(!child){char *args[]={"mer-stock-preflight-v2","--image",RAM_ROOT"/mer1200g-v2-window.bin","--mtd","/dev/mtd9",NULL};fexecve(fd,args,environ);_exit(125);}
 close(fd);struct timespec start;if(clock_gettime(CLOCK_MONOTONIC,&start))fail(errno);
 for(;;){
  if(mer_ping_existing_watchdog(&watchdog))fail(errno);
  int status;pid_t result=waitpid(child,&status,WNOHANG);
  if(result==child){if(!WIFEXITED(status) || WEXITSTATUS(status))fail(EIO);break;}
  if(result<0 && errno!=EINTR)fail(errno);
  struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))fail(errno);
  if(now.tv_sec-start.tv_sec>=30)fail(ETIMEDOUT); /* hold, never signal an uncertain child here */
  struct timespec tick={0,200000000};nanosleep(&tick,NULL);
 }
 if(mer_status_stage(MER_PH_PREFLIGHT_DONE,0))fail(errno);
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
 if(getpid()!=1){
  if(argc==1 || (argc==2 && (!strcmp(argv[1],"--help") || !strcmp(argv[1],"--build-info")))){
   puts(MER_PROFILE_NAME ": fixed 207-block stock SSH installation; normal JFFS2 unmount; permanent hold, no reboot. nonce=" MER_BUNDLE_NONCE " image=" MER_BUNDLE_IMAGE_SHA " status=192.168.1.2:39001/UDP; reviewed PID1 contract required. ");return 0;
  }
  fputs("REJECTED: not PID1; no namespace/device/network access attempted\n",stderr);return 78;
 }
 int wfd=inherited_watchdog();if(wfd==-2)fail(EINVAL);
 if(mer_prepare_watchdog_state(wfd,&watchdog))fail(errno);
 char hash[65];
 int ram_phase=argc==3 && !strcmp(argv[1],"--ram-phase");
 if(!ram_phase && argc!=2)fail(EINVAL);
 if(contract_read(argv[ram_phase?2:1],hash))fail(errno);
 if(mer_prepare_standard_fds())fail(errno);
 if(!ram_phase){
  if(mer_status_open(watchdog.timer1_control) || mer_status_stage(MER_PH_READY,0))fail(errno);
  struct stat running,file;
  if(stat("/proc/self/exe",&running) || lstat("/tmp/upgraded",&file) || !S_ISREG(file.st_mode) || running.st_dev!=file.st_dev || running.st_ino!=file.st_ino || file.st_size<1 || file.st_size>1048576)fail(EINVAL);
  struct mer_bundle_entry entries[]={
   {"/tmp/upgraded","sbin/mer-handover",hash,(size_t)file.st_size},
   {"/tmp/mer_stock_preflight_v2","sbin/mer-stock-preflight-v2",MER_PREFLIGHT_SHA,MER_PREFLIGHT_BYTES},
   {"/tmp/mer1200g-v2-window.bin","mer1200g-v2-window.bin",MER_BUNDLE_IMAGE_SHA,0xcf0000u},
   {"/tmp/mer_snapshot_v2","sbin/mer-snapshot-v2",MER_SNAPSHOT_HELPER_SHA,MER_SNAPSHOT_HELPER_BYTES}};
  if(mer_status_stage(MER_PH_BUNDLE_BEFORE,0) || mer_build_readonly_bundle(entries) || mer_status_stage(MER_PH_BUNDLE_DONE,0))fail(errno);
  if(mer_status_stage(MER_PH_RAM_EXEC_BEFORE,0))fail(errno);
  int status_fd;unsigned sequence;if(mer_status_export_exec(&status_fd,&sequence))fail(errno);
  char fd_text[16],seq_text[16];snprintf(fd_text,sizeof(fd_text),"%d",status_fd);snprintf(seq_text,sizeof(seq_text),"%u",sequence);
  if(setenv("MER_RAM_PHASE",MER_BUNDLE_NONCE,1) || setenv("MER_STATUS_FD",fd_text,1) || setenv("MER_STATUS_SEQ",seq_text,1))fail(errno);
  if(watchdog.mode==MER_WD_INHERITED){int flags=fcntl(watchdog.fd,F_GETFD);if(flags<0 || fcntl(watchdog.fd,F_SETFD,flags&~FD_CLOEXEC))fail(errno);}
  char *args[]={"mer-handover","--ram-phase",CONTRACT,NULL};mer_pid1_exec_ram_helper(RAM_ROOT"/sbin/mer-handover",hash,args,environ);fail(errno);
 }
 const char *marker=getenv("MER_RAM_PHASE");unsigned long status_fd,sequence;
 if(!marker || strcmp(marker,MER_BUNDLE_NONCE) || decimal(getenv("MER_STATUS_FD"),1024,&status_fd) || status_fd<3 || decimal(getenv("MER_STATUS_SEQ"),UINT_MAX-1,&sequence))fail(EINVAL);
 struct stat running,ram;if(stat("/proc/self/exe",&running) || stat(RAM_ROOT"/sbin/mer-handover",&ram) || running.st_dev!=ram.st_dev || running.st_ino!=ram.st_ino || mer_require_ram_static_elf(RAM_ROOT"/sbin/mer-handover"))fail(EINVAL);
 if(mer_status_import_exec((int)status_fd,(unsigned)sequence,watchdog.timer1_control) || mer_status_stage(MER_PH_RAM_EXEC_DONE,0))fail(errno);
 preflight();mer_pid1_start_transition(&watchdog);
}
