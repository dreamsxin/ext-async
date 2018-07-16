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

#ifndef PHP_ASYNC_H
#define PHP_ASYNC_H

#include "awaitable.h"
#include "context.h"
#include "deferred.h"
#include "fiber.h"
#include "task.h"
#include "task_scheduler.h"

extern zend_module_entry async_module_entry;
#define phpext_async_ptr &async_module_entry

#define PHP_ASYNC_VERSION "0.1.0"

#ifdef PHP_WIN32
# define ASYNC_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
# define ASYNC_API __attribute__ ((visibility("default")))
#else
# define ASYNC_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#define ASYNC_OP_RESOLVED 64
#define ASYNC_OP_FAILED 65

ZEND_BEGIN_MODULE_GLOBALS(async)
	/* Root fiber context (main thread). */
	async_fiber_context root;

	/* Active fiber, NULL when in main thread. */
	async_fiber *current_fiber;

	/* Root context. */
	async_context *context;

	/* Active task context. */
	async_context *current_context;

	/* Default shared task scheduler. */
	async_task_scheduler *scheduler;

	/* Running task scheduler. */
	async_task_scheduler *current_scheduler;

	/* Default fiber C stack size. */
	zend_long stack_size;

	/* Error to be thrown into a fiber (will be populated by throw()). */
	zval *error;

ZEND_END_MODULE_GLOBALS(async)

ASYNC_API ZEND_EXTERN_MODULE_GLOBALS(async)
#define ASYNC_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(async, v)

#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#define ASYNC_CHECK_FATAL(expr, message) do { \
	if (UNEXPECTED(expr)) { \
		zend_error_noreturn(E_CORE_ERROR, message); \
	} \
} while (0)

#define ASYNC_CHECK_ERROR(expr, message) do { \
    if (UNEXPECTED(expr)) { \
    	zend_throw_error(NULL, message); \
    	return; \
    } \
} while (0)

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
