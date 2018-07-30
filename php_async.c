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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"

#include "php_async.h"

#include "uv.h"

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
	async_io_ce_register();
	async_fiber_ce_register();
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
	uv_loop_t *loop;

#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	loop = emalloc(sizeof(uv_loop_t));
	uv_loop_init(loop);

	ASYNC_G(loop) = loop;

	return SUCCESS;
}

static PHP_RSHUTDOWN_FUNCTION(async)
{
	uv_loop_t *loop;

	async_task_scheduler_shutdown();
	async_context_shutdown();
	async_fiber_shutdown();

	loop = ASYNC_G(loop);

	if (loop != NULL) {
		if (uv_loop_alive(loop)) {
			async_task_scheduler_get();
			async_task_scheduler_shutdown();
		}

		ZEND_ASSERT(!uv_loop_alive(loop));

		uv_loop_close(loop);
		efree(loop);
	}

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
