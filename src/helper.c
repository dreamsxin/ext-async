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

#include "php_async.h"
#include "async_helper.h"

char *async_status_label(zend_uchar status)
{
	if (status == ASYNC_OP_RESOLVED) {
		return "RESOLVED";
	}

	if (status == ASYNC_OP_FAILED) {
		return "FAILED";
	}

	return "PENDING";
}

#if PHP_VERSION_ID < 70400
void async_prop_write_handler_readonly(zval *object, zval *member, zval *value, void **cache_slot)
{
	zend_throw_error(NULL, "Cannot write to property \"%s\" of %s", ZSTR_VAL(Z_STR_P(member)), ZSTR_VAL(Z_OBJCE_P(object)->name));
}
#else
zval *async_prop_write_handler_readonly(zval *object, zval *member, zval *value, void **cache_slot)
{
	zend_throw_error(NULL, "Cannot write to property \"%s\" of %s", ZSTR_VAL(Z_STR_P(member)), ZSTR_VAL(Z_OBJCE_P(object)->name));

	return NULL;
}
#endif
