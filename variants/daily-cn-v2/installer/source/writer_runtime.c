/* Linked exclusively into the explicit WRITE_REVIEW profile. */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "stage_policy.h"
#if !MER_PROFILE_WRITE || !MER_CORE_EXECUTE
#error "Write runtime requires the separate review profile"
#endif
#include "writer_runtime.h"
#include "writer_flow.h"
#include "handover_ops.h"
#include "live_guard.h"
#include "status.h"
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
struct os {int ro,pin,rw;const struct mer_watchdog *wdt;struct mer_traced_io trace;struct mer_writer_flow flow;};
static int bad(int e){errno=e;return -1;}
static int checked_open(const char *path,int device,int *pinned){
 int pin=open(path,O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;struct stat a,b;
 if(fstat(pin,&a) || (device?(!S_ISCHR(a.st_mode) || major(a.st_rdev)!=90 || minor(a.st_rdev)!=18):(!S_ISREG(a.st_mode) || a.st_size!=MER_IMAGE))){close(pin);return bad(EINVAL);}
 char name[64];snprintf(name,sizeof(name),"/proc/self/fd/%d",pin);int fd=open(name,O_RDONLY|O_CLOEXEC);int saved=errno;
 if(fd<0){close(pin);errno=saved;return -1;}
 struct statfs fs;
 if(fstat(fd,&b) || a.st_dev!=b.st_dev || a.st_ino!=b.st_ino || (!device && (fstatfs(fd,&fs) || fs.f_type!=0x01021994 || !(fs.f_flags&ST_RDONLY)))){close(fd);close(pin);return bad(ESTALE);}
 if(pinned)*pinned=pin;else close(pin);return fd;
}
static void *reserve(size_t n){unsigned char *p=malloc(n);if(!p)return NULL;volatile unsigned char *v=p;for(size_t i=0;i<n;i+=4096)v[i]=0;v[n-1]=0;return p;}
static ssize_t file_read(void *ctx,void *buf,size_t n,uint64_t at){return pread(*(int *)ctx,buf,n,(off_t)at);}
static ssize_t os_read(void *ctx,void *buf,size_t n,uint64_t at){return file_read(&((struct os *)ctx)->ro,buf,n,at);}
static int geometry_fd(int fd,struct mer_geometry *g,int write){struct mtd_info_user i;memset(&i,0,sizeof(i));if(ioctl(fd,MEMGETINFO,&i))return -1;if(write && !(i.flags&MTD_WRITEABLE))return bad(EROFS);*g=(struct mer_geometry){i.size,i.erasesize,i.writesize,i.oobsize,i.type};return mer_geometry_ok(g);}
static int geometry(void *ctx,struct mer_geometry *g){return geometry_fd(((struct os *)ctx)->ro,g,0);}
static int raw_close(void *ctx){struct os *o=ctx;int fd=o->rw;o->rw=-1;return fd<0?bad(EBADF):close(fd);}
static int begin(void *ctx){
 struct os *o=ctx;
 if(mer_status_stage(MER_PH_WR_READY,*o->flow.checks) || mer_writer_live_guard(o->ro,o->pin,0) || mer_ping_existing_watchdog(o->wdt))return -1;
 if(o->wdt->mode==MER_WD_TMR1_NOT_RESET){struct mer_watchdog fresh;if(mer_prepare_watchdog_state(-1,&fresh))return -1;}
 char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",o->pin);o->rw=open(path,O_RDWR|O_CLOEXEC);if(o->rw<0)return -1;
 struct stat a,b;struct mer_geometry g;
 if(fstat(o->pin,&a) || fstat(o->rw,&b) || a.st_dev!=b.st_dev || a.st_ino!=b.st_ino || !S_ISCHR(b.st_mode) || major(b.st_rdev)!=90 || minor(b.st_rdev)!=18 || geometry_fd(o->rw,&g,1)){
  int saved=errno?errno:EINVAL;(void)mer_traced_close(&o->trace);errno=saved;return -1;
 }
 return 0;
}
static int raw_erase(void *ctx,uint64_t at,size_t n){struct os *o=ctx;if(o->rw<0 || mer_write_range(at,n,1))return bad(EINVAL);struct erase_info_user e={(uint32_t)at,(uint32_t)n};return ioctl(o->rw,MEMERASE,&e);}
static ssize_t raw_program(void *ctx,const void *p,size_t n,uint64_t at){struct os *o=ctx;if(o->rw<0 || mer_write_range(at,n,0))return bad(EINVAL);return pwrite(o->rw,p,n,(off_t)at);}
static int erase(void *ctx,uint64_t at,size_t n){return mer_traced_erase(&((struct os *)ctx)->trace,at,n);}
static ssize_t program(void *ctx,const void *p,size_t n,uint64_t at){return mer_traced_program(&((struct os *)ctx)->trace,p,n,at);}
static int gate(void *ctx,enum mer_event event,uint64_t at){struct os *o=ctx;if(mer_ping_existing_watchdog(o->wdt))return -1;return mer_writer_gate(&o->flow,event,at);}
static int end(void *ctx){struct os *o=ctx;int result=mer_traced_close(&o->trace),saved=errno;struct mer_writer_payload p={0};int stop=0;(void)mer_writer_notify(&o->flow,MW_AUDIT_BEGIN,&p,&stop);errno=saved;return result;}
int mer_run_fixed_writer(const struct mer_watchdog *wdt,unsigned *checks){
 if(!wdt || !checks || *checks!=MER_PREPARATION_CHECKS || mer_status_stage(MER_PH_WR_PREPARE,*checks))return -1;
 struct mer_workspace w={reserve(MER_FLASH),reserve(MER_REGION),reserve(MER_ERASE),MER_FLASH,MER_REGION,MER_ERASE,0};
 struct mer_report r={0};struct os o={.ro=-1,.pin=-1,.rw=-1,.wdt=wdt};int fd=-1,result=-1,saved=ENOMEM;
 o.flow=(struct mer_writer_flow){&r,&o.trace,checks,0};o.trace=(struct mer_traced_io){.raw_ctx=&o,.notify_ctx=&o.flow,.erase=raw_erase,.program=raw_program,.close_rw=raw_close,.notify=mer_writer_notify};
 if(!w.snapshot || !w.desired || !w.block)goto done;
 fd=checked_open("/mer1200g-v2-window.bin",0,NULL);if(fd<0){saved=errno;goto done;}
 if(mer_read_exact(file_read,&fd,w.desired,MER_IMAGE,0,MER_IMAGE)){saved=errno;goto done;}
 if(close(fd)){fd=-1;saved=errno;goto done;}fd=-1;
 o.ro=checked_open("/dev/mtd9",1,&o.pin);if(o.ro<0){saved=errno;goto done;}
 if(mer_writer_live_guard(o.ro,o.pin,0)){saved=errno;goto done;}
 struct mer_backend b={&o,os_read,geometry,begin,erase,program,gate,end};
 result=mer_writer_transaction(&b,&w,&r,&o.flow);saved=errno;
done:
 if(fd>=0 && close(fd) && !result){result=-1;saved=errno;}
 if(o.rw>=0){(void)end(&o);result=-1;}
 if(o.ro>=0 && close(o.ro) && !result){result=-1;saved=errno;}
 if(o.pin>=0 && close(o.pin) && !result){result=-1;saved=errno;}
 free(w.snapshot);free(w.desired);free(w.block);errno=saved;return result;
}
