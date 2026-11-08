# SukiSU Ultra KPM 集成与排查方案（实战验证版）

> 本文基于 2026-11-08 在 **Redmi K90 Ultra（warsaw / M332BF，内核 6.6.142-android15，4K 页，GKI）** 上的实测。
> 结果：`ksud extension check kpm` = **supported**，`Value: 1`，KP 正常接管。失败路径与坑一并记录，避免重复踩。
>
> 说明：文中 `deliverables/` 下的镜像名对应本项目的发布产物，不在源码仓库内。

---

## 1. 结论先行

SukiSU Ultra 的 KPM **不是独立实现**，它是 KernelPatch（KP）的薄桥接层：

- SukiSU 侧只做三件事：提供 `KSU_IOCTL_EXTENSION/KSU_EXTENSION_KPM` 接口、探测 KP 是否存在、**反向 EXPORT 一批符号给 KP 用**（`sukisu_super_access` / `sukisu_super_find_struct` / `sukisu_is_su_allow_uid` 等，见 `kernel/extension/kpm/compact.c`、`super_access.c`）。
- 判定逻辑（`kernel/extension/kpm/kpm.c`）：

```c
fn = find_kernel_symbol_exact("__arm64_sys_truncate");
if (!fn) return -EOPNOTSUPP;
if (fn(&regs) != SUKISU_KPM_ABI_VERSION) return -EOPNOTSUPP;   // 没有 KP 时走这里
```

- 也就是说：**仅开 `CONFIG_KPM=y` 不够**，必须把 KP 的 payload（kpimg）注入内核镜像。KP 没有 Kbuild / `obj-m` / `MODULE_LICENSE`，`insmod` 与"当驱动编进源码树"都不可行，`kernel/kernel-source/` 只是编译兼容头文件。

```
CONFIG_KPM=y  →  SukiSU 有控制面
kpimg 注入 boot →  KP 在 __arm64_sys_truncate 上装 supercall 入口
两者齐备     →  extension check kpm = supported
```

---

## 2. 必须用官方工具链（血的教训）

官方工具在 **`SukiSU-Ultra/SukiSU_patch`** 仓库的 `kpm/` 目录（**不是** `ShirkNeko/SukiSU_KernelPatch_patch` 的源码仓，也不是自己编）：

| 文件 | 尺寸（2026-10 版） | 说明 |
|---|---|---|
| `kpimg` | 294,384 字节，`version=0xe00`，编译于 2026-10-27 | **唯一可用**的 payload |
| `kptools-linux` | 1,672,840 字节 | x86_64 静态工具（在 PC 上用） |
| `kptools-android` | 1,619,664 字节 | arm64（在手机上用） |

### 两次开不了机的失败记录（务必避开）

| # | 用了什么 | 为什么崩 |
|---|---|---|
| ① | 本地旧版 `SukiSU_patch/kpm/kpimg`（**180,160 字节**，`0xd00`，2026-03-15） | 旧协议：里面是 `hook sukisu_kpm_info/control/list …` 共 **14 条 stub hook 字符串**。新 SukiSU 已删除那些 stub → KP 去 hook 不存在的函数 → 内核崩 |
| ② | 用 `aarch64-linux-gnu-gcc` 自己编 kpimg | 尺寸同为 294,384 但**与官方二进制差 157,114 字节**（Ubuntu 工具链 + 裸机链接，代码生成不同）→ 同样开不了机 |

判别方法（编前必查）：

```bash
strings -a kpimg | grep -c 'hook sukisu'      # 必须为 0（非 0 = 旧协议，禁止使用）
strings -a kpimg | grep 'sukisu_kpm_supercall_install'   # 应能看到
```

---

## 3. 正确操作步骤

### 3.1 内核侧：开 CONFIG_KPM

```kconfig
# 位置：drivers/kernelsu/Kconfig（SukiSU 自己的 Kconfig，不是 KernelPatch 的）
config KPM
    bool "Enable SukiSU KPM"
    depends on KSU && 64BIT
    default n
    select KALLSYMS
    select KALLSYMS_ALL
```

构建配置里加 `CONFIG_KPM=y`；`CONFIG_KSU=y / KALLSYMS=y / KALLSYMS_ALL=y` 需同时为 y。

**验证编进去了**（不用开机就能查）：

```bash
grep -E '^CONFIG_(KPM|KSU|KALLSYMS|KALLSYMS_ALL)=' <out-dir>/.config
# 期望：四项全部 =y，且不存在 "CONFIG_KPM is not set"
```

### 3.2 注入 KP：修补 **裸 Image**，不是 boot.img

`kptools` 的 `-i` 要的是**裸 kernel Image**；直接喂 boot.img 会报 `kernel image magic error`。

```bash
# 变量替换成实际路径
IMG=<out-dir>/arch/arm64/boot/Image
KPTOOLS=/path/to/kptools-linux     # chmod +x
KPIMG=/path/to/kpimg               # 官方 294,384 字节那份

"$KPTOOLS" -p -i "$IMG" -k "$KPIMG" -o Image-kp

# 自检：必须出现 patched=true 与 [kpimg] 段
"$KPTOOLS" -l -i Image-kp
```

期望输出包含：

```
patched=true
[kpimg]
version=0xe00
compile_time=20:07:48 Oct 27 2026
config=android,release
arch=arm64
```

### 3.3 重新打包 boot.img

用工程自带的 packer（会正确重算 AVB 尾部）：

```bash
python3 <kernel-repo>/project/pack_boot.py pack \
        <template.img>  Image-kp  boot_kp.img
```

* 模板：`deliverables/boot_stock_4.0.15_template.img`（100,663,296 字节）。
  模板也可从自己设备 dump 原厂 boot（`dd if=/dev/block/by-name/boot`），或使用发布产物里的 `boot_stock_4.0.15_template.img`（100,663,296 字节）。
* 自检：

```bash
python3 .../pack_boot.py inspect boot_kp.img
# 关注：kernel_size 正确、ARM64 魔数 41524d64、vbmeta 与内核"无冲突"
```

### 3.4 交叉验证（强烈建议）

把打包后的镜像里内核段抠出来再验一次，确认补丁真的在成品里：

```bash
KSIZE=$(python3 -c "import struct;d=open('boot_kp.img','rb').read(64);print(struct.unpack_from('<I',d,8)[0])")
dd if=boot_kp.img of=k.bin bs=4096 skip=1 count=$((KSIZE/4096)) status=none
"$KPTOOLS" -l -i k.bin | head -12      # 仍应 patched=true
```

### 3.5 刷入并验证

刷 `boot_kp.img` 后冷重启，然后：

```bash
su -c 'zcat /proc/config.gz | grep CONFIG_KPM'    # CONFIG_KPM=y
su -c '/data/adb/ksud extension check kpm'         # supported
su -c '/data/adb/ksud extension get kpm'           # Value: 1
su -c 'dmesg | grep -E "KP KernelPatch (Version|Compile)"'
# 期望：KP KernelPatch Version: e00 / Compile Time: 20:07:48 Oct 27 2026
```

ksud 日志里还会出现：

```
ksud::kpm: KPM: version check ok: 3584 (20:07:48 Oct 27 2026)
```

`3584 = 0xe00`，就是 kpimg 版本号；版本对不上会直接失败。

---

## 4. 加载 .kpm 模块

```bash
su -c '/data/adb/ksud kpm load /data/adb/kpm/xxx.kpm'
su -c '/data/adb/ksud kpm list'
su -c '/data/adb/ksud kpm info xxx'
su -c '/data/adb/ksud kpm unload xxx'
```

放在 `/data/adb/kpm/` 下的模块会在 **ksud 的 post-fs-data 阶段自动加载**（日志 `load_event=post-fs-data`）。

### 时序陷阱（重要）

`ksud` 的 post-fs-data 顺序是：

```
post-fs-data.d 脚本 → 各模块 sepolicy.rule 注入 → KPM 模块加载 → 模块 post-fs-data.sh
```

所以任何**依赖"注入前干净策略"的模块**（如 selinux_MAF_fork）如果在默认时机加载，抓到的都是**已被污染的 policy**，等于无效。

若确实需要提前：在 `/data/adb/post-fs-data.d/` 放脚本（该目录最早执行）：

```sh
#!/system/bin/sh
timeout 8 /data/adb/ksud kpm load /data/adb/kpm/selinux_MAF_fork.kpm
exit 0
```

---

## 5. 已知限制（本内核不可绕的点）

**KP 自带一层 `selinux_hide`**（伪造 `/sys/fs/selinux/status`、拦截 context/access 写入），开关是**文件存在性**：

```
/data/adb/ap/selinux_hide        （存在即启用，内容无关）
/data/adb/ap/selinux_hide_strict
```

由 `report_user_event("post-fs-data", "before")` 触发 —— 这是 **APatch 的 apd 才发的**事件，**SukiSU 的 ksud 只发不带参数的 `post-fs-data`**，所以这层在 SukiSU 下**永远不会初始化**（实测：建了文件、重启后日志里没有任何 `selinux_hide: ... exists`）。

**本内核的硬限制（实测）**：内核开了 `CONFIG_LTO_CLANG=y + CONFIG_LTO_CLANG_THIN=y`，以下符号被内联/消除，kallsyms 里查不到：

```
sel_write_access      → 0 命中
sel_write_context     → 0 命中
selinux_setprocattr   → 0 命中
```

后果：**SukiSU 自带的 `selinux_hide` 与 `selinux_MAF_fork.kpm` 都无法 hook**（前者报 `selinux_setprocattr_hook err: -2` 然后自我降级；后者报 `missing=7`，`sel_write_access missing addr=0`）。

要解决只有两条：关 ThinLTO 重编内核（实测该类内核起不来）或等上游把 hook 目标换成不被内联的符号。

---

## 6. 回滚

| 情况 | 动作 |
|---|---|
| KP 注入后开不了机 | 刷回未注入的镜像：`deliverables/boot_warsaw_6.6.142-...d1900-kpm-4k.img`（md5 `f8324dc675f0e3a62f485bacbb82a46b`） |
| 只想去掉 KPM | 关 `CONFIG_KPM` 重编 + 刷回未注入镜像，两者都要做 |
| 已验证可用的镜像 | `deliverables/boot_warsaw_6.6.142-...d1900-kpm-4k_KP-official.img`（md5 `cd8bd828cb0880399a51de0dae472c76`）——**这就是本机当前在跑的那份** |

---

## 7. 若要把注入并入构建脚本（可选，谨慎）

在 `build_variant.sh` 的 **打包之前**插入（顺序不能反：先编内核，再注入，再打包）：

```bash
KPTOOLS=/path/to/kptools-linux
KPIMG=/path/to/kpimg
"$KPTOOLS" -p -i "$IMG" -k "$KPIMG" -o "$IMG.kp" && mv "$IMG.kp" "$IMG"
```

**风险提示**：一旦并入，任何一次构建都产出"带 KP 的镜像"；若某次内核改动与 KP 不兼容，会直接失去可开机产物。建议：
1. 先在脚本里加开关（如 `ENABLE_KP=1` 才注入）；
2. 保留未注入镜像作为回滚；
3. 每次注入后跑 `kptools -l` 自检并把结果写进构建日志。

---

## 8. 一页速查

```
开能力：CONFIG_KPM=y（SukiSU Kconfig，select KALLSYMS/KALLSYMS_ALL）
打镜像：kptools -p -i Image -k kpimg -o Image-kp      # 用裸 Image，不是 boot.img
打包：  python3 project/pack_boot.py pack <template.img> Image-kp boot_kp.img
验证：  kptools -l -i Image-kp → patched=true + [kpimg] version=0xe00
刷后：  ksud extension check kpm → supported；get kpm → Value: 1
模块：  ksud kpm load <path>；放 /data/adb/kpm/ 自动加载（但晚于 sepolicy 注入）
禁令：  不用 180,160 字节的旧 kpimg；不用自己编的 kpimg；不直接喂 boot.img 给 kptools
```
