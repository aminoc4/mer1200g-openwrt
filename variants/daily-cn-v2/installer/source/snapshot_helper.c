/* Default read-only frontend. Actual mutation is a separate compile profile. */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#ifndef MER_OS_WRITE
#define MER_OS_WRITE 0
#endif
#if MER_OS_WRITE != 0 && MER_OS_WRITE != 1
#error "Invalid OS profile"
#endif
#if MER_OS_WRITE && !MER_CORE_EXECUTE
#error "Explicit OS write profile requires separately compiled mutation core"
#endif
#include "writer_core.h"
#include "sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mtd/mtd-user.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
struct os_backend {int ro,pin,rw;};
static int reject(int error){errno=error;return -1;}
static int checked_open(const char *path,int device,int *pinned){
 int pin=open(path,O_PATH|O_NOFOLLOW|O_CLOEXEC);if(pin<0)return -1;struct stat before,after;
 if(fstat(pin,&before) || (device?(!S_ISCHR(before.st_mode) || major(before.st_rdev)!=90 || minor(before.st_rdev)!=18):(!S_ISREG(before.st_mode) || before.st_size!=MER_IMAGE))){close(pin);return reject(EINVAL);}
 char name[64];snprintf(name,sizeof(name),"/proc/self/fd/%d",pin);int fd=open(name,O_RDONLY|O_CLOEXEC);int saved=errno;
 if(fd<0){close(pin);errno=saved;return -1;}
 if(fstat(fd,&after) || before.st_dev!=after.st_dev || before.st_ino!=after.st_ino){close(fd);close(pin);return reject(ESTALE);}
 if(pinned)*pinned=pin;else close(pin);return fd;
}
static void *reserve(size_t n){unsigned char *p=malloc(n);if(!p)return NULL;volatile unsigned char *v=p;for(size_t i=0;i<n;i+=4096)v[i]=0;v[n-1]=0;return p;}
static ssize_t file_read(void *ctx,void *buf,size_t n,uint64_t at){if(at>INT64_MAX)return reject(EOVERFLOW);return pread(*(int *)ctx,buf,n,(off_t)at);}
static ssize_t os_read(void *ctx,void *buf,size_t n,uint64_t at){struct os_backend *o=ctx;return file_read(&o->ro,buf,n,at);}
static int info_geometry(int fd,struct mer_geometry *g,int writable){
 struct mtd_info_user info;memset(&info,0,sizeof(info));if(ioctl(fd,MEMGETINFO,&info))return -1;
 if(writable && !(info.flags&MTD_WRITEABLE))return reject(EROFS);
 *g=(struct mer_geometry){info.size,info.erasesize,info.writesize,info.oobsize,info.type};return mer_geometry_ok(g);
}
static int os_geometry(void *ctx,struct mer_geometry *g){struct os_backend *o=ctx;return info_geometry(o->ro,g,0);}
static void report(const struct mer_report *r){
 printf("MER-WRITER-RESULT phase=%s error=%d offset=%08llx attempted=%d erased=%u verified=%u guard_valid=%d guard_error=%d\n",r->phase?r->phase:"none",r->error,(unsigned long long)r->offset,r->mutation_attempted,r->erased,r->verified,r->guard_after_valid,r->guard_error);
 printf("GUARD boot_before=%s boot_after=%s rest_before=%s rest_after=%s metadata_before=%s metadata_after=%s\n",r->boot_before,r->boot_after,r->rest_before,r->rest_after,r->metadata_before,r->metadata_after);
 printf("REGION desired=%s readback=%s; allocated=%u tail=%u; no reboot/rollback stage\n",r->desired_sha,r->readback_sha,(unsigned)MER_ALLOCATION,(unsigned)(MER_REGION-MER_IMAGE));
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
 if(argc==1 || (argc==2 && (!strcmp(argv[1],"--help") || !strcmp(argv[1],"--build-info")))){
  puts("mer-fixed-writer --offline-image-only --image FILE | --preflight --image FILE\nFixed MER1200G v2 window; whole-chip read-only snapshot; only target[0x40000,0xd30000); no arbitrary range/device. Default OS profile is read-only.");
  return 0;
 }
 int execute=0,offline=0;const char *image=NULL;
 if(argc==4 && !strcmp(argv[2],"--image") && (!strcmp(argv[1],"--offline-image-only") || !strcmp(argv[1],"--preflight"))){image=argv[3];offline=!strcmp(argv[1],"--offline-image-only");}
 else {fputs("REJECTED option/profile; no access attempted\n",stderr);return 78;}
 struct mer_workspace w={reserve(MER_FLASH),reserve(MER_REGION),reserve(MER_ERASE),MER_FLASH,MER_REGION,MER_ERASE,0};
 struct mer_report result;memset(&result,0,sizeof(result));struct os_backend os={.ro=-1,.pin=-1,.rw=-1};int fd=-1,rc=1;
 if(!w.snapshot || !w.desired || !w.block){errno=ENOMEM;perror("preallocation");goto done;}
 fd=checked_open(image,0,NULL);if(fd<0){perror("regular candidate");goto done;}
 if(execute){struct statfs fs;if(fstatfs(fd,&fs) || fs.f_type!=0x01021994 || !(fs.f_flags&ST_RDONLY)){errno=EINVAL;perror("candidate requires RO tmpfs");goto done;}}
 if(mer_read_exact(file_read,&fd,w.desired,MER_IMAGE,0,MER_IMAGE)){perror("candidate read");goto done;}
 char hash[65];mer_sha256(w.desired,MER_IMAGE,hash);if(strcmp(hash,MER_IMAGE_SHA)){errno=EINVAL;perror("candidate identity");goto done;}
 if(close(fd)){fd=-1;perror("candidate close");goto done;}fd=-1;
 if(offline){puts("PASS exact v2 window identity; no MTD opened; all user buffers preallocated/touched; no mutation");rc=0;goto done;}
 os.ro=checked_open("/dev/mtd9",1,&os.pin);if(os.ro<0){perror("whole-chip MTD RO");goto done;}
 struct mer_backend backend={.ctx=&os,.read=os_read,.geometry=os_geometry};
 if(mer_prepare(&backend,&w,&result)){report(&result);goto done;}
 report(&result);puts(execute?"PASS fixed write/readback/guards only; no reboot or bootability/recovery acceptance":"PASS read-only precheck; not permission to write or evidence of device exclusivity");rc=0;
done:
 if(fd>=0)close(fd);
 if(os.ro>=0 && close(os.ro))rc=1;
 if(os.pin>=0)close(os.pin);
 free(w.snapshot);free(w.desired);free(w.block);return rc;
}
