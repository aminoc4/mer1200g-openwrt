#ifndef MER_WRITER_LEDGER_H
#define MER_WRITER_LEDGER_H
#include "writer_protocol.h"
#include "snapshot_report.h"
struct mer_writer_ledger {
 struct mer_snapshot_report before,after;
 struct mer_writer_payload last_io,report;
 uint32_t baseline,report_valid,pending,close_pending,closed,id,erase_ok,program_calls,program_bytes,verified,in_block,block_start,erase_ff;
};
int mer_writer_ledger_apply(struct mer_writer_ledger *ledger,unsigned kind,unsigned phase,const struct mer_writer_payload *payload,const struct mer_snapshot_report *hashes);
int mer_writer_ledger_final(const struct mer_writer_ledger *ledger,int check_io);
#endif
