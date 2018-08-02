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

HashTable *async_info_init()
{
	HashTable *info;

	ALLOC_HASHTABLE(info);
	zend_hash_init(info, 0, NULL, ZVAL_PTR_DTOR, 0);

	return info;
}

void async_info_prop(HashTable *info, char *key, zval *value)
{
	zend_string *k;

	k = zend_string_init(key, strlen(key), 0);
	zend_hash_add(info, k, value);
	zend_string_release(k);
}

void async_info_prop_bool(HashTable *info, char *key, zend_bool value)
{
	zval v;

	ZVAL_BOOL(&v, value);
	async_info_prop(info, key, &v);
}

void async_info_prop_long(HashTable *info, char *key, zend_ulong value)
{
	zval v;

	ZVAL_LONG(&v, value);
	async_info_prop(info, key, &v);
}

void async_info_prop_str(HashTable *info, char *key, zend_string *value)
{
	zval v;

	if (value == NULL) {
		ZVAL_NULL(&v);
	} else {
		ZVAL_STR(&v, value);
	}

	async_info_prop(info, key, &v);
}

void async_info_prop_cstr(HashTable *info, char *key, char *value)
{
	zval v;

	if (value == NULL) {
		ZVAL_NULL(&v);
	} else {
		ZVAL_STRING(&v, value);
	}

	async_info_prop(info, key, &v);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
