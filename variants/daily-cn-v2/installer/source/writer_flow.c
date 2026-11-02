#include "writer_flow.h"
#include "status.h"
#include "stage_policy.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
static void hashes(struct mer_snapshot_report *h,const struct mer_report *r,int after){
 memset(h,0,sizeof(*h));
 snprintf(h->boot,sizeof(h->boot),"%s",after?r->boot_after:r->boot_before);
 snprintf(h->rest,sizeof(h->rest),"%s",after?r->rest_after:r->rest_before);
 snprintf(h->metadata,sizeof(h->metadata),"%s",after?r->metadata_after:r->metadata_before);
 snprintf(h->desired,sizeof(h->desired),"%s",after?r->readback_sha:r->desired_sha);
}
int mer_writer_notify(void *ctx,unsigned kind,const struct mer_writer_payload *input,int *stop){
 struct mer_writer_payload copy=*input,*p=&copy;
 struct mer_writer_flow *f=ctx;const struct mer_report *r=f->report;unsigned phase;
 switch(kind){case MW_BASELINE:phase=81;break;case MW_IO_BEFORE:phase=83;break;case MW_IO_RETURN:phase=84;break;case MW_CLOSE_BEFORE:phase=85;break;case MW_CLOSE_RETURN:phase=86;if(!p->result && !p->io_error)f->closed=1;break;case MW_AUDIT_BEGIN:phase=87;break;case MW_REPORT:phase=89;break;default:errno=EINVAL;return -1;}
 p->erased=r->erased;p->verified=r->verified;p->attempted=(unsigned)r->mutation_attempted;
 p->target_valid=(unsigned)r->target_valid;p->target_equal=(unsigned)r->target_equal;p->guard_valid=(unsigned)r->guard_after_valid;
 p->core_error=r->error;p->guard_error=r->guard_error;p->target_error=r->target_error;p->close_error=r->close_error;p->closed=(unsigned)f->closed;
 snprintf(p->label,sizeof(p->label),"%s",r->phase?r->phase:"none");
 struct mer_snapshot_report h;hashes(&h,r,kind==MW_REPORT);
 return mer_status_writer_event(kind,phase,*f->checks,p,(kind==MW_BASELINE || kind==MW_REPORT)?&h:NULL,stop);
}
int mer_writer_gate(struct mer_writer_flow *f,enum mer_event event,uint64_t at){
 if(f->trace->stop_error){errno=f->trace->stop_error;return -1;}
 if(event==MER_BEFORE_ERASE)return 0;
 if(event==MER_FINAL_VERIFIED)return mer_status_stage(MER_PH_WR_VERIFY_GATE,*f->checks);
 struct mer_writer_payload p={.offset=(uint32_t)at,.erased=f->report->erased,.verified=f->report->verified};int stop=0;
 unsigned phase=event==MER_ERASED?MER_PH_WR_ERASE_FF:MER_PH_WR_BLOCK;
 int result=mer_status_writer_event(MW_PROGRESS,phase,*f->checks,&p,NULL,&stop);
 if(result || stop){f->trace->stop_error=stop?stop:(errno?errno:EIO);errno=f->trace->stop_error;return -1;}return 0;
}
int mer_writer_transaction(struct mer_backend *b,struct mer_workspace *w,struct mer_report *r,struct mer_writer_flow *f){
 f->report=r;
 if(mer_prepare(b,w,r))return -1;
 *f->checks|=MER_WR_BASELINE_BIT;
 struct mer_writer_payload p={0};int stop=0;
 int result=mer_writer_notify(f,MW_BASELINE,&p,&stop);
 if(result || stop){r->error=stop?stop:(errno?errno:EIO);r->phase="baseline-gate";result=-1;}
 else result=mer_execute(b,w,r);
 if(result)(void)mer_audit_failed_transaction(b,w,r);
 if(r->target_valid && r->target_equal && !r->target_error)*f->checks|=MER_WR_TARGET_BIT;
 if(r->guard_after_valid && !r->guard_error)*f->checks|=MER_WR_GUARDS_BIT;
 if(f->closed && !r->close_error)*f->checks|=MER_WR_CLOSED_BIT;
 memset(&p,0,sizeof(p));stop=0;
 if(mer_writer_notify(f,MW_REPORT,&p,&stop) || stop){if(!r->error)r->error=stop?stop:(errno?errno:EIO);result=-1;}
 if(result){errno=r->error?r->error:EIO;return -1;}return 0;
}
