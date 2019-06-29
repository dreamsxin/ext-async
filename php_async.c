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
#include "async_fiber.h"
#include "async_ssl.h"

#include "SAPI.h"
#include "php_main.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

static void async_execute_ex(zend_execute_data *exec);
static void (*orig_execute_ex)(zend_execute_data *exec);


static void execute_root(zend_execute_data *exec)
{
	async_task_scheduler *scheduler;

	scheduler = async_task_scheduler_get();

	zend_try {
		orig_execute_ex(exec);
	} zend_catch {
		async_task_scheduler_handle_exit(scheduler);
	} zend_end_try();
	
	if (UNEXPECTED(EG(exception))) {
		async_task_scheduler_handle_error(scheduler, EG(exception));
		zend_clear_exception();
	}

	async_task_scheduler_run(scheduler, exec);

	ASYNC_FORWARD_EXIT();
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
	STD_PHP_INI_ENTRY("async.forked", "0", PHP_INI_SYSTEM, OnUpdateBool, forked, zend_async_globals, async_globals)
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

ASYNC_CALLBACK init_threads(uv_work_t *req) { }

ASYNC_CALLBACK after_init_threads(uv_work_t *req, int status)
{
	ASYNC_G(threads) = 0;

	free(req);
}

PHP_MINIT_FUNCTION(async)
{
	uv_work_t *req;

	char entry[4];

	ASYNC_G(cli) = (strncmp(sapi_module.name, "cli", sizeof("cli")-1) == SUCCESS);

	REGISTER_INI_ENTRIES();

	if (ASYNC_G(cli)) {
		req = malloc(sizeof(uv_work_t));

		sprintf(entry, "%d", (int) MAX(4, MIN(128, ASYNC_G(threads))));
		uv_os_setenv("UV_THREADPOOL_SIZE", (const char *) entry);

		uv_queue_work(uv_default_loop(), req, init_threads, after_init_threads);
		uv_cancel((uv_req_t *) req);
		uv_run(uv_default_loop(), UV_RUN_DEFAULT);
	}

#ifdef HAVE_ASYNC_SSL
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined (LIBRESSL_VERSION_NUMBER)
	SSL_library_init();
	OPENSSL_config(NULL);
	SSL_load_error_strings();
#else
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL);
#endif
#endif

	async_task_ce_register();
	async_stream_ce_register();
	async_socket_ce_register();

	async_channel_ce_register();
	async_console_ce_register();
	async_context_ce_register();
	async_deferred_ce_register();
	async_dns_ce_register();
	async_event_ce_register();
	async_monitor_ce_register();
	async_pipe_ce_register();
	async_poll_ce_register();
	async_process_ce_register();
	async_signal_ce_register();
	async_ssl_ce_register();
	async_sync_ce_register();
	async_tcp_ce_register();
	async_thread_ce_register();
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
	async_channel_ce_unregister();
	async_deferred_ce_unregister();
	async_dns_ce_unregister();
	async_monitor_ce_unregister();
	async_ssl_ce_unregister();
	async_tcp_ce_unregister();
	async_udp_socket_ce_unregister();

	async_task_ce_unregister();
	async_thread_ce_unregister();

	UNREGISTER_INI_ENTRIES();

	zend_execute_ex = orig_execute_ex;

	if (ASYNC_G(cli)) {
		uv_tty_reset_mode();
	}
	
	return SUCCESS;
}

PHP_MINFO_FUNCTION(async)
{
	char uv_version[20];

	sprintf(uv_version, "%d.%d.%d", UV_VERSION_MAJOR, UV_VERSION_MINOR, UV_VERSION_PATCH);

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
	async_helper_init();

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
