# xiaomi_kernel_for_warsaw_sukisu_susfs

Redmi K90 Ultra（代号 **warsaw**）自编译内核项目：**AOSP GKI 6.6.142 + SukiSU Ultra + SuSFS**，附带可复现的构建脚本与 boot.img 打包工具。

## 特性

- **内核**：AOSP GKI `android15-6.6` @ Linux 6.6.142，保留 GKI 的 LTO_CLANG_THIN / CFI_CLANG / DEBUG_INFO_BTF
- **Root**：SukiSU Ultra v4.3.0（KSU 版本 41100），官方签名校验
- **隐藏**：SuSFS v2.3.0（Tracepoint Syscall Redirect），10 个开关全开
- **构建**：`build_variant.sh` 一条命令完成配置变更 + 增量编译 + 打包校验（增量约 110 秒）
- **打包**：`pack_boot.py` 支持 boot.img v4 的内核替换与 AVB 尾部重算
- **验证**：`verify_susfs.sh` 一键检查 SUSFS 符号、版本号、镜像完整性

## 版本对应表

| 组件 | 版本 / 说明 |
|---|---|
| 设备 | Redmi K90 Ultra（`warsaw`，型号 M332BF） |
| 系统 | HyperOS 4.0.15 |
| 内核源码 | AOSP GKI `android15-6.6` @ 6.6.142 |
| SukiSU Ultra | `v4.3.0`（commit `e67b14e6`，KSU 41100） |
| SuSFS | `v2.3.0`（kernelsu 侧移植：14 文件 / +459 −12） |
| 工具链 | clang-r522817 + kernel-build-tools（pahole 1.25） |
| boot.img | header v4，header_size 1584，ramdisk_size 0，内核位于偏移 4096，总大小 **100663296** 字节 |

> **版本强绑定**：KSU 版本号 = `40000 + LOCAL_COUNT − 2815`，必须与管理器 APK 的 versionCode 完全一致，否则管理器会显示「未安装」。本仓库补丁把 `LOCAL_COUNT` 固定为 **3915**（→ 41100），对应官方 `SukiSU_v4.3.0_41100-release.apk`。

## 仓库结构

```
.
├── patches/
│   ├── susfs-for-sukisu-v4.3.0.patch   # 核心产出：SuSFS → SukiSU 移植
│   └── README.md                        # 补丁说明与已知边界
├── scripts/
│   ├── build_variant.sh                 # 一条命令：配置 + 增量编译 + 打包
│   ├── pack_boot.py                     # boot.img v4 重打包（AVB 感知）
│   └── verify_susfs.sh                  # 编译产物验收
├── docs/
│   ├── K90U_4.0.15_TEE深度排查报告.md
│   ├── 内核编译CPU功耗与效率实测报告.md
│   └── Redmi_K90_Ultra_Kernel_SukiSU_总结.md
├── ak3/
│   └── README.md                        # AnyKernel3 模板改动说明
├── LICENSE
└── README.md
```

## 快速开始

> **本仓库已包含完整编译所需的源码**（`kernel/` + `SukiSU-Ultra/`），两个补丁**均已应用**。
> 只有想从零复现补丁流程时，才需要执行下面的 1、2 步。

### 最简：clone 后直接编译

```bash
export CLANG_DIR=/path/to/clang-r522817
export KBT_DIR=/path/to/kernel-build-tools

# 使用仓库内置配置（已含 KSU + SuSFS 全部开关）
./scripts/build_variant.sh warsaw --clean --localver -android15-8-warsaw-4k
```

产物：`out-warsaw/arch/arm64/boot/Image`

配置文件：`configs/warsaw_6.6.142_defconfig`

### 0. 环境要求

#### 主机

| 项目 | 要求 |
|---|---|
| 系统 | Linux x86_64（实测 Debian/Ubuntu） |
| 磁盘 | **≥ 15 GB**（源码检出 1.8 GB + 构建产物 2.6 GB + 余量） |
| 内存 | ≥ 8 GB；`-j$(nproc)` 并行编译建议 16 GB |
| Python | 3.8+（`pack_boot.py` 依赖） |
| 基础工具 | `git`、`patch`、`zip`、`unzip`、`curl`、`make`、`bash` |

#### 工具链（**版本必须匹配**）

| 工具 | 说明 |
|---|---|
| **clang-r522817** | Android 15 对应的 LLVM 工具链。版本不匹配会导致 `LTO_CLANG_THIN` / `CFI_CLANG` 链接失败 |
| **kernel-build-tools** | 需提供 **pahole 1.25**，`DEBUG_INFO_BTF=y` 必需；缺失会报 `BTF: .tmp_vmlinux.btf: pahole not found` |
| LLVM binutils | `ld.lld` / `llvm-ar` / `llvm-objcopy`，clang 工具链自带 |

```bash
export CLANG_DIR=/path/to/clang-r522817
export KBT_DIR=/path/to/kernel-build-tools
export PATH=$CLANG_DIR/bin:$KBT_DIR/bin:$PATH
```

#### 源码（本仓库已附带，仅从零复现时需要）

| 需要 | 获取方式 |
|---|---|
| 内核源码 | AOSP GKI `android15-6.6`，checkout 到 **6.6.142** |
| SukiSU Ultra | `git clone https://github.com/SukiSU-Ultra/SukiSU-Ultra`，checkout **v4.3.0** |
| SuSFS 内核补丁 | [susfs4ksu](https://github.com/Simonpunk/susfs4ksu) 的 `kernel_patches/50_add_susfs_in_gki-android15-6.6.patch` |
| 原厂 boot.img | **需自行从设备 dump**（见下文），仅打包阶段需要 |

### 1. 内核侧：应用 SuSFS 补丁

```bash
cd kernel
patch -p1 < ../50_add_susfs_in_gki-android15-6.6.patch
```

### 2. KSU 侧：应用本仓库的移植补丁

```bash
cd SukiSU-Ultra
patch -p1 < ../patches/susfs-for-sukisu-v4.3.0.patch

# 在内核源码里挂软链接
ln -sfn ../../SukiSU-Ultra/kernel kernel/drivers/kernelsu
```

### 3. 编译

```bash
export CLANG_DIR=/path/to/clang-r522817
export KBT_DIR=/path/to/kernel-build-tools
export BOOT_TEMPLATE=/path/to/stock_boot.img   # 可选，用于自动打包

./scripts/build_variant.sh mytest --localver -android15-8-mytest-4k
```

**耗时实测**（16 线程）：clean build **358 秒**；仅改配置开关的增量 build **约 110 秒**。

成功时输出形如：

```
>>> clean 构建（不使用已有产物）
>>> 基础配置: .../configs/warsaw_6.6.142_defconfig
>>> 生效配置:
CONFIG_KSU=y
CONFIG_KSU_DEBUG=y
# CONFIG_KPM is not set
CONFIG_LOCALVERSION="-android15-8-mytest-4k"
>>> MAKE_EXIT=0  耗时 358 秒
-rwxrwxr-x ... out-mytest/arch/arm64/boot/Image
>>> 内核版本: Linux version 6.6.142-android15-8-mytest-4k
```

产物：`out-mytest/arch/arm64/boot/Image`（约 38 MB）。

### 4. 打包 boot.img

```bash
python3 scripts/pack_boot.py pack stock_boot.img \
    out-mytest/arch/arm64/boot/Image \
    boot_warsaw_mytest.img
```

### 5. 刷入

```bash
fastboot flash boot boot_warsaw_mytest.img
```

## 关键实现说明

### SuSFS 移植策略

上游 susfs4ksu 的补丁是针对**官方 KernelSU** 编写的。直接套到 SukiSU 会混入大量上游重构（删除 SukiSU 的 `ksu_late_loaded`、修改 `ksu_handle_execveat_ksud` 签名、删除 `hook/lsm_hook.h` 的 include），导致编译报大量 undeclared identifier。

本仓库的做法：

1. 只保留含 `susfs` / `CONFIG_KSU_SUSFS` / `SUSFS_MAGIC` / `CMD_SUSFS_` 的 hunk，**逐行**挑出 SUSFS 相关改动
2. 跳过所有上游重构，不做任何「顺手清理」
3. 不修改 SukiSU 新增的扩展体系（`kernel/extension/`）

最终成果：14 个文件、+459/−12，与官方 KernelSU 上的等效实现逐行核对一致。

### boot.img v4 布局

Xiaomi 的 boot 分区（4.0.15）关键字段：

| 偏移 | 内容 |
|---|---|
| 0 | boot header（`header_version = 4`，`header_size = 1584`，`ramdisk_size = 0`） |
| 8 | `kernel_size`（u32 LE） |
| 4096 | 内核 Image 起始 |
| `round_up(4096 + kernel_size, 4096)` | vbmeta blob |
| 文件末尾 64 字节 | AVB footer（大端：`original_image_size`@12、`vbmeta_offset`@20、`vbmeta_size`@28） |

替换内核时必须把 **vbmeta blob 整体重定位**到新的页对齐位置，并重写 footer 的三个字段，否则 AVB 校验失败。`pack_boot.py` 已处理这些细节，并保证输出文件大小与模板完全一致（100663296 字节）。

### 获取原厂 boot.img

```bash
# 从设备直接读取
adb shell su -c 'dd if=/dev/block/by-name/boot of=/sdcard/stock_boot.img'
adb pull /sdcard/stock_boot.img
```

> 本仓库**不附带**原厂 boot.img。它是 Xiaomi 的专有镜像，分发涉及版权问题，请自行提取。

## 已知问题

### 1. KPM 需要 KernelPatch

v4.3.0 起，KPM 不再是内置加载器，而是 **KernelPatch 的薄桥接**：

```c
/* Both built-in and LKM modes use the same KernelPatch kernel-only bridge. */
fn = find_kernel_symbol_exact("__arm64_sys_truncate");
if (fn(&regs) != SUKISU_KPM_ABI_VERSION) return -EOPNOTSUPP;
```

**只开 `CONFIG_KPM=y` 不够**，还需要 [SukiSU_KernelPatch_patch](https://github.com/ShirkNeko/SukiSU_KernelPatch_patch)。

症状：管理器显示「KPM 不支持」，`ksud kpm list` 返回 `os error 95 (EOPNOTSUPP)`。

### 2. TEE 与 KPM 的历史问题

v4.2.x 时代，开启 `CONFIG_KPM=y` 会导致 KeyMint 失败：

```
boot_level_keys.rs:120 → getKeyCharacteristics → Upgrade failed
→ Error::Km(INVALID_ARGUMENT) → Boot stage key absent
→ security_level.rs:318 Failed to handle super encryption → Error::Rc(LOCKED)
```

表现为 **CE 存储不可用、第三方应用打不开**。根因是结构性问题（链接布局副作用），不是逻辑 bug。

**v4.3.0 已不复现** —— 因为 KPM 被改成了薄桥接层，内核改动量大幅减少。

### 3. SuSFS 补丁版本绑定

移植补丁是针对 **SukiSU v4.3.0** 的。SukiSU 每次更新都可能改动 `dispatch.c`、`selinux_hide.c` 等文件，届时需要重新 rebase。

### 4. 不要回锁 bootloader

本项目的镜像是用 **Xiaomi 的密钥**签名的（AVB vbmeta 原样保留）。回锁 bootloader 会因为签名不匹配而**变砖**。

## 许可证

- 本仓库的补丁、脚本与文档：**GPL-3.0**（见 [LICENSE](LICENSE)）
- Linux 内核：**GPL-2.0 WITH Linux-syscall-note**
- [SukiSU Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra)：**GPL-3.0**
- [susfs4ksu](https://github.com/Simonpunk/susfs4ksu)：**GPL-3.0**

由于移植补丁是 SukiSU / susfs4ksu 的衍生作品，整体按 GPL-3.0 分发。

## 致谢

- [AOSP GKI](https://source.android.com/docs/core/architecture/kernel/generic-kernel-image)
- [SukiSU Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra)
- [susfs4ksu](https://github.com/Simonpunk/susfs4ksu)
- [KernelPatch](https://github.com/bmax121/KernelPatch) / [APatch](https://github.com/bmax121/APatch)
- [AnyKernel3](https://github.com/osm0sis/AnyKernel3)

## 免责声明

刷机有风险。本项目仅用于学习与研究，不保证任何设备的安全性与稳定性。因使用本项目导致的设备损坏、数据丢失或保修失效，作者不承担责任。
