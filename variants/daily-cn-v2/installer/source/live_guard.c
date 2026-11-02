/* Verify RAM-only userspace and mount graph for normal JFFS2 teardown.
 * No MTD RW open/kill/ptrace/pivot/remount/unmount implementation in this file.
 */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "live_guard.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#define TMPFS_TYPE 0x01021994
static int reject(void){errno=EBUSY;return -1;}
static int fs_pin(int fd,struct statfs *fs){char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",fd);return statfs(path,fs);}
static int ram_path(const char *path,int readonly){
 int pin=open(path,O_PATH|O_CLOEXEC);if(pin<0)return -1;
 struct statfs fs;int result=fs_pin(pin,&fs);close(pin);if(result)return -1;
 return fs.f_type==TMPFS_TYPE && (!readonly || (fs.f_flags&ST_RDONLY))?0:reject();
}
static int numeric(const char *s,pid_t *pid){if(!s || !*s)return 0;unsigned long n=0;for(;*s;s++){if(*s<'0' || *s>'9' || n>214748364u)return 0;n=n*10+(unsigned)(*s-'0');if(n>2147483647u)return 0;}if(!n)return 0;*pid=(pid_t)n;return 1;}
static int task_info(pid_t pid,char *state,unsigned long *flags){
 char path[64],line[4096];snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);FILE *f=fopen(path,"r");if(!f)return -1;
 int okay=fgets(line,sizeof(line),f)!=NULL && strchr(line,'\n')!=NULL;fclose(f);if(!okay)return reject();
 char *end=strrchr(line,')');if(!end || end[1]!=' ')return reject();
 char *save,*token=strtok_r(end+2," ", &save);if(!token || strlen(token)!=1)return reject();*state=*token;
 for(unsigned field=4;field<=9;field++){token=strtok_r(NULL," ",&save);if(!token)return reject();}
 char *tail;errno=0;*flags=strtoul(token,&tail,10);return errno || *tail?reject():0;
}
static int mounted_flash(int allow){
 FILE *f=fopen("/proc/self/mountinfo","r");if(!f)return -1;char line[8192];unsigned count=0,jffs=0;int result=0;
 while(fgets(line,sizeof(line),f)){
  unsigned id,parent,ma,mi;char root[256],path[256],options[256],fs[32];
  char *sep=strstr(line," - ");
  if(++count>64 || !strchr(line,'\n') || !sep || strstr(line," shared:") || strstr(line," master:") || strstr(line," propagate_from:") || sscanf(line,"%u %u %u:%u %255s %255s %255s",&id,&parent,&ma,&mi,root,path,options)!=7 || sscanf(sep+3,"%31s",fs)!=1 || strchr(root,'\\') || strchr(path,'\\')){result=reject();break;}
  if(!strncmp(path,"/held/userconfig/",17)){result=reject();break;}
  if(allow && ma==31 && mi==7 && !strcmp(path,"/held/userconfig") && !strcmp(fs,"jffs2") && (!strcmp(options,"rw") || !strncmp(options,"rw,",3))){jffs++;continue;}
  if(ma==31 || (strcmp(fs,"tmpfs") && strcmp(fs,"proc") && strcmp(fs,"sysfs") && strcmp(fs,"devpts") && strcmp(fs,"debugfs"))){result=reject();break;}
 }
 if(ferror(f))result=-1;
 fclose(f);if(result)return -1;return count && jffs==(unsigned)allow?0:reject();
}
static int survivor(pid_t pid,int own_mtd_fd,int own_pin){
 char path[96];snprintf(path,sizeof(path),"/proc/%ld/exe",(long)pid);int pin=open(path,O_PATH|O_CLOEXEC);if(pin<0)return -1;
 struct stat exe;struct statfs fs;int result=fstat(pin,&exe) || fs_pin(pin,&fs);close(pin);
 if(result || !S_ISREG(exe.st_mode) || fs.f_type!=TMPFS_TYPE || !(fs.f_flags&ST_RDONLY))return reject();
 const char *links[]={"root","cwd"};for(unsigned i=0;i<2;i++){snprintf(path,sizeof(path),"/proc/%ld/%s",(long)pid,links[i]);if(ram_path(path,0))return -1;}
 snprintf(path,sizeof(path),"/proc/%ld/task",(long)pid);DIR *tasks=opendir(path);if(!tasks)return -1;unsigned threads=0;struct dirent *e;
 while((e=readdir(tasks))){pid_t tid;if(!numeric(e->d_name,&tid))continue;threads++;if(tid!=pid)result=-1;}
 closedir(tasks);if(result || threads!=1)return reject();
 snprintf(path,sizeof(path),"/proc/%ld/maps",(long)pid);FILE *maps=fopen(path,"r");if(!maps)return -1;char line[8192];
 while(fgets(line,sizeof(line),maps)){
  unsigned ma,mi;unsigned long long start,end,offset,inode;char perms[5];int used=0;
  if(!strchr(line,'\n') || sscanf(line,"%llx-%llx %4s %llx %x:%x %llu%n",&start,&end,perms,&offset,&ma,&mi,&inode,&used)!=7 || start>=end){result=-1;break;}
  if(inode){if(ma!=major(exe.st_dev) || mi!=minor(exe.st_dev) || inode!=(unsigned long long)exe.st_ino){result=-1;break;}}
  else {char *name=line+used;while(*name==' ' || *name=='\t')name++;name[strcspn(name,"\n")]=0;if(ma || mi || (*name && strcmp(name,"[heap]") && strcmp(name,"[stack]") && strcmp(name,"[vdso]"))){result=-1;break;}}
 }
 if(ferror(maps))result=-1;
 fclose(maps);if(result)return reject();
 snprintf(path,sizeof(path),"/proc/%ld/fd",(long)pid);DIR *fds=opendir(path);if(!fds)return -1;
 while((e=readdir(fds))){
  char *end;long n=strtol(e->d_name,&end,10);if(!*e->d_name || *end || n<0 || n>2147483647)continue;
  snprintf(path,sizeof(path),"/proc/%ld/fd/%ld",(long)pid,n);pin=open(path,O_PATH|O_CLOEXEC);if(pin<0){result=-1;break;}
  struct stat st;int bad=fstat(pin,&st) || fs_pin(pin,&fs);close(pin);
  if(bad){result=-1;break;}
  if(S_ISCHR(st.st_mode) && major(st.st_rdev)==90){if(pid!=getpid() || (n!=own_mtd_fd && n!=own_pin) || minor(st.st_rdev)!=18){result=-1;break;}}
  if(!(fs.f_type==TMPFS_TYPE || fs.f_type==0x9fa0 || fs.f_type==0x62656572 || fs.f_type==0x1cd1 || fs.f_type==0x09041934 || fs.f_type==0x534f434b || fs.f_type==0x50495045)){result=-1;break;}
 }
 closedir(fds);return result?reject():0;
}
int mer_writer_live_guard(int own_mtd_fd,int own_pin,int allow_retained_jffs){
 if(geteuid()!=0 || getpid()<=1 || getppid()!=1 || (allow_retained_jffs!=0 && allow_retained_jffs!=1))return reject();
 if(ram_path("/",1) || mounted_flash(allow_retained_jffs))return -1;
 struct stat self_ns,parent_ns,self_root,parent_root;
 if(stat("/proc/self/ns/mnt",&self_ns) || stat("/proc/1/ns/mnt",&parent_ns) || stat("/",&self_root) || stat("/proc/1/root",&parent_root) || self_ns.st_dev!=parent_ns.st_dev || self_ns.st_ino!=parent_ns.st_ino || self_root.st_dev!=parent_root.st_dev || self_root.st_ino!=parent_root.st_ino)return reject();
 DIR *proc=opendir("/proc");if(!proc)return -1;struct dirent *e;int result=0;unsigned users=0;
 while((e=readdir(proc))){
  pid_t pid;if(!numeric(e->d_name,&pid))continue;char state;unsigned long flags;
  if(task_info(pid,&state,&flags)){if(errno==ENOENT || errno==ESRCH)continue;result=-1;break;}
  if((flags&0x00200000u) || state=='Z' || state=='X')continue;
  if((pid!=1 && pid!=getpid()) || survivor(pid,own_mtd_fd,own_pin)){result=-1;break;}users++;
 }
 closedir(proc);return !result && users==2?0:reject();
}
