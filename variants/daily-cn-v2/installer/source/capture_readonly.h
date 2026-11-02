#ifndef MER_CAPTURE_READONLY_H
#define MER_CAPTURE_READONLY_H
#include <stddef.h>
struct mer_capture_hooks {void *ctx;int (*tick)(void *);void (*child_setup)(void *);};
/* Borrowed pinned fd; never signals a child. Budget/error stops acceptance,
 * but function keeps draining/waiting until it knows child IO has returned. */
int mer_capture_readonly(int executable_fd,char *const argv[],char *const envp[],unsigned budget,const struct mer_capture_hooks *hooks,char output[2048],size_t *bytes);
#endif
