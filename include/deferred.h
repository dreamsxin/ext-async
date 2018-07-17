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

#ifndef ASYNC_DEFERRED_H
#define ASYNC_DEFERRED_H

#include "php.h"
#include "awaitable.h"

BEGIN_EXTERN_C()

extern zend_class_entry *async_deferred_ce;
extern zend_class_entry *async_deferred_awaitable_ce;

typedef struct _async_deferred async_deferred;
typedef struct _async_deferred_awaitable async_deferred_awaitable;

struct _async_deferred {
	/* PHP object handle. */
	zend_object std;

	/* Status of the deferred, one of the ASYNC_DEFERRED_STATUS_* constants. */
	zend_uchar status;

	/* Result (or error) value in case of resolved deferred. */
	zval result;

	/* Linked list of registered continuation callbacks (can be NULL). */
	async_awaitable_cb *continuation;
};

extern const zend_uchar ASYNC_DEFERRED_STATUS_PENDING;
extern const zend_uchar ASYNC_DEFERRED_STATUS_RESOLVED;
extern const zend_uchar ASYNC_DEFERRED_STATUS_FAILED;

struct _async_deferred_awaitable {
	/* PHP object handle. */
	zend_object std;

	/* Refers to the deferred object that created the awaitable. */
	async_deferred *defer;
};

void async_deferred_ce_register();

END_EXTERN_C()

typedef struct _async_deferred_combine async_deferred_combine;

struct _async_deferred_combine {
	async_deferred *defer;
	zend_long counter;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

typedef struct _async_deferred_transform async_deferred_transform;

struct _async_deferred_transform {
	async_deferred *defer;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
