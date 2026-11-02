#include "traced_io.h"
#include "writer_core.h"
#include <errno.h>
#include <string.h>
static int fail(int error){errno=error?error:EIO;return -1;}
static int before(struct mer_traced_io *t,struct mer_writer_payload *p,unsigned op,uint64_t at,size_t n){
 if(!t || !t->notify || t->id==UINT32_MAX || mer_write_range(at,n,op==MW_ERASE))return fail(EINVAL);
 if(t->stop_error)return fail(t->stop_error);
 memset(p,0,sizeof(*p));p->op=op;p->id=++t->id;p->offset=(uint32_t)at;p->bytes=(uint32_t)n;
 int stop=0;if(t->notify(t->notify_ctx,MW_IO_BEFORE,p,&stop)){t->stop_error=stop?stop:(errno?errno:EIO);return fail(t->stop_error);}
 if(stop){t->stop_error=stop;return fail(stop);}return 0;
}
static int returned(struct mer_traced_io *t,struct mer_writer_payload *p,int result,int error){
 p->result=result;p->io_error=error;int stop=0;
 int notified=t->notify(t->notify_ctx,MW_IO_RETURN,p,&stop),notify_error=errno;
 if(error){t->stop_error=error;return fail(error);}
 if(notified || stop){t->stop_error=stop?stop:(notify_error?notify_error:EIO);return fail(t->stop_error);}
 return result;
}
int mer_traced_erase(struct mer_traced_io *t,uint64_t at,size_t n){
 if(!t || !t->erase)return fail(EINVAL);
 struct mer_writer_payload p;
 if(before(t,&p,MW_ERASE,at,n))return -1;
 int result=t->erase(t->raw_ctx,at,n),error=result<0?(errno?errno:EIO):result?EIO:0;
 return returned(t,&p,result,error);
}
ssize_t mer_traced_program(struct mer_traced_io *t,const void *data,size_t n,uint64_t at){
 if(!t || !t->program)return fail(EINVAL);
 struct mer_writer_payload p;
 if(before(t,&p,MW_PROGRAM,at,n))return -1;
 ssize_t result=t->program(t->raw_ctx,data,n,at);int error=0;
 if(result<0)error=errno?errno:EIO;else if(!result || (size_t)result>n)error=EIO;
 return returned(t,&p,(int)result,error);
}
int mer_traced_close(struct mer_traced_io *t){
 if(!t || !t->close_rw || !t->notify)return fail(EINVAL);
 struct mer_writer_payload p;memset(&p,0,sizeof(p));p.id=t->id;int stop=0;
 /* Cleanup is not a new erase/program permission. Notify best effort, but
  * always allow the known-returned writer to close exactly once. */
 if(t->notify(t->notify_ctx,MW_CLOSE_BEFORE,&p,&stop) && !t->stop_error)t->stop_error=errno?errno:EIO;
 if(stop && !t->stop_error)t->stop_error=stop;
 int result=t->close_rw(t->raw_ctx),error=result?(errno?errno:EIO):0;p.result=result;p.io_error=error;p.closed=!result;
 stop=0;if(t->notify(t->notify_ctx,MW_CLOSE_RETURN,&p,&stop) && !t->stop_error)t->stop_error=errno?errno:EIO;
 if(stop && !t->stop_error)t->stop_error=stop;
 if(error)return fail(error);
 return 0;
}
