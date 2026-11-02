#ifndef MER_HANDOVER_OPS_H
#define MER_HANDOVER_OPS_H
#include <stddef.h>
#include <sys/types.h>
enum { MER_WD_UNKNOWN=0, MER_WD_INHERITED=1, MER_WD_TMR1_NOT_RESET=2 };
struct mer_watchdog { int fd;int timeout;int validated;int mode;unsigned timer1_control; };
struct mer_bundle_entry { const char *source;const char *name;const char *sha256;size_t bytes; };
int mer_build_readonly_bundle(const struct mer_bundle_entry entries[4]);
int mer_require_ram_static_elf(const char *path);
int mer_accept_inherited_watchdog(int fd,struct mer_watchdog *out);
int mer_prepare_watchdog_state(int inherited_fd,struct mer_watchdog *out);
int mer_prepare_standard_fds(void);
int mer_ping_existing_watchdog(const struct mer_watchdog *wdt);
int mer_pid1_exec_ram_helper(const char *path,const char *expected_sha256,char *const argv[],char *const envp[]);
int mer_signal_other_users(pid_t controller,int signal_number);
int mer_pivot_prepared_ram_root(void);
int mer_check_survivor_references(pid_t controller);
int mer_detach_old_mounts(void);
_Noreturn void mer_pid1_failure_hold(const struct mer_watchdog *wdt,int reason);
_Noreturn void mer_pid1_supervise_child(pid_t child,const struct mer_watchdog *wdt,unsigned deadline_seconds);
_Noreturn void mer_child_terminal_failure(int reason);
_Noreturn void mer_pid1_fork_supervised(const struct mer_watchdog *wdt,int (*controller)(void *),void *contract);
_Noreturn void mer_pid1_start_transition(const struct mer_watchdog *wdt);
#endif
