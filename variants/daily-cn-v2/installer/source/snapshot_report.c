#include "stage_policy.h"
#include "snapshot_report.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int failed(void){errno=EPROTO;return -1;}
static int hex(const char h[65]){for(unsigned i=0;i<64;i++)if(!((h[i]>='0' && h[i]<='9') || (h[i]>='a' && h[i]<='f')))return 0;return !h[64];}
int mer_snapshot_valid(const struct mer_snapshot_report *r){return r && hex(r->boot) && hex(r->rest) && hex(r->metadata) && hex(r->desired) && !strcmp(r->boot,MER_SNAPSHOT_BOOT) && !strcmp(r->desired,MER_SNAPSHOT_DESIRED)?0:failed();}
int mer_snapshot_parse(char *text,size_t n,struct mer_snapshot_report *r){
 if(!text || !r || !n || n>=2048 || text[n-1]!='\n' || memchr(text,0,n))return failed();
 char *lines[4],*next=text;memset(r,0,sizeof(*r));
 for(unsigned i=0;i<4;i++){lines[i]=next;char *end=memchr(next,'\n',n-(size_t)(next-text));if(!end)return failed();*end=0;next=end+1;}
 if(next!=text+n)return failed();
 if(strcmp(lines[0],"MER-WRITER-RESULT phase=precheck-complete error=0 offset=00000000 attempted=0 erased=0 verified=0 guard_valid=0 guard_error=0"))return failed();
 int used=0;
 if(sscanf(lines[1],"GUARD boot_before=%64[0-9a-f] boot_after= rest_before=%64[0-9a-f] rest_after= metadata_before=%64[0-9a-f] metadata_after=%n",r->boot,r->rest,r->metadata,&used)!=3 || !used || lines[1][used])return failed();
 used=0;
 unsigned allocated=0,tail=1;
 if(sscanf(lines[2],"REGION desired=%64[0-9a-f] readback=; allocated=%u tail=%u; no reboot/rollback stage%n",r->desired,&allocated,&tail,&used)!=3 || allocated!=MER_SNAPSHOT_ALLOC || tail!=0 || !used || lines[2][used])return failed();
 if(strcmp(lines[3],"PASS read-only precheck; not permission to write or evidence of device exclusivity"))return failed();
 return mer_snapshot_valid(r);
}
