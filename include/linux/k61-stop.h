/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DEBUG (bring-up): "k61.stop=init" and "k61.stop=init2" test modes, see
 * init/main.c. In both, userspace may only mount block filesystems
 * read-only, may not open storage (UFS or dm) block devices for writing and
 * may not reboot; doing so trips the guard, which panics. With "init", PID 1
 * executing /system/bin/init (the end of first stage) panics as well. In
 * both, the block layer refuses kernel writes to the UFS LUNs.
 *
 * "k61.data=ro" (with init2, Phase 2.4a) lets /metadata and /data mount:
 * read-write block mounts become read-only mounts, and userspace write opens
 * of storage other than ssd fail with -EROFS (so fsck, mkfs and wipes fail
 * cleanly) instead of tripping the guard. "k61.data_hold=<s>"
 * (default 30) panics that many seconds after /data is mounted. Once vold
 * has added the first hw-wrapped fscrypt key, the files listed in
 * "k61.data_probe=<path>,..." (default: the user 0 DE key files vold reads)
 * are read and their sha256 logged, to compare with 5.4.
 *
 * "k61.data=rw" (Phase 2.4b) is the same, except that the /data mount is
 * read-write and the kernel may write the userdata partition (through
 * f2fs and dm-default-key only; userspace write opens stay denied, and
 * /metadata stays read-only). The run stops at the first exec of
 * app_process (zygote) after /data is mounted. Any trip then syncs, closes
 * the userdata write window and only then holds and panics. Nothing on
 * /data may be unlinked, removed or replaced by a rename (-EPERM, logged;
 * guard F, "k61.guard_f=0" turns it off).
 *
 * "k61.data=ckpt" (Phase 2.6c) is k61.data=rw for a full system run under
 * the userdata f2fs checkpoint that 5.4 started (vdc checkpoint
 * startCheckpoint), so that everything 6.1 writes to /data is dropped at
 * the next mount. Every rw f2fs mount must ask for checkpoint=disable (else
 * the run stops before mounting) and /data must come up with the
 * checkpoint disabled. The commit is blocked: f2fs refuses every remount
 * that would enable the checkpoint again, /metadata stays read-only and
 * partition write opens stay denied. Whole UFS LUNs may be opened for
 * writing (the boot HAL reads the GPT through a read-write fd), but the
 * first write to one stops the run: that is the boot HAL marking the slot
 * successful, the first step of vold's commitChanges, which system_server
 * calls right before it sets sys.boot_completed. The run does not stop at
 * zygote; it stops there, at init's "Boot completed" /dev/kmsg line, or
 * k61.data_hold seconds (default 300) after the /data mount. Trips do not
 * sync; they close the userdata window, hold and panic.
 */
#ifndef _LINUX_K61_STOP_H
#define _LINUX_K61_STOP_H

#include <linux/types.h>

struct block_device;
struct dentry;
struct path;
struct super_block;

extern bool k61_stop_init;
extern bool k61_stop_exec;
extern bool k61_stop_data_ro;
extern bool k61_stop_data_rw;
extern bool k61_stop_data_ckpt;
extern bool k61_stop_data_closed;
extern bool k61_stop_efs_rw;

__printf(1, 2) __noreturn void k61_stop_guard_trip(const char *fmt, ...);
__printf(1, 2) void k61_stop_guard_trip_async(const char *fmt, ...);
void k61_stop_block_mounted(const struct path *path, struct super_block *sb,
			    const char *source, const char *fstype, int err);
int k61_stop_data_delete_check(const char *op, struct dentry *dentry,
			       struct dentry *from);
void k61_stop_data_probe(void);
bool k61_stop_rw_mount_allowed(const struct path *path);
void k61_stop_exec_check(const char *filename);
void k61_stop_ckpt_mount_check(const struct path *path, const char *source,
			       const char *fstype, const void *data);
bool k61_stop_ckpt_whole_disk(struct block_device *bdev);
void k61_stop_ckpt_whole_disk_write(struct block_device *bdev);
void k61_stop_ckpt_refuse_enable(const char *id);
void k61_stop_ckpt_kmsg(const char *line, size_t len);
bool f2fs_k61_checkpoint_disabled(struct super_block *sb);
bool k61_stop_efs_volume(const char *volname, bool write);

#endif
