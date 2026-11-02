#ifndef MER_WRITER_LIVE_GUARD_H
#define MER_WRITER_LIVE_GUARD_H
int mer_writer_live_guard(int own_mtd_fd,int own_pin,int allow_retained_jffs);
/* Both explicit RAM profiles use the same normal JFFS2 teardown. */
int mer_unmount_retained_jffs2(void);
#endif
