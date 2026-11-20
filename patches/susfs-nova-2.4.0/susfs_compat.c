// SPDX-License-Identifier: GPL-2.0
/*
 * SuSFS 2.4.0 兼容层（临时）
 *
 * SuSFS 2.4.0 的 fs 钩子改为调用官方 KernelSU 的 ksu_handle_* API，
 * 而本模块（SukiSU Nova）仍是 2.3.0 时代架构。这里提供最小的符号定义，
 * 让内核能链接通过并验证开机；后续再逐步接入真实逻辑。
 *
 * 当前行为：全部 no-op（钩子不产生任何效果）
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/jump_label.h>
#include <linux/static_key.h>
#include <linux/file.h>
#include <linux/printk.h>

struct user_arg_ptr;

/* 内核侧声明为 struct static_key_true，类型必须一致 */
DEFINE_STATIC_KEY_TRUE(ksu_is_init_rc_hook_enabled);
DEFINE_STATIC_KEY_TRUE(ksu_is_input_hook_enabled);
DEFINE_STATIC_KEY_TRUE(ksu_su_compat_enabled);

int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv,
                        void *envp, int *flags)
{
    return 0;
}

int ksu_handle_post_execveat_sucompat(int *fd, struct filename **filename_ptr, void *argv,
                                      void *envp, int *flags, int *retval)
{
    return 0;
}

int ksu_setup_preinit_file(struct filename **filename_ptr, struct user_arg_ptr *argv_user)
{
    return 0;
}

int ksu_preinit_handle_post_execveat(int *fd, struct filename **filename_ptr,
                                     void *argv_user, void *envp_user,
                                     int *flags, int *retval, int *preinit_fd)
{
    return 0;
}

int ksu_handle_faccessat(int *dfd, struct filename **filename, int *mode, int *__unused_flags)
{
    return 0;
}

/* init.rc 长度伪造：fstat 时把 KSU 注入的内容长度加回去 */
extern bool is_init_rc(struct file *fp);
extern const size_t ksu_rc_len;
extern size_t module_rc_len;

void ksu_handle_vfs_fstat(int fd, loff_t *kstat_size_ptr)
{
    loff_t orig_size = *kstat_size_ptr;
    size_t extra = 0;
    bool is_rc = false;
    struct file *file = fget(fd);

    if (file) {
        if (is_init_rc(file))
            is_rc = true;
        fput(file);
    }

    if (is_rc) {
        extra = ksu_rc_len + module_rc_len;
        *kstat_size_ptr = orig_size + extra;
        pr_info("adding rc len: %lld -> %lld (static=%zu module=%zu)",
                orig_size, *kstat_size_ptr, ksu_rc_len, module_rc_len);
    }
}

int ksu_handle_stat(int *dfd, struct filename **filename, int *flags)
{
    return 0;
}

void ksu_handle_sys_read(unsigned int fd)
{
}

/* 2.4.0 内核 fs/exec.c 调用的 su_compat 钩子（真逻辑由 Nova 的 syscall hook 完成） */
int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr, void *argv,
                                 void *envp, int *flags)
{
    return 0;
}

/* 2.4.0 内核 kernel/sys.c 调用（3 参） */
int ksu_handle_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    return 0;
}
