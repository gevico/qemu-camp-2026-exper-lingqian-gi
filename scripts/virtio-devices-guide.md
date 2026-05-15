# G233 VirtIO 设备使用指南

## 目录

1. [概述](#1-概述)
2. [QEMU 侧代码变更](#2-qemu-侧代码变更)
3. [Linux 内核配置](#3-linux-内核配置)
4. [virtio-rng](#4-virtio-rng)
5. [virtio-console](#5-virtio-console)
6. [virtio-9p](#6-virtio-9p)
7. [virtio-gpu](#7-virtio-gpu)
8. [完整启动示例](#8-完整启动示例)
9. [性能与兼容性](#9-性能与兼容性)
10. [测试方法](#10-测试方法)
11. [附录：内核配置片段](#11-附录内核配置片段)

---

## 1. 概述

G233 板级支持四类扩展 virtio 设备，通过 virtio-mmio 传输层与 guest 通信：

| 设备 | virtio ID | 功能 | QEMU 源文件 | Linux 驱动 |
|------|-----------|------|-------------|-----------|
| **virtio-rng** | 4 (`VIRTIO_ID_RNG`) | 硬件随机数生成器 | `hw/virtio/virtio-rng.c` | `CONFIG_HW_RANDOM_VIRTIO` |
| **virtio-console** | 3 (`VIRTIO_ID_CONSOLE`) | 字符设备 / 管理通道 | `hw/char/virtio-console.c` | `CONFIG_VIRTIO_CONSOLE` |
| **virtio-9p** | 9 (`VIRTIO_ID_9P`) | 宿主机目录共享 | `hw/9pfs/virtio-9p-device.c` | `CONFIG_NET_9P_VIRTIO` + `CONFIG_9P_FS` |
| **virtio-gpu** | 16 (`VIRTIO_ID_GPU`) | Framebuffer 显示输出 | `hw/display/virtio-gpu.c` | `CONFIG_DRM_VIRTIO_GPU` |

这些设备共享 G233 上现有的 **8 个 virtio-mmio 传输层实例**（地址 `0x10001000~0x10008FFF`，IRQ 1~8），也可通过 GPEX PCIe 总线接入 PCI 变体。

---

## 2. QEMU 侧代码变更

### 2.1 Kconfig 编译配置

**文件**: `hw/riscv/Kconfig`

```kconfig
config GEVICO_G233
    bool
    default y
    depends on RISCV32 || RISCV64
    select VIRTIO_MMIO          # virtio-mmio 传输层
    select VIRTIO_RNG           # virtio-rng 设备
    select VIRTIO_SERIAL        # virtio-serial 总线（virtio-console 依赖）
    select VIRTIO_9P            # virtio-9p 设备
    select FSDEV_9P             # 9P 文件系统后端
    ...
```

这些 `select` 确保对应设备的 `.c` 文件被编译进 `qemu-system-riscv64`。

### 2.2 板级 include

**文件**: `hw/riscv/g233.c`

```c
#include "hw/virtio/virtio-rng.h"      /* TYPE_VIRTIO_RNG    = "virtio-rng-device"    */
#include "hw/virtio/virtio-serial.h"   /* TYPE_VIRTIO_SERIAL = "virtio-serial-device" */
#include "hw/virtio/virtio-gpu.h"      /* TYPE_VIRTIO_GPU    = "virtio-gpu-device"    */
#include "hw/9pfs/virtio-9p.h"         /* TYPE_VIRTIO_9P     = "virtio-9p-device"     */
```

引入类型宏后，可在板级代码中直接引用这些设备的 QEMU 对象类型名称。

---

## 3. Linux 内核配置

四种设备需要在内核中开启对应驱动。在 `build/g233-kernel-extra.config` 中追加：

```kconfig
# === Already present (no change needed) ===
CONFIG_VIRTIO=y
CONFIG_VIRTIO_MMIO=y
CONFIG_VIRTIO_PCI=y
CONFIG_HW_RANDOM_VIRTIO=y           # virtio-rng 驱动

# === Need to add ===
CONFIG_VIRTIO_CONSOLE=y             # virtio-console 驱动
CONFIG_NET_9P=y                     # 9P 网络层
CONFIG_NET_9P_VIRTIO=y              # 9P virtio 传输
CONFIG_9P_FS=y                      # 9P 文件系统
CONFIG_9P_FS_POSIX_ACL=y            # 9P ACL 支持（可选）
CONFIG_DRM=y                        # DRM 框架
CONFIG_DRM_VIRTIO_GPU=y             # virtio-gpu DRM 驱动
```

> **注意**: 当前 `g233-kernel-extra.config` 已包含 `CONFIG_HW_RANDOM_VIRTIO=y`，因此 virtio-rng 在内核侧无需额外配置。

---

## 4. virtio-rng

### 4.1 原理

为 Linux 内核提供熵源，缓解 `random: crng init done` 长时间阻塞问题。设备使用 1 个 virtqueue，guest 将 buffer 放入队列，QEMU 从宿主机 `RngBackend`（默认 `/dev/urandom`）获取随机数回填。

### 4.2 驱动确认

```bash
# 确认内核已加载驱动
cat /sys/devices/virtio-ports/hwrng/rng_current
# 或检查 dmesg
dmesg | grep -i hwrng
```

### 4.3 启动参数

```bash
# 基本用法（VPCI 变体）
-device virtio-rng-pci

# Sysbus/MMIO 变体
-device virtio-rng-device

# 自定义速率限制（默认：max_bytes=INT64_MAX, period=65536ms）
-device virtio-rng-pci,max-bytes=1048576,period=1000
```

### 4.4 验证

```bash
# 查看熵池大小（应快速增长到 256 以上）
watch -n1 cat /proc/sys/kernel/random/entropy_avail

# 测试随机数生成速度
dd if=/dev/random of=/dev/null bs=1k count=64 2>&1 | tail -1

# 确认 CRNG 已初始化
dmesg | grep "crng init done"
```

---

## 5. virtio-console

### 5.1 原理

提供两级架构：
- **`virtio-serial-device`**：总线设备，管理多个端口（默认最多 31 个）
- **`virtconsole`**：端口设备，绑定到宿主机 chardev

两个 virtqueue 分别用于控制信息和数据收发。

### 5.2 启动参数

```bash
# ===== 作为独立管理串口（TCP socket 后端） =====
-chardev socket,id=ch1,path=/tmp/g233-console.sock,server=on,wait=off
-device virtio-serial-device
-device virtconsole,chardev=ch1,name=console.0

# ===== 作为标准输出重定向 =====
-chardev stdio,id=ch1,mux=on
-device virtio-serial-device
-device virtconsole,chardev=ch1,name=console.0

# ===== 结合启动参数，让 guest 输出到 virtio-console =====
-append "console=ttyS0 console=hvc0 root=/dev/vda rw"
```

### 5.3 连接客户端

```bash
# 终端 1：启动 QEMU（socket server 模式）
# 终端 2：连接 console
socat UNIX-CONNECT:/tmp/g233-console.sock STDIO
```

### 5.4 Guest 侧验证

```bash
# 确认 virtio-console 设备存在
ls /dev/hvc0 /dev/vport*

# 检查 sysfs 信息
cat /sys/class/virtio-ports/vport0p1/name
```

---

## 6. virtio-9p

### 6.1 原理

通过 9P2000.L 协议将宿主机目录映射到 guest。设备使用 1 个 virtqueue，最大支持 `MAX_REQ`（128）个并发请求。后端通过 QEMU 的 `fsdev` 子系统访问宿主机文件系统。

### 6.2 启动参数

```bash
# 基本用法
-fsdev local,id=fs1,path=/home/user/shared,security_model=mapped
-device virtio-9p-device,fsdev=fs1,mount_tag=hostshare

# PCI 变体
-fsdev local,id=fs1,path=/home/user/shared,security_model=mapped
-device virtio-9p-pci,fsdev=fs1,mount_tag=hostshare

# 不同安全模式
-fsdev local,id=fs1,path=/home/user/shared,security_model=passthrough
-fsdev local,id=fs1,path=/home/user/shared,security_model=none
```

### 6.3 Guest 侧挂载

```bash
# 基本挂载
mkdir -p /mnt/host
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host

# 带缓存选项
mount -t 9p -o trans=virtio,version=9p2000.L,cache=loose hostshare /mnt/host

# 带 msync 等选项
mount -t 9p -o trans=virtio,version=9p2000.L,cache=mmap,noatime hostshare /mnt/host

# 查看挂载信息
mount | grep 9p
```

### 6.4 验证

```bash
# 读写测试
echo "Hello G233!" > /mnt/host/test.txt
cat /mnt/host/test.txt

# 目录浏览
ls -la /mnt/host/

# 性能测试（简单）
dd if=/mnt/host/bigfile of=/dev/null bs=4k count=1024
```

### 6.5 安全模式说明

| 模式 | 说明 | 适用场景 |
|------|------|---------|
| `mapped` | QEMU 将文件操作映射到运行 QEMU 的用户权限 | 推荐；安全，兼容性好 |
| `passthrough` | 直接以 QEMU 进程用户身份操作文件 | 宿主机测试环境 |
| `none` | 不尝试映射 uid/gid | 容器场景 |

---

## 7. virtio-gpu

### 7.1 原理

最小 framebuffer 输出方案。设备使用 2 个 virtqueue：
- **控制队列**（64 项）：处理 2D 资源创建、scanout 设置、flush 命令
- **光标队列**（16 项）：处理光标更新

2D 模式下使用 pixman 在 QEMU 进程内完成软件渲染。每个 scanout 对应一个 `QemuConsole`，由 QEMU 显示后端（GTK/SDL/VNC）负责输出。

### 7.2 启动参数

```bash
# 基本用法（需要显示后端）
-display gtk
-device virtio-gpu-pci

# SDL 后端
-display sdl
-device virtio-gpu-pci

# VNC 后端（远程桌面）
-display vnc=:0
-device virtio-gpu-pci

# 多显示器
-display gtk
-device virtio-gpu-pci,max_outputs=2
```

### 7.3 Guest 侧配置

```bash
# 确认 DRM 设备
ls /dev/dri/
# 输出: card0  renderD128

# 查看 framebuffer
cat /proc/fb
# 输出: 0 virtiodrmfb

# 设置控制台分辨率（需内核支持）
# 在 U-Boot 或 cmdline 中:
video=640x480-32@60

# 或不使用 DRM，配置简单 framebuffer
# 需内核开启:
# CONFIG_FB_VIRTIO=y  # 如果存在）
# CONFIG_FB_SIMPLE=y
```

### 7.4 验证

```bash
# 查看 DRM 信息
dmesg | grep -i virtio-gpu

# 查看 connectors/encoders
cat /sys/kernel/debug/dri/0/state

# 测试显示（需要 fbset 或类似工具）
# 检查 framebuffer 内容
dd if=/dev/fb0 of=/tmp/fb.bin bs=1k count=1
```

---

## 8. 完整启动示例

### 8.1 启动脚本

**文件**: `build/boot-g233-full.sh`

```bash
#!/usr/bin/env bash
# Boot G233 with all virtio devices
set -euo pipefail

TOP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU="${TOP_DIR}/build/qemu-system-riscv64"
KERNEL="${TOP_DIR}/build/linux/arch/riscv/boot/Image"
ROOTFS="${TOP_DIR}/build/rootfs.ext4"
SHARED_DIR="${TOP_DIR}/shared"

mkdir -p "${SHARED_DIR}"

# Console socket path
CONSOLE_SOCK="/tmp/g233-console.sock"

# Remove stale socket
rm -f "${CONSOLE_SOCK}"

"${QEMU}" \
    -M g233 \
    -smp 2 -m 2G \
    -bios default \
    -kernel "${KERNEL}" \
    -append "console=ttyS0 root=/dev/vda rw" \
    -drive file="${ROOTFS}",format=raw,if=virtio \
    \
    # virtio-rng: 自动提供熵源
    -device virtio-rng-device \
    \
    # virtio-console: 独立管理串口
    -chardev socket,id=ch1,path="${CONSOLE_SOCK}",server=on,wait=off \
    -device virtio-serial-device \
    -device virtconsole,chardev=ch1,name=console.0 \
    \
    # virtio-9p: 共享目录
    -fsdev local,id=fs1,path="${SHARED_DIR}",security_model=mapped \
    -device virtio-9p-device,fsdev=fs1,mount_tag=hostshare \
    \
    # virtio-gpu: 显示输出（需 GTK/SDL 后端）
    -device virtio-gpu-device \
    \
    -nographic
```

### 8.2 使用方式

```bash
# 终端 1：启动 G233
./build/boot-g233-full.sh

# 终端 2（启动后）：连接 console
socat UNIX-CONNECT:/tmp/g233-console.sock STDIO

# 在 guest 中挂载共享目录
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
```

---

## 9. 性能与兼容性

### 9.1 virtio-rng

| 项目 | 说明 |
|------|------|
| **吞吐量** | 取决于宿主机 `/dev/urandom` 速度。默认无限速率。可通过 `max-bytes` + `period` 限速 |
| **延迟** | 微秒级；guest 提交 buffer → QEMU 读取 `/dev/urandom` → 回写通知 |
| **兼容性** | 所有 Linux 版本均支持；CRNG 初始化前特别有用 |
| **多队列** | 仅 1 个 vq，无多队列支持 |

### 9.2 virtio-console

| 项目 | 说明 |
|------|------|
| **吞吐量** | 取决于 chardev 后端。TCP socket 模式受网络延迟影响 |
| **并发端口** | 最多 31 个端口（可调 `max_nr_ports`） |
| **兼容性** | 所有 Linux 版本均支持；`hvc` 驱动成熟稳定 |
| **注意事项** | 控制台的 `chr_write` 回调不会启用 throttling（见 `virtio-console.c` 注释），大量写可能导致内存堆积 |

### 9.3 virtio-9p

| 项目 | 说明 |
|------|------|
| **吞吐量** | `cache=none` 模式约 100~200 MB/s；`cache=loose` 模式约 500~800 MB/s（受宿主机文件系统性能限制） |
| **协议版本** | 支持 9P2000.L（推荐）、9P2000.U |
| **兼容性** | 所有 Linux 版本均支持；常见限制：无 mmap 接口、无 fallocate |
| **多队列** | 仅 1 个 vq，最大 128 并发请求 |
| **已知限制** | `security_model=passthrough` 需 QEMU 进程有权限访问共享路径；`CONFIG_9P_FS` 需静态编译或模块加载 |

### 9.4 virtio-gpu

| 项目 | 说明 |
|------|------|
| **分辨率** | 默认 1280x720；可调 max_outputs（最多 16 个） |
| **渲染** | 2D 模式：pixman 软件渲染，CPU 开销较低；3D 模式（virgl）：需宿主机 OpenGL + `-display egl-headless,gl=on` |
| **兼容性** | Linux v4.4+ 支持 DRM virtio-gpu 驱动；需 `CONFIG_DRM_VIRTIO_GPU=y` |
| **帧率** | 2D 模式下受 QEMU 显示后端刷新率影响；`-display gtk` 约 30~60 fps |
| **注意事项** | 无 `-nographic` 情况下使用；需图形显示后端（GTK/SDL/VNC） |

### 9.5 资源占用

| 设备 | 内存占用 | virtqueue 深度 | IRQ |
|------|---------|---------------|-----|
| virtio-rng | ~2 KB | 8 | 1 个（共享 MMIO 实例的 IRQ） |
| virtio-console | ~8 KB + chardev buffer | 每端口一对 | 1 个 |
| virtio-9p | ~32 KB + 文件系统缓存 | 128（MAX_REQ） | 1 个 |
| virtio-gpu | ~16 MB（framebuffer） | control: 64, cursor: 16 | 1 个 |

---

## 10. 测试方法

### 10.1 QTest 测试（QEMU 单元测试）

四种设备的 QEMU 源码中已包含 qtest 测试用例：

```bash
# 运行单个设备的 qtest（需编译 qemu）
cd build

# virtio-rng test
meson test qtest-riscv64/virtio-rng-test --print-errorlogs

# virtio-console test
meson test qtest-riscv64/virtio-console-test --print-errorlogs

# virtio-9p test
meson test qtest-riscv64/virtio-9p-test --print-errorlogs

# virtio-gpu test（需要图形环境）
meson test qtest-riscv64/virtio-gpu-test --print-errorlogs
```

### 10.2 手动功能测试

```bash
# 1. 编译 QEMU
make -f Makefile.camp build

# 2. 使用完整启动脚本（见第 8 节）
./build/boot-g233-full.sh

# 3. 在 guest 中运行测试
# virtio-rng:
dd if=/dev/random of=/dev/null bs=1k count=64

# virtio-console:
echo "hello" > /dev/hvc0

# virtio-9p:
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/host
touch /mnt/host/test

# virtio-gpu:
cat /sys/kernel/debug/dri/0/state
```

### 10.3 各设备测试要点

| 设备 | 测试项 | 预期行为 |
|------|--------|---------|
| virtio-rng | `entropy_avail` 增长 | 数值从 0 快速增长到 256 以上 |
| virtio-console | socat 连接后收发数据 | 双向数据正常传输 |
| virtio-9p | 读/写/创建文件 | 文件操作与原生无差异 |
| virtio-gpu | `/dev/dri/card0` 存在 | DRM 设备可访问，`cat /dev/fb0` 有数据 |

---

## 11. 附录：内核配置片段

将以下内容追加到 `build/g233-kernel-extra.config`：

```kconfig
# ─────────────────────────────────────────────
# G233 virtio devices — kernel config fragment
# ─────────────────────────────────────────────

# virtio-console
CONFIG_VIRTIO_CONSOLE=y

# virtio-9p (Plan 9 Filesystem over virtio)
CONFIG_NET_9P=y
CONFIG_NET_9P_VIRTIO=y
CONFIG_9P_FS=y
CONFIG_9P_FS_POSIX_ACL=y

# virtio-gpu (DRM framebuffer)
CONFIG_DRM=y
CONFIG_DRM_VIRTIO_GPU=y
CONFIG_FB=y                     # framebuffer 支持（可选，用于 console fb）
CONFIG_FB_SIMPLE=y              # simplefb（可选，boot 阶段显示）

# virtio-rng (已存在, 无需重复添加)
# CONFIG_HW_RANDOM_VIRTIO=y
```

> **重要**: 修改后需重新编译内核：
> ```bash
> cd build/linux
> cat ../g233-kernel-extra.config >> .config
> make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
> make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc)
> ```
