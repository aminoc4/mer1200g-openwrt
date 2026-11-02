#ifndef MER_MOUNT_GRAPH_H
#define MER_MOUNT_GRAPH_H
#include <stdio.h>
enum { MER_GRAPH_PREPIVOT=1, MER_GRAPH_POSTPIVOT=2 };
enum { MER_MOVE_CHILD=1, MER_ORDINARY_UMOUNT=2 };
struct mer_mount_action { int kind;unsigned id;char source[256],destination[256]; };
struct mer_mount_plan { unsigned count;struct mer_mount_action actions[8];int approved,phase; };
/* Recognize only the original MER graph and its explicitly relocated RAM form.
 * Approved means graph matches the source plan, never permission to execute it.
 */
int mer_original_mount_plan(const char *mountinfo,struct mer_mount_plan *plan,FILE *report);
int mer_check_mount_action(const char *mountinfo,const struct mer_mount_action *action);
#endif
