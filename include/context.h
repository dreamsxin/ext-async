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

#ifndef CONCURRENT_CONTEXT_H
#define CONCURRENT_CONTEXT_H

#include "php.h"

BEGIN_EXTERN_C()

extern zend_class_entry *concurrent_context_ce;

typedef struct _concurrent_context concurrent_context;

struct _concurrent_context {
	zend_object std;

	concurrent_context *parent;

	uint32_t param_count;

	union {
		struct {
			zend_string *name;
			zval value;
		} var;
		HashTable *params;
	} data;
};

concurrent_context *concurrent_context_object_create(HashTable *params);

concurrent_context *concurrent_context_get();

void concurrent_context_ce_register();
void concurrent_context_shutdown();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
