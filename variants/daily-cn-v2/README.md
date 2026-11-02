# MER1200G Ver1.1 轻量中文日用版 v2

沿用上游 LuCI OpenWrt2020 主题与简体中文翻译，只调整字体和间距。固件中移除 Bootstrap 与旧自制首页，不打包重复主题。历史原版 LuCI 和旧中文版本继续保留在相邻目录。

本目录是源码增量，不是官方机型支持声明，也不是自动刷写包。基线为 OpenWrt 25.12.2、Linux 6.12.74，依赖归档锁见 sources.lock.json。新增 PPPoE 配置页、DDNS 配置页和脚本、原生 HTTPS 证书生成。root 密码由用户自行设置；没有预置个人配置、密钥或无线密码。

## 构建

完整保留本仓库目录结构。在 Linux 的全新构建目标目录运行：

    python3 tools/prepare.py /path/to/archives /path/to/new-build
    BUILD_ROOT=/path/to/new-build sh build.sh
    python3 tools/package-review.py /path/to/new-build /path/to/new-output

prepare.py 复用 ../compact-cn/firmware 的固定源码解包与板级补丁，然后应用本目录的最终文件，不依赖作者电脑上的私有目录。需 Python 3.12+、pyelftools 与 OpenWrt 官方构建依赖。已安装固件使用现有构建树重建；本合并配方尚未完成一次全新环境的全量可复现构建。源码增量不能替代二进制再分发所需的全部对应源码。

## 存储与配置

16MiB SPI NOR。bootloader 为 0x000000–0x040000；kernel 为 0x040000–0x340000（3MiB）；rootfs 为 0x340000–0x7c0000（4.5MiB）；JFFS2 为 0x7c0000–0xd30000（5.4375MiB）。0xd30000 后的厂商信息、旧数据与 ART 保留只读。2.4GHz 与 5GHz 使用本机原厂 ART，分别偏移 0 和 0x1000。没有附校准数据。

迁移钩子保留已有主网名称和密码。IoT 使用 WPA2-PSK/CCMP、独立 br-iot 与 192.168.30.0/24：允许主网主动访问 IoT，拒绝 IoT 主动访问主网及路由器管理页，允许 IoT 到 WAN、DNS/DHCP 和必需 ICMPv6。全新设备若无有效无线密码则保持 IoT 关闭。DDNS 服务账号和域名由使用者配置。

## 验收范围

见 acceptance.json。真实 WAN 网口、PPPoE、前缀委派、外网 IPv6/DDNS、无线客户端关联与吞吐需要实际接线或客户端验收。不能用 QEMU 用户态迁移脚本测试替代 MT7620、交换机及无线硬件测试。Reset GPIO 尚未确认，常规 sysupgrade/mtd 和网页升级仍未开放；专用迁移流程不等同于通用升级支持。

首装曾发现预置 JFFS2 漏了根目录 `.fs_state -> 2` 符号链接，fstools 首启因此清空预置内容。CRC 正确不代表启动语义正确；普通文本文件不能代替此链接。设备在已完成原生初始化的 overlay 中恢复原配置后继续验收。该次私有安装镜像已停用，不作为发布物或后续刷写输入。

## 许可

沿用 ../compact-cn/firmware/NOTICE.md 与 licenses/。OpenWrt2020 cascade.css 来自固定 LuCI 源码，原许可见其文件头及上游主题目录；本仓库 MIT 不重新授权第三方代码。本目录没有固件二进制、bootloader、校准备份、设备日志或个人凭据。

机身 1～5 号口已逐口确认，实机角色已对齐 1 号 WAN、2～5 号 LAN；见 [网口映射与接线](../../docs/verified-ports.md)。这属于实机配置验收，不改写历史安装镜像或 ROM 恢复出厂默认值。
