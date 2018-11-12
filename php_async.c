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

#include "ext/standard/info.h"
#include "SAPI.h"

#include "async_fiber.h"
#include "async_task.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_bool async_cli;
char async_ssl_config_file[MAXPATHLEN];

static void async_execute_ex(zend_execute_data *exec);
static void (*orig_execute_ex)(zend_execute_data *exec);

const int ASYNC_SIGNAL_SIGHUP = 1;
const int ASYNC_SIGNAL_SIGINT = 2;

#ifdef PHP_WIN32
const int ASYNC_SIGNAL_SIGQUIT = -1;
const int ASYNC_SIGNAL_SIGKILL = -1;
const int ASYNC_SIGNAL_SIGTERM = -1;
#else
const int ASYNC_SIGNAL_SIGQUIT = 3;
const int ASYNC_SIGNAL_SIGKILL = 9;
const int ASYNC_SIGNAL_SIGTERM = 15;
#endif

#ifdef SIGUSR1
const int ASYNC_SIGNAL_SIGUSR1 = SIGUSR1;
#else
const int ASYNC_SIGNAL_SIGUSR1 = -1;
#endif

#ifdef SIGUSR2
const int ASYNC_SIGNAL_SIGUSR2 = SIGUSR2;
#else
const int ASYNC_SIGNAL_SIGUSR2 = -1;
#endif

/* Custom executor being used to run the task scheduler before shutdown functions. */
static void async_execute_ex(zend_execute_data *exec)
{
	async_fiber *fiber;
	zend_object *error;

	fiber = ASYNC_G(current_fiber);

	if (orig_execute_ex) {
		orig_execute_ex(exec);
	}

	if (fiber == NULL && exec->prev_execute_data == NULL) {
		error = EG(exception);
		EG(exception) = NULL;

		async_task_scheduler_run();

		EG(exception) = error;
	}
}

void async_init()
{
	async_dns_init();
	async_timer_init();

#ifdef HAVE_ASYNC_FS
	async_filesystem_init();
#endif
}

void async_shutdown()
{
#ifdef HAVE_ASYNC_FS
	async_filesystem_shutdown();
#endif

	async_timer_shutdown();
	async_dns_shutdown();
}


PHP_INI_MH(OnUpdateFiberStackSize)
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


PHP_GINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	ZEND_SECURE_ZERO(async_globals, sizeof(zend_async_globals));
}

PHP_MINIT_FUNCTION(async)
{
	if (0 == strcmp(sapi_module.name, "cli") || 0 == strcmp(sapi_module.name, "phpdbg")) {
		async_cli = 1;
	} else {
		async_cli = 0;
	}

#ifdef HAVE_ASYNC_SSL
	char *file;

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined (LIBRESSL_VERSION_NUMBER)
	SSL_library_init();
	SSL_load_error_strings();
	OPENSSL_config(NULL);
#else
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL);
#endif

	file = getenv("OPENSSL_CONF");

	if (file == NULL) {
		file = getenv("SSLEAY_CONF");
	}

	if (file == NULL) {
		snprintf(async_ssl_config_file, sizeof(async_ssl_config_file), "%s/%s", X509_get_default_cert_area(), "openssl.cnf");
	} else {
		strlcpy(async_ssl_config_file, file, sizeof(async_ssl_config_file));
	}

#endif

	async_stream_ce_register();
	async_socket_ce_register();

	async_awaitable_ce_register();
	async_context_ce_register();
	async_deferred_ce_register();
	async_dns_ce_register();
	async_fiber_ce_register();
	async_process_ce_register();
	async_signal_watcher_ce_register();
	async_ssl_ce_register();
	async_stream_watcher_ce_register();
	async_task_ce_register();
	async_task_scheduler_ce_register();
	async_tcp_ce_register();
	async_timer_ce_register();
	async_udp_socket_ce_register();

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


PHP_MINFO_FUNCTION(async)
{
	char uv_version[20];

	sprintf(uv_version, "%d.%d", UV_VERSION_MAJOR, UV_VERSION_MINOR);

	php_info_print_table_start();
	php_info_print_table_row(2, "Fiber backend", async_fiber_backend_info());
	php_info_print_table_row(2, "Libuv version", uv_version);

#ifdef HAVE_ASYNC_FS
	php_info_print_table_row(2, "Async file IO", async_cli ? "On" : "Off");
#else
	php_info_print_table_row(2, "Async file IO", "Off");
#endif

	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}


PHP_RINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	async_init();

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(async)
{
	async_shutdown();

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
