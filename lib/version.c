/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#include "git-stamp.h"

#ifndef GIT_STAMP
#define git_hash  ""
#else
#define git_hash  " -- " GIT_STAMP
#endif

const char *libevenless_version_string = "evl." LIBSERIAL git_hash;
