# Mercury MER1200G OpenWrt

水星 MER1200G v1 / Ver1.1 的 OpenWrt 适配项目，包含板级源码、构建配置和轻量中文管理界面。

当前版本：**[轻量中文日用版 v2](variants/daily-cn-v2/README.md)** · OpenWrt 25.12.2 · Linux 6.12.74

## 功能

- **轻量中文界面**：采用上游 LuCI OpenWrt2020 主题，调整中文字体与页面间距。
- **双频无线**：2.4GHz、5GHz 主网采用 WPA3；另设 WPA2 IoT 网络，兼顾智能家居设备。
- **IoT 独立网络**：主网可主动访问 IoT，IoT 到主网及管理页的主动访问由防火墙拦截。
- **联网与管理**：PPPoE、IPv6、动态 DNS、HTTPS 管理及原生 LuCI 防火墙配置页。
- **配置持久化**：SquashFS 系统搭配 JFFS2 可写分区，配置和 SSH 密钥在重启后保留。
- **空间优化**：移除重复主题和旧自制首页，当前实机配置后的可写空间约 **5.15 MiB**。

## 网口与接线

五个机身网口已逐口确认，均观察到千兆全双工连接。当前实机按以下方式配置：

| 机身网口 | 用途 | 接线 |
| --- | --- | --- |
| **1 号** | **WAN** | 光猫桥接业务绑定的 LAN 口 |
| **2～4 号** | LAN，可手动调整角色 | 电脑、交换机等内网设备 |
| **5 号** | LAN | 电脑或其他内网设备 |

WAN6 与 WAN 共用 1 号口。内部交换机端口、VLAN 对应关系见 [网口映射](docs/verified-ports.md)。

## 管理与联网

1. 电脑连接 LAN 口，打开 **[192.168.1.1](http://192.168.1.1/)**。管理员账号为 `root`，密码自行设置。
2. 进入 **网络 → 接口 → wan → 编辑**，选择 PPPoE，填写宽带账号和密码，保存并应用。
3. 将光猫桥接口接到 **1 号口**，在接口页面查看拨号状态；WAN6 通过 DHCPv6 请求 IPv6 地址和前缀。
4. 在 **网络 → 无线** 管理双频主网与 IoT，在 **服务 → 动态 DNS** 配置域名更新，在 **网络 → 防火墙** 管理访问规则。

主网网段为 `192.168.1.0/24`，IoT 网段为 `192.168.30.0/24`。主 Wi-Fi 接入主网，IoT Wi-Fi 接入独立网段。

## 版本目录

| 版本 | 内容 |
| --- | --- |
| **[daily-cn-v2](variants/daily-cn-v2/README.md)** | 当前版本：OpenWrt2020 中文界面、双频无线、独立 IoT、PPPoE / DDNS / HTTPS |
| [stock-luci](variants/stock-luci/) | 保留上游 LuCI 布局的源码版本 |
| [compact-cn](variants/compact-cn/) | 早期轻量中文首页版本 |
| [wireless-cn](variants/wireless-cn/README.md) | 双频无线适配的增量源码与记录 |

## 源码与开发

本仓库发布板级适配、界面文件、固定版本的构建配置及构建脚本。硬件配置为 **MT7620A、128 MiB RAM、16 MiB SPI NOR、RTL8367S 交换机**，无线使用本机原厂 ART 校准数据。

- [构建步骤与分区布局](variants/daily-cn-v2/README.md)
- [源码版本与归档校验值](variants/daily-cn-v2/sources.lock.json)
- [实机验收记录](variants/daily-cn-v2/acceptance.json)
- [开发记录与后续工作](docs/development-notes.md)
- [刷写实现设计](docs/flashing-design.md)

## 许可证

本项目新增代码使用 [MIT 许可证](LICENSE)。OpenWrt、Linux、LuCI 及其他组件沿用各自许可证，详见 [NOTICE](variants/compact-cn/firmware/NOTICE.md)。
