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

#ifndef ASYNC_AWAITABLE_H
#define ASYNC_AWAITABLE_H

#include "php.h"

BEGIN_EXTERN_C()

extern zend_class_entry *async_awaitable_ce;

typedef struct _async_awaitable_cb async_awaitable_cb;

typedef void (*async_awaitable_func)(void *obj, zval *data, zval *result, zend_bool success);

struct _async_awaitable_cb {
	void *object;
	zval data;
	async_awaitable_func func;
	async_awaitable_cb *next;
};

void async_awaitable_register_continuation(async_awaitable_cb **cont, void *obj, zval *data, async_awaitable_func func);
void async_awaitable_trigger_continuation(async_awaitable_cb **cont, zval *result, zend_bool success);

void async_awaitable_ce_register();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
