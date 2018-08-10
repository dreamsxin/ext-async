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
#include "async_task.h"

#include "ext/standard/info.h"
#include "SAPI.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_bool async_cli;

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
	async_cli = 0;

	if (0 == strcmp(sapi_module.name, "cli") || 0 == strcmp(sapi_module.name, "phpdbg")) {
		async_cli = 1;
	}

	// Use PHP's memory manager within libuv.
	uv_replace_allocator((uv_malloc_func) _emalloc, (uv_realloc_func) _erealloc, (uv_calloc_func) _ecalloc, (uv_free_func) _efree);

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


static zend_string *async_gethostbyname(char *name)
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

	if (res->ai_addrlen == 16) {
		uv_ip4_name((struct sockaddr_in*) res->ai_addr, name, res->ai_addrlen);
	} else {
		uv_ip6_name((struct sockaddr_in6*) res->ai_addr, name, res->ai_addrlen);
	}

	ZVAL_STRING(&result, name);

	uv_freeaddrinfo(res);

	q = (async_awaitable_queue *) req->data;

	async_awaitable_trigger_continuation(q, &result, 1);

	zval_ptr_dtor(&result);
}

static void async_gethostbyname_uv(char *name, zval *return_value, zend_execute_data *execute_data)
{
	async_awaitable_queue *q;

	uv_loop_t *loop;
	uv_getaddrinfo_t *req;

	int err;

	q = emalloc(sizeof(async_awaitable_queue));
	ZEND_SECURE_ZERO(q, sizeof(async_awaitable_queue));

	req = emalloc(sizeof(uv_getaddrinfo_t));
	req->data = q;

	loop = async_task_scheduler_get_loop();
	err = uv_getaddrinfo(loop, req, async_gethostbyname_cb, name, NULL, NULL);

	if (err != 0) {
		efree(req);
		efree(q);

		zend_throw_error(NULL, "Failed to start DNS request to resolve \"%s\": %s", name, uv_strerror(err));
		return;
	}

	async_task_suspend(q, return_value, execute_data, 0, NULL);

	efree(req);
	efree(q);
}

static PHP_FUNCTION(gethostbyname)
{
	char *name;
	size_t len;
	zend_string *ip;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STRING(name, len)
	ZEND_PARSE_PARAMETERS_END();

	ASYNC_CHECK_ERROR(len > MAXFQDNLEN, "Host name is too long, the limit is %d characters", MAXFQDNLEN);

	if (async_cli) {
		async_gethostbyname_uv(name, return_value, execute_data);

		return;
	}

	ip = async_gethostbyname(name);

	ASYNC_CHECK_ERROR(ip == NULL, "Failed to resolve \"%s\" into an IP address", name);

	RETURN_STR(ip);
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
