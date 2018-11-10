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


static zend_function *orig_gethostbyname;
static zif_handler orig_gethostbyname_handler;

static zend_function *orig_gethostbynamel;
static zif_handler orig_gethostbynamel_handler;


static void dns_gethostbyname_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *addr)
{
	async_awaitable_queue *q;
	zval result;

	q = (async_awaitable_queue *) req->data;
	
	ZVAL_LONG(&result, status)

	async_awaitable_trigger_continuation(q, &result, 1);
}

static int dns_gethostbyname(uv_getaddrinfo_t *req, char *name, zend_execute_data *execute_data)
{
	async_task_scheduler *scheduler;
	async_awaitable_queue *q;
	
	zval result;
	zend_bool cancelled;
	
	int code;
	
	scheduler = async_task_scheduler_get();
	
	if (async_cli) {
		q = async_awaitable_queue_alloc(scheduler);
		
		req->data = q;
	}

	code = uv_getaddrinfo(&scheduler->loop, req, async_cli ? dns_gethostbyname_cb : NULL, name, NULL, NULL);
	
	if (UNEXPECTED(code < 0)) {
		if (async_cli) {
			async_awaitable_queue_dispose(q);
		}
	
		uv_freeaddrinfo(req->addrinfo);
		
		return code;
	}
	
	if (async_cli) {
		async_task_suspend(q, &result, execute_data, &cancelled);
		async_awaitable_queue_dispose(q);
		
		if (cancelled) {
			uv_cancel((uv_req_t *) req);
			
			return UV_ECANCELED;
		}
		
		code = (int) Z_LVAL_P(&result);
		
		if (UNEXPECTED(code < 0)) {
			uv_freeaddrinfo(req->addrinfo);
			
			return code;
		}
	}
	
	return 0;
}

int async_dns_lookup_ipv4(char *name, struct sockaddr_in *dest, zend_execute_data *execute_data)
{
	uv_getaddrinfo_t req;
	struct addrinfo *info;
	int code;
	
	code = dns_gethostbyname(&req, name, execute_data);
	
	if (code != 0) {
		return code;
	}
	
	info = req.addrinfo;
	
	while (info != NULL) {
		if (info->ai_family == AF_INET && info->ai_protocol == 0) {
			memcpy(dest, info->ai_addr, sizeof(struct sockaddr_in));
			
			uv_freeaddrinfo(req.addrinfo);
			
			return 0;
		}
		
		info = info->ai_next;
	}
	
	uv_freeaddrinfo(req.addrinfo);
	
	return UV_EAI_NODATA;
}

int async_dns_lookup_ipv6(char *name, struct sockaddr_in6 *dest, zend_execute_data *execute_data)
{
	uv_getaddrinfo_t req;
	struct addrinfo *info;
	int code;

	code = dns_gethostbyname(&req, name, execute_data);

	if (code != 0) {
		return code;
	}

	info = req.addrinfo;

	while (info != NULL) {
		if (info->ai_family == AF_INET6 && info->ai_protocol == 0) {
			memcpy(dest, info->ai_addr, sizeof(struct sockaddr_in6));

			uv_freeaddrinfo(req.addrinfo);

			return 0;
		}

		info = info->ai_next;
	}

	uv_freeaddrinfo(req.addrinfo);

	return UV_EAI_NODATA;
}


static PHP_FUNCTION(asyncgethostbyname)
{
	char *name;
	size_t len;
	
	uv_getaddrinfo_t req;
	struct addrinfo *info;
	char ip[16];
	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STRING(name, len)
	ZEND_PARSE_PARAMETERS_END();

	if (len > MAXFQDNLEN) {
		php_error_docref(NULL, E_WARNING, "Host name is too long, the limit is %d characters", MAXFQDNLEN);
		RETURN_STRINGL(name, len);
	}
	
	if (!USED_RET()) {
		return;
	}
	
	code = dns_gethostbyname(&req, name, execute_data);
	
	if (code != 0) {
		RETURN_STRINGL(name, len);
	}
	
	info = req.addrinfo;
	
	while (info != NULL) {
		if (info->ai_family == AF_INET && info->ai_protocol == 0) {
			uv_ip4_name((struct sockaddr_in *) info->ai_addr, ip, info->ai_addrlen);
			uv_freeaddrinfo(req.addrinfo);
			
			RETURN_STRING(ip);
		}
		
		info = info->ai_next;
	}
	
	uv_freeaddrinfo(req.addrinfo);
}

static PHP_FUNCTION(asyncgethostbynamel)
{
	char *name;
	size_t len;
	
	uv_getaddrinfo_t req;
	struct addrinfo *info;
	char ip[16];
	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STRING(name, len)
	ZEND_PARSE_PARAMETERS_END();

	if (len > MAXFQDNLEN) {
		php_error_docref(NULL, E_WARNING, "Host name is too long, the limit is %d characters", MAXFQDNLEN);
		RETURN_STRINGL(name, len);
	}
	
	if (!USED_RET()) {
		return;
	}
	
	code = dns_gethostbyname(&req, name, execute_data);
	
	if (code != 0) {
		RETURN_FALSE;
	}

	info = req.addrinfo;
	
	array_init(return_value);
	
	while (info != NULL) {
		if (info->ai_family == AF_INET && info->ai_protocol == 0) {
			uv_ip4_name((struct sockaddr_in *) info->ai_addr, ip, info->ai_addrlen);
			
			add_next_index_string(return_value, ip);
		}
		
		info = info->ai_next;
	}
	
	uv_freeaddrinfo(req.addrinfo);
}


void async_dns_ce_register()
{
	// TODO: Create a contract for a userdefined DNS resolver that works in non-blocking mode.
}

void async_dns_init()
{
	orig_gethostbyname = (zend_function *) zend_hash_str_find_ptr(EG(function_table), ZEND_STRL("gethostbyname"));
	orig_gethostbyname_handler = orig_gethostbyname->internal_function.handler;
	
	orig_gethostbyname->internal_function.handler = PHP_FN(asyncgethostbyname);
	
	orig_gethostbynamel = (zend_function *) zend_hash_str_find_ptr(EG(function_table), ZEND_STRL("gethostbynamel"));
	orig_gethostbynamel_handler = orig_gethostbynamel->internal_function.handler;
	
	orig_gethostbynamel->internal_function.handler = PHP_FN(asyncgethostbynamel);
}

void async_dns_shutdown()
{
	orig_gethostbyname->internal_function.handler = orig_gethostbyname_handler;
	orig_gethostbynamel->internal_function.handler = orig_gethostbynamel_handler;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
