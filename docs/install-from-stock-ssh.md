# 从原厂 SSH 安装 OpenWrt

从 MER1200G 原厂系统已经开启 SSH、能够以 root 登录开始，按备份、准备镜像、RAM 接管、写入和启动的顺序整理。

**当前仓库提供固件构建源码；原厂 SSH 安装工具包与统一执行入口尚未发布。** 第 1～4 节给出可执行的准备步骤，第 5～7 节说明本机实际采用的安装过程和工具接口。安装器整理进度见 [开发记录](development-notes.md)。

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

将输出文件复制到 Windows 当前目录后，上传到路由器 `/tmp`，再下载到不同文件名校验传输结果：

```powershell
scp -O -P 33400 .\firmware-window.review.DO_NOT_FLASH.bin root@192.168.1.1:/tmp/mer1200g-v2-window.bin
scp -O -P 33400 root@192.168.1.1:/tmp/mer1200g-v2-window.bin .\mer1200g-v2-window.readback.bin
Get-FileHash .\firmware-window.review.DO_NOT_FLASH.bin -Algorithm SHA256
Get-FileHash .\mer1200g-v2-window.readback.bin -Algorithm SHA256
```

两份 SHA-256 应与 `review.json` 对应值一致。这样即使原厂 SSH 没有 `sha256sum`，也能核对上传后的完整文件。

## 5. 原厂 SSH 下实际如何进入刷写阶段

本机安装采用 **原厂 procd 升级通路 → RAM 环境 → 受限写入器**，保留原厂 bootloader。进入 RAM 后，旧根文件系统正常卸载，再开始擦写。

第一次从原厂启动 OpenWrt 时，使用过以下专用文件名：

| 路由器中的路径 | 用途 |
| --- | --- |
| `/tmp/mer1200g-rx4-experimental.uimage` | 最初的 OpenWrt initramfs 启动镜像 |
| `/tmp/mer_preflight_v3` | 镜像与原厂 MTD 预检查 |
| `/tmp/mer_writer_preflight` | RAM 内的闪存只读检查助手 |
| `/tmp/mer_install_write_v4.review` | 带 RAM 接管逻辑的首装写入器 |
| `/tmp/mer-install-write-v4.contract` | 与该写入器配套的执行契约 |

原厂阶段实际使用的预检查命令是：

```sh
/tmp/mer_install_write_v4.review --build-info
/tmp/mer_preflight_v3 --image /tmp/mer1200g-rx4-experimental.uimage --mtd /dev/mtd9
```

检查器核对镜像、NOR 几何、当前系统和原厂备份基线。配套文件及契约准备完成后，写入器复制到原厂升级机制使用的 `/tmp/upgraded`，通过以下实际接口接管：

```sh
cp /tmp/mer_install_write_v4.review /tmp/upgraded
chmod 500 /tmp/upgraded
ubus call system nandupgrade '{"path":"/tmp/mer-install-write-v4.contract"}'
```

上面是**早期 initramfs 首装工具的实际接口**，对应 79 个擦除块的旧启动镜像。当前 v2 的目标是完整 207 块窗口，原厂到 v2 的统一安装包需要配套整理；两者的镜像、契约和写入范围分别生成。

## 6. 写入进度与完成条件

RAM 接管后原厂 SSH 会退出，电脑通过独立进度接收器查看执行结果。首装工具曾向电脑 `192.168.1.2:39001/UDP` 发送进度；本机操作先启动接收器，再触发升级接口。

写入器依次执行：

1. 读取当前闪存快照，核对镜像和原厂数据。
2. 确认旧文件系统已卸载，待写区域没有继续作为运行根目录使用。
3. 按 64 KiB 块擦除、检查擦除结果、分页写入，并逐块读回。
4. 关闭写入描述符，完整回读目标窗口。
5. 比对 bootloader、厂商元数据和 ART 等窗口外区域。
6. 输出最终完成记录。

当前 v2 的实际升级记录采用 **207/207 块验证完成、完整窗口哈希一致、保护区域一致、退出码为 0** 作为成功条件。早期 79 块首装工具则使用对应的 `FINAL` 记录及校验字段。

## 7. 重启并核对安装结果

按安装器的结束方式重启：早期首装在最终记录完成后断电重启；后续带 RAM SSH 的持久版升级在全部读回通过后执行软件重启。

进入 OpenWrt 后，用新系统的 SSH 端口登录：

```powershell
ssh root@192.168.1.1
```

检查新系统、分区与可写挂载：

```sh
ubus call system board
cat /proc/mtd
mount
df -h /overlay
readlink /overlay/.fs_state
```

v2 中 `kernel` 为 3 MiB、`rootfs` 为 4.5 MiB、`rootfs_data` 为 5.4375 MiB；`/overlay` 挂载为 JFFS2，`.fs_state` 链接目标为 `2`。最后进行一次正常软件重启，复核配置和 SSH 密钥保留情况。

网口对应关系见 [机身网口映射](verified-ports.md)。安装镜像的 ROM 初始配置与当前实机已经提交的网口角色分别记录在 [开发记录](development-notes.md)。
