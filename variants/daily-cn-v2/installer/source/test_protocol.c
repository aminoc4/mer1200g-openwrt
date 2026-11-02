#include "writer_ledger.h"
#include "writer_core.h"
#include "traced_io.h"
#include "stage_policy.h"
#include "phase_budget.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static struct mer_snapshot_report hashes(void){struct mer_snapshot_report h;strcpy(h.boot,MER_SNAPSHOT_BOOT);memset(h.rest,'1',64);h.rest[64]=0;memset(h.metadata,'2',64);h.metadata[64]=0;strcpy(h.desired,MER_SNAPSHOT_DESIRED);return h;}
static unsigned raw_calls,notifications,closes,deny_kind;static int raw_error,stop_error;
static int notify(void *ctx,unsigned kind,const struct mer_writer_payload *p,int *stop){(void)ctx;(void)p;notifications++;*stop=kind==deny_kind?stop_error:0;return *stop?-1:0;}
static int raw_erase(void *ctx,uint64_t at,size_t n){(void)ctx;(void)at;(void)n;raw_calls++;if(raw_error){errno=raw_error;return -1;}return 0;}
static ssize_t raw_program(void *ctx,const void *p,size_t n,uint64_t at){(void)ctx;(void)p;(void)at;raw_calls++;if(raw_error){errno=raw_error;return -1;}return (ssize_t)n;}
static int raw_close(void *ctx){(void)ctx;closes++;return 0;}
int main(void){
 struct timespec start={100,0},before={7299,999999999},expired={7300,0};
 assert(mer_budget_class(83)==5 && mer_budget_class(84)==5 && mer_budget_class(88)==5 && mer_budget_class(93)==5 && mer_budget_seconds(5)==7200);
 assert(!mer_budget_expired(&start,&before,5) && mer_budget_expired(&start,&expired,5));
 assert(mer_budget_class(85)==6 && mer_budget_seconds(6)==600 && mer_budget_class(87)==7 && mer_budget_seconds(7)==1800 && mer_budget_class(90)==8 && mer_budget_seconds(8)==180);
 struct mer_snapshot_report h=hashes();struct mer_writer_ledger l={0};struct mer_writer_payload p={0};
 assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);
 assert(!mer_writer_ledger_apply(&l,MW_BASELINE,81,&p,&h));assert(mer_writer_ledger_apply(&l,MW_BASELINE,81,&p,&h)<0);
 p=(struct mer_writer_payload){.op=MW_PROGRAM,.id=1,.offset=MER_START,.bytes=256};assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);
 p=(struct mer_writer_payload){.op=MW_ERASE,.id=1,.offset=MER_START-1,.bytes=MER_ERASE};assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);
 p.offset=MER_START;assert(!mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL));assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);
 p.id=2;assert(mer_writer_ledger_apply(&l,MW_IO_RETURN,84,&p,NULL)<0);p.id=1;assert(!mer_writer_ledger_apply(&l,MW_IO_RETURN,84,&p,NULL));assert(mer_writer_ledger_apply(&l,MW_IO_RETURN,84,&p,NULL)<0);
 p=(struct mer_writer_payload){.op=MW_PROGRAM,.id=2,.offset=MER_START,.bytes=256};assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);
 struct mer_writer_payload progress={.offset=MER_START,.erased=1};assert(!mer_writer_ledger_apply(&l,MW_PROGRESS,93,&progress,NULL));
 p.bytes=257;assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);p.bytes=256;p.offset++;
 assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);p.offset=MER_START;
 assert(!mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL));p.result=63;assert(!mer_writer_ledger_apply(&l,MW_IO_RETURN,84,&p,NULL));assert(l.program_bytes==63);
 p.id=3;p.offset=MER_START;p.bytes=193;assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);p.offset+=63;
 assert(!mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL));p.result=-1;p.io_error=EINTR;assert(!mer_writer_ledger_apply(&l,MW_IO_RETURN,84,&p,NULL));assert(!l.pending && l.program_bytes==63);
 assert(mer_writer_ledger_final(&l,1)<0);assert(!mer_writer_ledger_apply(&l,MW_CLOSE_BEFORE,85,&p,NULL));p.result=0;p.io_error=0;
 assert(!mer_writer_ledger_apply(&l,MW_CLOSE_RETURN,86,&p,NULL));assert(l.closed);assert(mer_writer_ledger_apply(&l,MW_IO_BEFORE,83,&p,NULL)<0);
 p=(struct mer_writer_payload){.target_valid=1,.target_equal=1,.guard_valid=1};struct mer_snapshot_report wrong=h;wrong.desired[0]='0';assert(mer_writer_ledger_apply(&l,MW_REPORT,89,&p,&wrong)<0);
 wrong=h;wrong.rest[0]='0';assert(mer_writer_ledger_apply(&l,MW_REPORT,89,&p,&wrong)<0);
 p.guard_error=EIO;assert(!mer_writer_ledger_apply(&l,MW_REPORT,89,&p,&wrong));assert(mer_writer_ledger_final(&l,1)<0);
 struct mer_traced_io t={.erase=raw_erase,.program=raw_program,.close_rw=raw_close,.notify=notify};unsigned char data[256]={0};
 deny_kind=MW_IO_BEFORE;stop_error=ETIMEDOUT;assert(mer_traced_erase(&t,MER_START,MER_ERASE)<0 && !raw_calls);assert(mer_traced_program(&t,data,256,MER_START)<0 && !raw_calls);
 assert(!mer_traced_close(&t) && closes==1);
 memset(&t,0,sizeof(t));t.erase=raw_erase;t.program=raw_program;t.close_rw=raw_close;t.notify=notify;deny_kind=MW_IO_RETURN;raw_error=EINTR;stop_error=ENETUNREACH;
 assert(mer_traced_erase(&t,MER_START,MER_ERASE)<0 && errno==EINTR && raw_calls==1);assert(mer_traced_erase(&t,MER_START,MER_ERASE)<0 && raw_calls==1);
 assert(!mer_traced_close(&t) && closes==2);assert(notifications>=6);
 printf("ABI status_event=%zu ack=%zu ETIMEDOUT=%d ECANCELED=%d ENETUNREACH=%d\n",sizeof(struct mer_status_event),sizeof(struct mer_status_ack),ETIMEDOUT,ECANCELED,ENETUNREACH);
 puts("PASS ledger rejects missing baseline, duplicate/reordered IO, wrong block/page, no FF certificate, gaps/overlap, mismatched hashes and premature FINAL; traced denial/no retry/cleanup/actual errno preservation; operational phase budgets");return 0;
}
