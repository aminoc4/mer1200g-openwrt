#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
task_source=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${BUILD_ROOT:?Use an OpenWrt25.12.2 tree with the pinned LuCI067535e feed already prepared}"
cd "$BUILD_ROOT"
test ! -e package/luci-app-mer-home
test -f feeds/luci/luci.mk
cp -a "$task_source/package" package/luci-app-mer-home
task_config_before=$(sha256sum .config)
export SOURCE_DATE_EPOCH=1774469393
make -j"${JOBS:-2}" V=s LUCI_MINIFY_LUA=0 LUCI_MINIFY_CSS=0 PKG_SRC_VERSION=0.260323.75780~067535e PKG_PO_VERSION=0.260323.75780~067535e CONFIG_PACKAGE_luci-app-mer-home=m package/luci-app-mer-home/compile
test "$(sha256sum .config)" = "$task_config_before"
printf '%s\n' 'Built package only; .config unchanged. Sign with your own existing build key; no signing key is provided.'
