# SPDX-License-Identifier: MIT
include $(TOPDIR)/rules.mk

LUCI_TITLE:=MER1200G lightweight Chinese homepage
LUCI_DEPENDS:=+luci-base
# Preserve the exact JS accepted on hardware as homepage r3.
LUCI_MINIFY_JS:=0
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_LICENSE:=MIT
PKG_MAINTAINER:=MER1200G OpenWrt contributors

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
