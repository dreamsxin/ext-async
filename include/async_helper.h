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

#ifndef ASYNC_HELPER_H
#define ASYNC_HELPER_H

char *async_status_label(zend_uchar status);

HashTable *async_info_init();
void async_info_prop(HashTable *info, char *key, zval *value);
void async_info_prop_bool(HashTable *info, char *key, zend_bool value);
void async_info_prop_long(HashTable *info, char *key, zend_ulong value);
void async_info_prop_str(HashTable *info, char *key, zend_string *value);
void async_info_prop_cstr(HashTable *info, char *key, char *value);

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
