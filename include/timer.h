/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
  +----------------------------------------------------------------------+
*/

#ifndef ASYNC_TIMER_H
#define ASYNC_TIMER_H

#include "php.h"
#include "uv.h"

BEGIN_EXTERN_C()

extern zend_class_entry *async_timer_ce;

typedef struct _async_timer async_timer;

struct _async_timer {
	/* PHP object handle. */
	zend_object std;

	/* Callback info and cache. */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	/* UV timer handle. */
	uv_timer_t timer;

	zend_bool nodelay;

	/* Is set when the timer has a repeat interval. */
	zend_bool repeat;
};

void async_timer_ce_register();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
