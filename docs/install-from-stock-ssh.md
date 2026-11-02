# 从原厂 SSH 安装 OpenWrt

从 MER1200G 原厂系统已经开启 SSH、能够以 root 登录开始，按备份、准备镜像、RAM 接管、写入和启动的顺序整理。

仓库提供 [原厂 SSH 安装工具源码](../variants/daily-cn-v2/installer/README.md)，流程为：备份 → 构建固件与本机工具包 → 只读演练 → 返回原厂 → 正式刷写 → 启动验收。

当前 207 块安装器已通过离线与 QEMU 用户态测试，原厂直接安装 v2 的完整硬件流程待实机复验；v2 系统本身已在 MER1200G Ver1.1 上运行并通过重启验收。

## 1. 电脑连接与登录

电脑通过网线连接原厂系统的 LAN 口。以下以原厂管理地址 `192.168.1.1` 为例；已经修改地址时使用实际地址。

Windows PowerShell 登录：

```powershell
ssh -p 33400 root@192.168.1.1
```

也可以使用 WinSCP 的 SCP 协议连接：

| 项目 | 内容 |
| --- | --- |
| 文件协议 | SCP |
| 主机 | 路由器当前 LAN 地址 |
| 端口 | 33400 |
| 用户 | root |
| 密码 | 已取得的原厂 SSH 密码 |

WinSCP 使用 SCP 传文件，终端操作可通过“命令 → 在 PuTTY 中打开”进入。

## 2. 核对原厂系统与分区

以下命令在路由器 SSH 中执行：

```sh
uname -a
cat /proc/mtd
cat /proc/meminfo
df -h /tmp
cat /proc/cmdline
```

本机原厂系统的分区如下：

| 节点 | 名称 | 大小 |
| --- | --- | --- |
| mtd0 | bootloader | `0x040000`，256 KiB |
| mtd1 | kernel | `0x150000` |
| mtd2 | rootfs | `0xba0000` |
| mtd3 | device-info | 64 KiB |
| mtd4 | firmware-info | 64 KiB |
| mtd5 | tddp | 64 KiB |
| mtd6 | log | 128 KiB |
| mtd7 | rootfs_data | 2 MiB |
| mtd8 | art | 64 KiB |
| mtd9 | firmware | 16 MiB，整片闪存映射 |

后续备份命令按这张原厂分区表对应节点。OpenWrt 的分区编号在安装后会改变。

## 3. 备份并下载到电脑

在原厂 SSH 中执行：

```sh
dd if=/dev/mtd9 of=/tmp/mer1200g-full-backup.bin
dd if=/dev/mtd0 of=/tmp/mer1200g-bootloader.bin
dd if=/dev/mtd5 of=/tmp/mer1200g-tddp.bin
dd if=/dev/mtd8 of=/tmp/mer1200g-art.bin

ls -l /tmp/mer1200g-*.bin
md5sum /tmp/mer1200g-*.bin
```

完整备份应为 **16,777,216 字节**，bootloader 为 **262,144 字节**，tddp 和 ART 各为 **65,536 字节**。整片备份已经包含后三者，单独导出便于核对和后续读取。

在电脑新开 PowerShell，将备份下载到本地：

```powershell
New-Item -ItemType Directory -Force .\mer1200g-backup
scp -O -P 33400 root@192.168.1.1:/tmp/mer1200g-full-backup.bin .\mer1200g-backup\
scp -O -P 33400 root@192.168.1.1:/tmp/mer1200g-bootloader.bin .\mer1200g-backup\
scp -O -P 33400 root@192.168.1.1:/tmp/mer1200g-tddp.bin .\mer1200g-backup\
scp -O -P 33400 root@192.168.1.1:/tmp/mer1200g-art.bin .\mer1200g-backup\

Get-ChildItem .\mer1200g-backup\*.bin | Get-FileHash -Algorithm MD5
```

将电脑端 MD5 与路由器端对应文件的 MD5 逐项核对。`-O` 表示使用传统 SCP 协议，与原厂文件传输服务对应。WinSCP 也可直接从 `/tmp` 下载这四份文件。

后续上传镜像前，电脑端备份校验完成即可删除路由器 RAM 中的这四个临时副本，释放空间；电脑上的备份保留。

```sh
rm /tmp/mer1200g-full-backup.bin /tmp/mer1200g-bootloader.bin /tmp/mer1200g-tddp.bin /tmp/mer1200g-art.bin
df -h /tmp
```

## 4. 准备当前版本镜像

在 Linux / WSL 中，按 [源码锁文件](../variants/daily-cn-v2/sources.lock.json) 下载源码归档。保留完整仓库结构，从仓库根目录进入版本目录构建：

```sh
cd variants/daily-cn-v2
python3 tools/prepare.py /path/to/archives /path/to/new-build
BUILD_ROOT=/path/to/new-build sh build.sh
python3 tools/package-review.py /path/to/new-build /path/to/output
```

构建环境与文件说明见 [版本构建文档](../variants/daily-cn-v2/README.md#构建)。输出包括：

| 文件 | 内容 |
| --- | --- |
| `kernel.multi-uimage.DO_NOT_FLASH` | 原厂引导格式对应的内核封装组件 |
| `rootfs.squashfs` | 系统根文件系统组件 |
| `firmware-window.review.DO_NOT_FLASH.bin` | 从物理偏移 `0x040000` 开始的安装窗口数据 |
| `review.json` | 文件大小、SHA-256 与写入范围 |

这些文件由专用 RAM 安装器装载和写入。安装窗口的起点为 `0x040000`，终点为 `0xd30000`，长度 **13,565,952 字节**，共 **207 个 64 KiB 擦除块**。

生成与上传前的校验值：

```sh
sha256sum /path/to/output/kernel.multi-uimage.DO_NOT_FLASH
sha256sum /path/to/output/rootfs.squashfs
sha256sum /path/to/output/firmware-window.review.DO_NOT_FLASH.bin
```

## 5. 编译本机安装工具包

安装器保留原厂 bootloader，通过原厂 procd 进入 RAM，正常卸载旧根文件系统，再执行限定范围写入。它使用本机原厂整片备份生成校验策略。

在 Linux / WSL 的仓库根目录执行。`STAGING_DIR` 和 `--cc` 使用第 4 节固件构建目录内的工具链，示例路径替换为自己的实际路径：

```sh
export STAGING_DIR=/path/to/new-build/staging_dir
python3 variants/daily-cn-v2/installer/build_bundle.py \
  --backup /mnt/c/mer1200g-backup/mer1200g-full-backup.bin \
  --image /path/to/output/firmware-window.review.DO_NOT_FLASH.bin \
  --output /mnt/c/mer-install-private \
  --cc /path/to/new-build/staging_dir/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin/mipsel-openwrt-linux-musl-gcc
```

输出的 `C:\mer-install-private` 包含镜像、四个静态工具、`manifest.json` 和本机校验策略。它与刚才的原厂备份配套使用。生成器会核对 uImage CRC、MIPS 入口、DTB 内存/分区布局和空白 JFFS2 初始化标记。

离线测试命令见 [安装工具文档](../variants/daily-cn-v2/installer/README.md#离线测试)。

## 6. 电脑端检查和上传

电脑网卡设为 **192.168.1.2 / 255.255.255.0**，直连原厂 LAN；路由器地址为 **192.168.1.1**。这套入口固定使用这些地址和原厂 SSH 端口 **33400**。路由器暂时只连接这台电脑，拔下其他业务网线。

安装 Python 3 和 Paramiko，在 Windows PowerShell 中从仓库根目录执行：

```powershell
py -m pip install paramiko
py .\variants\daily-cn-v2\installer\install.py inspect --bundle C:\mer-install-private
```

首次连接显示路由器 SSH 指纹，核对后输入 `yes`，再输入原厂 root 密码。密码从终端读取。`inspect` 只读核对原厂 10 个分区、128 MiB 内存及备份对应的原厂数据。

进度通过 `192.168.1.1 → 192.168.1.2:39001/UDP` 接收。Windows 防火墙需要允许这个入站通路；可在管理员 PowerShell 中为直连网卡创建规则（将 `以太网` 替换为实际网卡名称）：

```powershell
New-NetFirewallRule -DisplayName "MER1200G install progress" -Direction Inbound -Action Allow -Protocol UDP -LocalAddress 192.168.1.2 -LocalPort 39001 -RemoteAddress 192.168.1.1 -InterfaceAlias "以太网" -Profile Any
```

接着上传并检查：

```powershell
py .\variants\daily-cn-v2\installer\install.py stage --bundle C:\mer-install-private
```

`stage` 上传工具和镜像，读回校验 SHA-256，再运行路由器端流式只读检查。后续入口也会重新核对这些文件。

## 7. 只读演练

```powershell
py .\variants\daily-cn-v2\installer\install.py rehearse --bundle C:\mer-install-private
```

程序先启动进度接收器，再调用一次原厂升级接口进入 RAM。原厂 SSH 和网页会退出，电脑端继续显示阶段进度。此步骤读取完整闪存快照、核对数据、完成旧系统卸载，不执行固件区域擦写。

看到下面的最终结果后，将路由器断电约 5 秒再上电，等待原厂系统重新启动：

```text
PASS read-only snapshot and RAM handover. Power-cycle the router to return to original stock.
```

演练记录保存在工具包的 `rehearsal.json`，详细记录在 `run-*` 目录。正式刷写会核对同一工具包、同一 SSH 主机密钥以及已经改变的 boot ID。

## 8. 正式刷写

回到原厂系统后，在电脑执行：

```powershell
py .\variants\daily-cn-v2\installer\install.py flash --bundle C:\mer-install-private
```

入口重新检查原厂和文件，进入 RAM 后，逐块擦除、检查、分页写入、读回校验。目标共 **207 块**，物理范围 `[0x040000, 0xd30000)`；bootloader 和范围外的数据保持原样。

程序核对连续三条 FINAL：207 块完成、完整窗口 SHA-256 一致、范围外数据一致、写入描述符关闭及错误码为 0。最终显示：

```text
PASS final readback and preserved regions. Power-cycle the router to boot OpenWrt.
```

此时断电约 5 秒再上电，启动 OpenWrt。对应 `run-*` 目录的 `result.json` 应为 `completed: true`。若未收到 PASS，先查看该目录的 `result.json`、`progress.json` 和 `packets.jsonl`；SSH 断开本身不代表完成。

## 9. 启动验收

新系统 SSH 端口为 **22**，原厂为 **33400**。首次启动后，电脑登录：

```powershell
ssh root@192.168.1.1
```

在新系统执行：

```sh
ubus call system board
cat /proc/mtd
mount
df -h /overlay
readlink /overlay/.fs_state
passwd
```

应识别为 MER1200G；`kernel` 为 3 MiB、`rootfs` 为 4.5 MiB、`rootfs_data` 为 5.4375 MiB；`/overlay` 为 JFFS2，`.fs_state` 为 `2`。`passwd` 由自己设置 root 密码。

验证配置写入并正常重启：

```sh
uci set system.@system[0].hostname='MER1200G'
uci commit system
reboot
```

重新登录后，`uci get system.@system[0].hostname` 应仍为 `MER1200G`，SSH 主机密钥保持不变，`/overlay` 正常挂载。

当前镜像首次启动时五个网口均为 LAN；已运行设备的“1 号 WAN、2～5 号 LAN”是后续提交的配置。机身和交换机端口的对应关系见 [网口映射](verified-ports.md)。
