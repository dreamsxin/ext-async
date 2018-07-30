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

#ifndef ASYNC_IO_H
#define ASYNC_IO_H

#include "php.h"
#include "php_network.h"
#include "php_streams.h"

#include "awaitable.h"

#include "uv.h"

#if !defined(PHP_WIN32) || (defined(HAVE_SOCKETS) && !defined(COMPILE_DL_SOCKETS))
#define ASYNC_SOCKETS 1
#else
#define ASYNC_SOCKETS 0
#endif

#if defined(HAVE_SOCKETS) && !defined(COMPILE_DL_SOCKETS)
#include "ext/sockets/php_sockets.h"
#elif !defined(PHP_WIN32)
typedef struct {
	int bsd_socket;
} php_socket;
#endif

BEGIN_EXTERN_C()

extern zend_class_entry *async_watcher_ce;

typedef struct _async_watcher async_watcher;

struct _async_watcher {
	/* PHP object handle. */
	zend_object std;

	zval error;

	zval resource;

	php_socket_t fd;

	uv_poll_t poll;

	async_awaitable_queue reads;
	async_awaitable_queue writes;
};

void async_io_ce_register();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
