# Source and license boundaries

This directory is a source export for an **offline, unbooted persistent review candidate**, not a factory firmware or supported upgrade. A device installation/recovery mechanism has not been established. No flashing commands or writer are included. The prior 79-block writer cannot install this 207-block proposal.

OpenWrt v25.12.2, Linux 6.12.74 and their packages retain their individual upstream licenses. OpenWrt is principally GPL-2.0; the Linux patch and board network integration are GPL-2.0. The DTS carries its original dual GPL-2.0-or-later OR MIT notice. LuCI retains Apache-2.0 and its NOTICE, included under licenses/. Package recipes identify additional licenses and source hashes. A repository root MIT license does not relicense these upstream works. New portable Python/shell tooling in this export is MIT; original upstream-derived files keep their original terms.

The build uses an unmodified upstream LuCI layout, except the flash menu is suppressed while upgrade support is unavailable. The underlying sysupgrade/mtd commands remain disabled. Native mount_root is restored for SquashFS/overlay. Chinese translations and a custom homepage are independent optional packages and are not embedded in this baseline.

The factory MAC parser reads the uniquely named tddp partition after checking its physical offset and size. No factory bytes, user MAC, backup, ART, host key, password, signing private key, device log or debug ELF is included here. Standard public verification keys needed by a build are generated/provided by its own build system, not exported from the local review build.
