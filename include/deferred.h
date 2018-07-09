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

#ifndef CONCURRENT_DEFERRED_H
#define CONCURRENT_DEFERRED_H

#include "php.h"
#include "awaitable.h"

BEGIN_EXTERN_C()

extern zend_class_entry *concurrent_deferred_ce;
extern zend_class_entry *concurrent_deferred_awaitable_ce;

typedef struct _concurrent_deferred concurrent_deferred;
typedef struct _concurrent_deferred_awaitable concurrent_deferred_awaitable;

struct _concurrent_deferred {
	zend_object std;

	zend_uchar status;

	zval result;

	/* Linked list of registered continuation callbacks. */
	concurrent_awaitable_cb *continuation;
};

extern const zend_uchar CONCURRENT_DEFERRED_STATUS_PENDING;
extern const zend_uchar CONCURRENT_DEFERRED_STATUS_RESOLVED;
extern const zend_uchar CONCURRENT_DEFERRED_STATUS_FAILED;

struct _concurrent_deferred_awaitable {
	zend_object std;

	concurrent_deferred *defer;
};

void concurrent_deferred_ce_register();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
