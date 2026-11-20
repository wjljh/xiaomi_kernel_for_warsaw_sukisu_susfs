# SuSFS v2.4.0 + SukiSU Nova 集成

## 组成

| 文件 | 说明 |
|---|---|
| `../susfs-2.4.0-kernel.patch` | 内核侧补丁（27 文件 / 132 hunk），来自 `gitlab.com/simonpunk/susfs4ksu` 的 `gki-android15-6.6` 分支 |
| `susfs_compat.c` | KSU 侧兼容层，放到 `kernel/extension/`，并加入 `kernel/Kbuild` |

## 应用步骤

```bash
# 1. 内核树
patch -p1 < patches/susfs-2.4.0-kernel.patch

# 2. 复制 susfs 源文件（补丁只加 hook，源文件是独立的）
cp kernel_patches/fs/susfs.c              fs/susfs.c
cp kernel_patches/include/linux/susfs.h   include/linux/susfs.h
cp kernel_patches/include/linux/susfs_def.h include/linux/susfs_def.h

# 3. 挂 KSU 模块
#   drivers/Kconfig  末尾: source "drivers/kernelsu/Kconfig"
#   drivers/Makefile 末尾: obj-$(CONFIG_KSU) += kernelsu/

# 4. SukiSU 模块（Nova）
cp susfs_compat.c <nova>/kernel/extension/
#   kernel/Kbuild: kernelsu-objs += extension/susfs_compat.o
```

## 两处必须处理的符号冲突

### 1. static_key 类型不匹配

```
内核:  extern struct static_key_true ksu_su_compat_enabled;   /* 16 字节 */
Nova:  bool ksu_su_compat_enabled = true;                     /* 1 字节 */
```

`static_branch_unlikely(&ksu_su_compat_enabled)` 会把 bool 后面的相邻内存当成 jump_entry 指针 → 野指针 panic（`Unable to handle kernel paging request`）。

修复：Nova 内部改名 `ksu_su_compat_flag`，兼容层定义 `DEFINE_STATIC_KEY_TRUE(ksu_su_compat_enabled)`。

### 2. 符号同名但签名不同

```
内核 fs/exec.c:  ksu_handle_execveat_sucompat(int *fd, struct filename **, void *, void *, int *flags)  /* 5 参 */
Nova sucompat.c: ksu_handle_execveat_sucompat(const char __user **, int, struct pt_regs *)            /* 3 参 */
```

内核把 `&flags`（一个 int）当 `pt_regs *` 传进去，`PT_REGS_PARM3` 解引用垃圾内存 → panic。

崩溃栈（实测）：

```
ksu_handle_execve_sucompat_common+0x8c
ksu_handle_execveat_sucompat+0x20
do_execveat_common+0x2e0
__arm64_sys_execve+0x48
```

修复：Nova 的改名 `ksu_syshook_execveat_sucompat`，兼容层提供 5 参 no-op。
同类问题：`ksu_handle_setresuid`（内核 3 参 vs Nova 2 参）→ 改名 `ksu_syshook_setresuid`。

## 兼容层设计

`susfs_compat.c` 提供内核期望的 `ksu_handle_*` 符号：

| 符号 | 实现 | 原因 |
|---|---|---|
| `ksu_handle_execveat` / `_faccessat` / `_stat` | no-op | Nova 的 syscall 层已完整处理，fs 层重复处理反而冲突 |
| `ksu_setup_preinit_file` / `ksu_preinit_handle_post_execveat` | no-op | Nova 已有完整 preinit（`preinit/preinit.c` + nolibc runner） |
| `ksu_handle_sys_read` | no-op | Nova 用 `f_op` 代理（`ksu_install_rc_hook`）注入 init.rc |
| **`ksu_handle_vfs_fstat`** | **真实实现** | init.rc 长度补偿，配合 f_op 代理注入 |
| `ksu_handle_execveat_sucompat` / `ksu_handle_setresuid` | no-op | 见上，签名冲突 |

## 验证

- 内核：`ksu_handle_vfs_fstat` 符号 128 字节（空函数约 8）
- 设备：KSU root、KPM 模块自动加载、LSPosed `lspd` 运行、`ksud` 由 init 拉起
