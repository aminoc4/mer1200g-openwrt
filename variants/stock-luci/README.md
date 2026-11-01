# MER1200G persistent upstream-LuCI baseline source

Offline review only. Actual local build/audit passed; the candidate has **not booted**. Installation, writable overlay initialization, factory MAC application and recovery remain unverified. Wireless and WAN are disabled. This is a separate OpenWrt source baseline, not Mercury's stock firmware.

On a supported Linux OpenWrt build host, install upstream build prerequisites plus Python 3.10 with tarfile extraction-filter support (or Python 3.12+) and pyelftools. Download the three archives named in `sources.lock.json` from their listed URLs into one directory, using the listed filenames. `tools/prepare.py` verifies all SHA256 values before extraction. It refuses an existing build directory.

```sh
python3 tools/prepare.py /path/to/archives /path/to/fresh-build
BUILD_ROOT=/path/to/fresh-build sh build.sh
python3 tools/audit_early.py /path/to/fresh-build/build_dir/target-mipsel_24kc_musl/linux-ramips_mt7620/vmlinux.debug /path/to/fresh-build/build_dir/target-mipsel_24kc_musl/linux-ramips_mt7620/linux-6.12.74/.config
python3 tools/package-review.py /path/to/fresh-build /path/to/new-review-output
```

`audit_early.py` additionally requires capstone. Build downloads use each upstream recipe's pinned hash. LuCI commit and package versions, source epoch and builder identity are pinned; local build signing keys, toolchain/environment and package metadata can change output bytes. This is a reproducible **procedure**, not a proven clean-room bit-for-bit reproduction. The original local build reused a copied SDK cache; portable preparation is checked independently against pristine locked archives. Do not import old binary package repositories: higher wall-clock LuCI versions can override the pinned version; audit the final installed APK database.

The review constructor verifies the new ELF entry/direct jump, raw kernel/LZMA relationship, staging margin, SquashFS magic and capacities. It emits a one-component legacy Linux/MIPS/type4/LZMA uImage and a bounded review window. It is not a complete runtime/DT/filesystem acceptance audit. New builds need the same independent artifact review before any use.

Physical layout: bootloader 0x000000–0x040000 remains read-only; kernel 0x040000–0x440000 is read-only; rootfs 0x440000–0x940000 is read-only; rootfs_data 0x940000–0xd30000 is the only proposed writable partition. Metadata, stock JFFS2 and ART from 0xd30000 onward remain read-only. The marker in the new overlay is `deadc0de`; filesystem initialization can later write/erase it and has not been tested on this device. No power-loss atomicity or rollback is claimed.

The running protected RAM kernel lacks MTD_WRITEABLE, KEXEC, DEVMEM, spidev/UIO/PHRAM and an OF-overlay mechanism. SSH file transfer does not establish a flash or handoff path. Keeping a backup and bootloader bytes does not establish recovery transport. The review output is labeled DO_NOT_FLASH for these concrete reasons.

Read NOTICE.md before redistributing source or binaries. A complete binary release must provide corresponding sources/license notices for the exact compiled packages and toolchain, not merely this small board overlay.
