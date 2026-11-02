#ifndef MER_WRITER_PROTOCOL_H
#define MER_WRITER_PROTOCOL_H
#include <stdint.h>
enum mer_writer_kind {MW_NONE=0,MW_BASELINE=1,MW_IO_BEFORE=2,MW_IO_RETURN=3,MW_PROGRESS=4,MW_CLOSE_BEFORE=5,MW_CLOSE_RETURN=6,MW_AUDIT_BEGIN=7,MW_REPORT=8};
enum mer_writer_op {MW_ERASE=1,MW_PROGRAM=2};
struct mer_writer_payload {
 uint32_t op,id,offset,bytes;
 int32_t result,io_error;
 uint32_t erased,verified,attempted,target_valid,target_equal,guard_valid;
 int32_t core_error,guard_error,target_error,close_error;
 uint32_t closed;
 char label[32];
};
struct mer_status_ack {int32_t permit,error;};
#endif
