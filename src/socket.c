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

zend_class_entry *async_server_ce;
zend_class_entry *async_socket_ce;
zend_class_entry *async_socket_exception_ce;
zend_class_entry *async_socket_stream_ce;


ZEND_METHOD(Socket, close) { }
ZEND_METHOD(Socket, getAddress) { }
ZEND_METHOD(Socket, getPort) { }
ZEND_METHOD(Socket, setOption) { }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_get_address, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_get_port, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_set_option, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

static const zend_function_entry async_socket_functions[] = {
	ZEND_ME(Socket, close, arginfo_socket_close, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(Socket, getAddress, arginfo_socket_get_address, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(Socket, getPort, arginfo_socket_get_port, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(Socket, setOption, arginfo_socket_set_option, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};

ZEND_METHOD(SocketStream, getRemoteAddress) { }
ZEND_METHOD(SocketStream, getRemotePort) { }
ZEND_METHOD(SocketStream, writeAsync) { }
ZEND_METHOD(SocketStream, getWriteQueueSize) { }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_stream_get_remote_address, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_stream_get_remote_port, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_stream_write_async, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_socket_get_write_queue_size, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_socket_stream_functions[] = {
	ZEND_ME(SocketStream, getRemoteAddress, arginfo_socket_stream_get_remote_address, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(SocketStream, getRemotePort, arginfo_socket_stream_get_remote_port, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(SocketStream, writeAsync, arginfo_socket_stream_write_async, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(SocketStream, getWriteQueueSize, arginfo_socket_get_write_queue_size, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


ZEND_METHOD(Server, accept) { }

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_server_accept, 0, 0, Concurrent\\Network\\SocketStream, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_server_functions[] = {
	ZEND_ME(Server, accept, arginfo_server_accept, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


static const zend_function_entry empty_funcs[] = {
	ZEND_FE_END
};


void async_socket_ce_register()
{
	zend_class_entry ce;
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\Socket", async_socket_functions);
	async_socket_ce = zend_register_internal_interface(&ce);
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\SocketStream", async_socket_stream_functions);
	async_socket_stream_ce = zend_register_internal_interface(&ce);

	zend_class_implements(async_socket_stream_ce, 2, async_socket_ce, async_duplex_stream_ce);
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\Server", async_server_functions);
	async_server_ce = zend_register_internal_interface(&ce);

	zend_class_implements(async_server_ce, 1, async_socket_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\SocketException", empty_funcs);
	async_socket_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_socket_exception_ce, async_stream_exception_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
