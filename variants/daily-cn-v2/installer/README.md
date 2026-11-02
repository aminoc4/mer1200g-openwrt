# 原厂 SSH 安装工具

用于 MER1200G Ver1.1 的原厂 128 MiB RAM / 16 MiB NOR 系统，将 daily-cn-v2 安装窗口写入物理范围 `[0x040000, 0xd30000)`。保留原厂 bootloader、厂商信息、tddp 和 ART。

完整操作顺序见 [原厂 SSH 安装文档](../../../docs/install-from-stock-ssh.md)。入口在电脑运行，密码由终端输入。

| 入口 | 操作 |
| --- | --- |
| `inspect` | 读取原厂分区、内存和闪存，核对本机备份 |
| `stage` | 上传工具与镜像，下载读回并运行只读检查 |
| `rehearse` | 通过原厂 procd 进入 RAM，卸载旧系统并完成只读快照检查 |
| `flash` | 核对同一工具包的演练记录，执行 207 块擦写和完整读回 |

演练和刷写均在最终记录通过后停留于 RAM，由操作者断电重启。演练完成后返回原厂，再执行刷写；`flash` 会检查两次启动的 boot ID 不同。

## 生成专属于本机的工具包

在 Linux / WSL 中使用固件构建产生的 MIPSEL musl 工具链：

```sh
export STAGING_DIR=/path/to/openwrt-build/staging_dir
python3 build_bundle.py \
  --backup /path/to/mer1200g-full-backup.bin \
  --image /path/to/firmware-window.review.DO_NOT_FLASH.bin \
  --output /path/to/mer-install-private \
  --cc /path/to/openwrt-build/staging_dir/toolchain-mipsel_24kc_gcc-14.3.0_musl/bin/mipsel-openwrt-linux-musl-gcc
```

生成器核对 uImage CRC、MIPS 入口跳转、DTB、128 MiB 内存、全部分区及空白 overlay，随后按本机原厂备份生成校验策略、随机会话标识和静态工具。输出目录包含 `manifest.json`、`image.bin`、四个 MIPSEL 程序和生成后的源码。该目录保存在自己电脑上。

## 离线测试

需要主机 GCC、QEMU 用户态 `qemu-mipsel` 和上述交叉编译器：

```sh
python3 test_bundle.py --bundle /path/to/mer-install-private \
  --backup /path/to/mer1200g-full-backup.bin \
  --cc /path/to/mipsel-openwrt-linux-musl-gcc
python3 test_controller.py --bundle /path/to/mer-install-private \
  --backup /path/to/mer1200g-full-backup.bin \
  --cc /path/to/mipsel-openwrt-linux-musl-gcc
```

测试使用内存 NOR 和模拟 SSH，覆盖 207 块边界、55 组写入核心测试、故障注入、ASan/UBSan、MIPS 用户态 IPC 和电脑端进度判定。

## 电脑端入口

电脑直连原厂 LAN，地址设为 `192.168.1.2/24`；路由器为 `192.168.1.1`、SSH 端口 `33400`。安装 Python 3 和 Paramiko：

```powershell
py -m pip install paramiko
py .\install.py inspect --bundle C:\mer-install-private
py .\install.py rehearse --bundle C:\mer-install-private
# 演练 PASS 后断电重启，等原厂重新启动
py .\install.py flash --bundle C:\mer-install-private
```

电脑接收 `192.168.1.1 → 192.168.1.2:39001/UDP` 的进度。程序核对会话、镜像、序列号、连续三条 FINAL、目标读回和保留区域；日志写入工具包内的 `run-*` 目录。`result.json` 中 `completed: true` 表示该次操作通过。

## 验证状态

当前 207 块工具已通过上述离线测试；原厂系统直接安装 daily-cn-v2 的完整硬件流程尚待实机复验。RAM 接管基于此前已实机完成的 79 块首装实现，daily-cn-v2 内核和根文件系统已在本机正常启动并通过重启持久化验收。

## 源码文件

- `build_bundle.py`：镜像校验、生成本机策略、交叉编译。
- `install.py`：SSH 核对、上传、启动演练或刷写。
- `collector.py`：接收及核对进度。
- `source/stock_preflight.c`：流式只读检查。
- `source/enabled_entry.c`、`handover_ops.c`：原厂 PID1 到 RAM 的接管。
- `source/writer_*.c`：限定范围写入、监督和读回。
- `test_bundle.py`、`test_controller.py`、`source/test_*.c`：离线测试。

新增代码使用 [MIT 许可证](LICENSE)。
