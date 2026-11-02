#ifndef MER_WRITER_FLOW_H
#define MER_WRITER_FLOW_H
#include "writer_core.h"
#include "traced_io.h"
struct mer_writer_flow {struct mer_report *report;struct mer_traced_io *trace;unsigned *checks;int closed;};
int mer_writer_notify(void *,unsigned,const struct mer_writer_payload *,int *);
int mer_writer_gate(struct mer_writer_flow *,enum mer_event,uint64_t);
int mer_writer_transaction(struct mer_backend *,struct mer_workspace *,struct mer_report *,struct mer_writer_flow *);
#endif
