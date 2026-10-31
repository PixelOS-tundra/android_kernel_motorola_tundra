#include <linux/anon_inodes.h>
#include <linux/err.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "uapi/supercall.h"
#include "supercall/internal.h"
#include "supercall/supercall.h"
#include "hook/syscall_hook.h"
#include "arch.h"
#include "util.h"
#include "klog.h" // IWYU pragma: keep

#define KSU_DRIVER_PERMISSION_SU_SESSION (1UL << 0)

struct ksu_driver_context {
    unsigned long permissions;
};


static int anon_ksu_release(struct inode *inode, struct file *filp)
{
    kfree(filp->private_data);
    pr_info("ksu fd released\n");
    return 0;
}

static long anon_ksu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return ksu_supercall_handle_ioctl(filp, cmd, (void __user *)arg);
}

static const struct file_operations anon_ksu_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = anon_ksu_ioctl,
    .compat_ioctl = anon_ksu_ioctl,
    .release = anon_ksu_release,
};

static int ksu_install_fd_with_permissions(unsigned int fd_flags, unsigned long permissions)
{
    struct ksu_driver_context *context;
    struct file *filp;
    const char *name;
    int fd;

    context = kzalloc(sizeof(*context), GFP_KERNEL);
    if (!context)
        return -ENOMEM;

    context->permissions = permissions;
    name = permissions & KSU_DRIVER_PERMISSION_SU_SESSION ? "[ksu_driver_su]" : "[ksu_driver]";

    fd = get_unused_fd_flags(fd_flags);
    if (fd < 0) {
        pr_err("ksu_install_fd: failed to get unused fd\n");
        kfree(context);
        return fd;
    }

    filp = anon_inode_getfile(name, &anon_ksu_fops, context, O_RDWR);
    if (IS_ERR(filp)) {
        pr_err("ksu_install_fd: failed to create anon inode file\n");
        put_unused_fd(fd);
        kfree(context);
        return PTR_ERR(filp);
    }

    fd_install(fd, filp);
    pr_info("ksu fd installed: %d for pid %d\n", fd, current->pid);
    return fd;
}

int ksu_install_fd(void)
{
    return ksu_install_fd_with_permissions(O_CLOEXEC, 0);
}

int ksu_install_su_fd(void)
{
    // This descriptor must be installed after the exec into ksud.
    return ksu_install_fd_with_permissions(O_CLOEXEC, KSU_DRIVER_PERMISSION_SU_SESSION);
}

bool ksu_is_su_session_fd(const struct file *filp)
{
    const struct ksu_driver_context *context = filp->private_data;

    return context && (context->permissions & KSU_DRIVER_PERMISSION_SU_SESSION);
}

/*
 * reboot(KSU_INSTALL_MAGIC1, KSU_INSTALL_MAGIC2, 0, int __user *outp) is the
 * preinit runner's (PID 1) request for a KSU fd. Inline builds have no kprobes,
 * so this is routed through the sys_enter syscall dispatcher; PID 1 is always
 * marked. We are in syscall context, so the fd is installed directly instead
 * of via task_work.
 */
long __nocfi ksu_hook_reboot(int orig_nr, const struct pt_regs *regs)
{
    int magic1 = (int)PT_REGS_SYSCALL_PARM1(regs);
    int magic2 = (int)PT_REGS_PARM2(regs);

    if (magic1 == KSU_INSTALL_MAGIC1 && magic2 == KSU_INSTALL_MAGIC2) {
        int __user *outp = (int __user *)PT_REGS_SYSCALL_PARM4(regs);
        int fd = ksu_install_fd();

        pr_info("[%d] install ksu fd: %d\n", current->pid, fd);
        if (fd >= 0 && copy_to_user(outp, &fd, sizeof(fd))) {
            pr_err("install ksu fd reply err\n");
            ksu_close_fd(fd);
        }
        /* Same result the real sys_reboot gives for a bad magic. */
        return -EINVAL;
    }

    return ksu_syscall_table[orig_nr](regs);
}

void __init ksu_supercalls_init(void)
{
    ksu_supercall_dump_commands();
}

void __exit ksu_supercalls_exit(void)
{
    ksu_supercall_cleanup_state();
}
