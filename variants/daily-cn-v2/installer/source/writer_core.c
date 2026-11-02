/* Same allocation-free C transaction core for synthetic and actual OS backends. */
#include "writer_core.h"
#include "sha256.h"
#include <errno.h>
#include <string.h>
#include "protected_policy.h"
#define PREPARED 0x4d455731u
_Static_assert(MER_START%MER_ERASE==0 && MER_END%MER_ERASE==0,"fixed aligned range");
_Static_assert(MER_START+MER_IMAGE<=MER_END && MER_END<=MER_METADATA,"preserved metadata");
static int bad(int error){errno=error;return -1;}
int mer_geometry_ok(const struct mer_geometry *g){return g && g->bytes==MER_FLASH && g->erase==MER_ERASE && g->write==1 && g->oob==0 && g->type==3?0:bad(EINVAL);}
int mer_write_range(uint64_t offset,size_t bytes,int erase){
 if(!bytes || offset<MER_START || offset>MER_END || bytes>MER_END-offset)return bad(EINVAL);
 if(erase){if(bytes!=MER_ERASE || offset%MER_ERASE)return bad(EINVAL);}
 else if(bytes>MER_PAGE || offset/MER_PAGE!=(offset+bytes-1)/MER_PAGE)return bad(EINVAL);
 return 0;
}
int mer_read_exact(ssize_t (*fn)(void *,void *,size_t,uint64_t),void *ctx,void *buffer,size_t bytes,uint64_t offset,uint64_t limit){
 if(!fn || !buffer || !bytes || limit>MER_FLASH || offset>limit || bytes>limit-offset)return bad(EINVAL);
 size_t done=0;unsigned interrupts=0;
 while(done<bytes){
  ssize_t got=fn(ctx,(unsigned char *)buffer+done,bytes-done,offset+done);
  if(got<0){if(errno==EINTR && ++interrupts<=32)continue;return -1;}
  if(!got || (size_t)got>bytes-done)return bad(EIO);
  done+=(size_t)got;
 }return 0;
}
static int workspace_ok(const struct mer_workspace *w){return w && w->snapshot && w->desired && w->block && w->snapshot_bytes==MER_FLASH && w->desired_bytes==MER_REGION && w->block_bytes==MER_ERASE?0:bad(EINVAL);}
static int failure(struct mer_report *r,int error,const char *phase,uint64_t at){r->error=error?error:EIO;r->phase=phase;r->offset=at;return bad(r->error);}
static void before_hashes(struct mer_workspace *w,struct mer_report *r){
 mer_sha256(w->snapshot,MER_START,r->boot_before);
 mer_sha256(w->snapshot+MER_END,MER_FLASH-MER_END,r->rest_before);
 mer_sha256(w->snapshot+MER_METADATA,MER_FLASH-MER_METADATA,r->metadata_before);
}
int mer_prepare(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r){
 if(!r)return bad(EINVAL);
 memset(r,0,sizeof(*r));r->phase="precheck";
 if(workspace_ok(w) || !b || !b->read || !b->geometry)return failure(r,EINVAL,"workspace",0);
 w->prepared=0;struct mer_geometry g;
 if(b->geometry(b->ctx,&g) || mer_geometry_ok(&g))return failure(r,errno,"geometry",0);
 char hash[65];mer_sha256(w->desired,MER_IMAGE,hash);
 if(strcmp(hash,MER_IMAGE_SHA))return failure(r,EINVAL,"image-identity",0);
 for(uint64_t at=0;at<MER_FLASH;at+=MER_ERASE)
  if(mer_read_exact(b->read,b->ctx,w->snapshot+(size_t)at,MER_ERASE,at,MER_FLASH))return failure(r,errno,"snapshot-read",at);
 mer_sha256(w->snapshot+MER_START,MER_REGION,hash);
 if(strcmp(hash,MER_STOCK_SHA))return failure(r,EINVAL,"stock-region-identity",MER_START);
 for(unsigned i=0;i<sizeof(static_slices)/sizeof(static_slices[0]);i++){
  const struct mer_static_slice *s=&static_slices[i];mer_sha256(w->snapshot+s->start,s->end-s->start,hash);
  if(strcmp(hash,s->sha))return failure(r,EINVAL,s->name,s->start);
 }
 memcpy(w->desired+MER_IMAGE,w->snapshot+MER_START+MER_IMAGE,MER_REGION-MER_IMAGE);
 before_hashes(w,r);mer_sha256(w->desired,MER_REGION,r->desired_sha);
 if(strcmp(r->desired_sha,MER_DESIRED_SHA))return failure(r,EINVAL,"desired-region-identity",MER_START);
 w->prepared=PREPARED;r->phase="precheck-complete";return 0;
}
#if MER_CORE_EXECUTE
/* Read the entire untouched address set, compare every byte, and hash actual
 * returned data. Also report the complete metadata suffix, including dynamic
 * log/JFFS2 bytes against the current transaction baseline (not an old backup).
 */
static int guard_scan(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r,int post){
 r->guard_after_valid=0;r->guard_error=0;r->boot_after[0]=r->rest_after[0]=r->metadata_after[0]=0;
 if(post)r->guard_audited=1;
 struct mer_sha256_ctx boot,rest,meta;mer_sha256_init(&boot);mer_sha256_init(&rest);mer_sha256_init(&meta);int mismatch=0;
 for(unsigned range=0;range<2;range++){
  uint64_t start=range?MER_END:0,end=range?MER_FLASH:MER_START;
  for(uint64_t at=start;at<end;at+=MER_ERASE){
   if(mer_read_exact(b->read,b->ctx,w->block,MER_ERASE,at,MER_FLASH)){r->guard_error=errno?errno:EIO;r->guard_after_valid=0;return -1;}
   if(memcmp(w->block,w->snapshot+(size_t)at,MER_ERASE))mismatch=1;
   mer_sha256_update(range?&rest:&boot,w->block,MER_ERASE);
   if(at>=MER_METADATA)mer_sha256_update(&meta,w->block,MER_ERASE);
  }
 }
 mer_sha256_final(&boot,r->boot_after);mer_sha256_final(&rest,r->rest_after);mer_sha256_final(&meta,r->metadata_after);r->guard_after_valid=1;
 if(mismatch || strcmp(r->boot_before,r->boot_after) || strcmp(r->rest_before,r->rest_after) || strcmp(r->metadata_before,r->metadata_after)){r->guard_error=EIO;return bad(EIO);}
 r->guard_error=0;return 0;
}
static int target_scan(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r){
 r->target_audited=1;r->target_valid=0;r->target_equal=0;r->target_error=0;r->readback_sha[0]=0;
 struct mer_sha256_ctx hash;mer_sha256_init(&hash);int mismatch=0;
 for(uint64_t at=MER_START;at<MER_END;at+=MER_ERASE){
  if(mer_read_exact(b->read,b->ctx,w->block,MER_ERASE,at,MER_FLASH)){r->target_error=errno?errno:EIO;return -1;}
  if(memcmp(w->block,w->desired+(size_t)(at-MER_START),MER_ERASE))mismatch=1;
  mer_sha256_update(&hash,w->block,MER_ERASE);
 }
 mer_sha256_final(&hash,r->readback_sha);r->target_valid=1;
 r->target_equal=!mismatch && !strcmp(r->desired_sha,r->readback_sha);
 if(!r->target_equal){r->target_error=EIO;return bad(EIO);}return 0;
}
int mer_audit_failed_transaction(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r){
 if(!b || !b->read || !b->end || workspace_ok(w) || !r)return bad(EINVAL);
 int saved=r->error?r->error:EIO;
 if(r->backend_open){r->backend_open=0;if(b->end(b->ctx) && !r->close_error)r->close_error=errno?errno:EIO;}
 if(!r->target_audited)(void)target_scan(b,w,r);
 if(!r->guard_audited)(void)guard_scan(b,w,r,1);
 errno=saved;return -1;
}
static int finish_error(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r,int error,const char *phase,uint64_t at){
 failure(r,error,phase,at);return mer_audit_failed_transaction(b,w,r);
}
int mer_execute(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r){
 if(!r)return bad(EINVAL);
 if(workspace_ok(w) || !b || !b->geometry || !b->read || !b->begin || !b->erase || !b->write || !b->gate || !b->end || w->prepared!=PREPARED)return failure(r,EINVAL,"execute-contract",0);
 w->prepared=0; /* one shot: neither success nor partial failure can be resumed */
 r->target_audited=r->guard_audited=0;
 char hash[65];mer_sha256(w->desired,MER_IMAGE,hash);
 if(strcmp(hash,MER_IMAGE_SHA))return failure(r,EINVAL,"image-changed",0);
 mer_sha256(w->desired,MER_REGION,hash);
 if(strcmp(hash,r->desired_sha) || memcmp(w->desired+MER_IMAGE,w->snapshot+MER_START+MER_IMAGE,MER_REGION-MER_IMAGE))return failure(r,EINVAL,"tail-or-desired-changed",0);
 struct mer_geometry g;
 if(b->geometry(b->ctx,&g) || mer_geometry_ok(&g))return failure(r,errno,"geometry-changed",0);
 if(guard_scan(b,w,r,0))return failure(r,errno,"outside-changed-before-write",0);
 /* begin must close any newly opened RW alias on failure. Its close may still
  * invoke a kernel release callback, so refresh read-only guards even then. */
 if(b->begin(b->ctx))return finish_error(b,w,r,errno,"exclusive-RAM-preconditions",0);
 r->backend_open=1;
 for(uint64_t at=MER_START;at<MER_END;at+=MER_ERASE){
  size_t position=(size_t)(at-MER_START);r->offset=at;
  if(mer_write_range(at,MER_ERASE,1))return finish_error(b,w,r,errno,"erase-boundary",at);
  if(mer_read_exact(b->read,b->ctx,w->block,MER_ERASE,at,MER_FLASH))return finish_error(b,w,r,errno,"before-block-read",at);
  if(memcmp(w->block,w->snapshot+(size_t)at,MER_ERASE))return finish_error(b,w,r,EIO,"target-changed",at);
  if(b->gate(b->ctx,MER_BEFORE_ERASE,at))return finish_error(b,w,r,errno,"before-erase-gate",at);
  r->mutation_attempted=1;r->phase="erase";
  /* Never retry an erase, including EINTR: completion can be uncertain. */
  if(b->erase(b->ctx,at,MER_ERASE))return finish_error(b,w,r,errno,"erase",at);
  r->erased++;
  if(mer_read_exact(b->read,b->ctx,w->block,MER_ERASE,at,MER_FLASH))return finish_error(b,w,r,errno,"erased-read",at);
  for(size_t i=0;i<MER_ERASE;i++)if(w->block[i]!=0xff)return finish_error(b,w,r,EIO,"erase-not-FF",at);
  if(b->gate(b->ctx,MER_ERASED,at))return finish_error(b,w,r,errno,"erased-gate",at);
  for(size_t page=0;page<MER_ERASE;page+=MER_PAGE){
   size_t done=0;
   while(done<MER_PAGE){
    uint64_t dest=at+page+done;size_t bytes=MER_PAGE-done;
    if(mer_write_range(dest,bytes,0))return finish_error(b,w,r,errno,"write-boundary",dest);
    ssize_t count=b->write(b->ctx,w->desired+position+page+done,bytes,dest);
    /* A negative write can follow partial physical programming in this vendor
     * stack. Do not retry EINTR/errors and do not guess retlen or rollback. */
    if(count<0)return finish_error(b,w,r,errno,"write",dest);
    if(!count || (size_t)count>bytes)return finish_error(b,w,r,EIO,"write-progress",dest);
    done+=(size_t)count;
   }
  }
  if(mer_read_exact(b->read,b->ctx,w->block,MER_ERASE,at,MER_FLASH))return finish_error(b,w,r,errno,"block-readback",at);
  if(memcmp(w->block,w->desired+position,MER_ERASE))return finish_error(b,w,r,EIO,"block-mismatch",at);
  r->verified++;
  if(b->gate(b->ctx,MER_BLOCK_VERIFIED,at))return finish_error(b,w,r,errno,"block-verified-gate",at);
 }
 /* mtdchar release can call driver sync. Close before the final target AND
  * outside readback, retaining the independent RO alias for every check. */
 r->backend_open=0;
 if(b->end(b->ctx)){r->close_error=errno?errno:EIO;return finish_error(b,w,r,r->close_error,"backend-close",MER_END);}
 if(target_scan(b,w,r))return finish_error(b,w,r,r->target_error,r->target_valid?"final-region-mismatch":"final-region-read",MER_START);
 if(guard_scan(b,w,r,1))return failure(r,errno,"outside-changed-after-write",0);
 if(b->gate(b->ctx,MER_FINAL_VERIFIED,MER_END))return failure(r,errno,"final-status-gate",MER_END);
 r->phase="complete";r->offset=MER_END;r->error=0;return 0;
}
#endif
