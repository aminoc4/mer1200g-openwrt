/* Normal JFFS2 cleanup linked only into the new preflight-only RAM stage. */
#define _GNU_SOURCE 1
#include "live_guard.h"
#include <sys/mount.h>
int mer_unmount_retained_jffs2(void){
 if(mer_writer_live_guard(-1,-1,1))return -1;
 /* Normal unmount may flush configuration. Original kill_sb stops/waits for
  * GC, then generic shutdown runs put_super. No lazy/force/direct GC signal. */
 if(umount2("/held/userconfig",0))return -1;
 return mer_writer_live_guard(-1,-1,0);
}
