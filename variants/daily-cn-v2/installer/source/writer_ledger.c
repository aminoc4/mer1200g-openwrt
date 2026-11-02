#include "writer_ledger.h"
#include "writer_core.h"
#include "stage_policy.h"
#include <errno.h>
#include <string.h>
static int bad(void){errno=EPROTO;return -1;}
static int hex(const char h[65]){for(unsigned i=0;i<64;i++)if(!((h[i]>='0' && h[i]<='9') || (h[i]>='a' && h[i]<='f')))return 0;return !h[64];}
static int label(const char text[32]){for(unsigned i=0;i<32;i++){unsigned char c=(unsigned char)text[i];if(!c)return 1;if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_'))return 0;}return 0;}
int mer_writer_ledger_apply(struct mer_writer_ledger *l,unsigned kind,unsigned phase,const struct mer_writer_payload *p,const struct mer_snapshot_report *h){
 if(!l || !p || !label(p->label))return bad();
 if((kind==MW_BASELINE && phase!=81) || (kind==MW_IO_BEFORE && phase!=83) || (kind==MW_IO_RETURN && phase!=84) || (kind==MW_CLOSE_BEFORE && phase!=85) || (kind==MW_CLOSE_RETURN && phase!=86) || (kind==MW_AUDIT_BEGIN && phase!=87) || (kind==MW_REPORT && phase!=89))return bad();
 if(kind==MW_BASELINE){
  if(l->baseline || !h || mer_snapshot_valid(h) || p->core_error || p->attempted || p->erased || p->verified)return bad();
  l->before=*h;l->baseline=1;return 0;
 }
 if(!l->baseline)return bad();
 if(kind==MW_IO_BEFORE){
  if(l->pending || l->close_pending || l->closed || l->verified>=207 || p->id!=l->id+1 || (p->op!=MW_ERASE && p->op!=MW_PROGRAM) || mer_write_range(p->offset,p->bytes,p->op==MW_ERASE))return bad();
  uint32_t expected=MER_START+l->verified*MER_ERASE;
  if(p->op==MW_ERASE){if(p->offset!=expected || l->erase_ok!=l->verified)return bad();}
  else if(!l->erase_ff || l->erase_ok!=l->verified+1 || p->offset!=l->block_start+l->in_block || p->bytes>MER_ERASE-l->in_block)return bad();
  l->id=p->id;l->last_io=*p;l->pending=1;return 0;
 }
 if(kind==MW_IO_RETURN){
  if(!l->pending || p->id!=l->last_io.id || p->op!=l->last_io.op || p->offset!=l->last_io.offset || p->bytes!=l->last_io.bytes)return bad();
  if(!p->io_error && ((p->op==MW_ERASE && p->result!=0) || (p->op==MW_PROGRAM && (p->result<=0 || (uint32_t)p->result>p->bytes))))return bad();
  l->last_io=*p;l->pending=0;
  if(!p->io_error){if(p->op==MW_ERASE){l->erase_ok++;l->block_start=p->offset;l->in_block=0;l->erase_ff=0;}else{l->program_calls++;l->program_bytes+=(uint32_t)p->result;l->in_block+=(uint32_t)p->result;}}
  return 0;
 }
 if(kind==MW_PROGRESS){
  if(l->pending || p->offset!=l->block_start || p->erased!=l->erase_ok)return bad();
  if(phase==93){if(l->erase_ok!=l->verified+1 || l->in_block)return bad();l->erase_ff=1;}
  else if(phase==88){if(!l->erase_ff || l->in_block!=MER_ERASE || p->verified!=l->verified+1 || p->verified>207)return bad();l->verified++;l->erase_ff=0;}
  else return bad();
  l->report.erased=p->erased;l->report.verified=p->verified;return 0;
 }
 if(kind==MW_CLOSE_BEFORE){if(l->close_pending || l->closed)return bad();l->close_pending=1;return 0;}
 if(kind==MW_CLOSE_RETURN){if(!l->close_pending)return bad();l->close_pending=0;l->closed=!p->result && !p->io_error;return 0;}
 if(kind==MW_AUDIT_BEGIN)return 0;
 if(kind==MW_REPORT){
  if(!h || p->erased>207 || p->verified>p->erased || p->attempted>1 || p->target_valid>1 || p->target_equal>1 || p->guard_valid>1 || p->closed>1)return bad();
  if(p->guard_valid && (!hex(h->boot) || !hex(h->rest) || !hex(h->metadata)))return bad();
  if(p->target_valid && !hex(h->desired))return bad();
  if(p->target_equal && (!p->target_valid || p->target_error || strcmp(h->desired,l->before.desired)))return bad();
  if(p->guard_valid && !p->guard_error && (strcmp(h->boot,l->before.boot) || strcmp(h->rest,l->before.rest) || strcmp(h->metadata,l->before.metadata)))return bad();
  l->after=*h;l->report=*p;l->report_valid=1;return 0;
 }
 return bad();
}
int mer_writer_ledger_final(const struct mer_writer_ledger *l,int io){
 const struct mer_writer_payload *p=&l->report;
 if(!l->baseline || !l->report_valid || p->core_error || p->guard_error || p->target_error || p->close_error || !p->attempted || p->erased!=207 || p->verified!=207 || !p->target_valid || !p->target_equal || !p->guard_valid || !p->closed)return bad();
 if(strcmp(l->before.boot,l->after.boot) || strcmp(l->before.rest,l->after.rest) || strcmp(l->before.metadata,l->after.metadata) || strcmp(l->before.desired,l->after.desired))return bad();
 if(io && (l->pending || l->close_pending || !l->closed || l->erase_ok!=207 || l->verified!=207 || l->program_bytes!=MER_REGION))return bad();
 return 0;
}
