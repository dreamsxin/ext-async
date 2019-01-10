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
	async_uv_op *op;
	
	op = (async_uv_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	if (status == UV_ECANCELED) {
		ASYNC_FREE_OP(op);
	} else {	
		op->code = status;
		
		ASYNC_FINISH_OP(op);
	}
}

static int dns_gethostbyname(uv_getaddrinfo_t *req, char *name, int proto)
{
	async_task_scheduler *scheduler;
	async_uv_op *op;
	
	struct addrinfo hints;
	int code;
	
	op = NULL;
	scheduler = async_task_scheduler_get();
	
	if (async_cli) {
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
		
		req->data = op;
	}
	
	ZEND_SECURE_ZERO(&hints, sizeof(struct addrinfo));
	
	if (proto > 0) {
		hints.ai_protocol = proto;
	}

	code = uv_getaddrinfo(&scheduler->loop, req, async_cli ? dns_gethostbyname_cb : NULL, name, NULL, &hints);
	
	if (UNEXPECTED(code < 0)) {
		if (async_cli) {
			ASYNC_FREE_OP(op);
		}
	
		uv_freeaddrinfo(req->addrinfo);
		
		return code;
	}
	
	if (async_cli) {
		if (async_await_op((async_op *) op) == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
			
			if (0 == uv_cancel((uv_req_t *) req)) {
				ASYNC_FREE_OP(op);
			} else {
				op->base.status = ASYNC_STATUS_FAILED;
			}

			return FAILURE;
		}
		
		code = op->code;

		ASYNC_FREE_OP(op);
		
		if (UNEXPECTED(code < 0)) {
			uv_freeaddrinfo(req->addrinfo);
			
			return code;
		}
	}
	
	return 0;
}

ASYNC_API int async_dns_lookup_ipv4(char *name, struct sockaddr_in *dest, int proto)
{
	uv_getaddrinfo_t req;
	struct addrinfo *info;
	int code;
	
	code = dns_gethostbyname(&req, name, proto);
	
	if (code != 0) {
		return code;
	}
	
	info = req.addrinfo;
	
	while (info != NULL) {
		if (info->ai_family == AF_INET && (info->ai_protocol == proto || proto == 0)) {
			memcpy(dest, info->ai_addr, sizeof(struct sockaddr_in));
			
			uv_freeaddrinfo(req.addrinfo);
			
			return 0;
		}
		
		info = info->ai_next;
	}
	
	uv_freeaddrinfo(req.addrinfo);
	
	return UV_EAI_NODATA;
}

ASYNC_API int async_dns_lookup_ipv6(char *name, struct sockaddr_in6 *dest, int proto)
{
	uv_getaddrinfo_t req;
	struct addrinfo *info;
	int code;

	code = dns_gethostbyname(&req, name, proto);

	if (code != 0) {
		return code;
	}

	info = req.addrinfo;

	while (info != NULL) {
		if (info->ai_family == AF_INET6 && (info->ai_protocol == proto || proto == 0)) {
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
	
	code = dns_gethostbyname(&req, name, 0);
	
	if (code != 0) {
		RETURN_STRINGL(name, len);
	}
	
	info = req.addrinfo;
	
	while (info != NULL) {
		if (info->ai_family == AF_INET) {
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
	
	zval tmp;
	zend_string *k;
	
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
	
	code = dns_gethostbyname(&req, name, 0);
	
	if (code != 0) {
		RETURN_FALSE;
	}

	info = req.addrinfo;
	
	array_init(&tmp);
	
	while (info != NULL) {
		if (info->ai_family == AF_INET) {
			uv_ip4_name((struct sockaddr_in *) info->ai_addr, ip, info->ai_addrlen);
			
			add_assoc_bool_ex(&tmp, ip, strlen(ip), 1);
		}
		
		info = info->ai_next;
	}
	
	uv_freeaddrinfo(req.addrinfo);
			
	array_init(return_value);
	
	ZEND_HASH_FOREACH_STR_KEY(Z_ARRVAL_P(&tmp), k) {
		add_next_index_str(return_value, zend_string_copy(k));
	} ZEND_HASH_FOREACH_END();
	
	zval_ptr_dtor(&tmp);
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
