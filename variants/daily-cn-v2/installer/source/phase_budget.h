#ifndef MER_PHASE_BUDGET_H
#define MER_PHASE_BUDGET_H
#include "status.h"
#include <time.h>
/* Operational observation budgets, not bounds on kernel IO completion. */
static unsigned mer_budget_class(unsigned phase){if(phase==90)return 8;if(phase==87 || phase==89 || phase==91)return 7;if(phase==85 || phase==86)return 6;if(phase>=82 && phase<=93)return 5;if(phase==80 || phase==81)return 3;if(phase==50 || phase==71)return 4;if(phase>=68 && phase<=70)return 3;if(phase==66 || phase==67)return 2;return 1;}
static unsigned mer_budget_seconds(unsigned group){return group==5?7200u:group==6?600u:group==7?1800u:group==3?600u:group==2?300u:180u;}
static int mer_budget_expired(const struct timespec *start,const struct timespec *now,unsigned group){long long elapsed=(long long)(now->tv_sec-start->tv_sec)*1000000000LL+now->tv_nsec-start->tv_nsec;return elapsed<0 || elapsed>=(long long)mer_budget_seconds(group)*1000000000LL;}
#endif
