# patches/

## susfs-for-sukisu-v4.3.0.patch

把 [susfs4ksu](https://github.com/Simonpunk/susfs4ksu) 的 SuSFS 接口层移植到 **SukiSU Ultra v4.3.0** 的内核模块。

- 规模：**14 个文件，+459 / −12**
- 基线：SukiSU Ultra `v4.3.0`（commit `e67b14e6`）
- 内核侧仍使用 susfs4ksu 的 `50_add_susfs_in_gki-android15-6.6.patch`（`fs/susfs.c` 等）

### 涉及文件

| 文件 | 改动 |
|---|---|
| `kernel/Kbuild` | 固定 `LOCAL_COUNT := 3915`（→ KSU 41100） |
| `kernel/Kconfig` | 新增 9 个 `KSU_SUSFS_*` 开关 |
| `kernel/Makefile` | `SUSFS_VERSION` 打印 |
| `kernel/core/init.c` | `#include <linux/susfs.h>` + `susfs_init()` |
| `kernel/feature/adb_root.c` | 3 个 include |
| `kernel/feature/selinux_hide.c` | 6 组符号去 `static`（供 SuSFS 复用） |
| `kernel/feature/sucompat.c` | `#include <linux/susfs_def.h>` |
| `kernel/hook/setuid_hook.c` | zygote / zygote_next 的 umount + SuSFS extra work |
| `kernel/hook/syscall_event_bridge.c` | no_su 标记（`susfs_set_current_proc_no_su`） |
| `kernel/policy/app_profile.c` | `disable_seccomp` 去 `static` |
| `kernel/selinux/rules.c` | `susfs_set_batch_sid()` |
| `kernel/selinux/selinux.c` | SuSFS 的 5 个 SID + 判定/批处理函数 |
| `kernel/selinux/selinux.h` | 上述声明 |
| `kernel/supercall/dispatch.c` | `CMD_SUSFS_*` 完整分派 + sdcard monitor + no_su |

### 与上游补丁的差异（重要）

上游补丁是针对**官方 KernelSU** 写的，直接 `patch -p1` 会混入大量上游重构，编译失败。本补丁**只包含 SuSFS 相关行**，明确跳过了：

- 删除 `hook/syscall_hook.h` / `hook/lsm_hook.h` 的 include
- 删除 `ksu_late_loaded` 变量
- 把 `ksu_su_compat_enabled` 改成 static key
- 修改 `ksu_adb_root_handle_execve` / `ksu_handle_execve_sucompat` / `ksu_handle_execveat_ksud` 的函数签名
- 删除 Kconfig 的 `KSU_X86_PATCH_SYSCALL_DISPATCHER`
- 重写 `is_exec_adbd`、调整 init 顺序

另外**刻意移除**了两个上游 hunk：

| 上游 hunk | 原因 |
|---|---|
| `fs/read_write.c`（`ksu_handle_sys_read`） | SukiSU 不使用该路径 |
| `drivers/input/input.c`（`ksu_handle_input_handle_event`） | SukiSU 通过 `runtime/ksud_integration.c` 的 `input_event_kp` 实现 |

### 应用方式

```bash
cd SukiSU-Ultra
patch -p1 < ../patches/susfs-for-sukisu-v4.3.0.patch
```

正常应输出 `0 FAILED`、无 `.rej` 文件。若失败，说明 SukiSU 版本已变动，需要重新 rebase。

### 验证

```bash
nm out-xxx/vmlinux | grep -cE ' [tTdDbB] susfs_'   # 应 > 0（实测 73）
strings -a out-xxx/arch/arm64/boot/Image | grep -ci susfs   # 应 > 0（实测 194）
```
