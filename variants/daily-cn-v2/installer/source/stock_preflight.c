/* SPDX-License-Identifier: MIT
 * Streaming stock preflight: 64KiB buffer, regular image and fixed MTD RO only.
 */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "writer_core.h"
#include "sha256.h"
#include "protected_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <unistd.h>
static int digest(int fd,uint64_t at,size_t length,const char *expected){
 unsigned char block[65536];struct mer_sha256_ctx c;mer_sha256_init(&c);
 while(length){size_t n=length>sizeof(block)?sizeof(block):length,done=0;
  while(done<n){ssize_t r=pread(fd,block+done,n-done,(off_t)(at+done));if(r<0 && errno==EINTR)continue;if(r<=0 || (size_t)r>n-done)return -1;done+=(size_t)r;}
  mer_sha256_update(&c,block,n);at+=n;length-=n;
 }
 char result[65];mer_sha256_final(&c,result);return strcmp(result,expected)?-1:0;
}
int main(int argc,char **argv){
 if(argc==2 && !strcmp(argv[1],"--build-info")){puts("MER1200G v2 streaming read-only preflight; scratch=65536; image=13565952; no erase/write backend");return 0;}
 int offline=argc==3 && !strcmp(argv[1],"--check-image");
 if(!offline && !(argc==5 && !strcmp(argv[1],"--image") && !strcmp(argv[3],"--mtd") && !strcmp(argv[4],"/dev/mtd9")))return 78;
 const char *path=argv[2];int image=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
 if(image<0 || fstat(image,&st) || !S_ISREG(st.st_mode) || st.st_size!=MER_IMAGE || digest(image,0,MER_IMAGE,MER_IMAGE_SHA)){if(image>=0)close(image);fputs("REJECT image\n",stderr);return 1;}
 if(close(image))return 1;
 if(offline){puts("PASS image identity; no device opened");return 0;}
 struct utsname u;if(uname(&u) || strncmp(u.release,"3.10.",5)){fputs("REJECT stock kernel family\n",stderr);return 1;}
 int fd=open("/dev/mtd9",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct mtd_info_user info;
 if(fd<0 || fstat(fd,&st) || !S_ISCHR(st.st_mode) || major(st.st_rdev)!=90 || minor(st.st_rdev)!=18 || ioctl(fd,MEMGETINFO,&info) || info.type!=MTD_NORFLASH || info.size!=MER_FLASH || info.erasesize!=MER_ERASE || info.writesize!=1 || info.oobsize){if(fd>=0)close(fd);fputs("REJECT stock NOR geometry\n",stderr);return 1;}
 int result=digest(fd,MER_START,MER_REGION,MER_STOCK_SHA);
 for(unsigned i=0;!result && i<sizeof(static_slices)/sizeof(static_slices[0]);i++)result=digest(fd,static_slices[i].start,static_slices[i].end-static_slices[i].start,static_slices[i].sha);
 if(close(fd))result=-1;
 puts(result?"REJECT stock identity":"PASS image, stock immutable range and protected identities; read-only");return result?1:0;
}
