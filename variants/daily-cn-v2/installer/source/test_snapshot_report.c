#include "snapshot_report.h"
#include "stage_policy.h"
#include "phase_budget.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static void change(char *text,const char *old,const char *newtext){char *at=strstr(text,old);assert(at && strlen(old)==strlen(newtext));memcpy(at,newtext,strlen(old));}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);char original[2048],test[2048];size_t n=fread(original,1,2047,f);assert(!ferror(f) && feof(f) && n>0);fclose(f);original[n]=0;
 struct mer_snapshot_report r;memcpy(test,original,n+1);assert(!mer_snapshot_parse(test,n,&r));assert(!strcmp(r.boot,MER_SNAPSHOT_BOOT) && !strcmp(r.desired,MER_SNAPSHOT_DESIRED));
 const char *old[]={"error=0","attempted=0","erased=0","verified=0","allocated=30408704","tail=0","precheck-complete","boot_after= ","readback=;"};
 const char *bad[]={"error=1","attempted=1","erased=1","verified=1","allocated=30408703","tail=1","precheck-failure!","boot_after=x","readback=x"};
 for(unsigned i=0;i<sizeof(old)/sizeof(old[0]);i++){memcpy(test,original,n+1);change(test,old[i],bad[i]);assert(mer_snapshot_parse(test,n,&r)<0);}
 memcpy(test,original,n+1);test[7]=0;assert(mer_snapshot_parse(test,n,&r)<0);
 memcpy(test,original,n+1);assert(mer_snapshot_parse(test,n-1,&r)<0);
 memcpy(test,original,n+1);test[n]='X';assert(mer_snapshot_parse(test,n+1,&r)<0);
 memcpy(test,original,n+1);char *at=strstr(test,MER_SNAPSHOT_DESIRED);assert(at);at[0]=at[0]=='0'?'1':'0';assert(mer_snapshot_parse(test,n,&r)<0);
 memcpy(test,original,n+1);at=strstr(test,MER_SNAPSHOT_BOOT);assert(at);at[0]='A';assert(mer_snapshot_parse(test,n,&r)<0);
 assert(mer_budget_class(41)==1 && mer_budget_seconds(1)==180);
 assert(mer_budget_class(66)==2 && mer_budget_class(67)==2 && mer_budget_seconds(2)==300);
 assert(mer_budget_class(68)==3 && mer_budget_class(69)==3 && mer_budget_class(70)==3 && mer_budget_seconds(3)==600);
 assert(mer_budget_class(71)==4 && mer_budget_class(50)==4 && mer_budget_seconds(4)==180);
 struct timespec start={100,500000000},early={700,499999999},expired={700,500000000};assert(!mer_budget_expired(&start,&early,3) && mer_budget_expired(&start,&expired,3));
 puts("PASS actual-readonly-output parser, bad/error/mutation/allocation/hash/truncation/NUL rejection and phase budgets180/300/600/180");return 0;
}
