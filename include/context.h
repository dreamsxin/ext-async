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

#ifndef ASYNC_CONTEXT_H
#define ASYNC_CONTEXT_H

#include "php.h"

BEGIN_EXTERN_C()

extern zend_class_entry *async_context_ce;
extern zend_class_entry *async_context_var_ce;

typedef struct _async_context async_context;
typedef struct _async_context_var async_context_var;

struct _async_context {
	/* PHP object handle. */
	zend_object std;

	/* Refers to the parent context. */
	async_context *parent;

	/* Context var or NULL. */
	async_context_var *var;

	/* Value of the context var, defaults to zval NULL. */
	zval value;
};

struct _async_context_var {
	/* PHP object handle. */
	zend_object std;
};

async_context *async_context_get();

void async_context_ce_register();
void async_context_shutdown();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
