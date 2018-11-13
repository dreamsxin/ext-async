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
#include "async_ssl.h"
#include "async_xp.h"

static php_stream_transport_factory orig_tcp_factory;
static php_stream_transport_factory orig_tls_factory;

static php_stream_ops tcp_socket_ops;

typedef struct {
	ASYNC_XP_SOCKET_DATA_BASE;
    uv_tcp_t handle;
    uint16_t pending;
    async_op_queue ops;
    zend_bool encrypt;
} async_xp_socket_data_tcp;


static void free_cb(uv_handle_t *handle)
{
	efree(handle->data);
}

static int tcp_socket_bind(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
{
	struct sockaddr_in dest;
	unsigned int flags;
	
	char *ip;
	int port;
	int code;
	zval *tmp;
	
	flags = 0;
	
	if (PHP_STREAM_CONTEXT(stream)) {
		tmp = php_stream_context_get_option(PHP_STREAM_CONTEXT(stream), "socket", "ipv6_v6only");
		
		if (tmp != NULL && Z_TYPE_P(tmp) != IS_NULL && zend_is_true(tmp)) {
			flags |= UV_TCP_IPV6ONLY;
		}
	}
	
	ip = NULL;
	ip = async_xp_parse_ip(xparam->inputs.name, xparam->inputs.namelen, &port, xparam->want_errortext, &xparam->outputs.error_text);
	code = async_dns_lookup_ipv4(ip, &dest, EG(current_execute_data));
	
	if (ip != NULL) {
		efree(ip);
	}
	
	if (UNEXPECTED(code != 0)) {
		return FAILURE;
	}
	
	dest.sin_port = htons(port);
	
	code = uv_tcp_bind((uv_tcp_t *) &data->handle, (const struct sockaddr *) &dest, flags);
	
	return code;
}

static void tcp_socket_listen_cb(uv_stream_t *server, int status)
{
	async_xp_socket_data_tcp *tcp;
	async_uv_op *op;
	
	tcp = (async_xp_socket_data_tcp *) server->data;
	
	if (status < 0) {
		while (tcp->ops.first != NULL) {
			ASYNC_DEQUEUE_CUSTOM_OP(&tcp->ops, op, async_uv_op);
			
			op->code = status;
			
			ASYNC_FINISH_OP(op);
		}
	} else {	
		tcp->pending++;
		
		if (tcp->ops.first != NULL) {
			ASYNC_DEQUEUE_CUSTOM_OP(&tcp->ops, op, async_uv_op);
			ASYNC_FINISH_OP(op);
		}
	}
}

static int tcp_socket_listen(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
{
	int code;
	
	code = uv_listen((uv_stream_t *) &data->handle, xparam->inputs.backlog, tcp_socket_listen_cb);

	return code;
}

#ifdef HAVE_ASYNC_SSL
#ifdef ASYNC_TLS_SNI
static zend_string *get_peer_name(php_stream *stream, php_stream_xport_param *xparam)
{
	zval *val;

	if (!ASYNC_XP_SOCKET_SSL_OPT(stream, "SNI_enabled", val) || zend_is_true(val)) {
		char tmp[256];
		char *name;
		int pos;

		name = NULL;

		ASYNC_XP_SOCKET_SSL_OPT_STRING(stream, "peer_name", name);

		if (name == NULL) {
			name = xparam->inputs.name + xparam->inputs.namelen - 1;

			for (pos = xparam->inputs.namelen - 1; pos >= 0; pos--) {
				if (*name == ':') {
					break;
				}

				name--;
			}

			if (pos > 0) {
				memcpy(tmp, xparam->inputs.name, pos);
				tmp[pos] = '\0';
			} else {
				strcpy(tmp, xparam->inputs.name);
			}

			return zend_string_init(tmp, strlen(tmp), 0);
		}
		
		return zend_string_init(name, strlen(name), 0);
	}
	
	return NULL;
}
#endif
#endif

static void tcp_socket_connect_cb(uv_connect_t *req, int status)
{
	async_uv_op *op;	

	op = (async_uv_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	op->code = status;
	
	ASYNC_FINISH_OP(op);
}

static int tcp_socket_connect(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
{
	async_xp_socket_data_tcp *tcp;
	async_uv_op *op;
	
	uv_connect_t req;
	struct sockaddr_in dest;
	
	char message[512];
	char *ip;
	int port;
	int code;
	
	tcp = (async_xp_socket_data_tcp *) data;
	
	ip = NULL;
	ip = async_xp_parse_ip(xparam->inputs.name, xparam->inputs.namelen, &port, xparam->want_errortext, &xparam->outputs.error_text);
	code = async_dns_lookup_ipv4(ip, &dest, EG(current_execute_data));
	
	if (ip != NULL) {
		efree(ip);
	}
	
	if (UNEXPECTED(code != 0)) {
		return FAILURE;
	}
	
	dest.sin_port = htons(port);
	
	code = uv_tcp_connect(&req, (uv_tcp_t *) &data->handle, (const struct sockaddr *) &dest, tcp_socket_connect_cb);
	
	if (EXPECTED(code == 0)) {
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
		ASYNC_ENQUEUE_OP(&tcp->ops, op);
		
		req.data = op;
		
		if (async_await_op((async_op *) op) == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
			ASYNC_FREE_OP(op);
			
			return FAILURE;
		}
		
		code = op->code;
		
		ASYNC_FREE_OP(op);
	}
	
	if (UNEXPECTED(code < 0)) {
		sprintf(message, "Connect failed: %s", uv_strerror(code));
	
		xparam->outputs.error_code = code;
		xparam->outputs.error_text = zend_string_init(message, strlen(message), 0);
		
		return FAILURE;
	}
	
	data->astream = async_stream_init((uv_stream_t *) &data->handle, 0);
	
	if (tcp->encrypt) {
#ifdef HAVE_ASYNC_SSL
		async_ssl_handshake_data handshake;
		
		data->astream->ssl.ctx = async_ssl_create_context();
		data->astream->ssl.settings.verify_depth = ASYNC_SSL_DEFAULT_VERIFY_DEPTH;

		zval *val;
		
		if (ASYNC_XP_SOCKET_SSL_OPT(stream, "verify_depth", val)) {
			data->astream->ssl.settings.verify_depth = (int) Z_LVAL_P(val);
		}

		async_ssl_setup_verify_callback(data->astream->ssl.ctx, &data->astream->ssl.settings);
		async_ssl_create_engine(&data->astream->ssl);

		async_ssl_setup_encryption(data->astream->ssl.ssl, &data->astream->ssl.settings);

		ZEND_SECURE_ZERO(&handshake, sizeof(async_ssl_handshake_data));

#ifdef ASYNC_TLS_SNI
		zend_string *str;
		
		str = get_peer_name(stream, xparam);
		
		if (str != NULL) {
			data->astream->ssl.settings.peer_name = str;
			
			strcpy(message, ZSTR_VAL(str));
			handshake.host = message;
		}
#endif

		code = async_stream_ssl_handshake(data->astream, &handshake);

#endif
	}
	
	return SUCCESS;
}

static int tcp_socket_shutdown(async_xp_socket_data *data, int how)
{
	int flag;
	
	switch (how) {
	case ASYNC_XP_SOCKET_SHUT_RD:
		flag = ASYNC_STREAM_SHUT_RD;
		break;
	case ASYNC_XP_SOCKET_SHUT_WR:
		flag = ASYNC_STREAM_SHUT_WR;
		break;
	default:
		flag = ASYNC_STREAM_SHUT_RDWR;
	}
	
	async_stream_shutdown(data->astream, flag);
	
	return SUCCESS;
}

static int tcp_socket_get_peer(async_xp_socket_data *data, zend_bool remote, zend_string **textaddr, struct sockaddr **addr, socklen_t *len)
{
	php_sockaddr_storage sa;
	int sl;
	int code;
	
	memset(&sa, 0, sizeof(php_sockaddr_storage));
	sl = sizeof(php_sockaddr_storage);
	
	if (remote) {
		code = uv_tcp_getpeername((uv_tcp_t *) &data->handle, (struct sockaddr *) &sa, &sl);
	} else {
		code = uv_tcp_getsockname((uv_tcp_t *) &data->handle, (struct sockaddr *) &sa, &sl);
	}
	
	if (UNEXPECTED(code < 0)) {
		return code;
	}

	php_network_populate_name_from_sockaddr((struct sockaddr *) &sa, sl, textaddr, addr, len);
	
	return SUCCESS;
}

static int tcp_socket_accept(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
{
	async_xp_socket_data_tcp *server;
	async_xp_socket_data_tcp *client;
	async_uv_op *op;
	
	int code;
	
	server = (async_xp_socket_data_tcp *) data;
	code = 0;
	
	client = emalloc(sizeof(async_xp_socket_data_tcp));
	ZEND_SECURE_ZERO(client, sizeof(async_xp_socket_data_tcp));
	
	uv_tcp_init(&server->scheduler->loop, &client->handle);
	
	client->handle.data = client;

	do {
		if (server->pending > 0) {
			server->pending--;
			
			code = uv_accept((uv_stream_t *) &server->handle, (uv_stream_t *) &client->handle);
			
			if (code == 0) {
				xparam->outputs.client = async_xp_socket_create((async_xp_socket_data *) client, &tcp_socket_ops, NULL STREAMS_CC);
				
				break;
			}
		}
		
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
		ASYNC_ENQUEUE_OP(&server->ops, op);
		
		if (async_await_op((async_op *) op) == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
			ASYNC_FREE_OP(op);
			
			break;
		}
		
		code = op->code;
		
		ASYNC_FREE_OP(op);
		
		if (code < 0) {
			break;
		}
	} while (xparam->outputs.client == NULL);
	
	if (UNEXPECTED(xparam->outputs.client == NULL)) {
		uv_close((uv_handle_t *) &client->handle, free_cb);
	
		return code;
	}
	
	xparam->outputs.client->ctx = stream->ctx;
	
	if (stream->ctx) {
		GC_ADDREF(stream->ctx);
	}
	
	client->astream = async_stream_init((uv_stream_t *) &client->handle, 0);
	
	client->shutdown = tcp_socket_shutdown;
	client->get_peer = tcp_socket_get_peer;

	return SUCCESS;
}

static php_stream *tcp_socket_factory(const char *proto, size_t plen, const char *res, size_t reslen,
	const char *pid, int options, int flags, struct timeval *timeout, php_stream_context *context STREAMS_DC)
{
	async_xp_socket_data_tcp *data;
	php_stream *stream;

	data = emalloc(sizeof(async_xp_socket_data_tcp));
	ZEND_SECURE_ZERO(data, sizeof(async_xp_socket_data_tcp));
	
	stream = async_xp_socket_create((async_xp_socket_data *) data, &tcp_socket_ops, pid STREAMS_CC);

	if (UNEXPECTED(stream == NULL)) {
		efree(data);
	
		return NULL;
	}
 	
 	uv_tcp_init(&data->scheduler->loop, &data->handle);
 	
 	data->connect = tcp_socket_connect;
 	data->bind = tcp_socket_bind;
 	data->listen = tcp_socket_listen;
 	data->accept = tcp_socket_accept;
	data->shutdown = tcp_socket_shutdown;
	data->get_peer = tcp_socket_get_peer;
	
	if (strncmp(proto, "tcp", plen) != 0) {
		data->encrypt = 1;
	}
	
	return stream;
}

void async_tcp_socket_init()
{
	async_xp_socket_populate_ops(&tcp_socket_ops, "tcp_socket/async");

	orig_tcp_factory = async_xp_socket_register("tcp", tcp_socket_factory);
	orig_tls_factory = async_xp_socket_register("tls", tcp_socket_factory);
}

void async_tcp_socket_shutdown()
{
	php_stream_xport_register("tls", orig_tls_factory);
	php_stream_xport_register("tcp", orig_tcp_factory);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
