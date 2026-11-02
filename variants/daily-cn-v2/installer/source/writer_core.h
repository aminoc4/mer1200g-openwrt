#ifndef MER_WRITER_CORE_H
#define MER_WRITER_CORE_H
#include "install_policy.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#define MER_FLASH 0x1000000u
#define MER_START 0x40000u
#define MER_END 0xd30000u
#define MER_REGION (MER_END-MER_START)
#define MER_ERASE 0x10000u
#define MER_IMAGE 0xcf0000u
#define MER_METADATA 0xd30000u
#define MER_PAGE 256u
#define MER_ALLOCATION (MER_FLASH+MER_REGION+MER_ERASE)
struct mer_geometry {uint64_t bytes;uint32_t erase,write,oob,type;};
enum mer_event {MER_BEFORE_ERASE=1,MER_ERASED=2,MER_BLOCK_VERIFIED=3,MER_FINAL_VERIFIED=4};
struct mer_backend {
 void *ctx;
 ssize_t (*read)(void *,void *,size_t,uint64_t);
 int (*geometry)(void *,struct mer_geometry *);
 /* Mutation callbacks are absent from the default OS backend. */
 int (*begin)(void *);
 int (*erase)(void *,uint64_t,size_t);
 ssize_t (*write)(void *,const void *,size_t,uint64_t);
 int (*gate)(void *,enum mer_event,uint64_t);
 int (*end)(void *);
};
struct mer_workspace {unsigned char *snapshot,*desired,*block;size_t snapshot_bytes,desired_bytes,block_bytes;uint32_t prepared;};
struct mer_report {
 int error,guard_error,mutation_attempted,guard_after_valid,backend_open;
 int target_error,close_error,target_valid,target_equal,target_audited,guard_audited;
 unsigned erased,verified;uint64_t offset;const char *phase;
 char boot_before[65],rest_before[65],metadata_before[65];
 char boot_after[65],rest_after[65],metadata_after[65],desired_sha[65],readback_sha[65];
};
int mer_geometry_ok(const struct mer_geometry *geometry);
int mer_write_range(uint64_t offset,size_t bytes,int erase);
int mer_read_exact(ssize_t (*readfn)(void *,void *,size_t,uint64_t),void *ctx,void *buffer,size_t bytes,uint64_t offset,uint64_t limit);
int mer_prepare(struct mer_backend *backend,struct mer_workspace *work,struct mer_report *report);
/* Only explicitly compiled mutation-core profiles define this function. */
#if MER_CORE_EXECUTE
int mer_execute(struct mer_backend *backend,struct mer_workspace *work,struct mer_report *report);
int mer_audit_failed_transaction(struct mer_backend *backend,struct mer_workspace *work,struct mer_report *report);
#endif
#endif
