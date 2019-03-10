/*
 * SPDX-License-Identifier: MIT
 */
#ifndef _LIB_EVL_PARSE_VDSO_H
#define _LIB_EVL_PARSE_VDSO_H

void evl_init_vdso(void);

void *evl_lookup_vdso(const char *version, const char *name);

void *evl_request_vdso(const char *version, const char *name);

#endif /* !_LIB_EVL_PARSE_VDSO_H */
