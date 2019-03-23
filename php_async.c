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
#include "async_ssl.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

ASYNC_API zend_class_entry *async_awaitable_ce;

static void async_execute_ex(zend_execute_data *exec);
static void (*orig_execute_ex)(zend_execute_data *exec);

static int implements_awaitable(zend_class_entry *entry, zend_class_entry *implementor)
{
	if (implementor == async_deferred_awaitable_ce) {
		return SUCCESS;
	}

	if (implementor == async_task_ce) {
		return SUCCESS;
	}

	zend_error_noreturn(
		E_CORE_ERROR,
		"Class %s must not implement interface %s, create an awaitable using %s instead",
		ZSTR_VAL(implementor->name),
		ZSTR_VAL(entry->name),
		ZSTR_VAL(async_deferred_ce->name)
	);

	return FAILURE;
}

static const zend_function_entry empty_funcs[] = {
	ZEND_FE_END
};

static void execute_root(zend_execute_data *exec)
{
	zend_object *error;
	
	error = NULL;

	zend_try {
		orig_execute_ex(exec);
	} zend_catch {
		ASYNC_G(exit) = 1;
	} zend_end_try();
	
	error = EG(exception);
	
	if (UNEXPECTED(error != NULL)) {
		ASYNC_ADDREF(error);
		zend_clear_exception();
	}
	
	async_task_scheduler_run();
	
	EG(exception) = error;
}

/* Custom executor being used to run the task scheduler before shutdown functions. */
static void async_execute_ex(zend_execute_data *exec)
{
	if (UNEXPECTED(exec->prev_execute_data == NULL && ASYNC_G(task) == NULL)) {
		execute_root(exec);
	} else {
		orig_execute_ex(exec);
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

static PHP_INI_MH(OnUpdateThreadCount)
{
	OnUpdateLong(entry, new_value, mh_arg1, mh_arg2, mh_arg3, stage);

	if (ASYNC_G(threads) < 4) {
		ASYNC_G(threads) = 4;
	}

	if (ASYNC_G(threads) > 128) {
		ASYNC_G(threads) = 128;
	}

	return SUCCESS;
}

PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("async.dns", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateBool, dns_enabled, zend_async_globals, async_globals)
	STD_PHP_INI_ENTRY("async.filesystem", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateBool, fs_enabled, zend_async_globals, async_globals)
	STD_PHP_INI_ENTRY("async.stack_size", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateFiberStackSize, stack_size, zend_async_globals, async_globals)
	STD_PHP_INI_ENTRY("async.tcp", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateBool, tcp_enabled, zend_async_globals, async_globals)
	STD_PHP_INI_ENTRY("async.threads", "4", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateThreadCount, threads, zend_async_globals, async_globals)
	STD_PHP_INI_ENTRY("async.timer", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateBool, timer_enabled, zend_async_globals, async_globals)
	STD_PHP_INI_ENTRY("async.udp", "0", PHP_INI_SYSTEM | PHP_INI_PERDIR, OnUpdateBool, udp_enabled, zend_async_globals, async_globals)
PHP_INI_END()

PHP_GINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	memset(async_globals, 0, sizeof(zend_async_globals));
}

PHP_MINIT_FUNCTION(async)
{
	zend_class_entry ce;

	REGISTER_INI_ENTRIES();

#ifdef HAVE_ASYNC_SSL
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined (LIBRESSL_VERSION_NUMBER)
	SSL_library_init();
	OPENSSL_config(NULL);
	SSL_load_error_strings();
#else
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL);
#endif
#endif

	INIT_CLASS_ENTRY(ce, "Concurrent\\Awaitable", empty_funcs);
	async_awaitable_ce = zend_register_internal_interface(&ce);
	async_awaitable_ce->interface_gets_implemented = implements_awaitable;

	async_stream_ce_register();
	async_socket_ce_register();

	async_channel_ce_register();
	async_console_ce_register();
	async_context_ce_register();
	async_deferred_ce_register();
	async_dns_ce_register();
	async_process_ce_register();
	async_signal_watcher_ce_register();
	async_ssl_ce_register();
	async_stream_watcher_ce_register();
	async_task_ce_register();
	async_tcp_ce_register();
	async_timer_ce_register();
	async_udp_socket_ce_register();
	
#ifdef HAVE_ASYNC_SSL
	REGISTER_LONG_CONSTANT("ASYNC_SSL_SUPPORTED", 1, CONST_CS|CONST_PERSISTENT);
	
#ifdef ASYNC_TLS_SNI
	REGISTER_LONG_CONSTANT("ASYNC_SSL_SNI_SUPPORTED", 1, CONST_CS|CONST_PERSISTENT);
#else
	REGISTER_LONG_CONSTANT("ASYNC_SSL_SNI_SUPPORTED", 0, CONST_CS|CONST_PERSISTENT);
#endif
	
#ifdef ASYNC_TLS_ALPN
	REGISTER_LONG_CONSTANT("ASYNC_SSL_ALPN_SUPPORTED", 1, CONST_CS|CONST_PERSISTENT);
#else
	REGISTER_LONG_CONSTANT("ASYNC_SSL_ALPN_SUPPORTED", 0, CONST_CS|CONST_PERSISTENT);
#endif

#else
	REGISTER_LONG_CONSTANT("ASYNC_SSL_SUPPORTED", 0, CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("ASYNC_SSL_SNI_SUPPORTED", 0, CONST_CS|CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("ASYNC_SSL_ALPN_SUPPORTED", 0, CONST_CS|CONST_PERSISTENT);
#endif

	orig_execute_ex = zend_execute_ex;
	zend_execute_ex = async_execute_ex;

	return SUCCESS;
}


PHP_MSHUTDOWN_FUNCTION(async)
{
	async_task_ce_unregister();

	UNREGISTER_INI_ENTRIES();

	zend_execute_ex = orig_execute_ex;

	if (ASYNC_CLI) {
		uv_tty_reset_mode();
	}
	
	return SUCCESS;
}


PHP_MINFO_FUNCTION(async)
{
	char uv_version[20];

	sprintf(uv_version, "%d.%d", UV_VERSION_MAJOR, UV_VERSION_MINOR);

	php_info_print_table_start();
	php_info_print_table_row(2, "Fiber backend", async_fiber_backend_info());
	php_info_print_table_row(2, "Libuv version", uv_version);

	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}


PHP_RINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	async_context_init();
	async_task_scheduler_init();

	if (ASYNC_G(dns_enabled)) {
		async_dns_init();
	}
	
	if (ASYNC_G(timer_enabled)) {
		async_timer_init();
	}
	
	async_filesystem_init();
	async_tcp_socket_init();
	async_udp_socket_init();

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(async)
{	
	if (ASYNC_G(dns_enabled)) {
		async_dns_shutdown();
	}
	
	if (ASYNC_G(timer_enabled)) {
		async_timer_shutdown();
	}
	
	async_filesystem_shutdown();
	async_tcp_socket_shutdown();
	async_udp_socket_shutdown();
	
	async_task_scheduler_shutdown();
	async_context_shutdown();
	
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
