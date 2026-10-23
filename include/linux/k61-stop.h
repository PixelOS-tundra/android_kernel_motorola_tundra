/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DEBUG (bring-up): "k61.stop=init" and "k61.stop=init2" test modes, see
 * init/main.c. In both, userspace may only mount block filesystems
 * read-only, may not open storage (UFS or dm) block devices for writing and
 * may not reboot; doing so trips the guard, which panics. With "init", PID 1
 * executing /system/bin/init (the end of first stage) panics as well.
 */
#ifndef _LINUX_K61_STOP_H
#define _LINUX_K61_STOP_H

#include <linux/types.h>

extern bool k61_stop_init;
extern bool k61_stop_exec;

__printf(1, 2) __noreturn void k61_stop_guard_trip(const char *fmt, ...);

#endif
