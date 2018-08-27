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

	fiber = ASYNC_G(current_fiber);

	if (orig_execute_ex) {
		orig_execute_ex(exec);
	}

	if (fiber == NULL && exec->prev_execute_data == NULL) {
		async_task_scheduler_run();
	}
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

	async_awaitable_ce_register();
	async_context_ce_register();
	async_deferred_ce_register();
	async_fiber_ce_register();
	async_signal_watcher_ce_register();
	async_stream_ce_register();
	async_stream_watcher_ce_register();
	async_task_ce_register();
	async_task_scheduler_ce_register();
	async_timer_ce_register();

	async_process_ce_register();
	async_tcp_ce_register();

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
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}


PHP_RINIT_FUNCTION(async)
{
#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(async)
{
	async_task_scheduler_shutdown();

	async_context_shutdown();
	async_fiber_shutdown();

	return SUCCESS;
}


static zend_string *async_gethostbyname_sync(char *name)
{
	struct hostent *hp;
	struct in_addr in;
	char *address;

	hp = php_network_gethostbyname(name);

	if (!hp || !*(hp->h_addr_list)) {
		return NULL;
	}

	memcpy(&in.s_addr, *(hp->h_addr_list), sizeof(in.s_addr));

	address = inet_ntoa(in);
	return zend_string_init(address, strlen(address), 0);
}

static void async_gethostbyname_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res)
{
	async_awaitable_queue *q;
	char name[64];

	zval result;

	q = (async_awaitable_queue *) req->data;

	efree(req);

	if (status != 0) {
		uv_freeaddrinfo(res);

		if (status == UV_ECANCELED) {
			return;
		}

		ZVAL_LONG(&result, status);

		async_awaitable_trigger_continuation(q, &result, 1);

		return;
	}

	if (res->ai_family == AF_INET) {
		uv_ip4_name((struct sockaddr_in*) res->ai_addr, name, res->ai_addrlen);
		ZVAL_STRING(&result, name);
	} else if (res->ai_family == AF_INET6) {
		uv_ip6_name((struct sockaddr_in6*) res->ai_addr, name, res->ai_addrlen);
		ZVAL_STRING(&result, name);
	} else {
		ZVAL_NULL(&result);
	}

	uv_freeaddrinfo(res);

	async_awaitable_trigger_continuation(q, &result, 1);

	zval_ptr_dtor(&result);
}

static void async_gethostbyname_uv(char *name, zval *return_value, zend_execute_data *execute_data)
{
	async_awaitable_queue *q;
	zend_bool cancelled;

	uv_loop_t *loop;
	uv_getaddrinfo_t *req;

	zval result;
	int err;

	if (strcasecmp(name, "localhost") == 0) {
		RETURN_STRING("127.0.0.1");
	}

	q = emalloc(sizeof(async_awaitable_queue));
	ZEND_SECURE_ZERO(q, sizeof(async_awaitable_queue));

	req = emalloc(sizeof(uv_getaddrinfo_t));
	ZEND_SECURE_ZERO(req, sizeof(uv_getaddrinfo_t));

	req->data = q;

	loop = async_task_scheduler_get_loop();
	err = uv_getaddrinfo(loop, req, async_gethostbyname_cb, name, NULL, NULL);

	if (err != 0) {
		efree(req);
		efree(q);

		zend_throw_error(NULL, "Failed to start DNS request to resolve \"%s\": %s", name, uv_strerror(err));
		return;
	}

	async_task_suspend(q, &result, execute_data, &cancelled);

	efree(q);

	if (cancelled) {
		uv_cancel((uv_req_t *) req);
		return;
	}

	if (Z_TYPE_P(&result) == IS_LONG) {
		zend_throw_error(NULL, "Failed to resolve \"%s\": %s", name, uv_strerror((int) Z_LVAL_P(&result)));
		return;
	}

	if (Z_TYPE_P(&result) == IS_NULL) {
		zend_throw_error(NULL, "No IP could be resolved for \"%s\"", name);
		return;
	}

	RETURN_ZVAL(&result, 1, 1);
}

void async_gethostbyname(char *name, zval *return_value, zend_execute_data *execute_data)
{
	zend_string *ip;

	ASYNC_CHECK_ERROR(strlen(name) > MAXFQDNLEN, "Host name is too long, the limit is %d characters", MAXFQDNLEN);

	if (async_cli) {
		async_gethostbyname_uv(name, return_value, execute_data);
	} else {
		ip = async_gethostbyname_sync(name);

		ASYNC_CHECK_ERROR(ip == NULL, "Failed to resolve \"%s\" into an IP address", name);

		RETURN_STR(ip);
	}
}

static PHP_FUNCTION(gethostbyname)
{
	char *name;
	size_t len;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STRING(name, len)
	ZEND_PARSE_PARAMETERS_END();

	async_gethostbyname(name, return_value, execute_data);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_async_gethostbyname, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

static zend_function_entry async_functions[] = {
	ZEND_NS_FE("Concurrent", gethostbyname, arginfo_async_gethostbyname)
	PHP_FE_END
};


zend_module_entry async_module_entry = {
	STANDARD_MODULE_HEADER,
	"async",
	async_functions,
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
