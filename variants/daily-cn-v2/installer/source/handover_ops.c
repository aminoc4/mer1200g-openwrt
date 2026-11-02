/* Real Linux syscall primitives, unlinked from the default inspection executable.
 * Separately enabled no-MTD-write entry links these reviewed operations.
 * No MTD mutation interface, hidden writer or reboot operation.
 */
#ifndef MER_REVIEW_REAL_OPS
#error "Compile only explicitly for source/object review; never link into default helper"
#endif
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "handover_ops.h"
#include "sha256.h"
#include "mount_graph.h"
#include "status.h"
#include "pinned_fs.h"
#include "stage_policy.h"
#include "phase_budget.h"
#if MER_PROFILE_WRITE
#include "writer_runtime.h"
#endif
#include "live_guard.h"
#include "capture_readonly.h"
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define RAM_ROOT MER_RAM_ROOT
#define TMPFS_TYPE 0x01021994
#define SQUASHFS_TYPE 0x73717368
#define OVERLAYFS_TYPE 0x794c7630
static unsigned progress_checks;
static int checkpoint(unsigned phase,unsigned done_bits){progress_checks|=done_bits;return mer_status_stage(phase,progress_checks);}
static int reject(int why){errno=why;return -1;}
static int read_exact(int fd,void *p,size_t n,off_t at){
 size_t done=0;unsigned interrupts=0;
 while(done<n){ssize_t got=pread(fd,(char *)p+done,n-done,at+(off_t)done);
  if(got<0){if(errno==EINTR && ++interrupts<=32)continue;return -1;}
  if(!got)return reject(EIO);
  done+=(size_t)got;
 }return 0;
}
static int open_ram_static_elf(const char *path){
 /* Regular-file/static-ELF/residency checks; NOT immutable-manifest identity. */
 int pin=open(path,O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;
 struct stat st;struct statfs fs;
 if(fstat(pin,&st) || mer_statfs_pinned(pin,&fs) || !S_ISREG(st.st_mode) || fs.f_type!=TMPFS_TYPE || !(fs.f_flags&ST_RDONLY)){close(pin);return reject(EINVAL);}
 char procpath[64];snprintf(procpath,sizeof(procpath),"/proc/self/fd/%d",pin);
 int fd=open(procpath,O_RDONLY|O_CLOEXEC);close(pin);if(fd<0)return -1;
 Elf32_Ehdr header;
 if(read_exact(fd,&header,sizeof(header),0)){close(fd);return -1;}
 if(memcmp(header.e_ident,ELFMAG,SELFMAG) || header.e_ident[EI_CLASS]!=ELFCLASS32 || header.e_ident[EI_DATA]!=ELFDATA2LSB || header.e_machine!=EM_MIPS || header.e_type!=ET_EXEC || header.e_phentsize!=sizeof(Elf32_Phdr) || !header.e_phnum || header.e_phnum>32 || (header.e_flags&0xf0000000u)!=0x70000000u || (header.e_flags&0x0000f000u)!=0x00001000u || (header.e_flags&0x20u)){close(fd);return reject(ENOEXEC);}
 if((uint64_t)header.e_phoff+(uint64_t)header.e_phnum*sizeof(Elf32_Phdr)>(uint64_t)st.st_size){close(fd);return reject(ENOEXEC);}
 Elf32_Phdr loads[32];unsigned count=0;int entry_ok=0,abi_ok=0;
 for(unsigned i=0;i<header.e_phnum;i++){
  Elf32_Phdr ph;
  uint64_t at=(uint64_t)header.e_phoff+i*sizeof(ph);
  if(at+sizeof(ph)>(uint64_t)st.st_size || read_exact(fd,&ph,sizeof(ph),(off_t)at)){close(fd);return reject(ENOEXEC);}
  if(ph.p_type==PT_INTERP || ph.p_type==PT_DYNAMIC){close(fd);return reject(ENOEXEC);}
  if((uint64_t)ph.p_offset+ph.p_filesz>(uint64_t)st.st_size){close(fd);return reject(ENOEXEC);}
  if(ph.p_type==PT_LOAD){
   uint64_t limit=(uint64_t)ph.p_vaddr+ph.p_memsz;
   if(ph.p_filesz>ph.p_memsz || ph.p_vaddr<4096 || limit>0x80000000u || (ph.p_flags&(PF_W|PF_X))==(PF_W|PF_X) || (ph.p_align>1 && ((ph.p_align&(ph.p_align-1)) || (ph.p_vaddr%ph.p_align)!=(ph.p_offset%ph.p_align)))){close(fd);return reject(ENOEXEC);}
   for(unsigned j=0;j<count;j++)if(ph.p_vaddr<(uint64_t)loads[j].p_vaddr+loads[j].p_memsz && loads[j].p_vaddr<limit){close(fd);return reject(ENOEXEC);}
   loads[count++]=ph;
   if((ph.p_flags&PF_X) && header.e_entry>=ph.p_vaddr && (uint64_t)header.e_entry<(uint64_t)ph.p_vaddr+ph.p_filesz)entry_ok=1;
  }
  if(ph.p_type==0x70000003u){
   unsigned char abi[24];
   if(ph.p_filesz!=sizeof(abi) || read_exact(fd,abi,sizeof(abi),(off_t)ph.p_offset) || abi[0] || abi[1] || abi[2]!=32 || abi[3]!=2 || abi[4]!=1 || abi[5]!=0 || abi[6]!=0 || abi[7]!=3){close(fd);return reject(ENOEXEC);}
   abi_ok=1; /* MIPS32r2, 32-bit GPRs, soft float */
  }
 }
 if(!count || !entry_ok || !abi_ok){close(fd);return reject(ENOEXEC);}
 return fd;
}
int mer_require_ram_static_elf(const char *path){
 int fd=open_ram_static_elf(path);if(fd<0)return -1;return close(fd);
}
static int copy_bundle_entry(const struct mer_bundle_entry *entry){
 int pin=open(entry->source,O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;
 struct stat st;if(fstat(pin,&st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size!=entry->bytes){close(pin);return reject(EINVAL);}
 char fdpath[64];snprintf(fdpath,sizeof(fdpath),"/proc/self/fd/%d",pin);
 int input=open(fdpath,O_RDONLY|O_CLOEXEC);close(pin);if(input<0)return -1;
 unsigned char *buffer=malloc(entry->bytes);if(!buffer){close(input);return -1;}
 if(read_exact(input,buffer,entry->bytes,0)){free(buffer);close(input);return -1;}
 close(input);char digest[65];mer_sha256(buffer,entry->bytes,digest);
 if(strcmp(digest,entry->sha256)){free(buffer);return reject(EINVAL);}
 char output[384];snprintf(output,sizeof(output),RAM_ROOT"/%s",entry->name);
 int fd=open(output,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0){free(buffer);return -1;}
 struct statfs fs;if(fstatfs(fd,&fs) || fs.f_type!=TMPFS_TYPE){free(buffer);close(fd);return reject(EINVAL);}
 size_t done=0;unsigned interrupts=0;
 while(done<entry->bytes){
  ssize_t n=write(fd,buffer+done,entry->bytes-done);
  if(n<0){if(errno==EINTR && ++interrupts<=32)continue;free(buffer);close(fd);return -1;}
  if(!n || (size_t)n>entry->bytes-done){free(buffer);close(fd);return reject(EIO);}
  done+=(size_t)n;
 }
 free(buffer);mode_t mode=strncmp(entry->name,"sbin/",5)?0444:0555;
 if(fchmod(fd,mode)){close(fd);return -1;}return close(fd);
}
int mer_build_readonly_bundle(const struct mer_bundle_entry entries[4]){
 /* Separate tmpfs superblock, no writable alias produced by this implementation.
  * Preparation is before PID1 handover; failure leaves only a reported RAM mount.
  */
 if(geteuid()!=0)return reject(EPERM);
 const char *names[]={"sbin/mer-handover","sbin/mer-stock-preflight-v2","mer1200g-v2-window.bin","sbin/mer-snapshot-v2"};
 for(unsigned i=0;i<4;i++){
  if(!entries[i].source || !entries[i].name || !entries[i].sha256 || strcmp(entries[i].name,names[i]) || strlen(entries[i].sha256)!=64 || !entries[i].bytes || entries[i].bytes>0xcf0000u)return reject(EINVAL);
 }
 if(entries[2].bytes!=0xcf0000u || strcmp(entries[2].sha256,MER_BUNDLE_IMAGE_SHA))return reject(EINVAL);
 if(entries[1].bytes!=MER_PREFLIGHT_BYTES || strcmp(entries[1].sha256,MER_PREFLIGHT_SHA))return reject(EINVAL);
 if(entries[3].bytes!=MER_SNAPSHOT_HELPER_BYTES || strcmp(entries[3].sha256,MER_SNAPSHOT_HELPER_SHA))return reject(EINVAL);
 if(mkdir(RAM_ROOT,0700))return -1; /* reject existing directory/mount, never reuse */
 if(mount("mer-ram-bundle",RAM_ROOT,"tmpfs",MS_NOSUID|MS_NODEV,"size=33554432,mode=0700"))return -1;
 if(mount(NULL,RAM_ROOT,NULL,MS_PRIVATE,NULL))return -1;
 const char *dirs[]={"sbin","mnt","proc","sys","dev","tmp","held","held/lower","held/upper","held/userconfig"};
 for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++){
  char path[384];snprintf(path,sizeof(path),RAM_ROOT"/%s",dirs[i]);if(mkdir(path,0555))return -1;
 }
 for(unsigned i=0;i<4;i++)if(copy_bundle_entry(&entries[i]))return -1;
 if(chmod(RAM_ROOT,0555) || mount(NULL,RAM_ROOT,NULL,MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV,NULL))return -1;
 /* Verify after read-only remount, not merely before sealing. */
 for(unsigned i=0;i<4;i++){
  char path[384];snprintf(path,sizeof(path),RAM_ROOT"/%s",entries[i].name);
  int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
  struct statfs fs;if(fstatfs(fd,&fs) || fs.f_type!=TMPFS_TYPE || !(fs.f_flags&ST_RDONLY)){close(fd);return reject(EINVAL);}
  unsigned char *data=malloc(entries[i].bytes);if(!data){close(fd);return -1;}
  if(read_exact(fd,data,entries[i].bytes,0)){free(data);close(fd);return -1;}
  close(fd);char digest[65];mer_sha256(data,entries[i].bytes,digest);free(data);
  if(strcmp(digest,entries[i].sha256))return reject(EINVAL);
 }
 char helper[384];snprintf(helper,sizeof(helper),RAM_ROOT"/sbin/mer-handover");
 return mer_require_ram_static_elf(helper);
}
int mer_accept_inherited_watchdog(int fd,struct mer_watchdog *out){
 /* No device open, timeout change or disable. Unknown/nonstandard devices reject. */
 struct stat st;int timeout=0;memset(out,0,sizeof(*out));out->fd=-1;
 if(fd<3 || fstat(fd,&st) || !S_ISCHR(st.st_mode) || major(st.st_rdev)!=10 || minor(st.st_rdev)!=130)return reject(EINVAL);
 if(ioctl(fd,WDIOC_GETTIMEOUT,&timeout) || timeout<5)return reject(ENOTSUP);
 out->fd=fd;out->timeout=timeout;out->validated=1;out->mode=MER_WD_INHERITED;return 0;
}
static int timer1_control_readonly(unsigned *value){
 /* Exact already-reviewed stock register: physical10000128, one32-bit load.
  * O_PATH verifies char1:1 without opening a device; real mapping is PROT_READ.
  */
 int pin=open("/dev/mem",O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;
 struct stat st;if(fstat(pin,&st) || !S_ISCHR(st.st_mode) || major(st.st_rdev)!=1 || minor(st.st_rdev)!=1){close(pin);return reject(EINVAL);}
 char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",pin);
 int fd=open(path,O_RDONLY|O_SYNC|O_CLOEXEC);close(pin);if(fd<0)return -1;
 long page=sysconf(_SC_PAGESIZE);uint64_t address=0x10000128u;
 if(page<4096 || page>65536 || ((unsigned long)page&((unsigned long)page-1))){close(fd);return reject(EINVAL);}
 off_t base=(off_t)(address&~((uint64_t)page-1));size_t offset=(size_t)(address-(uint64_t)base);
 void *map=mmap(NULL,(size_t)page,PROT_READ,MAP_SHARED,fd,base);
 if(map==MAP_FAILED){int saved=errno;close(fd);errno=saved;return -1;}
 *value=*(volatile const uint32_t *)((const unsigned char *)map+offset);
 int rc=munmap(map,(size_t)page),saved=errno;if(close(fd) && !rc)return -1;errno=saved;return rc;
}
int mer_prepare_watchdog_state(int inherited_fd,struct mer_watchdog *out){
 unsigned ctl;memset(out,0,sizeof(*out));out->fd=-1;
 if(timer1_control_readonly(&ctl))return -1;
 if(inherited_fd>=3){
  if(mer_accept_inherited_watchdog(inherited_fd,out))return -1;
  out->timer1_control=ctl;return 0;
 }
 if((ctl&0x80u) && ((ctl>>4)&3u)==3u)return reject(ENOTSUP);
 out->validated=1;out->mode=MER_WD_TMR1_NOT_RESET;out->timer1_control=ctl;
 return 0; /* Preserve sampled non-reset TMR1; no start/stop/timeout/ping write. */
}
int mer_prepare_standard_fds(void){
 /* UDP is the review channel. Release old-root console inode references without
  * configuring a terminal: only an existing RAM-backed char1:3 /dev/null opens.
  */
 int pin=open("/dev/null",O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;
 struct stat st;struct statfs fs;
 if(fstat(pin,&st) || mer_statfs_pinned(pin,&fs) || fs.f_type!=TMPFS_TYPE || !S_ISCHR(st.st_mode) || major(st.st_rdev)!=1 || minor(st.st_rdev)!=3){close(pin);return reject(EINVAL);}
 char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",pin);
 int fd=open(path,O_RDWR|O_CLOEXEC);close(pin);if(fd<0)return -1;
 for(int i=0;i<3;i++)if(dup2(fd,i)<0){if(fd>2)close(fd);return -1;}
 if(fd>2)return close(fd);
 return 0;
}
int mer_ping_existing_watchdog(const struct mer_watchdog *wdt){
 if(wdt && wdt->validated && wdt->mode==MER_WD_TMR1_NOT_RESET)return 0;
 if(!wdt || !wdt->validated || wdt->mode!=MER_WD_INHERITED || wdt->fd<3 || wdt->timeout<5)return reject(EINVAL);
 unsigned interrupts=0;
 for(;;){ssize_t n=write(wdt->fd,"\0",1);if(n==1)return 0;if(n<0 && errno==EINTR && ++interrupts<=32)continue;return reject(EIO);}
}
int mer_pid1_exec_ram_helper(const char *path,const char *expected_sha256,char *const argv[],char *const envp[]){
 if(getpid()!=1 || geteuid()!=0)return reject(EPERM);
 if(!expected_sha256 || strlen(expected_sha256)!=64)return reject(EINVAL);
 int fd=open_ram_static_elf(path);if(fd<0)return -1;
 struct stat st;if(fstat(fd,&st) || st.st_size<1 || st.st_size>1048576){close(fd);return reject(EFBIG);}
 unsigned char *contents=malloc((size_t)st.st_size);if(!contents){close(fd);return -1;}
 if(read_exact(fd,contents,(size_t)st.st_size,0)){free(contents);close(fd);return -1;}
 char digest[65];mer_sha256(contents,(size_t)st.st_size,digest);free(contents);
 if(strcmp(digest,expected_sha256)){close(fd);return reject(EINVAL);}
 /* The expected hash and inherited-fd environment must come from the reviewed contract.
  * Execute the same pinned inode; no pathname replacement between hash and exec.
  */
 int rc=fexecve(fd,argv,envp);int saved=errno;close(fd);errno=saved;return rc;
}
static int numeric_pid(const char *name,pid_t *pid){
 char *end;long value=strtol(name,&end,10);
 if(!*name || *end || value<1 || value>0x7fffffff)return 0;
 *pid=(pid_t)value;return 1;
}
struct task_identity {char state;unsigned long long flags,starttime;};
static int task_identity(pid_t pid,struct task_identity *out){
 char path[64],buf[4096];snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return -1;
 ssize_t got=read(fd,buf,sizeof(buf)-1);int saved=errno;close(fd);errno=saved;
 if(got<=0 || got==(ssize_t)sizeof(buf)-1)return reject(EIO);
 buf[got]=0;char *end_name=strrchr(buf,')');if(!end_name || end_name[1]!=' ' || end_name[3]!=' ')return reject(EIO);
 out->state=end_name[2];char *cursor=end_name+4;
 for(unsigned field=4;field<=22;field++){
  char *end;errno=0;unsigned long long value=strtoull(cursor,&end,10);
  if(end==cursor || errno || (*end!=' ' && *end!='\n'))return reject(EIO);
  if(field==9)out->flags=value;
  if(field==22)out->starttime=value;
  cursor=end+1;
 }return 0;
}
static int trace_kill_identity(pid_t pid,const struct task_identity *before){
 /* Old kernels have no pidfd. Hold the tracee until wait/reap rather than blind PID kill.
  * If ptrace is unavailable or racing, fail; never signal an unpinned numeric PID.
  */
 if(ptrace(PTRACE_SEIZE,pid,NULL,(void *)(uintptr_t)(PTRACE_O_TRACEEXIT|PTRACE_O_EXITKILL)))return -1;
 /* After attach, never return to an ordinary caller on error. PID1 supervises.
  * EXITKILL ensures tracees do not resume if the controller is terminated.
  */
 if(ptrace(PTRACE_INTERRUPT,pid,NULL,NULL))goto attached_failure;
 int status=0,stopped=0;struct timespec tick={0,10000000};
 for(unsigned tries=0;tries<300;tries++){
  pid_t result=waitpid(pid,&status,__WALL|WNOHANG);
  if(result<0){if(errno==EINTR)continue;goto attached_failure;}
  if(result==pid){if(WIFEXITED(status)||WIFSIGNALED(status))return 0;if(WIFSTOPPED(status)){stopped=1;break;}goto attached_failure;}
  if(nanosleep(&tick,NULL) && errno!=EINTR)goto attached_failure;
 }
 if(!stopped){errno=ETIMEDOUT;goto attached_failure;}
 struct task_identity after;
 if(task_identity(pid,&after) || after.starttime!=before->starttime || (after.flags&0x00200000u)){errno=ESTALE;goto attached_failure;}
 /* The stopped tracee's PID remains allocated until its tracer reaps it. */
 if(kill(pid,SIGKILL))goto attached_failure;
 /* A traced EXIT event may require continuing before the final reap. */
 for(unsigned tries=0;tries<300;tries++){
  pid_t result=waitpid(pid,&status,__WALL|WNOHANG);
  if(result<0){if(errno==EINTR)continue;goto attached_failure;}
  if(result==pid){if(WIFEXITED(status)||WIFSIGNALED(status))return 0;if(WIFSTOPPED(status) && ptrace(PTRACE_CONT,pid,NULL,NULL))goto attached_failure;}
  if(nanosleep(&tick,NULL) && errno!=EINTR)goto attached_failure;
 }
 errno=ETIMEDOUT;
attached_failure:
 /* No _exit/detach/retry from an uncertain partially attached state. */
 {
  int reason=errno;struct sigaction ignore;memset(&ignore,0,sizeof(ignore));ignore.sa_handler=SIG_IGN;sigemptyset(&ignore.sa_mask);sigaction(SIGPIPE,&ignore,NULL);
  dprintf(STDERR_FILENO,"CONTROLLER PARKED with attached tracee=%ld reason=%d; PID1 supervises; EXITKILL armed\n",(long)pid,reason);
  mer_status_child_failure(reason);
 }
 for(;;){struct timespec park={1,0};nanosleep(&park,NULL);}
}
int mer_signal_other_users(pid_t controller,int signal_number){
 /* Controller must run under the already-replaced RAM PID1; no TERM/respawn race fallback. */
 if(controller!=getpid() || getppid()!=1 || signal_number!=SIGKILL)return reject(EINVAL);
 DIR *dir=opendir("/proc");if(!dir)return -1;struct dirent *entry;int result=0;
 while((entry=readdir(dir))){
  pid_t pid;if(!numeric_pid(entry->d_name,&pid) || pid==1 || pid==controller)continue;
  struct task_identity identity;
  if(task_identity(pid,&identity)){if(errno==ENOENT || errno==ESRCH)continue;result=-1;break;}
  if((identity.flags&0x00200000u) || identity.state=='Z' || identity.state=='X')continue;
  if(trace_kill_identity(pid,&identity)){result=-1;break;}
 }
 int saved=errno;closedir(dir);errno=saved;return result;
}
static int prepared_dir(const char *path){
 struct stat st;if(lstat(path,&st))return -1;
 if(!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))return reject(EINVAL);
 return 0;
}
int mer_pivot_prepared_ram_root(void){
 /* All paths/bundle entries must already exist and be independently authenticated. */
 struct statfs fs;if(statfs(RAM_ROOT,&fs) || fs.f_type!=TMPFS_TYPE)return reject(EINVAL);
 const char *dirs[]={RAM_ROOT,RAM_ROOT"/mnt",RAM_ROOT"/proc",RAM_ROOT"/sys",RAM_ROOT"/dev",RAM_ROOT"/tmp",RAM_ROOT"/held",RAM_ROOT"/held/lower",RAM_ROOT"/held/upper",RAM_ROOT"/held/userconfig"};
 for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++)if(prepared_dir(dirs[i]))return -1;
 /* The fresh bundle is already its own mount. Do not leave a redundant hidden
  * bind/base mount beneath /tmp when promoting it to namespace root.
  */
 struct mer_mount_plan before;
 if(mer_original_mount_plan("/proc/self/mountinfo",&before,NULL) || before.phase!=MER_GRAPH_PREPIVOT)return reject(ENOTSUP);
 if(checkpoint(MER_PH_PROC_MOVE_BEFORE,0))return -1;
 if(mount("/proc",RAM_ROOT"/proc",NULL,MS_MOVE,NULL))return -1;
 if(checkpoint(MER_PH_PROC_MOVED,0x2) || checkpoint(MER_PH_PIVOT_BEFORE,0))return -1;
 if(syscall(SYS_pivot_root,RAM_ROOT,RAM_ROOT"/mnt"))return -1;
 if(chdir("/"))return -1;
 if(checkpoint(MER_PH_PIVOTED,0x4) || checkpoint(MER_PH_SYSTEM_MOVES_BEFORE,0))return -1;
 const char *sources[]={"/mnt/sys","/mnt/dev","/mnt/tmp"};const char *destinations[]={"/sys","/dev","/tmp"};
 const unsigned before_phase[]={MER_PH_SYS_BEFORE,MER_PH_DEV_BEFORE,MER_PH_TMP_BEFORE};
 const unsigned after_phase[]={MER_PH_SYS_MOVED,MER_PH_DEV_MOVED,MER_PH_TMP_MOVED};
 for(unsigned i=0;i<3;i++){
  if(checkpoint(before_phase[i],0) || mount(sources[i],destinations[i],NULL,MS_MOVE,NULL) || checkpoint(after_phase[i],0x8u<<i))return -1;
 }
 if(checkpoint(MER_PH_SYSTEM_MOVED,0))return -1;
 return 0;
}
static int check_ram_path(const char *path){
 int fd=open(path,O_PATH|O_CLOEXEC);if(fd<0)return -1;
 struct statfs fs;int rc=mer_statfs_pinned(fd,&fs);close(fd);
 if(rc)return -1;
 if(fs.f_type!=TMPFS_TYPE)return reject(EBUSY);
 return 0;
}
int mer_check_survivor_references(pid_t controller){
 if(controller<=1)return reject(EINVAL);
 struct stat own_ns;if(stat("/proc/self/ns/mnt",&own_ns))return -1;
 DIR *dir=opendir("/proc");if(!dir)return -1;struct dirent *entry;int result=0;
 while((entry=readdir(dir))){
  pid_t pid;if(!numeric_pid(entry->d_name,&pid))continue;
  char path[96];struct task_identity identity;
  if(task_identity(pid,&identity)){result=-1;break;}
  if((identity.flags&0x00200000u) || identity.state=='Z' || identity.state=='X')continue;
  if(pid!=1 && pid!=controller){result=reject(EBUSY);break;}
  snprintf(path,sizeof(path),"/proc/%ld/task",(long)pid);DIR *tasks=opendir(path);
  if(!tasks){result=-1;break;}
  unsigned threads=0;struct dirent *thread;
  while((thread=readdir(tasks))){pid_t tid;if(!numeric_pid(thread->d_name,&tid))continue;threads++;if(tid!=pid)result=reject(EBUSY);}
  closedir(tasks);if(result || threads!=1){result=reject(EBUSY);break;}
  /* Narrow target contract: exactly two known single-threaded static RAM images.
   * Old 3.10 target lacks map_files. Compare dev+inode, never pathname guesses.
   */
  snprintf(path,sizeof(path),"/proc/%ld/exe",(long)pid);int exe_pin=open(path,O_PATH|O_CLOEXEC);
  if(exe_pin<0){result=-1;break;}
  struct stat executable;struct statfs exe_fs;
  int exe_bad=fstat(exe_pin,&executable) || mer_statfs_pinned(exe_pin,&exe_fs) || !S_ISREG(executable.st_mode) || exe_fs.f_type!=TMPFS_TYPE;
  close(exe_pin);if(exe_bad){result=reject(EBUSY);break;}
  struct stat ns;snprintf(path,sizeof(path),"/proc/%ld/ns/mnt",(long)pid);
  if(stat(path,&ns) || ns.st_dev!=own_ns.st_dev || ns.st_ino!=own_ns.st_ino){result=reject(EBUSY);break;}
  const char *links[]={"exe","root","cwd"};
  for(unsigned i=0;i<3;i++){
   snprintf(path,sizeof(path),"/proc/%ld/%s",(long)pid,links[i]);if(check_ram_path(path)){result=-1;break;}
  }
  if(result)break;
  snprintf(path,sizeof(path),"/proc/%ld/maps",(long)pid);FILE *maps=fopen(path,"r");if(!maps){result=-1;break;}
  char line[8192];while(fgets(line,sizeof(line),maps)){
   unsigned major_id,minor_id;unsigned long long start,end,offset,inode;char perms[5];int used=0;
   if(!strchr(line,'\n') || sscanf(line,"%llx-%llx %4s %llx %x:%x %llu%n",&start,&end,perms,&offset,&major_id,&minor_id,&inode,&used)!=7 || start>=end){result=reject(EIO);break;}
   if(!inode){
    char *name=line+used;while(*name==' ' || *name=='\t')name++;
    name[strcspn(name,"\n")]=0;
    if(major_id || minor_id || (*name && strcmp(name,"[heap]") && strcmp(name,"[stack]") && strcmp(name,"[vdso]"))){result=reject(EBUSY);break;}
    continue;
   }
   if(major_id!=major(executable.st_dev) || minor_id!=minor(executable.st_dev) || inode!=(unsigned long long)executable.st_ino){result=reject(EBUSY);break;}
  }
  if(ferror(maps))result=-1;
  fclose(maps);if(result)break;
  snprintf(path,sizeof(path),"/proc/%ld/fd",(long)pid);DIR *fds=opendir(path);
  if(!fds){result=-1;break;}
  struct dirent *fentry;
  while((fentry=readdir(fds))){
   char *end;long number=strtol(fentry->d_name,&end,10);
   if(!*fentry->d_name || *end || number<0 || number>0x7fffffff)continue;
   snprintf(path,sizeof(path),"/proc/%ld/fd/%ld",(long)pid,number);
   /* O_PATH follows the proc fd link without invoking a device open method. */
   int pin=open(path,O_PATH|O_CLOEXEC);if(pin<0){result=-1;break;}
   struct statfs fs;int rc=mer_statfs_pinned(pin,&fs);close(pin);
   if(rc || !(fs.f_type==TMPFS_TYPE || fs.f_type==0x9fa0 || fs.f_type==0x62656572 || fs.f_type==0x1cd1 || fs.f_type==0x09041934 || fs.f_type==0x534f434b || fs.f_type==0x50495045)){result=reject(EBUSY);break;}
  }
  closedir(fds);if(result)break;
 }
 int saved=errno;closedir(dir);errno=saved;return result;
}
int mer_detach_old_mounts(void){
 struct mer_mount_plan plan;
 if(mer_original_mount_plan("/proc/self/mountinfo",&plan,NULL) || !plan.approved || plan.phase!=MER_GRAPH_POSTPIVOT)return reject(ENOTSUP);
 for(unsigned i=0;i<plan.count;i++){
  const struct mer_mount_action *a=&plan.actions[i];
  const unsigned before_phase[]={MER_PH_MOVE_LOWER_BEFORE,MER_PH_MOVE_UPPER_BEFORE,MER_PH_MOVE_JFFS_BEFORE,MER_PH_OVERLAY_UMOUNT_BEFORE,MER_PH_LOWER_UMOUNT_BEFORE,MER_PH_UPPER_UMOUNT_BEFORE};
  const unsigned after_phase[]={MER_PH_LOWER_HELD,MER_PH_UPPER_HELD,MER_PH_JFFS_HELD,MER_PH_OVERLAY_GONE,MER_PH_LOWER_GONE,MER_PH_UPPER_GONE};
  if(i>=6 || checkpoint(before_phase[i],0))return -1;
  if(mer_check_mount_action("/proc/self/mountinfo",a))return -1;
  if(a->kind==MER_MOVE_CHILD){
   if(prepared_dir(a->destination))return -1;
   struct statfs fs;if(statfs(a->destination,&fs) || fs.f_type!=TMPFS_TYPE || !(fs.f_flags&ST_RDONLY))return reject(EINVAL);
   if(mount(a->source,a->destination,NULL,MS_MOVE,NULL))return -1;
  }else if(a->kind==MER_ORDINARY_UMOUNT){
   if(umount2(a->source,0))return -1;
  }else return reject(EINVAL);
  if(checkpoint(after_phase[i],0x80u<<i))return -1;
 }
 return 0;
}
_Noreturn void mer_pid1_failure_hold(const struct mer_watchdog *wdt,int reason){
 /* If reached after a transition, never return to an old shell or exit PID1.
  * Unknown watchdog: issue no watchdog control; continued uptime is NOT guaranteed.
  */
 if(getpid()!=1)_exit(78);
 mer_status_note_error(reason);
 struct sigaction ignore;memset(&ignore,0,sizeof(ignore));ignore.sa_handler=SIG_IGN;
 sigemptyset(&ignore.sa_mask);sigaction(SIGPIPE,&ignore,NULL);
 dprintf(STDERR_FILENO,"HANDOVER HALTED reason=%d; no further erase/program or reboot; watchdog=%s\n",reason,wdt && wdt->validated?(wdt->mode==MER_WD_INHERITED?"inherited":"TMR1 not reset-mode (sample)"):"unknown");
 struct timespec last_status={0,0};
 for(;;){
  if(wdt && wdt->validated)mer_ping_existing_watchdog(wdt);
  int status;while(waitpid(-1,&status,WNOHANG)>0){}
  mer_status_pump(0);
  struct timespec now;if(!clock_gettime(CLOCK_MONOTONIC,&now) && (!last_status.tv_sec || now.tv_sec-last_status.tv_sec>=5)){mer_status_hold_repeat(reason);last_status=now;}
  (void)mer_status_wait_events(100);
 }
}
_Noreturn void mer_pid1_supervise_child(pid_t child,const struct mer_watchdog *wdt,unsigned deadline_seconds){
 if(getpid()!=1)_exit(78);
 if(child<=1 || !wdt || !wdt->validated || deadline_seconds!=180)mer_pid1_failure_hold(wdt,EINVAL);
 struct timespec start;if(clock_gettime(CLOCK_MONOTONIC,&start))mer_pid1_failure_hold(wdt,errno);
 struct timespec last_report=start;unsigned group=mer_budget_class(mer_status_current_phase());
 for(;;){
  struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))mer_pid1_failure_hold(wdt,errno);
  /* Reject already-expired results before dequeuing or ACKing advancement. */
  if(mer_budget_expired(&start,&now,group))mer_pid1_failure_hold(wdt,ETIMEDOUT);
  if(mer_ping_existing_watchdog(wdt))mer_pid1_failure_hold(wdt,EIO);
  if(mer_status_operation_stalled())mer_pid1_failure_hold(wdt,errno);
  if(mer_status_pump(1)<0)mer_pid1_failure_hold(wdt,errno);
  int status;pid_t result=waitpid(child,&status,WNOHANG);
  if(result==child)mer_pid1_failure_hold(wdt,WIFEXITED(status)?WEXITSTATUS(status):EIO);
  if(result<0 && errno!=EINTR)mer_pid1_failure_hold(wdt,errno);
  if(clock_gettime(CLOCK_MONOTONIC,&now))mer_pid1_failure_hold(wdt,errno);
  unsigned next_group=mer_budget_class(mer_status_current_phase());if(next_group!=group){group=next_group;start=now;}
  if(mer_budget_expired(&start,&now,group)){
   /* No signal/cancellation: an outstanding normal umount or read-only MTD
    * operation can still be running. Deny subsequent phase ACKs, keep PID1. */
   mer_pid1_failure_hold(wdt,ETIMEDOUT);
  }
  if(now.tv_sec-last_report.tv_sec>=5){if(mer_status_wait_report(0))mer_pid1_failure_hold(wdt,errno);last_report=now;}
  (void)mer_status_wait_events(100);
 }
}
_Noreturn void mer_child_terminal_failure(int reason){
 /* RAM static child exits only to its RAM PID1 supervisor, never an old-root shell. */
 if(getpid()==1)mer_pid1_failure_hold(NULL,reason);
 dprintf(STDERR_FILENO,"CONTROLLER HALTED reason=%d; no further erase/program or reboot\n",reason);
 mer_status_child_failure(reason);
 _exit(70);
}
_Noreturn void mer_pid1_fork_supervised(const struct mer_watchdog *wdt,int (*controller)(void *),void *contract){
 if(getpid()!=1)_exit(78);
 if(!controller || !wdt || !wdt->validated)mer_pid1_failure_hold(wdt,ENOTSUP);
 if(wdt->mode==MER_WD_TMR1_NOT_RESET){struct mer_watchdog fresh;if(mer_prepare_watchdog_state(-1,&fresh))mer_pid1_failure_hold(wdt,ENOTSUP);}
 /* Strict original mount strategy is checked before forking/killing/pivoting.
  * Other graphs are rejected; this new V4 ELF has no device execution evidence.
  */
 struct mer_mount_plan plan;
 if(mer_original_mount_plan("/proc/self/mountinfo",&plan,NULL) || !plan.approved || plan.phase!=MER_GRAPH_PREPIVOT || mer_require_ram_static_elf(RAM_ROOT"/sbin/mer-handover"))mer_pid1_failure_hold(wdt,ENOTSUP);
 if(mer_status_stage(MER_PH_READY,0) || mer_status_setup_pipes())mer_pid1_failure_hold(wdt,errno);
 pid_t child=fork();if(child<0)mer_pid1_failure_hold(wdt,errno);
 if(!child){if(mer_status_after_fork(1))mer_child_terminal_failure(errno);int result=controller(contract);if(!result)_exit(0);mer_child_terminal_failure(result);}
 if(mer_status_after_fork(0))mer_pid1_failure_hold(wdt,errno);
 mer_pid1_supervise_child(child,wdt,180);
}
static int only_allowed_users(pid_t controller){
 DIR *dir=opendir("/proc");if(!dir)return -1;struct dirent *entry;int result=0;
 while((entry=readdir(dir))){
  pid_t pid;if(!numeric_pid(entry->d_name,&pid) || pid==1 || pid==controller)continue;
  struct task_identity identity;
  if(task_identity(pid,&identity)){if(errno==ENOENT || errno==ESRCH)continue;result=-1;break;}
  if(!(identity.flags&0x00200000u) && identity.state!='Z' && identity.state!='X'){result=1;break;}
 }
 int saved=errno;closedir(dir);errno=saved;return result;
}
static int snapshot_tick(void *ctx){return mer_ping_existing_watchdog(ctx);}
static void snapshot_child_setup(void *ctx){
 const struct mer_watchdog *wdt=ctx;mer_status_close();if(wdt->fd>=3)close(wdt->fd);
}
static int full_snapshot_precheck(const struct mer_watchdog *wdt,struct mer_snapshot_report *report){
 const char *program="/sbin/mer-snapshot-v2";
 int fd=open_ram_static_elf(program);if(fd<0)return -1;struct stat st;
 if(fstat(fd,&st) || st.st_size!=MER_SNAPSHOT_HELPER_BYTES){close(fd);return reject(EINVAL);}
 unsigned char *bytes=malloc(MER_SNAPSHOT_HELPER_BYTES);if(!bytes){close(fd);return -1;}
 if(read_exact(fd,bytes,MER_SNAPSHOT_HELPER_BYTES,0)){free(bytes);close(fd);return -1;}
 char digest[65];mer_sha256(bytes,MER_SNAPSHOT_HELPER_BYTES,digest);free(bytes);
 if(strcmp(digest,MER_SNAPSHOT_HELPER_SHA)){close(fd);return reject(EINVAL);}
 char output[2048];size_t size=0;
 char *argv[]={"mer-snapshot-v2","--preflight","--image","/mer1200g-v2-window.bin",NULL};
 extern char **environ;struct mer_capture_hooks hooks={(void *)wdt,snapshot_tick,snapshot_child_setup};
 int result=mer_capture_readonly(fd,argv,environ,600,&hooks,output,&size),saved=errno;
 if(close(fd) && !result)return -1;
 errno=saved;if(result)return -1;
 return mer_snapshot_parse(output,size,report);
}
static int real_controller_workflow(void *contract){
 const struct mer_watchdog *wdt=contract;pid_t controller=getpid();
 if(!wdt || !wdt->validated)mer_child_terminal_failure(ENOTSUP);
 if(wdt->mode==MER_WD_TMR1_NOT_RESET){struct mer_watchdog fresh;if(mer_prepare_watchdog_state(-1,&fresh))mer_child_terminal_failure(ENOTSUP);}
 struct sigaction ignore;memset(&ignore,0,sizeof(ignore));ignore.sa_handler=SIG_IGN;sigemptyset(&ignore.sa_mask);
 if(sigaction(SIGPIPE,&ignore,NULL))mer_child_terminal_failure(errno);
 if(checkpoint(MER_PH_QUIESCE_BEFORE,0))mer_child_terminal_failure(errno);
 int settled=0;
 for(unsigned pass=0;pass<8;pass++){
  if(mer_signal_other_users(controller,SIGKILL))mer_child_terminal_failure(errno);
  int remaining=only_allowed_users(controller);if(remaining<0)mer_child_terminal_failure(errno);
  if(!remaining){
   struct timespec tick={0,100000000};if(nanosleep(&tick,NULL) && errno!=EINTR)mer_child_terminal_failure(errno);
   remaining=only_allowed_users(controller);if(remaining<0)mer_child_terminal_failure(errno);
   if(!remaining){settled=1;break;}
  }
 }
 if(!settled)mer_child_terminal_failure(EBUSY);
 if(checkpoint(MER_PH_QUIESCED,0x1))mer_child_terminal_failure(errno);
 if(mer_pivot_prepared_ram_root())mer_child_terminal_failure(errno);
 if(mer_check_survivor_references(controller))mer_child_terminal_failure(errno);
 if(checkpoint(MER_PH_REFS_CHECKED,0x40))mer_child_terminal_failure(errno);
 if(mer_detach_old_mounts())mer_child_terminal_failure(errno);
 /* Reuse the V3-proven normal last JFFS2 unmount and pinned read-only helper.
  * Physical config flush is possible in both compile-time profiles. */
 if(progress_checks!=0x1fffu)mer_child_terminal_failure(EPROTO);
 if(checkpoint(MER_PH_RAM_READY,0) || checkpoint(MER_PH_JFFS_BEFORE,0))mer_child_terminal_failure(errno);
 if(mer_unmount_retained_jffs2())mer_child_terminal_failure(errno);
 if(checkpoint(MER_PH_JFFS_GONE,0x2000u) || checkpoint(MER_PH_SNAPSHOT_BEFORE,0))mer_child_terminal_failure(errno);
 struct mer_snapshot_report snapshot;
 if(full_snapshot_precheck(wdt,&snapshot))mer_child_terminal_failure(errno);
 progress_checks|=0x4000u;
 if(mer_status_snapshot(&snapshot,progress_checks) || checkpoint(MER_PH_SNAPSHOT_DONE,0))mer_child_terminal_failure(errno);
 if(mer_check_survivor_references(controller) || mer_writer_live_guard(-1,-1,0))mer_child_terminal_failure(errno);
 if(checkpoint(MER_PH_POST_SNAPSHOT_REFS,0x8000u))mer_child_terminal_failure(errno);

 if(progress_checks!=MER_PREPARATION_CHECKS)mer_child_terminal_failure(EPROTO);
#if MER_PROFILE_WRITE
 if(mer_run_fixed_writer(wdt,&progress_checks))mer_child_terminal_failure(errno);
 if(mer_check_survivor_references(controller) || mer_writer_live_guard(-1,-1,0))mer_child_terminal_failure(errno);
 if(checkpoint(MER_PH_WR_REFS,MER_WR_REFS_BIT))mer_child_terminal_failure(errno);
#endif
 if(progress_checks!=MER_STAGE_FINAL_CHECKS)mer_child_terminal_failure(EPROTO);
 if(checkpoint(MER_PH_FINAL,0))mer_child_terminal_failure(errno);
 return 0;
}
_Noreturn void mer_pid1_start_transition(const struct mer_watchdog *wdt){
 mer_pid1_fork_supervised(wdt,real_controller_workflow,(void *)wdt);
}
