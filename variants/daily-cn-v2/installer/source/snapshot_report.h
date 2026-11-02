#ifndef MER_SNAPSHOT_REPORT_H
#define MER_SNAPSHOT_REPORT_H
#include <stddef.h>
struct mer_snapshot_report {char boot[65],rest[65],metadata[65],desired[65];};
int mer_snapshot_valid(const struct mer_snapshot_report *report);
int mer_snapshot_parse(char *output,size_t bytes,struct mer_snapshot_report *report);
#endif
