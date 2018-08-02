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

#include "async_fiber.h"
#include "ext/standard/info.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

static void async_execute_ex(zend_execute_data *exec);
static void (*orig_execute_ex)(zend_execute_data *exec);


/* Custom executor being used to run the task scheduler before shutdown functions. */
static void async_execute_ex(zend_execute_data *exec)
{
	async_fiber *fiber;

	fiber = ASYNC_G(current_fiber);

	if (orig_execute_ex) {
		orig_execute_ex(exec);
	}

	if (fiber == NULL && exec->prev_execute_data == NULL) {
		async_task_scheduler_shutdown();
	}
}


static PHP_INI_MH(OnUpdateFiberStackSize)
{
	OnUpdateLong(entry, new_value, mh_arg1, mh_arg2, mh_arg3, stage);

	if (ASYNC_G(stack_size) < 0) {
		ASYNC_G(stack_size) = 0;
	}

	return SUCCESS;
}

PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("async.stack_size", "0", PHP_INI_SYSTEM, OnUpdateFiberStackSize, stack_size, zend_async_globals, async_globals)
PHP_INI_END()


static PHP_GINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	ZEND_SECURE_ZERO(async_globals, sizeof(zend_async_globals));
}

PHP_MINIT_FUNCTION(async)
{
	async_awaitable_ce_register();
	async_context_ce_register();
	async_deferred_ce_register();
	async_fiber_ce_register();
	async_stream_watcher_ce_register();
	async_task_ce_register();
	async_task_scheduler_ce_register();
	async_timer_ce_register();

	REGISTER_INI_ENTRIES();

	orig_execute_ex = zend_execute_ex;
	zend_execute_ex = async_execute_ex;

	return SUCCESS;
}


PHP_MSHUTDOWN_FUNCTION(async)
{
	async_fiber_ce_unregister();

	UNREGISTER_INI_ENTRIES();

	zend_execute_ex = orig_execute_ex;

	return SUCCESS;
}


static PHP_MINFO_FUNCTION(async)
{
	char uv_version[20];

	sprintf(uv_version, "%d.%d", UV_VERSION_MAJOR, UV_VERSION_MINOR);

	php_info_print_table_start();
	php_info_print_table_row(2, "Fiber backend", async_fiber_backend_info());
	php_info_print_table_row(2, "Libuv version", uv_version);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}


static PHP_RINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	return SUCCESS;
}

static PHP_RSHUTDOWN_FUNCTION(async)
{
	async_task_scheduler_shutdown();
	async_context_shutdown();
	async_fiber_shutdown();

	return SUCCESS;
}

zend_module_entry async_module_entry = {
	STANDARD_MODULE_HEADER,
	"async",
	NULL,
	PHP_MINIT(async),
	PHP_MSHUTDOWN(async),
	PHP_RINIT(async),
	PHP_RSHUTDOWN(async),
	PHP_MINFO(async),
	PHP_ASYNC_VERSION,
	PHP_MODULE_GLOBALS(async),
	PHP_GINIT(async),
	NULL,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};


#ifdef COMPILE_DL_ASYNC
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(async)
#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
