/* Actual writer_core.c with memory-only callbacks; no device/ioctl backend. */
#include "writer_core.h"
#include "sha256.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum fault {NONE,READ_SHORT,READ_EINTR_ONCE,READ_EINTR_ALWAYS,READ_ZERO,READ_OVER,
 WRITE_SHORT,WRITE_EINTR,WRITE_ZERO,WRITE_OVER,WRITE_EIO,WRITE_PARTIAL_ERROR,
 ERASE_EINTR,ERASE_EIO,ERASE_NOT_FF,ERASE_SECOND_FAIL,ERASED_READ_FAIL,
 BLOCK_READ_FAIL,BLOCK_MISMATCH,FINAL_READ_FAIL,FINAL_MISMATCH,OUTSIDE_LATE,
 GATE_BEFORE,GATE_ERASED,GATE_BLOCK,GATE_FINAL,BEGIN_FAIL,GEOMETRY_CHANGED,END_FAIL,GUARD_READ_FAIL,WRITE_AND_GUARD_FAIL,END_TARGET_CORRUPT};
struct fake {unsigned char *data;enum fault fault;unsigned reads,writes,erases,geometries,begins,events,ends;int interrupted;enum mer_event event;};
static ssize_t fake_read(void *ctx,void *buf,size_t n,uint64_t at){
 struct fake *f=ctx;assert(at<=MER_FLASH && n<=MER_FLASH-at);f->reads++;
 if(f->fault==READ_EINTR_ALWAYS || (f->fault==READ_EINTR_ONCE && !f->interrupted++)){errno=EINTR;return -1;}
 if(f->fault==READ_ZERO)return 0;
 if(f->fault==READ_OVER)return (ssize_t)n+1;
 if(f->fault==READ_SHORT && n>1377)n=1377;
 if(f->writes && (at<MER_START || at>=MER_END) && (f->fault==GUARD_READ_FAIL || f->fault==WRITE_AND_GUARD_FAIL)){errno=EIO;return -1;}
 if(at>=MER_START && at<MER_END){
  if(f->fault==ERASED_READ_FAIL && f->erases && !f->writes){errno=EIO;return -1;}
  if(f->fault==BLOCK_READ_FAIL && f->writes){errno=EIO;return -1;}
  if(f->fault==FINAL_READ_FAIL && f->event==MER_BLOCK_VERIFIED && f->erases==207){errno=EIO;return -1;}
 }
 memcpy(buf,f->data+(size_t)at,n);
 if(f->fault==BLOCK_MISMATCH && f->writes && at>=MER_START && at<MER_END)((unsigned char *)buf)[0]^=1;
 return (ssize_t)n;
}
static int fake_geometry(void *ctx,struct mer_geometry *g){struct fake *f=ctx;f->geometries++;*g=(struct mer_geometry){MER_FLASH,MER_ERASE,1,0,3};if(f->fault==GEOMETRY_CHANGED && f->geometries>1)g->erase/=2;return 0;}
static int fake_begin(void *ctx){struct fake *f=ctx;f->begins++;if(f->fault==BEGIN_FAIL){errno=EBUSY;return -1;}return 0;}
static int fake_end(void *ctx){struct fake *f=ctx;f->ends++;if(f->fault==END_TARGET_CORRUPT)f->data[MER_START+17]^=1;if(f->fault==END_FAIL){errno=EIO;return -1;}return 0;}
static int fake_erase(void *ctx,uint64_t at,size_t n){
 struct fake *f=ctx;assert(!mer_write_range(at,n,1));f->erases++;
 if(f->fault==ERASE_SECOND_FAIL && f->erases==2){errno=EIO;return -1;}
 if(f->fault==ERASE_EIO){errno=EIO;return -1;}
 memset(f->data+(size_t)at,0xff,n);
 if(f->fault==ERASE_EINTR){errno=EINTR;return -1;}
 if(f->fault==ERASE_NOT_FF)f->data[(size_t)at+17]=0;
 return 0;
}
static ssize_t fake_write(void *ctx,const void *buf,size_t n,uint64_t at){
 struct fake *f=ctx;assert(!mer_write_range(at,n,0));f->writes++;
 if(f->fault==WRITE_EINTR){errno=EINTR;return -1;}
 if(f->fault==WRITE_ZERO)return 0;
 if(f->fault==WRITE_OVER)return (ssize_t)n+1;
 if(f->fault==WRITE_EIO){errno=EIO;return -1;}
 if(f->fault==WRITE_AND_GUARD_FAIL){errno=ENOSPC;return -1;}
 if(f->fault==WRITE_PARTIAL_ERROR){memcpy(f->data+(size_t)at,buf,n>7?7:n);errno=EIO;return -1;}
 if(f->fault==WRITE_SHORT && n>63)n=63;
 const unsigned char *p=buf;for(size_t i=0;i<n;i++){assert((f->data[(size_t)at+i]&p[i])==p[i]);f->data[(size_t)at+i]&=p[i];}
 if(f->fault==OUTSIDE_LATE && !f->interrupted++){f->data[MER_METADATA+5]^=1;}
 return (ssize_t)n;
}
static int fake_gate(void *ctx,enum mer_event event,uint64_t at){
 struct fake *f=ctx;f->events++;f->event=event;
 if((f->fault==GATE_BEFORE && event==MER_BEFORE_ERASE) || (f->fault==GATE_ERASED && event==MER_ERASED) || (f->fault==GATE_BLOCK && event==MER_BLOCK_VERIFIED) || (f->fault==GATE_FINAL && event==MER_FINAL_VERIFIED)){errno=ENETUNREACH;return -1;}
 if(f->fault==FINAL_MISMATCH && event==MER_BLOCK_VERIFIED && at==MER_END-MER_ERASE)f->data[MER_START+5]^=1;
 return 0;
}
static unsigned char *load(const char *path,size_t expected){FILE *file=fopen(path,"rb");assert(file);unsigned char *p=malloc(expected);assert(p && fread(p,1,expected,file)==expected && fgetc(file)==EOF);assert(!fclose(file));return p;}
static void reset(struct fake *f,struct mer_workspace *w,const unsigned char *original,const unsigned char *image,enum fault fault){
 unsigned char *data=f->data;memset(f,0,sizeof(*f));f->data=data;f->fault=fault;memcpy(data,original,MER_FLASH);memcpy(w->desired,image,MER_IMAGE);w->prepared=0;
}
static void success_checks(struct fake *f,struct mer_workspace *w,struct mer_report *r,const unsigned char *original,const unsigned char *image){
 assert(f->erases==207 && f->ends==1 && r->erased==207 && r->verified==207 && r->mutation_attempted && r->guard_after_valid && !r->error && !r->guard_error);
 assert(r->target_audited && r->guard_audited && r->target_valid && r->target_equal && !r->target_error && !r->close_error);
 assert(!memcmp(f->data,original,MER_START));assert(!memcmp(f->data+MER_START,image,MER_IMAGE));
 assert(!memcmp(f->data+MER_START+MER_IMAGE,w->snapshot+MER_START+MER_IMAGE,MER_REGION-MER_IMAGE));
 assert(!memcmp(f->data+MER_END,w->snapshot+MER_END,MER_FLASH-MER_END));assert(!strcmp(r->desired_sha,r->readback_sha));
 assert(!strcmp(r->boot_before,r->boot_after) && !strcmp(r->rest_before,r->rest_after) && !strcmp(r->metadata_before,r->metadata_after));
}
int main(int argc,char **argv){
 assert(argc==3 || (argc==4 && !strcmp(argv[3],"--nominal-only")));unsigned char *original=load(argv[1],MER_FLASH),*image=load(argv[2],MER_IMAGE);char h[65];mer_sha256(original,MER_FLASH,h);assert(!strcmp(h,MER_BACKUP_SHA));
 struct mer_workspace w={malloc(MER_FLASH),malloc(MER_REGION),malloc(MER_ERASE),MER_FLASH,MER_REGION,MER_ERASE,0};assert(w.snapshot && w.desired && w.block);
 struct fake f={.data=malloc(MER_FLASH)};assert(f.data);struct mer_backend b={&f,fake_read,fake_geometry,fake_begin,fake_erase,fake_write,fake_gate,fake_end};struct mer_report r;unsigned cases=0;
 /* Hash streaming pad boundaries and split updates use independently known
  * FIPS vectors; fixed candidate/backup identities above exercise real data. */
 mer_sha256("",0,h);assert(!strcmp(h,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
 struct mer_sha256_ctx hash;mer_sha256_init(&hash);mer_sha256_update(&hash,"a",1);mer_sha256_update(&hash,"bc",2);mer_sha256_final(&hash,h);assert(!strcmp(h,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));cases++;
 assert(!mer_write_range(MER_START,MER_ERASE,1) && !mer_write_range(MER_END-MER_ERASE,MER_ERASE,1));
 assert(mer_write_range(MER_START-1,MER_ERASE,1)<0 && mer_write_range(MER_END,1,0)<0 && mer_write_range(UINT64_MAX-1,4,0)<0 && mer_write_range(MER_START,0,0)<0 && mer_write_range(MER_START,MER_ERASE*2,1)<0 && mer_write_range(MER_START+1,MER_ERASE,1)<0 && mer_write_range(MER_START+255,2,0)<0 && mer_write_range(MER_START,257,0)<0);cases++;
 if(argc==4){reset(&f,&w,original,image,NONE);assert(!mer_prepare(&b,&w,&r) && !mer_execute(&b,&w,&r));success_checks(&f,&w,&r,original,image);puts("PASS actual MIPSEL same-C-core nominal fake transaction,207 bounded erase blocks, full readback/untouched guards andzero-byte tail; no device backend linked");goto done;}
 const enum fault prepare_fail[]={READ_EINTR_ALWAYS,READ_ZERO,READ_OVER};
 for(unsigned i=0;i<sizeof(prepare_fail)/sizeof(prepare_fail[0]);i++){reset(&f,&w,original,image,prepare_fail[i]);assert(mer_prepare(&b,&w,&r)<0 && !f.erases && !f.writes);if(prepare_fail[i]==READ_EINTR_ALWAYS)assert(f.reads==33);cases++;}
 reset(&f,&w,original,image,NONE);w.desired[10]^=1;assert(mer_prepare(&b,&w,&r)<0 && !f.reads);cases++;
 const uint32_t protected_offsets[]={MER_START+3,2,MER_METADATA+3,0xd40004,0xd50005,0xff0006};
 for(unsigned i=0;i<sizeof(protected_offsets)/sizeof(protected_offsets[0]);i++){reset(&f,&w,original,image,NONE);f.data[protected_offsets[i]]^=1;assert(mer_prepare(&b,&w,&r)<0 && !f.erases && !f.writes);cases++;}
 const enum fault passing[]={NONE,READ_SHORT,READ_EINTR_ONCE,WRITE_SHORT};
 for(unsigned i=0;i<sizeof(passing)/sizeof(passing[0]);i++){
  reset(&f,&w,original,image,passing[i]);assert(!mer_prepare(&b,&w,&r) && !mer_execute(&b,&w,&r));success_checks(&f,&w,&r,original,image);
  unsigned erases=f.erases,writes=f.writes;assert(mer_execute(&b,&w,&r)<0 && f.erases==erases && f.writes==writes);cases++;
 }
 /* Dynamic log and JFFS2 are protected against this transaction baseline,
  * not incorrectly required to equal a historical backup. */
 const uint32_t dynamic[]={0xd60009,0xdf0011,0xd80022};
 for(unsigned i=0;i<sizeof(dynamic)/sizeof(dynamic[0]);i++){reset(&f,&w,original,image,NONE);f.data[dynamic[i]]^=1;assert(!mer_prepare(&b,&w,&r) && !mer_execute(&b,&w,&r));success_checks(&f,&w,&r,original,image);cases++;}
 const enum fault exec_fail[]={WRITE_EINTR,WRITE_ZERO,WRITE_OVER,WRITE_EIO,WRITE_PARTIAL_ERROR,ERASE_EINTR,ERASE_EIO,ERASE_NOT_FF,ERASE_SECOND_FAIL,ERASED_READ_FAIL,BLOCK_READ_FAIL,BLOCK_MISMATCH,FINAL_READ_FAIL,FINAL_MISMATCH,OUTSIDE_LATE,GATE_BEFORE,GATE_ERASED,GATE_BLOCK,GATE_FINAL,BEGIN_FAIL,GEOMETRY_CHANGED,END_FAIL,GUARD_READ_FAIL,WRITE_AND_GUARD_FAIL,END_TARGET_CORRUPT};
 for(unsigned i=0;i<sizeof(exec_fail)/sizeof(exec_fail[0]);i++){
  reset(&f,&w,original,image,NONE);assert(!mer_prepare(&b,&w,&r));f.fault=exec_fail[i];int result=mer_execute(&b,&w,&r);if(result>=0 || !r.error)fprintf(stderr,"Unexpected acceptance fault=%u phase=%s\n",(unsigned)exec_fail[i],r.phase);assert(result<0 && r.error);
  assert(!memcmp(f.data,original,MER_START));
  if(exec_fail[i]!=OUTSIDE_LATE)assert(!memcmp(f.data+MER_END,original+MER_END,MER_FLASH-MER_END));
  if(exec_fail[i]==WRITE_EINTR || exec_fail[i]==WRITE_ZERO || exec_fail[i]==WRITE_OVER || exec_fail[i]==WRITE_EIO || exec_fail[i]==WRITE_PARTIAL_ERROR)assert(f.erases==1 && f.writes==1);
  if(exec_fail[i]==ERASE_EINTR || exec_fail[i]==ERASE_EIO || exec_fail[i]==ERASE_NOT_FF || exec_fail[i]==ERASED_READ_FAIL || exec_fail[i]==GATE_ERASED)assert(f.erases==1 && !f.writes);
  if(exec_fail[i]==BEGIN_FAIL || exec_fail[i]==GEOMETRY_CHANGED || exec_fail[i]==GATE_BEFORE)assert(!f.erases && !f.writes);
  if(exec_fail[i]==ERASE_SECOND_FAIL)assert(f.erases==2 && r.verified==1);
  if(exec_fail[i]==OUTSIDE_LATE)assert(r.guard_after_valid && r.guard_error && strcmp(r.metadata_before,r.metadata_after));
  if(exec_fail[i]==GUARD_READ_FAIL)assert(!r.guard_after_valid && r.guard_error==EIO);
  if(exec_fail[i]==GUARD_READ_FAIL || exec_fail[i]==WRITE_AND_GUARD_FAIL)assert(!r.boot_after[0] && !r.rest_after[0] && !r.metadata_after[0]);
  if(exec_fail[i]==WRITE_PARTIAL_ERROR)assert(r.target_valid && !r.target_equal && r.target_error==EIO && r.target_audited && r.guard_audited && r.readback_sha[0] && strcmp(r.desired_sha,r.readback_sha));
  if(exec_fail[i]==END_FAIL)assert(r.close_error==EIO && r.target_valid && r.guard_audited);
  if(exec_fail[i]==WRITE_AND_GUARD_FAIL)assert(!r.guard_after_valid && r.guard_error==EIO && r.error==ENOSPC && f.erases==1 && f.writes==1);
  if(f.begins && exec_fail[i]!=BEGIN_FAIL)assert(f.ends==1);
  if(exec_fail[i]==END_TARGET_CORRUPT)assert(!strcmp(r.phase,"final-region-mismatch") && r.erased==207 && r.verified==207);
  cases++;
 }
 for(unsigned change=0;change<4;change++){
  reset(&f,&w,original,image,NONE);assert(!mer_prepare(&b,&w,&r));
  if(change==0)f.data[MER_END+7]^=1;else if(change==1)f.data[MER_START+9]^=1;else if(change==2)w.desired[MER_IMAGE-1]^=1;else w.desired[9]^=1;
  assert(mer_execute(&b,&w,&r)<0 && !f.erases && !f.writes);cases++;
 }
 reset(&f,&w,original,image,NONE);w.block_bytes--;assert(mer_prepare(&b,&w,&r)<0 && !f.reads);w.block_bytes++;cases++;
 struct mer_geometry g={MER_FLASH,MER_ERASE,1,0,3};assert(!mer_geometry_ok(&g));
 for(unsigned i=0;i<5;i++){struct mer_geometry wrong=g;if(i==0)wrong.bytes--;if(i==1)wrong.erase/=2;if(i==2)wrong.write=2;if(i==3)wrong.oob=1;if(i==4)wrong.type=4;assert(mer_geometry_ok(&wrong)<0);cases++;}
 assert(mer_read_exact(fake_read,&f,w.block,1,UINT64_MAX,MER_FLASH)<0 && mer_read_exact(fake_read,&f,w.block,2,MER_FLASH-1,MER_FLASH)<0);cases++;
 printf("PASS %u same-C-core cases with authenticated full backup and exact V3 image; all fake erase/program offsets bounded; tail0; no real MTD backend linked\n",cases);
done:
 free(original);free(image);free(f.data);free(w.snapshot);free(w.desired);free(w.block);return 0;
}
