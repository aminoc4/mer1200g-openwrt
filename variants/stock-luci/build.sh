#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
: "${BUILD_ROOT:?Set BUILD_ROOT to the directory created by tools/prepare.py}"
task_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$BUILD_ROOT"
export SOURCE_DATE_EPOCH=1774469393
./scripts/feeds update -a
./scripts/feeds install -a -p luci
./scripts/feeds install -a -p webdeps
cp "$task_source/config.full" .config
make defconfig
# These are versions from the pinned LuCI commit, not the host clock.
make -j"${JOBS:-4}" V=s LUCI_MINIFY_LUA=0 LUCI_MINIFY_CSS=0 PKG_SRC_VERSION=0.260323.75780~067535e PKG_PO_VERSION=0.260323.75780~067535e
printf '%s\n' 'Build only. No installer or device command is supplied.'
