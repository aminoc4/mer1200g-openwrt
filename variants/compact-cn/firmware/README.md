# Compact Chinese persistent source variant

This source variant adds two pinned upstream Chinese translation packages and the native MER homepage r3 to the independently built stock-LuCI persistent candidate. The exact candidate was subsequently installed on one Ver1.1 device and passed software-reboot persistence checks; this source export is not a general installer. Native forms and advanced menus remain; the flash menu and mtd/sysupgrade commands stay disabled. Only rootfs_data is writable. Real JFFS2 overlay, committed UCI configuration and SSH keys survived a subsequent software reboot. Bootloader and protected metadata/calibration hashes remained unchanged. Failed-boot recovery remains unverified; see hardware-acceptance.json for the limited tested scope.

Use the same three source archives and SHA256 values in sources.lock.json. On a supported Linux OpenWrt build host with Python3.10 extraction-filter support (or3.12+), pyelftools and normal upstream build prerequisites:

    python3 tools/prepare.py /path/to/archives /path/to/fresh-build
    BUILD_ROOT=/path/to/fresh-build sh build.sh
    python3 tools/package-review.py /path/to/fresh-build /path/to/new-review-output

Archive filenames and URLs are in sources.lock.json. prepare.py refuses an existing destination and verifies each source archive. Source config selects exactly the three additions and zh_Hans. Homepage packaging disables only its own JS minifier to preserve the exact device-accepted r3 file; all upstream dependencies retain the pinned LuCI versions. No private key is supplied. A fresh build has its own signing identity and may produce different bytes.

The same source procedure was checked against pristine archives through feeds/defconfig for the stock variant. This compact variant was actually built using the existing SDK cache, and its installed rootfs/package/kernel contents were independently audited. The added local-package copy and language/default configuration are explicit in the portable prepare source. No new clean-room full build or bit-for-bit reproduction is claimed. tools/audit_early.py also needs capstone.

Read NOTICE.md and licenses/. Root MIT does not relicense upstream GPL/Apache code. A binary release needs complete corresponding package/toolchain sources and licenses; this overlay alone does not satisfy that requirement. No device backup, private key, device identifiers, log or debug ELF belongs in this export.
