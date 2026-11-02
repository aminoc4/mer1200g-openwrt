#ifndef MER_WRITER_RUNTIME_H
#define MER_WRITER_RUNTIME_H
struct mer_watchdog;
int mer_run_fixed_writer(const struct mer_watchdog *,unsigned *);
#endif
