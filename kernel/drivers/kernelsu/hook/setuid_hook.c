#include <linux/compiler.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/thread_info.h>
#include <linux/seccomp.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>
#include <linux/workqueue.h>
#include <linux/susfs_def.h>
#include "selinux/selinux.h"

#include "policy/allowlist.h"
#include "hook/setuid_hook.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_identity.h"
#include "infra/seccomp_cache.h"
#include "supercall/supercall.h"
#include "hook/tp_marker.h"
#include "feature/kernel_umount.h"
#include "feature/mount_hide.h"

#ifdef CONFIG_KSU_SUSFS
extern u32 susfs_zygote_sid;
extern u32 susfs_zygote_next_sid;
extern void disable_seccomp(void);
extern struct work_struct susfs_extra_works;

static inline void ksu_handle_extra_susfs_work(void)
{
    if (work_pending(&susfs_extra_works))
        return;

    schedule_work(&susfs_extra_works);
}

static int handle_zygote_setresuid(uid_t ruid)
{
    // Check if spawned process is isolated service first, and force to do umount if so
    if (is_isolated_process(ruid)) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        goto do_umount;
    }

    // - Since ksu manager app uid is excluded in allow_list_arr, so ksu_uid_should_umount(manager_uid)
    //   will always return true, that's why we need to explicitly check if new_uid belongs to
    //   ksu manager.
    // - Disable seccomp restriction for KSU manager since running with "su" will disable seccomp anyway
    if (likely(ksu_is_manager_appid_valid()) && unlikely(is_uid_manager(ruid))) {
        disable_seccomp();
        pr_info("install fd for manager: %d\n", ruid);
        ksu_install_fd();
        return 0;
    }

    // - Check if spawned process is normal user app and needs to be umounted
    // - Now app_profile for webview_zygote is available in KernelSU manager
    if (likely(is_appuid(ruid) && ksu_uid_should_umount(ruid))) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        goto do_umount;
    }

    // Disable seccomp restriction for root allowed apps since running with "su" will disable seccomp anyway
    if (ksu_is_allow_uid_for_current(ruid)) {
        disable_seccomp();
        return 0;
    }

    // Process not umounted but also root not allowed
    susfs_set_current_proc_no_su();
    return 0;

do_umount:
    {
        // Handle kernel umount
        ksu_handle_umount(current_uid().val, ruid);

        // Handle extra susfs work
        ksu_handle_extra_susfs_work();
    }

    return 0;
}

static int handle_zygote_next_setresuid(uid_t ruid)
{
    // Check if spawned process is isolated service first, and force to do umount if so
    if (is_isolated_process(ruid)) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        susfs_set_current_proc_umounted_for_zygote_next();
        goto do_susfs_work;
    }

    // - Since ksu manager app uid is excluded in allow_list_arr, so ksu_uid_should_umount(manager_uid)
    //   will always return true, that's why we need to explicitly check if new_uid belongs to
    //   ksu manager.
    // - Disable seccomp restriction for KSU manager since running with "su" will disable seccomp anyway
    if (likely(ksu_is_manager_appid_valid()) && unlikely(is_uid_manager(ruid))) {
        disable_seccomp();
        pr_info("install fd for manager: %d\n", ruid);
        ksu_install_fd();
        return 0;
    }

    // - Check if spawned process is normal user app and needs to be umounted
    // - Now app_profile for webview_zygote is available in KernelSU manager
    if (likely(is_appuid(ruid) && ksu_uid_should_umount(ruid))) {
        susfs_set_current_proc_no_su();
        susfs_set_current_proc_umounted();
        susfs_set_current_proc_umounted_for_zygote_next();
        goto do_susfs_work;
    }

    // Disable seccomp restriction for root allowed apps since running with "su" will disable seccomp anyway
    if (ksu_is_allow_uid_for_current(ruid)) {
        disable_seccomp();
        return 0;
    }

    // Process not umounted but also root not allowed
    susfs_set_current_proc_no_su();
    return 0;

do_susfs_work:
    {
        // Do not umount here as we are in init namespace now

        // Handle extra susfs work
        ksu_handle_extra_susfs_work();
    }

    return 0;
}
#endif // #ifdef CONFIG_KSU_SUSFS

int ksu_handle_setresuid(uid_t old_uid, uid_t new_uid)
{
#ifdef CONFIG_KSU_SUSFS
    // We only interest in processes spawned by zygote or zygote_next.
    // Unlike upstream KernelSU's LSM hook, SukiSU invokes this handler *after*
    // setresuid() has taken effect, so a root caller (old_uid == 0) is what
    // identifies a zygote child here.
    if (old_uid == 0) {
        if (susfs_is_sid_equal(current_cred(), susfs_zygote_sid))
            return handle_zygote_setresuid(new_uid);

        if (susfs_is_sid_equal(current_cred(), susfs_zygote_next_sid))
            return handle_zygote_next_setresuid(new_uid);
    }
#endif // #ifdef CONFIG_KSU_SUSFS

    // we rely on the fact that zygote always call setresuid(3) with same uids

    pr_info("handle_setresuid from %d to %d\n", old_uid, new_uid);

    if (unlikely(is_uid_manager(new_uid))) {
        spin_lock_irq(&current->sighand->siglock);
        ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
        ksu_set_task_tracepoint_flag(current);
        spin_unlock_irq(&current->sighand->siglock);

        pr_info("install fd for manager: %d\n", new_uid);
        ksu_install_fd();
        return 0;
    }

    if (ksu_is_allow_uid_for_current(new_uid)) {
        if (current->seccomp.mode == SECCOMP_MODE_FILTER && current->seccomp.filter) {
            spin_lock_irq(&current->sighand->siglock);
            ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
            spin_unlock_irq(&current->sighand->siglock);
        }
        ksu_set_task_tracepoint_flag(current);
    } else {
        ksu_clear_task_tracepoint_flag_if_needed(current);
    }

    // Handle kernel umount
    ksu_handle_umount(old_uid, new_uid);

    return 0;
}

void __init ksu_setuid_hook_init(void)
{
    ksu_kernel_umount_init();
    ksu_mount_hide_init();
}

void __exit ksu_setuid_hook_exit(void)
{
    pr_info("ksu_core_exit\n");
    ksu_kernel_umount_exit();
    ksu_mount_hide_exit();
}
