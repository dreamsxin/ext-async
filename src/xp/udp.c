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
#include "async_xp.h"

static php_stream_transport_factory orig_udp_factory;
static php_stream_ops udp_socket_ops;

#define ASYNC_XP_SOCKET_UDP_FLAG_RECEIVING 1 << 10

typedef struct {
	ASYNC_XP_SOCKET_DATA_BASE;
    uv_udp_t handle;
    async_op_queue senders;
    async_op_queue receivers;
} async_xp_socket_data_udp;

typedef struct {
	async_op base;
	int code;
	unsigned int flags;
	php_stream_xport_param *xparam;
} async_xp_udp_receive_op;


static int udp_socket_bind(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
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
			flags |= UV_UDP_IPV6ONLY;
		}
		
		tmp = php_stream_context_get_option(PHP_STREAM_CONTEXT(stream), "socket", "so_reuseport");
		
		if (tmp != NULL && Z_TYPE_P(tmp) != IS_NULL && zend_is_true(tmp)) {
			flags |= UV_UDP_REUSEADDR ;
		}
	}
	
	ip = NULL;
	ip = async_xp_parse_ip(xparam->inputs.name, xparam->inputs.namelen, &port, xparam->want_errortext, &xparam->outputs.error_text);
	code = async_dns_lookup_ipv4(ip, &dest, IPPROTO_UDP);
	
	if (ip != NULL) {
		efree(ip);
	}
	
	if (UNEXPECTED(code != 0)) {
		return FAILURE;
	}
	
	dest.sin_port = htons(port);
	
	code = uv_udp_bind((uv_udp_t *) &data->handle, (const struct sockaddr *) &dest, flags);
	
	if (code > 0) {
		return code;
	}
	
	if (PHP_STREAM_CONTEXT(stream)) {
		tmp = php_stream_context_get_option(PHP_STREAM_CONTEXT(stream), "socket", "so_broadcast");
		
		if (tmp != NULL && Z_TYPE_P(tmp) != IS_NULL && zend_is_true(tmp)) {
			code = uv_udp_set_broadcast((uv_udp_t *) &data->handle, 1);
			
			if (code < 0) {
				return code;
			}
		}
	}
	
	return SUCCESS;
}

static void udp_socket_send_cb(uv_udp_send_t *req, int status)
{
	async_uv_op *op;
	
	op = (async_uv_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	op->code = status;
	
	ASYNC_FINISH_OP(op);
}

static int udp_socket_send(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
{
	async_xp_socket_data_udp *udp;
	async_uv_op *op;

	uv_udp_send_t req;
	uv_buf_t bufs[1];
	int code;
	
	if (xparam->inputs.addr == NULL) {
		// TODO: Implement support for connected UDP sockets.
		return FAILURE;
	}	

	bufs[0] = uv_buf_init(xparam->inputs.buf, xparam->inputs.buflen);
	
	code = uv_udp_try_send((uv_udp_t *) &data->handle, bufs, 1, (const struct sockaddr *) xparam->inputs.addr);
	
	if (code >= 0) {
		return SUCCESS;
	}
	
	if (UNEXPECTED(code < 0 && code != UV_EAGAIN)) {
		return code;
	}
	
	code = uv_udp_send(&req, (uv_udp_t *) &data->handle, bufs, 1, (const struct sockaddr *) xparam->inputs.addr, udp_socket_send_cb);
	
	if (EXPECTED(code >= 0)) {
		udp = (async_xp_socket_data_udp *) data;
	
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
		ASYNC_ENQUEUE_OP(&udp->senders, op);
		
		req.data = op;
		
		if (async_await_op((async_op *) op) == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
			ASYNC_FREE_OP(op);
			
			return FAILURE;
		}
		
		code = op->code;
		
		ASYNC_FREE_OP(op);
	}
	
	return code;
}

static void udp_socket_receive_cb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned int flags)
{
	async_xp_socket_data_udp *udp;
	async_xp_udp_receive_op *op;
	
	udp = (async_xp_socket_data_udp *) handle->data;
	
	ZEND_ASSERT(udp->receivers.first != NULL);
	
	ASYNC_DEQUEUE_CUSTOM_OP(&udp->receivers, op, async_xp_udp_receive_op);
	
	op->code = nread;
	op->flags = flags;
	
	php_network_populate_name_from_sockaddr(
		(struct sockaddr *) addr,
		sizeof(struct sockaddr),
		op->xparam->want_textaddr ? &op->xparam->outputs.textaddr : NULL,
		op->xparam->want_addr ? &op->xparam->outputs.addr : NULL,
		op->xparam->want_addr ? &op->xparam->outputs.addrlen : NULL
	);
	
	ASYNC_FINISH_OP(op);
	
	if (udp->receivers.first == NULL) {
		uv_udp_recv_stop(handle);
		
		udp->flags ^= ASYNC_XP_SOCKET_UDP_FLAG_RECEIVING;
	}
}

static void udp_socket_receive_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	async_xp_socket_data_udp *udp;
	php_stream_xport_param *xparam;
	
	udp = (async_xp_socket_data_udp *) handle->data;
	
	ZEND_ASSERT(udp->receivers.first != NULL);
	
	xparam = ((async_xp_udp_receive_op *) udp->receivers.first)->xparam;
	
	buf->base = xparam->inputs.buf;
	buf->len = xparam->inputs.buflen;
}

static int udp_socket_receive(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam)
{
	async_xp_socket_data_udp *udp;
	async_xp_udp_receive_op *op;
	
	int code;
	
	udp = (async_xp_socket_data_udp *) data;
	
	if (!(udp->flags & ASYNC_XP_SOCKET_UDP_FLAG_RECEIVING)) {
		uv_udp_recv_start(&udp->handle, udp_socket_receive_alloc, udp_socket_receive_cb);
	}
	
	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_xp_udp_receive_op));
	ASYNC_ENQUEUE_OP(&udp->receivers, op);
	
	op->xparam = xparam;
	
	if (async_await_op((async_op *) op) == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
		ASYNC_FREE_OP(op);
		
		return FAILURE;
	}
	
	code = op->code;
	
	ASYNC_FREE_OP(op);
	
	return code;
}

static int udp_socket_get_peer(async_xp_socket_data *data, zend_bool remote, zend_string **textaddr, struct sockaddr **addr, socklen_t *len)
{
	php_sockaddr_storage sa;
	int sl;
	int code;
	
	memset(&sa, 0, sizeof(php_sockaddr_storage));
	sl = sizeof(php_sockaddr_storage);
	
	if (remote) {
		return FAILURE;
	}
	
	code = uv_udp_getsockname((uv_udp_t *) &data->handle, (struct sockaddr *) &sa, &sl);
	
	if (UNEXPECTED(code < 0)) {
		return code;
	}

	php_network_populate_name_from_sockaddr((struct sockaddr *) &sa, sl, textaddr, addr, len);
	
	return SUCCESS;
}

static php_stream *udp_socket_factory(const char *proto, size_t plen, const char *res, size_t reslen,
	const char *pid, int options, int flags, struct timeval *timeout, php_stream_context *context STREAMS_DC)
{
	async_xp_socket_data_udp *data;
	php_stream *stream;

	data = emalloc(sizeof(async_xp_socket_data_udp));
	ZEND_SECURE_ZERO(data, sizeof(async_xp_socket_data_udp));
	
	data->flags = ASYNC_XP_SOCKET_FLAG_DGRAM;
	
	stream = async_xp_socket_create((async_xp_socket_data *) data, &udp_socket_ops, pid STREAMS_CC);

	if (UNEXPECTED(stream == NULL)) {
		efree(data);
	
		return NULL;
	}
 	
 	uv_udp_init(&data->scheduler->loop, &data->handle);
 	
 	data->bind = udp_socket_bind;
 	data->send = udp_socket_send;
 	data->receive = udp_socket_receive;
 	data->get_peer = udp_socket_get_peer;
 	
	return stream;
}

void async_udp_socket_init()
{
	async_xp_socket_populate_ops(&udp_socket_ops, "udp_socket/async");

	orig_udp_factory = async_xp_socket_register("udp", udp_socket_factory);
}

void async_udp_socket_shutdown()
{
	php_stream_xport_register("udp", orig_udp_factory);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
