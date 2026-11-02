#ifndef MER_TRACED_IO_H
#define MER_TRACED_IO_H
#include "writer_protocol.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
struct mer_traced_io {
 void *raw_ctx,*notify_ctx;
 int (*erase)(void *,uint64_t,size_t);
 ssize_t (*program)(void *,const void *,size_t,uint64_t);
 int (*close_rw)(void *);
 int (*notify)(void *,unsigned,const struct mer_writer_payload *,int *);
 uint32_t id;
 int stop_error;
};
int mer_traced_erase(struct mer_traced_io *trace,uint64_t offset,size_t bytes);
ssize_t mer_traced_program(struct mer_traced_io *trace,const void *data,size_t bytes,uint64_t offset);
int mer_traced_close(struct mer_traced_io *trace);
#endif
