# 轻量中文首页

`luci-app-mer-home` 为现有 LuCI 增加一个“首页”：系统、网络、内存与北京时间、已识别设备，以及六个原生设置入口。只增加四个文本文件，约 10 KB；无第三方前端框架、远程字体、CDN 或遥测。原有 LuCI 菜单与表单继续保留。

首页使用五个只读 RPC，不写入配置、不放行端口。WAN 接口启动不等于联网成功，主机缓存记录不等于在线设备，IPv6 地址类型也不等于外网可达。北京时间仅为显示格式，不修改设备时区或校时设置。

## 构建

在已准备好的 OpenWrt 25.12.2 构建树中，固定 LuCI 版本为 `0.260323.75780~067535e`，执行：

```sh
BUILD_ROOT=/path/to/openwrt sh build-package.sh
```

脚本只编译独立包并检查 `.config` 不变。它不构建或刷写固件，不包含签名密钥；需要使用自己的构建签名密钥。简体中文翻译由上游 `luci-i18n-base-zh-cn` 与 `luci-i18n-firewall-zh-cn` 提供。

此目录仅包含界面源码。早期界面验收使用 initramfs 中的四个 RAM 文件；当前中文界面已集成到持久化镜像，并通过了软件重启后的配置保留检查。安装程序仍属于单独审查的设备操作，不由此源码目录提供通用刷写方法。

`test-home.cjs` 使用 Node 和确定性的 RPC 测试数据执行真实视图。将固定版本 LuCI 的 `modules/luci-base/htdocs/luci-static/resources/validation.js` 复制到自行创建的测试输出目录，再运行 `node test-home.cjs /path/to/output`。测试覆盖地址分类、错误状态、时间、文本转义及只读 RPC；输出目录还需上游 `bootstrap.css` 才能正确显示生成的布局预览。布局预览不代替实机验证。

新增界面代码采用 MIT 许可证；依赖的 OpenWrt、LuCI 等软件保留原许可证。

## 持久化候选

[firmware](firmware) 包含持久化候选的板级配置、完整构建配置、补丁和便携构建脚本。中文候选已构建、刷入一台 Ver1.1，并验证真实 JFFS2 overlay、UCI 提交值和 SSH 密钥跨软件重启保留。内核与保留的 stock-luci 候选相同，仅增加中文包及此首页。常规 sysupgrade 和网页刷写入口仍禁用；此源码目录不提供可直接执行的安装授权或已验证恢复方法。
