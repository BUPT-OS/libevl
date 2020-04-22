/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <stdarg.h>
#include <evl/thread.h>
#include <evl/sched.h>

int main(int argc, char *argv[])
{
	union evl_sched_ctlparam param;
	union evl_sched_ctlinfo info;
	struct evl_sched_attrs attrs;
	int tfd, cpu_state;

	tfd = evl_attach_self("me");
	evl_get_schedattr(tfd, &attrs);
	evl_set_schedattr(tfd, &attrs);

	param.quota.op = evl_quota_add;
	evl_control_sched(SCHED_QUOTA, &param, &info, 0);
	evl_get_cpustate(0, &cpu_state);
	evl_yield();

	return 0;
}
