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

#include "zend_inheritance.h"

zend_class_entry *async_duplex_stream_ce;
zend_class_entry *async_pending_read_exception_ce;
zend_class_entry *async_readable_stream_ce;
zend_class_entry *async_stream_closed_exception_ce;
zend_class_entry *async_stream_exception_ce;
zend_class_entry *async_writable_stream_ce;


ZEND_METHOD(ReadableStream, close) { }
ZEND_METHOD(ReadableStream, read) { }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_stream_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_stream_read, 0, 0, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_readable_stream_functions[] = {
	ZEND_ME(ReadableStream, close, arginfo_readable_stream_close, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(ReadableStream, read, arginfo_readable_stream_read, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


ZEND_METHOD(WritableStream, close) { }
ZEND_METHOD(WritableStream, write) { }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_stream_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_stream_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_writable_stream_functions[] = {
	ZEND_ME(WritableStream, close, arginfo_writable_stream_close, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(WritableStream, write, arginfo_writable_stream_write, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


static const zend_function_entry empty_funcs[] = {
	ZEND_FE_END
};


void async_stream_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\ReadableStream", async_readable_stream_functions);
	async_readable_stream_ce = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\WritableStream", async_writable_stream_functions);
	async_writable_stream_ce = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\DuplexStream", empty_funcs);
	async_duplex_stream_ce = zend_register_internal_interface(&ce);

	zend_class_implements(async_duplex_stream_ce, 2, async_readable_stream_ce, async_writable_stream_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\StreamException", empty_funcs);
	async_stream_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_stream_exception_ce, zend_ce_exception);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\StreamClosedException", empty_funcs);
	async_stream_closed_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_stream_closed_exception_ce, async_stream_exception_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\PendingReadException", empty_funcs);
	async_pending_read_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_pending_read_exception_ce, async_stream_exception_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
