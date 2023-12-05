/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _EVL_RROS_H
#define _EVL_RROS_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef RROS_DEBUG
#define DEBUG_PRINT(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#else
#define DEBUG_PRINT(...) do{ } while ( false )
#endif


#ifdef __cplusplus
}
#endif

#endif // _EVL_RROS_H