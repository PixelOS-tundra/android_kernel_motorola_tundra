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
 * (default 30) panics that many seconds after /data is mounted.
 */
#ifndef _LINUX_K61_STOP_H
#define _LINUX_K61_STOP_H

#include <linux/types.h>

struct path;

extern bool k61_stop_init;
extern bool k61_stop_exec;
extern bool k61_stop_data_ro;

__printf(1, 2) __noreturn void k61_stop_guard_trip(const char *fmt, ...);
__printf(1, 2) void k61_stop_guard_trip_async(const char *fmt, ...);
void k61_stop_block_mounted(const struct path *path, const char *source,
			    const char *fstype, int err);

#endif
