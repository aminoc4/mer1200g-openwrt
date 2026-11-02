#ifndef MER_STATUS_H
#define MER_STATUS_H
#include <stdint.h>
#include "snapshot_report.h"
#include "writer_protocol.h"
enum mer_phase {
 MER_PH_READY=1,MER_PH_QUIESCE_BEFORE=10,MER_PH_QUIESCED=11,
 MER_PH_BUNDLE_BEFORE=2,MER_PH_BUNDLE_DONE=3,MER_PH_PREFLIGHT_BEFORE=4,MER_PH_PREFLIGHT_DONE=5,MER_PH_RAM_EXEC_BEFORE=6,MER_PH_RAM_EXEC_DONE=7,
 MER_PH_PROC_MOVE_BEFORE=20,MER_PH_PROC_MOVED=21,MER_PH_PIVOT_BEFORE=22,MER_PH_PIVOTED=23,
 MER_PH_SYSTEM_MOVES_BEFORE=24,MER_PH_SYSTEM_MOVED=25,MER_PH_REFS_CHECKED=26,
 MER_PH_SYS_MOVED=27,MER_PH_DEV_MOVED=28,MER_PH_TMP_MOVED=29,
 MER_PH_MOVE_LOWER_BEFORE=30,MER_PH_LOWER_HELD=31,MER_PH_MOVE_UPPER_BEFORE=32,MER_PH_UPPER_HELD=33,
 MER_PH_MOVE_JFFS_BEFORE=34,MER_PH_JFFS_HELD=35,MER_PH_OVERLAY_UMOUNT_BEFORE=36,MER_PH_OVERLAY_GONE=37,
 MER_PH_LOWER_UMOUNT_BEFORE=38,MER_PH_LOWER_GONE=39,MER_PH_UPPER_UMOUNT_BEFORE=40,MER_PH_UPPER_GONE=41,
 MER_PH_FINAL=50,MER_PH_FAILURE=60,MER_PH_HOLD=61,
 MER_PH_SYS_BEFORE=62,MER_PH_DEV_BEFORE=63,MER_PH_TMP_BEFORE=64,
 MER_PH_RAM_READY=65,MER_PH_JFFS_BEFORE=66,MER_PH_JFFS_GONE=67,
 MER_PH_SNAPSHOT_BEFORE=68,MER_PH_SNAPSHOT_RESULT=69,MER_PH_SNAPSHOT_DONE=70,
 MER_PH_POST_SNAPSHOT_REFS=71,MER_PH_WAIT=72,
 MER_PH_WR_PREPARE=80,MER_PH_WR_BASELINE=81,MER_PH_WR_READY=82,
 MER_PH_WR_IO_BEFORE=83,MER_PH_WR_IO_RETURN=84,MER_PH_WR_CLOSE_BEFORE=85,
 MER_PH_WR_CLOSE_RETURN=86,MER_PH_WR_AUDIT_BEGIN=87,MER_PH_WR_BLOCK=88,
 MER_PH_WR_REPORT=89,MER_PH_WR_REFS=90,MER_PH_WR_VERIFY_GATE=91,MER_PH_WR_ERASE_FF=93
};
struct mer_status_event {uint32_t magic,phase;int32_t error;uint32_t checks,ack,at,snapshot,kind;struct mer_snapshot_report report;struct mer_writer_payload writer;};
_Static_assert(sizeof(struct mer_status_event)<=512,"atomic local pipe event");
int mer_status_open(unsigned timer1_control);
int mer_status_emit(unsigned phase,int error,unsigned checks);
int mer_status_setup_pipes(void);
int mer_status_after_fork(int child);
int mer_status_stage(unsigned phase,unsigned checks);
int mer_status_snapshot(const struct mer_snapshot_report *report,unsigned checks);
unsigned mer_status_current_phase(void);
int mer_status_wait_report(int reason);
int mer_status_wait_events(unsigned milliseconds);
int mer_status_operation_stalled(void);
void mer_status_note_error(int reason);
#if MER_PROFILE_WRITE
int mer_status_writer_event(unsigned kind,unsigned phase,unsigned checks,const struct mer_writer_payload *payload,const struct mer_snapshot_report *hashes,int *stop);
#endif
int mer_status_pump(int allow_advance);
void mer_status_child_failure(int error);
int mer_status_hold_repeat(int reason);
void mer_status_close(void);
int mer_status_export_exec(int *fd,unsigned *sequence);
int mer_status_import_exec(int fd,unsigned sequence,unsigned timer1_control);
#endif
