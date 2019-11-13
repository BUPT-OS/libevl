/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include <evl/evl.h>
#include "git-stamp.h"

#ifndef GIT_STAMP
#define git_hash  ""
#else
#define git_hash  " -- " GIT_STAMP
#endif

struct evl_version evl_get_version(void)
{
	return (struct evl_version){
		.api_level = __EVL__,
		.abi_level = -1,
		.version_string = "eshi." LIBSERIAL git_hash,
		};
}
