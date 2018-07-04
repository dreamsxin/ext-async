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

#include "php_task.h"

zend_class_entry *concurrent_awaitable_ce;


ZEND_METHOD(Awaitable, continueWith) { }

ZEND_BEGIN_ARG_INFO_EX(arginfo_awaitable_continue_with, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry awaitable_functions[] = {
	ZEND_ME(Awaitable, continueWith, arginfo_awaitable_continue_with, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


void concurrent_awaitable_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Awaitable", awaitable_functions);
	concurrent_awaitable_ce = zend_register_internal_interface(&ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
