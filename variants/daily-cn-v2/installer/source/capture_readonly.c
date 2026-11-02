#define _GNU_SOURCE 1
#include "stage_policy.h"
#include "capture_readonly.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
int mer_capture_readonly(int fd,char *const argv[],char *const envp[],unsigned budget,const struct mer_capture_hooks *hooks,char output[2048],size_t *bytes){
 if(fd<3 || !argv || !envp || !budget || !hooks || !hooks->tick || !hooks->child_setup || !output || !bytes){errno=EINVAL;return -1;}
 struct timespec start;if(clock_gettime(CLOCK_MONOTONIC,&start))return -1;
 int channel[2];if(pipe2(channel,O_CLOEXEC|O_NONBLOCK))return -1;
 pid_t child=fork();if(child<0){int saved=errno;close(channel[0]);close(channel[1]);errno=saved;return -1;}
 if(!child){
  close(channel[0]);int flags=fcntl(channel[1],F_GETFL);if(flags<0 || fcntl(channel[1],F_SETFL,flags&~O_NONBLOCK))_exit(124);
  if(dup2(channel[1],STDOUT_FILENO)<0 || dup2(channel[1],STDERR_FILENO)<0)_exit(124);
  if(channel[1]>2)close(channel[1]);
  hooks->child_setup(hooks->ctx);fexecve(fd,argv,envp);_exit(125);
 }
 close(channel[1]);*bytes=0;int reason=0,reaped=0,eof=0,read_broken=0,status=0;
 for(;;){
  if(hooks->tick(hooks->ctx) && !reason)reason=errno?errno:EIO;
  char chunk[512];ssize_t n=read(channel[0],chunk,sizeof(chunk));
  if(n>0){if((size_t)n>2047-*bytes){if(!reason)reason=EOVERFLOW;}else{memcpy(output+*bytes,chunk,(size_t)n);*bytes+=(size_t)n;}}
  else if(!n)eof=1;
  else if(errno!=EAGAIN && errno!=EINTR){read_broken=1;if(!reason)reason=errno?errno:EIO;}
  if(!reaped){pid_t result=waitpid(child,&status,WNOHANG);if(result==child)reaped=1;else if(result<0 && errno!=EINTR && !reason)reason=errno?errno:EIO;}
  struct timespec now;
  if(clock_gettime(CLOCK_MONOTONIC,&now)){if(!reason)reason=errno?errno:EIO;}
  else if(((long long)(now.tv_sec-start.tv_sec)*1000000000LL+now.tv_nsec-start.tv_nsec)>=(long long)budget*1000000000LL && !reason)reason=ETIMEDOUT;
  if(reaped && (eof || read_broken))break;
  /* No kill/alarm/cancellation. Even after error, preserve the parent and wait
   * for this read-only job; a blocked kernel operation has no inferred finish. */
  struct timespec tick={0,100000000};nanosleep(&tick,NULL);
 }
 close(channel[0]);output[*bytes]=0;
 if(!WIFEXITED(status) || WEXITSTATUS(status)){if(!reason)reason=EIO;}
 if(reason){errno=reason;return -1;}return 0;
}
