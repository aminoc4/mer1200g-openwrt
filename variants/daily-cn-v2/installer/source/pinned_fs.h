#ifndef MER_PINNED_FS_H
#define MER_PINNED_FS_H
#include <errno.h>
#include <stdio.h>
#include <sys/statfs.h>
/* Stock3.10 fd_statfs rejects FMODE_PATH, while proc_fd_link copies f_path.
 * Follow our still-open O_PATH pin by pathname; never reopen an unknown device,
 * pipe or socket and never invoke its open method to inspect filesystem metadata.
 */
static int mer_statfs_pinned(int pin,struct statfs *fs){
 if(pin<0){errno=EBADF;return -1;}
 char path[64];int length=snprintf(path,sizeof(path),"/proc/self/fd/%d",pin);
 if(length<0 || (size_t)length>=sizeof(path)){errno=EOVERFLOW;return -1;}
 return statfs(path,fs);
}
#endif
