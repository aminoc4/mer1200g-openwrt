#ifndef MER_STAGE_POLICY_H
#define MER_STAGE_POLICY_H
#include "install_policy.h"
#ifndef MER_PROFILE_WRITE
#define MER_PROFILE_WRITE 0
#endif
#if MER_PROFILE_WRITE == 0
#if (defined(MER_CORE_EXECUTE) && MER_CORE_EXECUTE) || (defined(MER_OS_WRITE) && MER_OS_WRITE)
#error "Default PREFLIGHT_ONLY cannot link an execution backend"
#endif
#define MER_PROFILE_NAME "stock-ssh-preflight-v2"
#define MER_BUNDLE_NONCE MER_PREFLIGHT_NONCE
#define MER_CONTRACT "/tmp/mer-stock-preflight-v2.contract"
#define MER_CONTRACT_MARKER "MER_STOCK_PREFLIGHT_V2"
#define MER_CONTRACT_ACK ""
#define MER_RAM_ROOT "/tmp/mer-stock-preflight-v2"
#define MER_STAGE_FINAL_CHECKS 0xffffu
#define MER_FINAL_PHASE_BEFORE 71u
#elif MER_PROFILE_WRITE == 1
#if !defined(MER_WRITE_REVIEW_BUILD) || MER_WRITE_REVIEW_BUILD != 1 || !defined(MER_CORE_EXECUTE) || MER_CORE_EXECUTE != 1
#error "Explicit separate WRITE_REVIEW compile profile is required"
#endif
#define MER_PROFILE_NAME "stock-ssh-write-v2"
#define MER_BUNDLE_NONCE MER_WRITE_NONCE
#define MER_CONTRACT "/tmp/mer-stock-write-v2.contract"
#define MER_CONTRACT_MARKER "MER_STOCK_WRITE_V2"
#define MER_CONTRACT_ACK "\nack=INSTALL_MER1200G_V2"
#define MER_RAM_ROOT "/tmp/mer-stock-write-v2"
#define MER_STAGE_FINAL_CHECKS 0x1fffffu
#define MER_FINAL_PHASE_BEFORE 90u
#else
#error "Unknown profile"
#endif
#define MER_PREPARATION_CHECKS 0xffffu
#define MER_WR_BASELINE_BIT 0x10000u
#define MER_WR_TARGET_BIT 0x20000u
#define MER_WR_GUARDS_BIT 0x40000u
#define MER_WR_CLOSED_BIT 0x80000u
#define MER_WR_REFS_BIT 0x100000u
#endif
