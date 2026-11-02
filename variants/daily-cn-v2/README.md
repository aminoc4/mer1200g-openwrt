# MER1200G 轻量中文日用版 v2

基于 **OpenWrt 25.12.2 / Linux 6.12.74**，采用上游 **LuCI OpenWrt2020** 主题和简体中文翻译，针对中文字体与页面间距做少量调整。

## 版本内容

| 项目 | 配置 |
| --- | --- |
| 界面 | OpenWrt2020、简体中文、原生 LuCI 表单 |
| 主无线 | 2.4GHz + 5GHz，当前实机主网使用 WPA3 |
| IoT 无线 | 2.4GHz、WPA2-PSK / CCMP、独立网段 |
| IoT 访问 | 主网可主动访问 IoT；IoT 主动访问主网及管理页由防火墙拦截 |
| 联网 | PPPoE、DHCPv6、IPv6 前缀请求 |
| 管理 | HTTPS、动态 DNS、原生防火墙规则页面 |
| 存储 | SquashFS + JFFS2，当前实机可写空间约 5.15 MiB |

镜像内移除 Bootstrap 和旧自制首页，保留单一主题。已有无线名称与密码由迁移钩子保留；管理员密码、宽带账号和 DDNS 服务由使用者配置。

接线与管理入口见 [项目首页](../../README.md)，内部端口对应关系见 [网口映射](../../docs/verified-ports.md)。

## 构建

准备 Linux 构建环境、Python 3.12+、pyelftools 和 OpenWrt 构建依赖。按 [sources.lock.json](sources.lock.json) 下载三个固定版本源码归档，放到同一目录。

从本目录执行，使用新的构建目标目录：

```sh
python3 tools/prepare.py /path/to/archives /path/to/new-build
BUILD_ROOT=/path/to/new-build sh build.sh
python3 tools/package-review.py /path/to/new-build /path/to/new-output
```

`prepare.py` 复用仓库中 `compact-cn/firmware` 的源码解包与板级补丁，再应用本版本的最终配置和文件。构建时保留完整仓库目录结构。

| 文件 | 用途 |
| --- | --- |
| `board.dts` | 板级硬件描述与分区布局 |
| `config.full` | 内核及软件包构建配置 |
| `files/` | 中文主题调整、首次启动配置及板级辅助函数 |
| `sources.lock.json` | 上游源码版本、下载地址与 SHA-256 |
| `build.sh` | 固定 LuCI 版本的构建入口 |
| `tools/package-review.py` | 内核封装与镜像窗口生成 |
| `MANIFEST.sha256` | 本目录文件校验清单 |

## 分区布局

| 分区 | 起始偏移 | 容量 |
| --- | --- | --- |
| bootloader | `0x000000` | 256 KiB |
| kernel | `0x040000` | 3 MiB |
| rootfs | `0x340000` | 4.5 MiB |
| rootfs_data | `0x7c0000` | 5.4375 MiB |
| 厂商信息、保留数据与 ART | `0xd30000` | 2.8125 MiB |

`rootfs_data` 为 JFFS2 可写分区。无线从本机原厂 ART 读取校准数据，2.4GHz 偏移为 `0`，5GHz 偏移为 `0x1000`。

## 配置与验收

- 主网：`192.168.1.0/24`，主无线桥接到 `br-lan`。
- IoT：`192.168.30.0/24`，IoT 无线桥接到 `br-iot`，提供 DHCP、DNS 和 IPv6 通告。
- 地区：北京时间、无线国家代码 `CN`。
- 原版 LuCI 与早期版本源码保留在相邻版本目录。

实机启动、重启持久化、网口链路、无线启动、IPv6 联网及各项测试状态见 [acceptance.json](acceptance.json)；构建与首装记录见 [开发记录](../../docs/development-notes.md)。

## 许可证

本版本沿用 [NOTICE](../compact-cn/firmware/NOTICE.md) 及对应组件许可证。OpenWrt2020 样式基于固定版本的上游 LuCI 源码，保留原有版权与许可。
