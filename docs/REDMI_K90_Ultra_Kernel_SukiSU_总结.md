# Redmi K90 Ultra (warsaw) 自编译内核 —— SukiSU Ultra + SuSFS 完整记录

> 最终成果：基于 AOSP GKI 6.6.142 源码，集成 SukiSU Ultra + SuSFS，自编译内核成功开机并稳定运行。
> 版本：6.6.142-android15-8-wjljh-2026y8m16d1430-4k
> 日期：2026-08-16 ~ 2026-08-17

---

## 1. 项目背景与目标

- 设备：Redmi K90 Ultra（代号 warsaw，骁龙 8 Elite / SM8750 平台，Android 15 MIUI HyperOS 2）
- 目标：在小米开源内核基础上，集成 **SukiSU Ultra**（内核级 Root）+ **SuSFS**（隐藏 Root 的模块），产出可开机的刷机包
- 踩坑过程中最终放弃了小米 warsaw-w-oss 源码（6.6.57，缺失大量厂商文件），改用 **AOSP GKI android15-6.6 分支**（6.6.142，与官方模块同 KMI）

## 2. 环境与前置条件

| 项目 | 值 |
|---|---|
| 构建机 | Ubuntu 22.04（wjljh-deskbook），8 核 / 15G 内存 |
| 磁盘 | 55G（峰值占用 47G，构建后清理至 20G 可用） |
| 代理 | HTTP/SOCKS5: 127.0.0.1:12000（必须；git/curl 用 HTTP/1.1 防断连） |
| 工具链 | **clang-r510928**（Google 定制 18.0.0，+pgo/+bolt/+lto/+mlgo，AOSP prebuilts） |
| 辅助 | AOSP kernel-build-tools（含 **pahole v1.25**，BTF 必需） |
| ADB | 设备 USB 直连（用于刷机测试、抓 log、提取官方配置） |

### 2.1 关键环境变量/技巧

```bash
# 代理（git 必须 HTTP/1.1，否则 TLS 被掐断）
export HTTPS_PROXY=http://127.0.0.1:12000 HTTP_PROXY=http://127.0.0.1:12000
git -c http.version=HTTP/1.1 clone ...

# HOME 只读（/home/wjljh 不可写）→ 重定向
export HOME=/home/wjljh/wjljh/DeepSeek-Harness-Temp/home

# 工具链 PATH（构建 GKI 时）
export PATH=$PWD/prebuilts/kernel-build-tools/linux-x86/bin:$PWD/prebuilts/clang/host/linux-x86/clang-r510928/bin:$PATH
```

## 3. 内核源码

### 3.1 最终采用：AOSP GKI android15-6.6（官方 GKI 源码）

```bash
# 下载分支 HEAD（SUBLEVEL=142）
curl -L -x http://127.0.0.1:12000 -o gki142.tar.gz \
  'https://android.googlesource.com/kernel/common/+archive/refs/heads/android15-6.6.tar.gz'
tar xzf gki142.tar.gz -C common/
# 验证版本
head -6 common/Makefile   # VERSION=6 PATCHLEVEL=6 SUBLEVEL=142
```

- AOSP 发布 tag 只到 android15-6.6-2025-10（6.6.102）；6.6.142 是分支最新（2026-08 初）
- 与设备官方固件（6.6.142）同 KMI，官方 /vendor 模块可加载

### 3.2 弃用：小米 warsaw-w-oss（6.6.57）

- 缺失大量文件：`drivers/misc/hwid/`、`drivers/power/supply/mca/`（symlink 指向不存在的 `vendor/xiaomi/proprietary/`）、DTS、完整 defconfig 等
- 即使补齐 stub 能编译，启动也失败（源码版本过旧 + 构建参数问题）

## 4. 构建流程与 .config 详解

### 4.1 集成 SukiSU + SUSFS

```bash
cd common
# SukiSU：symlink + Makefile/Kconfig
ln -sf ../../SukiSU-Ultra/kernel drivers/kernelsu
echo 'obj-$(CONFIG_KSU) += kernelsu/' >> drivers/Makefile
sed -i '/endmenu/i\source "drivers/kernelsu/Kconfig"' drivers/Kconfig

# SUSFS：补丁 + 文件
cp ../susfs4ksu/kernel_patches/50_add_susfs_in_gki-android15-6.6.patch .
cp -r ../susfs4ksu/kernel_patches/fs/* fs/
cp -r ../susfs4ksu/kernel_patches/include/linux/* include/linux/
patch -p1 --fuzz=3 < 50_add_susfs_in_gki-android15-6.6.patch

# SukiSU 附加补丁
cp ../SukiSU_patch/69_hide_stuff.patch . && patch -p1 -F 3 < 69_hide_stuff.patch
```

### 4.2 配置（★ 最关键的一步）

```bash
make ARCH=arm64 LLVM=1 O=../out gki_defconfig

# ★★★ 必须手动开启（gki_defconfig 默认 LTO_NONE！）
scripts/config --file ../out/.config --disable LTO_NONE
scripts/config --file ../out/.config --enable LTO_CLANG_THIN
scripts/config --file ../out/.config --enable DEBUG_INFO_BTF
scripts/config --file ../out/.config --enable DEBUG_INFO_BTF_MODULES

# 版本串
scripts/config --file ../out/.config --disable LOCALVERSION_AUTO
scripts/config --file ../out/.config --set-str LOCALVERSION "-android15-8-wjljh-2026y8m16d1430-4k"

make ARCH=arm64 LLVM=1 O=../out olddefconfig
```

### 4.3 .config 关键项清单

| 配置项 | 值 | 说明 |
|---|---|---|
| CONFIG_LTO_CLANG_THIN | y | **★ 启动关键**（默认 LTO_NONE，kleaf 构建时用 --lto=thin 覆盖） |
| CONFIG_CFI_CLANG | y | 依赖 LTO 才真正生效 |
| CONFIG_DEBUG_INFO_BTF | y | **★ 启动关键**（需要 pahole；AOSP kernel-build-tools 自带 v1.25） |
| CONFIG_DEBUG_INFO_BTF_MODULES | y | 模块 BTF |
| CONFIG_UNWIND_TABLES | y | 栈回溯 |
| CONFIG_DYNAMIC_SCS | y | 影子调用栈 |
| CONFIG_KSU | y | SukiSU |
| CONFIG_KSU_SUSFS | y | SuSFS（子项 SUS_MOUNT/SUS_KSTAT/SUS_MAP/SPOOF_UNAME 等） |
| CONFIG_MODVERSIONS | y | 符号 CRC（KMI） |
| CONFIG_MODULE_SIG | y | 模块签名（不强制） |
| CONFIG_LOCALVERSION | "-android15-8-wjljh-2026y8m16d1430-4k" | 版本串 |
| CONFIG_LOCALVERSION_AUTO | n | 关闭自动版本 |

### 4.4 编译

```bash
make ARCH=arm64 LLVM=1 O=../out -j8
# 产物：out/arch/arm64/boot/Image（约 38MB，与官方同级别）
```

### 4.5 打包 boot.img（与原版大小一致 100663296 字节）

- 用**设备当前 boot_a 分区**（dump 的 device_boot_a.img）作模板：header + kernel + padding + vbmeta + footer
- 更新 header 的 kernel_size（offset 8）
- AVB footer 字段为**大端**（vbmeta_offset 等在 offset+20 处用 >Q 解析）

### 4.6 打包 AnyKernel3

- 使用 SukiSU 官方框架（Wild Kernels 的 AK3：旧版小写变量 block=boot、do.devicecheck=0、split_boot/flash_boot 逻辑）
- 内核文件用 **raw Image**（非 Image.gz）

## 5. 踩坑全记录（血泪史）

### 5.1 网络/环境类
1. **代理**：github.com 直连被断；代理在 127.0.0.1:12000；HTTP/2 长连接被掐（TLS 断连）→ git 必须 `-c http.version=HTTP/1.1` + 大 postBuffer
2. **HOME 只读**：`~/.gitconfig`、`~/.repoconfig`、`~/.rustup` 都写不进 → 用 `HOME=/workdir/home` 或显式 `RUSTUP_HOME/CARGO_HOME`
3. **sdkmanager 不走代理**：JAVA_OPTS 和 --proxy 参数都无效 → 手动 curl 下载组件 zip
4. **pkill 自杀**：`pkill -f 'make ARCH=arm64'` 匹配到了自己的命令行

### 5.2 小米源码（warsaw-w-oss）类
5. **hwid/mca 缺失**：symlink 指向 `vendor/xiaomi/proprietary/`（小米私有目录未发布）→ 用 stub 头文件/Kconfig/Makefile 替代
6. **qti_battery_charger 被注释**：Makefile 里 `# obj-$(CONFIG_QTI_BATTERY_CHARGER)` 但其他模块依赖其导出符号 → 取消注释
7. **rsmc 写死 obj-m**：`drivers/rsmc/Makefile` 无条件编译，引用缺失符号 → 改为 `obj-$(CONFIG_CETC_RSMC)`
8. **defconfig 基础错误**：骁龙内核基础是 **gki_defconfig**（含 CONFIG_ANDROID_VENDOR_HOOKS），不是 defconfig（否则 WALT 编不过）
9. **DTS/完整 defconfig 缺失**：小米不发布，无法出 dtb/完整配置

### 5.3 编译类
10. **★ 核心坑：GKI 必须 LTO_THIN + CFI + BTF**——v1~v10 全部开机卡死（watchdog 复位，4~15 秒），根因是：
    - gki_defconfig 默认 **LTO_NONE**（kleaf 构建用 --lto=thin 覆盖，手动 make 必须显式开启）
    - CFI 依赖 LTO 才真正生成检查代码
    - BTF 需要 pahole（AOSP kernel-build-tools 自带，Ubuntu 没有）
    - **判定方法**：对比官方内核 Image（38MB）与自编译（30MB）的字符串数量（官方 16.6 万 vs 我们 6.1 万 → 差的是 BTF）；提取官方内核 IKCONFIG 逐项对比
11. **extract-cert.c key_pass bug**（6.6.142 上游）：PKCS#11 分支缺 #ifdef → 手动补
12. **abi_symbollist.raw 缺失**：GKI 的 KMI 机制要求该文件 → touch 空文件
13. **modpost undefined 链**：内建驱动依赖的框架（IPC_LOGGING、CRM_V2、WALT、UFS_CRYPTO_QTI、PHY_QCOM_UFS、REGULATOR_PROXY_CONSUMER、DEBUG_DMA_BUF_REF 等）也要内建
14. **duplicate symbol msm_dma_buf_create**：MEM_BUF 与 DMABUF_HEAPS 同时内建冲突 → 开 DEBUG_DMA_BUF_REF 或关其中一个
15. **-Werror 指针判断**：SukiSU 的 selinux_hide.c 里 `if (function_name)` 死代码 → 直接调用
16. **版本串 + 后缀**：LOCALVERSION_AUTO=n 时 git 仓库 dirty 或不在 annotated tag 上会加 `+` → commit 全部修改 + 打 annotated tag `v6.6.142`

### 5.4 启动/运行类
17. **早期 hang（非 panic）**：crash_history 无 kpanic 记录、mtdoops 不触发（早期 hang 在 watchdog 喂狗前）→ 判断为 hang 而非 panic
18. **gh-watchdog（hypervisor）**：设备有 Gunyah hypervisor watchdog，内核须在超时前初始化，否则复位
19. **管理器冲突**：KSU-Next 管理器与 SukiSU 内核共存导致应用打不开（/data/adb/ksud 争夺）→ 卸载 KSU-Next
20. **SUSFS 默认功能**：SUS_PATH 默认关、SUS_MAP/SUS_MOUNT 开（激进功能可能影响应用）

### 5.5 打包类
21. **boot.img 布局变化**：官方 6.6.118 固件的 vbmeta 在 36.8MB；当前 6.6.142 固件布局 vbmeta 在 38.2MB（kernel 区更大）→ 用设备当前 boot_a 作模板
22. **AVB footer 大端**：字段是 big-endian（u64 用 >Q 解析）
23. **第三方内核（38MB）放不进旧布局**：kernel 38.2MB > 旧布局 36.8MB 空间
24. **AK3 框架版本**：新版（大写变量 BLOCK=）与 SukiSU 刷入环境不兼容 → 用官方包框架（小写 block=、do.devicecheck=0）

### 5.6 工具/流程类
25. **repo 工具 --u bug**：ShirkNeko 构建脚本用 `--u`（双横线）导致 repo 2.65 报错 → 改 `-u`
26. **构建系统脚本硬编码**：bazel 的 --disk_cache=/home/runner（GitHub Actions 路径）→ 绕过 bazel 手动 make
27. **编译环境 12G**：AOSP prebuilts（clang + build-tools）很大，注意磁盘管理

## 6. 版本演进（v1 ~ v12）

| 版本 | 源码 | 工具链 | LTO/CFI/BTF | 结果 |
|---|---|---|---|---|
| v1 | 小米 6.6.57 + SukiSU | clang 14 | 无 | 卡 4-5s |
| v2-v4 | 小米 6.6.57 系列 | clang 14 | 无 | 卡 |
| v5 | GKI HEAD 6.6.142 | clang 14 | 无 | 卡 4-5s |
| v6 | GKI HEAD | clang 14 无LTO | 无 | 卡 |
| v7 | GKI HEAD | clang 18.1.8 | LTO/CFI ✓ 无BTF | 卡 10s |
| v8 | GKI HEAD + KernelSU | clang 18.1.8 | 同上 | 卡 10s |
| v9 | **AOSP tag 6.6.102** + SukiSU+SUSFS | **r510928** | LTO_NONE（未开） | 卡 10-15s |
| v10 | tag 6.6.102 | r510928 | LTO_THIN ✓ 无BTF | 卡 |
| **v11** | tag 6.6.102 | r510928 | **LTO+CFI+BTF 全开** | ✅ **开机** |
| **v12** | **HEAD 6.6.142** | r510928 | 全开 | ✅ **开机（正式版）** |
| **v13** | HEAD 6.6.142 | r522817（clang 18.0.1） | 全开 + KPM + IKHEADERS + KSU_DEBUG | ✅ **开机（KSU 40879，管理器包名内置）** |

**结论**：LTO_CLANG_THIN + CFI + BTF 三者齐全（+ r510928）是 GKI 能开机的必要条件。

## 7. 最终交付物

| 文件 | 说明 |
|---|---|
| **boot_warsaw_6.6.142-android15-8-wjljh-2026y8m16d1430-4k_v13.img** | **v13 正式版 boot 镜像（96MB，KSU 40879 + KPM + IKHEADERS + 管理器包名内置）** |
| **AnyKernel3-warsaw-6.6.142-...-2026y8m27d1608-4k-final.zip** | **v13 正式版 AK3 刷机包** |
| boot_warsaw_6.6.142-android15-8-wjljh-2026y8m16d1430-4k_v12.img | v12 备用（6.6.142，KSU 40879） |
| AnyKernel3-warsaw-6.6.142-...-v12-final.zip | v12 AK3 |
| boot_warsaw_6.6.102-sukisu_v11_btf.img | v11 备用（6.6.102） |
| AnyKernel3-...-v11-fullalign.zip | v11 AK3 |
| SukiSU_v4.1.3_40796-release.apk | SukiSU 管理器（官方 release，KSU 40796） |
| **SukiSU_v4.1.3_40879-release.apk** | **自编译管理器（versionCode 40879 = 内核 KSU 版本，R8 优化 + V2 签名）** |

## 8. 刷机与验证

```bash
# 临时测试（不写入，卡死重启即可恢复）
fastboot boot boot_warsaw_6.6.142-...-v12.img
# 正式刷入
fastboot flash boot boot_warsaw_6.6.142-...-v12.img
# 或 SukiSU 管理器刷 AK3 zip
```

验证：
- uname -r = 6.6.142-android15-8-wjljh-2026y8m16d1430-4k
- su（u:r:ksu:s0）✓
- SukiSU 管理器识别内核 ✓
- 应用/蓝牙/指纹等正常 ✓

## 9. 后续优化方向（待体验后决定）

1. sysctl 调优脚本（调度/内存参数，零风险）
2. 关闭冗余 SUSFS 功能（SUS_MAP 等有 hook 开销）
3. -O3 编译（需测试稳定性）
4. PGO/BOLT（与官方差距主要来源，工作量最大）
5. 与官方差异：无 WALT（骁龙专用调度器，GKI 内核没有）、无厂商私有调优、无 PGO/BOLT

## 10. SukiSU 管理器 APK 构建（✅ 完成）

> 目标：自编译 SukiSU 管理器 APK，versionCode **40879** 与内核 KSU 版本严格匹配
>（设备上旧管理器 40837 / 官方 release 40796 都会触发"管理器过旧"提示）

### 10.1 构建环境

| 组件 | 版本/来源 |
|---|---|
| JDK | Temurin 21.0.5+11（AGP 9.3.1 需要 JDK 17+，项目要求 21） |
| Android SDK | platform-tools + **build-tools 37.0.0** + **platforms;android-37.0/37.1** + **cmake 3.22.1**（AGP 9.x 自动补齐） |
| NDK | **29.0.14206865**（r29；项目默认 28.2.13676358，改 libs.versions.toml 为 29） |
| Rust | 1.97.1（rustup 安装 aarch64-linux-android / armv7-linux-androideabi / x86_64-linux-android 三个 target） |
| Gradle | 9.5.1（wrapper）+ AGP 9.3.1 + Kotlin 2.4.10 + Compose |

### 10.2 ksud 交叉编译（Rust）

```bash
export CARGO_HOME=$HOME/.cargo RUSTUP_HOME=$HOME/.rustup
cargo install --locked --path ksud --target aarch64-linux-android --target-dir target-aarch64
# 同理 armv7 / x86_64
# 产出三个 libksud.so，复制到
cp libksud.so SukiSU-Ultra/manager/app/src/main/jniLibs/{arm64-v8a,armeabi-v7a,x86_64}/
```

### 10.3 Gradle 构建（关键环境变量）

```bash
export HOME=/home/wjljh/wjljh/DeepSeek-Harness-Temp/home   # /home/wjljh 只读！
export GRADLE_USER_HOME=$HOME/gradle_home                   # Gradle 缓存
export ANDROID_USER_HOME=$HOME/.android                     # AGP/SDK 缓存（默认写 /home/wjljh/.android 会失败）
export JAVA_HOME=.../jdk-21.0.5+11  ANDROID_HOME=.../android-sdk
export HTTPS_PROXY=http://127.0.0.1:12000 HTTP_PROXY=http://127.0.0.1:12000
# gradle.properties 中 org.gradle.jvmargs 必须带 -Duser.home=...（daemon JVM 不继承 GRADLE_OPTS！）
./gradlew assembleRelease --no-daemon
```

### 10.4 版本号机制（关键坑）

`manager/build.gradle.kts` 中：

```kotlin
fun getVersionCode(): Int {
    val commitCount = getGitCommitCount()          // git rev-list --count HEAD
    return 4 * 10000 + commitCount - 2815          // = 40879 当 commitCount = 3694
}
fun getVersionName(): String = getGitDescribe()    // git describe → v4.1.3
```

- **浅克隆坑**：`git clone --depth=1` 时 `rev-list --count` = 1 → versionCode 37186 ❌
- 解决：`git fetch --unshallow origin`（走代理 HTTP/1.1）→ commitCount = **3694** → versionCode = **40879** ✓

### 10.5 踩坑记录

| # | 问题 | 现象 | 解决 |
|---|---|---|---|
| 1 | HOME 只读 | Gradle 无法写 /home/wjljh/.gradle、.android | 重定向 HOME + GRADLE_USER_HOME + ANDROID_USER_HOME |
| 2 | Kotlin 类加载崩溃 | `Could not generate a decorated class for KotlinCompile / MultiModuleICSettings` | 清空 gradle_home 缓存重试（依赖下载损坏） |
| 3 | Ninja 缺失 | `[CXX1416] Could not find Ninja on PATH or in SDK CMake bin folders` | 手动下载官方 cmake-3.22.1-linux.zip 解压到 SDK cmake/3.22.1（内含 bin/ninja） |
| 4 | SDK 许可未接受 | `Failed to install ... as some licences have not been accepted` | 创建 licenses/android-sdk-license（hash 24333f8a...）等两个许可文件 |
| 5 | SDK 包解析不一致 | 手工目录不被 AGP 认可（source.properties 缺 Pkg.Path / Desc 错误） | 让 AGP 自行下载官方包安装到 `android-37.0` / `build-tools/37.0.0`（-2 目录改名回正式名） |
| 6 | sdkmanager 不走代理 | JAVA_OPTS/--proxy 无效 | 手动 curl dl.google.com 下载组件 zip |
| 7 | NDK 版本不符 | 要求 28.2.13676358 实际装 r29 | libs.versions.toml 改 29.0.14206865 + symlink |
| 8 | compileSdk 语法 | minorApiLevel 块报错 | 简化为 `compileSdk = androidCompileSdkVersion`（37） |

### 10.6 交付物验证

```bash
aapt dump badging SukiSU_v4.1.3_40879-release.apk
# package: name='com.sukisu.ultra' versionCode='40879' versionName='v4.1.3'
# sdkVersion 26 / targetSdkVersion 37
apksigner verify --print-certs SukiSU_v4.1.3_40879-release.apk   # V2 签名 ✓
# 内含 lib/{arm64-v8a,armeabi-v7a,x86_64}/libksud.so + libkernelsu.so + libadbroot.so
```

- 最终产物：**SukiSU_v4.1.3_40879-release.apk**（11.4 MB，R8 压缩 + 资源收缩 + V2 签名，debug 证书可覆盖安装官方版）

---
*文档生成：2026-08-17 01:30*


---

## 11. v13 更新记录（2026-08-27）

> 起因：v12 使用期间 f2fs 损坏 → SukiSU 管理器识别失败（开机 APK 签名扫描读不到文件）。
> v13 目标：**KSU_MANAGER_PACKAGE 内置**（绕开签名扫描）+ KSU_DEBUG 自救接口 + 值得加的功能。

### 11.1 v13 相对 v12 的改动

| 项目 | v12 | v13 | 说明 |
|---|---|---|---|
| KSU_MANAGER_PACKAGE | 无 | `com.sukisu.ultra`（make 变量编入） | **核心修复**：f2fs 损坏不再掉管理员（throne_tracker 机制） |
| CONFIG_KSU_DEBUG | n | y | set-manager 调试接口，管理员丢失可一条命令自救 |
| CONFIG_KPM | n | y | Kernel Patch Module（SukiSU 内核级模块系统） |
| CONFIG_IKHEADERS | n | y | /sys/kernel/kheaders.tar.xz，设备上直接编模块 |
| CONFIG_MAGIC_SYSRQ | n | y | 假死时主动 dump/重启 |
| CONFIG_SCHEDSTATS | n | y | 调度器统计（Scene 可读更多信息） |
| 工具链 | clang-r510928 | clang-r522817（18.0.1） | master 分支已移除 r510928 |
| KSU 版本 | 40879 | **40879**（固定本地 commit 数） | 与管理器 APK 严格匹配 |

### 11.2 v13 关键构建细节

1. **SukiSU-Ultra 用 susfs_new 分支**（v12 同款，278d822a）——SUSFS 功能由分支内置 feature（adb_root/selinux_hide/uts_spoof）提供，**不需要** fs/susfs.c / CONFIG_KSU_SUSFS（v12 验证过该组合）
2. **版本号固定**：Kbuild 里 `LOCAL_COUNT := $(git_commit_count)`（本地 3694 → 40000+3694-2815=40879），否则走 GitHub API 会得到 40889 与 APK 不匹配
3. **69_hide_stuff.patch 需手工调整**：dentry 块被 50_add_susfs 补丁的 #ifdef 挤进 CONFIG_KSU_SUSFS_OPEN_REDIRECT 内 → 移到 SUS_KSTAT #endif 之后（否则 unused variable 报错）
4. **boot.img 打包**：v13 Image 42.2MB > 模板 kernel 区（38.2MB）→ vbmeta 必须从 38182912 移到 kernel 之后（42184704），footer 的 original_size/vbmeta_offset 同步更新（大端），总大小保持 100663296

### 11.3 v13 交付物

| 文件 | 说明 |
|---|---|
| boot_warsaw_6.6.142-android15-8-wjljh-2026y8m16d1430-4k_v13.img | v13 boot（100663296 字节，与原版一致） |
| AnyKernel3-warsaw-6.6.142-...-2026y8m27d1608-4k-final.zip | v13 AK3 刷机包 |
| SukiSU_v4.1.3_40879-release.apk | 管理器 APK（与 v13 内核 KSU 版本匹配） |

### 11.4 刷机后验证

```bash
# root 下
uname -r                                    # 6.6.142-android15-8-wjljh-2026y8m16d1430-4k-v13
zcat /proc/config.gz | grep -E 'KSU_DEBUG|KPM|IKHEADERS|MAGIC_SYSRQ|SCHEDSTATS'
dmesg | grep -i 'manager pkg'               # manager pkg: com.sukisu.ultra
dmesg | grep -i 'Crowning'                  # Crowning manager: com.sukisu.ultra(uid=...)
ls /sys/kernel/kheaders.tar.xz              # IKHEADERS 生效
```

若管理员仍异常：`su -c '/data/app/*/com.sukisu.ultra*/lib/arm64/libksud.so debug set-manager'`


---

## 12. v13 最终版更新记录（2026-08-27 下午）

> 上午 v13 初版刷机后出现两个问题，最终版已全部修复。

### 12.1 问题与修复

| # | 问题 | 根因 | 修复 |
|---|---|---|---|
| 1 | SukiSU 管理器显示"未安装" | 内核默认只认官方签名（shirkneko 证书 947ae944...）；自编译 APK 是 debug 签名（f5fea73f...），签名校验失败 is_manager:0 | 内核 Kbuild 默认签名改为自编译 APK 签名（0x2e8 / f5fea73f） |
| 2 | "当前内核包含 PR 签名支持"警告 | 曾用 KSU_EXPECTED_SIZE2/HASH2 加第二签名 → 内核 dispatch.c 检测到 EXPECTED_SIZE2 即置 KSU_GET_INFO_FLAG_PR_BUILD | 改用 SIZE1 单签名（去掉 SIZE2），不再触发 PR 标志 |

### 12.2 最终交付物（版本串更新为 2026y8m27d1608）

| 文件 | 说明 |
|---|---|
| **boot_warsaw_6.6.142-android15-8-wjljh-2026y8m27d1608-4k.img** | **最终版 boot（100663296 字节）** |
| AnyKernel3-warsaw-6.6.142-...-2026y8m27d1608-4k-final.zip | AK3（Image 已更新为最终版） |
| SukiSU_v4.1.3_40879-release.apk | 管理器（签名已编入内核，开机即识别） |

### 12.3 签名机制要点（网友"编译产物附带 APK"之谜）

- SukiSU 官方每次 release 都会附带**官方签名** APK（release.yml 的 manager/*.apk，签名=shirkneko 证书 947ae944...，与内核默认 EXPECTED_HASH 一致）
- 官方 CI 用 GitHub secrets 里的 key.jks 签名（公开仓库拿不到）；PR 构建用临时 key 并作为 SIZE2 编入内核（触发 PR 标志）
- 网友的 40837 APK = 官方每日 dev 版（官方签名），所以"编译产物里那个 APK 签名跟官方一样"——因为就是官方 CI 签的
- 我们的最终方案：内核单签名 = 自编译 APK 签名，版本 40879 完全匹配，无 PR 警告

### 12.4 f2fs 修复确认（✅ 已解决）

- TWRP 下正确执行：`umount -l /sdcard` 后 `fsck.f2fs -f -y /dev/block/dm-8`（userdata 有 metadata 加密，必须对 dm 映射跑；TWRP 把 data 挂 /sdcard 需先卸载）
- fsck 结果：356220 节点全检通过，修复 SIT types，28 秒完成
- 新内核 /data（dm-46）挂载正常，无 f2fs 错误
- 结合内核内置 KSU_MANAGER_PACKAGE=com.sukisu.ultra，f2fs 损坏不再导致管理员丢失

### 12.5 当前设备状态（2026-08-27 晚）

- 内核：6.6.142-android15-8-wjljh-2026y8m27d1608-4k（KSU 40879，KPM+IKHEADERS+KSU_DEBUG）
- su 正常（u:r:ksu:s0），SukiSU 管理器"工作中"，无 PR 警告
- f2fs 已修复，系统稳定运行

---

## 13. v14 更新记录：OS4 兼容修复（2026-09-01）

> 起因：系统 OTA 升级到 OS4（Android 17 / OS4.0.0.8.XHPCNXM）后，自编译内核出现第三方应用全部卡死、桌面崩溃的问题。

### 13.1 OS4 升级后的故障现象

| 现象 | 说明 |
|---|---|
| 第三方应用全部卡死/崩溃 | 只有系统应用（设置等）能打开 |
| 桌面（com.miui.home）崩溃 | 进桌面就闪退 |
| USB 反复断连 | 设备反复重启（v13 时代） |
| 锁屏凭据解密失败 | SyntheticPasswordCrypto.decryptBlob 报 Failed to decrypt blob |

### 13.2 排查过程（二分定位法）

| 版本 | SukiSU | KPM | KSU_DEBUG | IKHEADERS | 结果 |
|---|---|---|---|---|---|
| v13 | v4.1.3 susfs_new | ✅ | ✅ | ✅ | ❌ seccomp 崩溃循环（每 7 秒 WARNING） |
| v14 完整 | v4.2.0 main | ✅ | ✅ | ✅ | ❌ SP 解密失败（不崩但应用全挂） |
| v14 无KPM | v4.2.0 | ❌ | ❌ | ❌ | ✅ 正常 |
| v14 KPM | v4.2.0 | ✅ | ❌ | ❌ | ✅ 正常 |
| **v14 最终** | v4.2.0 | ✅ | ✅ | ❌ | ✅ **正常（元凶定位：IKHEADERS）** |

### 13.3 根因：CONFIG_IKHEADERS 与 OS4 system_dlkm 冲突

```
sysfs: cannot create duplicate filename '/kernel/kheaders.tar.xz'
init_module+0x38/0xfe4 [kheaders]
```

- 我们开启了 CONFIG_IKHEADERS（内置内核头文件打包），同时 OS4 的 **system_dlkm 自带 kheaders 模块**（6.6.118-android15-8-.../kernel/kheaders.ko）
- 两者都想创建 /kernel/kheaders.tar.xz → **sysfs 重复创建冲突** → 破坏内核 kobject/sysfs 结构 → 间接导致 keystore2/KeyMint 异常 → SP 密钥升级失败 → 应用全崩
- **修复：关闭 CONFIG_IKHEADERS**（设备上直接编模块的需求用不到，冲突得不偿失）

### 13.4 v13 崩溃的单独根因（seccomp）

- v13（SukiSU v4.1.3 susfs_new）在 OS4 上 escape_with_root_profile → seccomp_filter_release 每 7 秒 WARNING
- 原因：**6.6 内核的 seccomp_filter_release 有 backport 差异**（has_call_to_spin_lock 检测），旧版 SukiSU 未适配
- v4.2.0 已修复：f5ecd138 fix(kernel): adapt to backport of 6.6 kernel, fix #3629 (#3632)

### 13.5 最终 v14 配置与交付

| 项 | 值 |
|---|---|
| SukiSU | v4.2.0 main（08ba26ef） |
| KSU 版本 | **40900**（对齐官方 release） |
| 签名 | 947ae944（官方签名，官方管理器直接识别） |
| KPM | ✅ 开 |
| KSU_DEBUG | ✅ 开 |
| IKHEADERS | ❌ **关（关键修复）** |
| 版本串 | 6.6.142-android15-8-wjljh-2026y9m1d2148-4k |
| 管理器 | SukiSU v4.2.0 官方版（40900） |

**交付物**：deliverables/boot_warsaw_6.6.142-android15-8-wjljh-2026y9m1d2148-4k_v14.img（100663296 字节）

### 13.6 其他发现

- **OS4 官方内核 = 6.6.118-android15-8**（kleaf 构建，r510928 工具链）——仍是 android15-6.6 分支，KMI 与我们 6.6.142 相同
- 官方 OS4 boot 已备份：archive/boot_orig_os4.img（AVB SHA256_RSA4096 有效签名）
- 模块 vermagic 显示 6.6.57（vendor 旧模块缓存），system_dlkm 是 6.6.118，差异不影响加载（我们放宽了 same_magic 检查）
- 刷官方内核可完全恢复系统（无 root）——用于故障隔离测试
