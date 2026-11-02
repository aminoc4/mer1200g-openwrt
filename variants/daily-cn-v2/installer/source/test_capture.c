#define _GNU_SOURCE 1
#include "capture_readonly.h"
#include "snapshot_report.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static int tick(void *ctx){int *fail=ctx;if(*fail){errno=EACCES;return -1;}return 0;}
static void child_setup(void *ctx){(void)ctx;}
static double now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
int main(int argc,char **argv){
 assert(argc==3);const char *modes[]={"ok","error","overflow","delay","delay"};
 for(unsigned i=0;i<5;i++){
  int fd=open(argv[1],O_RDONLY|O_CLOEXEC);assert(fd>=3);int fail=i==4;
  struct mer_capture_hooks hooks={&fail,tick,child_setup};char output[2048];size_t n;char *args[]={"snapshot-fixture-child",(char *)modes[i],argv[2],NULL};
  double start=now();int result=mer_capture_readonly(fd,args,environ,i==3?1:10,&hooks,output,&n);int saved=errno;double elapsed=now()-start;close(fd);
  if(i==0){struct mer_snapshot_report report;assert(!result && !mer_snapshot_parse(output,n,&report));}
  if(i==1)assert(result<0 && saved==EIO);
  if(i==2)assert(result<0 && saved==EOVERFLOW);
  if(i==3)assert(result<0 && saved==ETIMEDOUT && elapsed>=1.3);
  if(i==4)assert(result<0 && saved==EACCES && elapsed>=1.3 && n>0);
  int status;assert(waitpid(-1,&status,WNOHANG)<0 && errno==ECHILD);
 }
 puts("PASS actual fork/fexec/pipe capture, error/overflow rejection, budget and tick faults wait for own child completion instead of kill, no lingering child");return 0;
}
