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
#include "async_task.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_udp_socket_ce;
zend_class_entry *async_udp_datagram_ce;

static zend_object_handlers async_udp_socket_handlers;
static zend_object_handlers async_udp_datagram_handlers;

static zend_object *async_udp_datagram_object_create(zend_class_entry *ce);

#define ASYNC_UDP_SOCKET_CONST(name, value) \
	zend_declare_class_constant_long(async_udp_socket_ce, name, sizeof(name)-1, (zend_long)value);

typedef struct _async_udp_send {
	uv_udp_send_t request;
	async_udp_socket *socket;
	async_udp_datagram *datagram;
	async_context *context;
	zend_bool cancelled;
} async_udp_send;


static void assemble_peer(async_udp_socket *socket, zval *return_value, zend_execute_data *execute_data)
{
	struct sockaddr_storage addr;

	char name[64];
	int port;

	int len;
	int code;

	len = sizeof(struct sockaddr_storage);
	code = uv_udp_getsockname(&socket->handle, (struct sockaddr *) &addr, &len);

	ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to get peer name: %s", uv_strerror(code));

	if (addr.ss_family == AF_INET) {
		code = uv_ip4_name((const struct sockaddr_in *) &addr, name, len);
		ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));

		port = ntohs(((struct sockaddr_in *) &addr)->sin_port);
	} else {
		code = uv_ip6_name((const struct sockaddr_in6 *) &addr, name, len);
		ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));

		port = ntohs(((struct sockaddr_in6 *) &addr)->sin6_port);
	}
	
	socket->ip = zend_string_init(name, strlen(name), 0);
	socket->port = port;
}

static void socket_closed(uv_handle_t *handle)
{
	async_udp_socket *socket;
	
	socket = (async_udp_socket *) handle->data;
	
	ASYNC_DELREF(&socket->std);
}

static void socket_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
	buffer->base = emalloc(8192);
	buffer->len = 8192;
}

static void socket_received(uv_udp_t *udp, ssize_t nread, const uv_buf_t *buffer, const struct sockaddr* addr, unsigned int flags)
{
	async_udp_socket *socket;
	async_udp_datagram *datagram;
	
	char peer[17] = { 0 };
	
	zval data;
	
	socket = (async_udp_socket *) udp->data;
	
	if (nread == 0) {
		efree(buffer->base);
		
		return;
	}
	
	if (nread > 0) {
		uv_ip4_name((struct sockaddr_in *) addr, peer, 16);
		
		datagram = (async_udp_datagram *) async_udp_datagram_object_create(async_udp_datagram_ce);
		datagram->data = zend_string_init(buffer->base, (int) nread, 0);
		datagram->address = zend_string_init(peer, strlen(peer), 0);
		datagram->port = ntohs(((struct sockaddr_in *) addr)->sin_port);
		
		efree(buffer->base);
		
		ZVAL_OBJ(&data, &datagram->std);
		async_awaitable_trigger_next_continuation(&socket->receivers, &data, 1);
		zval_ptr_dtor(&data);
	} else {	
		zend_throw_exception_ex(async_stream_exception_ce, 0, "UDP receive error: %s", uv_strerror((int) nread));
	
		ZVAL_OBJ(&data, EG(exception));
		EG(exception) = NULL;
	
		async_awaitable_trigger_continuation(&socket->receivers, &data, 0);
	}
	
	if (socket->receivers.first == NULL) {
		uv_udp_recv_stop(udp);
	}
}

static void socket_sent(uv_udp_send_t *req, int status)
{
	async_udp_send *send;
	
	zval data;
	
	send = (async_udp_send *) req->data;
	
	if (!send->cancelled) {
		if (status == 0) {
			ZVAL_NULL(&data);
		
			async_awaitable_trigger_next_continuation(&send->socket->senders, &data, 1);
		} else {	
			zend_throw_exception_ex(async_stream_exception_ce, 0, "UDP send error: %s", uv_strerror((int) status));
		
			ZVAL_OBJ(&data, EG(exception));
			EG(exception) = NULL;
		
			async_awaitable_trigger_continuation(&send->socket->senders, &data, 0);
		}
	}
	
	ASYNC_DELREF(&send->datagram->std);
	ASYNC_DELREF(&send->socket->std);
	
	efree(send);
}

static void socket_sent_async(uv_udp_send_t *req, int status)
{
	async_udp_send *send;
	
	send = (async_udp_send *) req->data;
	
	ASYNC_REF_EXIT(send->context, send->socket);
	
	ASYNC_DELREF(&send->context->std);
	ASYNC_DELREF(&send->datagram->std);
	ASYNC_DELREF(&send->socket->std);
	
	efree(send);
}

static async_udp_socket *async_udp_socket_object_create()
{
	async_udp_socket *socket;

	socket = emalloc(sizeof(async_udp_socket));
	ZEND_SECURE_ZERO(socket, sizeof(async_udp_socket));

	zend_object_std_init(&socket->std, async_udp_socket_ce);
	socket->std.handlers = &async_udp_socket_handlers;
	
	socket->scheduler = async_task_scheduler_get();
	
	async_awaitable_queue_init(&socket->senders, socket->scheduler);
	async_awaitable_queue_init(&socket->receivers, socket->scheduler);
	
	uv_udp_init(&socket->scheduler->loop, &socket->handle);
	socket->handle.data = socket;
	
	ZVAL_UNDEF(&socket->error);

	return socket;
}

static void async_udp_socket_object_dtor(zend_object *object)
{
	async_udp_socket *socket;

	socket = (async_udp_socket *) object;
	
	if (Z_TYPE_P(&socket->error) == IS_UNDEF) {
		ASYNC_ADDREF(&socket->std);
		
		uv_close((uv_handle_t *) &socket->handle, socket_closed);
	}
}

static void async_udp_socket_object_destroy(zend_object *object)
{
	async_udp_socket *socket;

	socket = (async_udp_socket *) object;
	
	zval_ptr_dtor(&socket->error);

	if (socket->name != NULL) {
		zend_string_release(socket->name);
	}
	
	if (socket->ip != NULL) {
		zend_string_release(socket->ip);
	}

	async_awaitable_queue_destroy(&socket->senders);
	async_awaitable_queue_destroy(&socket->receivers);
	
	zend_object_std_dtor(&socket->std);
}

ZEND_METHOD(UdpSocket, bind)
{
	async_udp_socket *socket;

	zend_string *name;
	zend_long port;
	
	zval ip;
	zval obj;
	
	struct sockaddr_in dest;
	int code;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_STR(name)
		Z_PARAM_LONG(port)
	ZEND_PARSE_PARAMETERS_END();
	
	async_gethostbyname(ZSTR_VAL(name), &ip, execute_data);

	ASYNC_RETURN_ON_ERROR();
	
	socket = async_udp_socket_object_create();
	socket->name = zend_string_copy(name);
	
	code = uv_ip4_addr(Z_STRVAL_P(&ip), (int) port, &dest);

	zval_ptr_dtor(&ip);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to assemble IP address: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	code = uv_udp_bind(&socket->handle, (const struct sockaddr *) &dest, UV_UDP_REUSEADDR);
	
	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to bind UDP socket: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	assemble_peer(socket, return_value, execute_data);
	
	if (UNEXPECTED(EG(exception))) {
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(UdpSocket, multicast)
{
	async_udp_socket *socket;

	zend_string *group;
	zend_long port;
	
	zval obj;
	
	struct sockaddr_in dest;
	int code;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_STR(group)
		Z_PARAM_LONG(port)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = async_udp_socket_object_create();
	socket->name = zend_string_init("localhost", sizeof("localhost")-1, 0);
	
	code = uv_ip4_addr("0.0.0.0", (int) port, &dest);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to assemble IP address: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	code = uv_udp_bind(&socket->handle, (const struct sockaddr *) &dest, UV_UDP_REUSEADDR);
	
	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to bind UDP socket: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	code = uv_udp_set_membership(&socket->handle, ZSTR_VAL(group), NULL, UV_JOIN_GROUP);
	
	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to join UDP multicast group: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	assemble_peer(socket, return_value, execute_data);
	
	if (UNEXPECTED(EG(exception))) {
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(UdpSocket, close)
{
	async_udp_socket *socket;

	zval *val;
	zval ex;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	
	if (Z_TYPE_P(&socket->error) != IS_UNDEF) {
		return;
	}
	
	zend_throw_exception(async_stream_closed_exception_ce, "Socket has been closed", 0);
	
	ZVAL_OBJ(&ex, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&ex), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}
	
	ZVAL_COPY(&socket->error, &ex);	
	zval_ptr_dtor(&ex);

	if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
		ASYNC_ADDREF(&socket->std);

		uv_close((uv_handle_t *) &socket->handle, socket_closed);
	}
}

ZEND_METHOD(UdpSocket, getAddress)
{
	async_udp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_udp_socket *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		ZVAL_STR_COPY(return_value, socket->ip);
	}
}

ZEND_METHOD(UdpSocket, getPort)
{
	async_udp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	
	RETURN_LONG(socket->port);
}

ZEND_METHOD(UdpSocket, setOption)
{
	async_udp_socket *socket;

	zend_long option;
	zval *val;

	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_LONG(option)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	code = 0;

	switch ((int) option) {
	case ASYNC_SOCKET_UDP_TTL:
		code = uv_udp_set_ttl(&socket->handle, (int) Z_LVAL_P(val));
		break;
	case ASYNC_SOCKET_UDP_MULTICAST_LOOP:
		code = uv_udp_set_multicast_loop(&socket->handle, Z_LVAL_P(val) ? 1 : 0);
		break;
	case ASYNC_SOCKET_UDP_MULTICAST_TTL:
		code = uv_udp_set_multicast_ttl(&socket->handle, (int) Z_LVAL_P(val));
		break;
	}

	ASYNC_CHECK_EXCEPTION(code != 0 && code != UV_ENOTSUP, async_socket_exception_ce, "Failed to set socket option: %s", uv_strerror(code));

	RETURN_LONG((code == 0) ? 1 : 0);
}

ZEND_METHOD(UdpSocket, receive)
{
	async_udp_socket *socket;
	async_context *context;
	zend_bool cancelled;
	
	int code;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	
	if (Z_TYPE_P(&socket->error) != IS_UNDEF) {
		Z_ADDREF_P(&socket->error);

		execute_data->opline--;
		zend_throw_exception_internal(&socket->error);
		execute_data->opline++;

		return;
	}
	
	if (socket->receivers.first == NULL) {
		code = uv_udp_recv_start(&socket->handle, socket_alloc_buffer, socket_received);
		
		ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to receive UDP data: %s", uv_strerror(code));
	}
	
	context = async_context_get();
	
	ASYNC_REF_ENTER(context, socket);
	async_task_suspend(&socket->receivers, USED_RET() ? return_value : NULL, execute_data, &cancelled);
	ASYNC_REF_EXIT(context, socket);
	
	if (cancelled && socket->receivers.first == NULL) {
		uv_udp_recv_stop(&socket->handle);
	}
	
	if (socket->scheduler->disposed && Z_TYPE_P(&socket->error) == IS_UNDEF) {
		ZVAL_COPY(&socket->error, &socket->scheduler->error);
		
		uv_udp_recv_stop(&socket->handle);
	
		if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
			ASYNC_ADDREF(&socket->std);
	
			uv_close((uv_handle_t *) &socket->handle, socket_closed);
		}
	}
}

ZEND_METHOD(UdpSocket, send)
{
	async_udp_socket *socket;
	async_udp_datagram *datagram;
	async_udp_send *send;
	
	struct sockaddr_in dest;
	uv_buf_t buffers[1];
	int code;
	
	zval *val;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	datagram = (async_udp_datagram *) Z_OBJ_P(val);
	
	if (Z_TYPE_P(&socket->error) != IS_UNDEF) {
		Z_ADDREF_P(&socket->error);

		execute_data->opline--;
		zend_throw_exception_internal(&socket->error);
		execute_data->opline++;

		return;
	}
	
	code = uv_ip4_addr(ZSTR_VAL(datagram->address), (int) datagram->port, &dest);
	
	ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble remote IP address: %s", uv_strerror(code));
	
	buffers[0] = uv_buf_init(ZSTR_VAL(datagram->data), ZSTR_LEN(datagram->data));
	
	if (socket->senders.first == NULL) {
	    code = uv_udp_try_send(&socket->handle, buffers, 1, (const struct sockaddr *)&dest);
	    
	    if (code >= 0) {
	        return;
	    }
	    
	    ASYNC_CHECK_EXCEPTION(code != UV_EAGAIN, async_socket_exception_ce, "Failed to send UDP data: %s", uv_strerror(code));
	}
	
	send = emalloc(sizeof(async_udp_send));
	send->request.data = send;
	send->socket = socket;
	send->datagram = datagram;
	send->context = async_context_get();
	
	code = uv_udp_send(&send->request, &socket->handle, buffers, 1, (const struct sockaddr *)&dest, socket_sent);
	
	if (code != 0) {	
		efree(send);
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to send UDP data: %s", uv_strerror(code));
		
		return;
	}
	
	ASYNC_ADDREF(&socket->std);
	ASYNC_ADDREF(&datagram->std);
	
	ASYNC_REF_ENTER(send->context, socket);
	async_task_suspend(&socket->senders, NULL, execute_data, &send->cancelled);
	ASYNC_REF_EXIT(send->context, socket);
	
	if (socket->scheduler->disposed && Z_TYPE_P(&socket->error) == IS_UNDEF) {
		ZVAL_COPY(&socket->error, &socket->scheduler->error);
	
		if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
			ASYNC_ADDREF(&socket->std);
	
			uv_close((uv_handle_t *) &socket->handle, socket_closed);
		}
	}
}

ZEND_METHOD(UdpSocket, sendAsync)
{
	async_udp_socket *socket;
	async_udp_datagram *datagram;
	async_udp_send *send;
	
	struct sockaddr_in dest;
	uv_buf_t buffers[1];
	int code;
	
	zval *val;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	datagram = (async_udp_datagram *) Z_OBJ_P(val);
	
	if (Z_TYPE_P(&socket->error) != IS_UNDEF) {
		Z_ADDREF_P(&socket->error);

		execute_data->opline--;
		zend_throw_exception_internal(&socket->error);
		execute_data->opline++;

		return;
	}
	
	code = uv_ip4_addr(ZSTR_VAL(datagram->address), (int) datagram->port, &dest);
	
	ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble remote IP address: %s", uv_strerror(code));
	
	buffers[0] = uv_buf_init(ZSTR_VAL(datagram->data), ZSTR_LEN(datagram->data));
	
	if (socket->senders.first == NULL) {
	    code = uv_udp_try_send(&socket->handle, buffers, 1, (const struct sockaddr *)&dest);
	    
	    if (code >= 0) {
	        RETURN_LONG(0);
	    }
	    
	    ASYNC_CHECK_EXCEPTION(code != UV_EAGAIN, async_socket_exception_ce, "Failed to send UDP data: %s", uv_strerror(code));
	}
	
	send = emalloc(sizeof(async_udp_send));
	send->request.data = send;
	send->socket = socket;
	send->datagram = datagram;
	send->context = async_context_get();
	
	code = uv_udp_send(&send->request, &socket->handle, buffers, 1, (const struct sockaddr *)&dest, socket_sent_async);
	
	if (code != 0) {
		efree(send);
		
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to send UDP data: %s", uv_strerror(code));
		return;
	}
	
	ASYNC_REF_ENTER(send->context, socket);
	
	ASYNC_ADDREF(&socket->std);
	ASYNC_ADDREF(&datagram->std);
	ASYNC_ADDREF(&send->context->std);
	
	RETURN_LONG(socket->handle.send_queue_size);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_udp_socket_bind, 0, 2, Concurrent\\Network\\UdpSocket, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_udp_socket_multicast, 0, 2, Concurrent\\Network\\UdpSocket, 0)
	ZEND_ARG_TYPE_INFO(0, group, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_get_address, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_get_port, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_set_option, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_udp_socket_receive, 0, 0, Concurrent\\Network\\UdpDatagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_send, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, datagram, Concurrent\\Network\\UdpDatagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_send_async, 0, 1, IS_LONG, 0)
	ZEND_ARG_OBJ_INFO(0, datagram, Concurrent\\Network\\UdpDatagram, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_udp_socket_functions[] = {
	ZEND_ME(UdpSocket, bind, arginfo_udp_socket_bind, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(UdpSocket, multicast, arginfo_udp_socket_multicast, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(UdpSocket, close, arginfo_udp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, getAddress, arginfo_udp_socket_get_address, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, getPort, arginfo_udp_socket_get_port, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, setOption, arginfo_udp_socket_set_option, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, receive, arginfo_udp_socket_receive, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, send, arginfo_udp_socket_send, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, sendAsync, arginfo_udp_socket_send_async, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zval *async_udp_datagram_read_property(zval *object, zval *member, int type, void **cache_slot, zval *rv)
{
	async_udp_datagram *datagram;
	
	char *key;
	
	datagram = (async_udp_datagram *) Z_OBJ_P(object);
	
	key = Z_STRVAL_P(member);
	
	if (strcmp(key, "data") == 0) {
		ZVAL_STR_COPY(rv, datagram->data);
	} else if (strcmp(key, "address") == 0) {
		ZVAL_STR_COPY(rv, datagram->address);
	} else if (strcmp(key, "port") == 0) {
		ZVAL_LONG(rv, datagram->port);
	} else {
		rv = &EG(uninitialized_zval);
	}
	
	return rv;
}

int async_udp_datagram_has_property(zval *object, zval *member, int has_set_exists, void **cache_slot)
{
	zval rv;
	zval *val;

    val = async_udp_datagram_read_property(object, member, 0, cache_slot, &rv);
    
    if (val == &EG(uninitialized_zval)) {
    	return zend_std_has_property(object, member, has_set_exists, cache_slot);
    }
    
    switch (has_set_exists) {
    	case ZEND_PROPERTY_EXISTS:
    	case ZEND_PROPERTY_ISSET:
    		zval_ptr_dtor(val);
    		return 1;
    }
    
    convert_to_boolean(val);
    
    return (Z_TYPE_P(val) == IS_TRUE) ? 1 : 0;
}

static zend_object *async_udp_datagram_object_create(zend_class_entry *ce)
{
	async_udp_datagram *datagram;
	
	datagram = emalloc(sizeof(async_udp_datagram));
	ZEND_SECURE_ZERO(datagram, sizeof(async_udp_datagram));
	
	zend_object_std_init(&datagram->std, ce);
	datagram->std.handlers = &async_udp_datagram_handlers;
	
	return &datagram->std;
}

static void async_udp_datagram_object_destroy(zend_object *object)
{
	async_udp_datagram *datagram;

	datagram = (async_udp_datagram *) object;
	
	zend_string_release(datagram->data);
	zend_string_release(datagram->address);

	zend_object_std_dtor(&datagram->std);
}

ZEND_METHOD(UdpDatagram, __construct)
{
	async_udp_datagram *datagram;
	
	zend_string *data;
	zend_string *address;
	zend_long port;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 3, 3)
		Z_PARAM_STR(data)
		Z_PARAM_STR(address)
		Z_PARAM_LONG(port)
	ZEND_PARSE_PARAMETERS_END();
	
	datagram = (async_udp_datagram *) Z_OBJ_P(getThis());
	datagram->data = zend_string_copy(data);
	datagram->address = zend_string_copy(address);
	datagram->port = port;
}

ZEND_METHOD(UdpDatagram, __debugInfo)
{
	async_udp_datagram *datagram;

	HashTable *info;

	ZEND_PARSE_PARAMETERS_NONE();
	
	datagram = (async_udp_datagram *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		info = async_info_init();

		async_info_prop_str(info, "data", datagram->data);
		async_info_prop_str(info, "address", datagram->address);
		async_info_prop_long(info, "port", datagram->port);

		RETURN_ARR(info);
	}
}

ZEND_METHOD(UdpDatagram, withData)
{
	async_udp_datagram *datagram;
	async_udp_datagram *result;
	
	zend_string *data;
	
	zval obj;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(data)
	ZEND_PARSE_PARAMETERS_END();
	
	datagram = (async_udp_datagram *) Z_OBJ_P(getThis());
	result = (async_udp_datagram *) async_udp_datagram_object_create(async_udp_datagram_ce);
	
	result->data = zend_string_copy(data);
	result->address = zend_string_copy(datagram->address);
	result->port = datagram->port;
	
	ZVAL_OBJ(&obj, &result->std);
	
	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(UdpDatagram, withPeer)
{
	async_udp_datagram *datagram;
	async_udp_datagram *result;
	
	zend_string *address;
	zend_long port;
	
	zval obj;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_STR(address)
		Z_PARAM_LONG(port)
	ZEND_PARSE_PARAMETERS_END();
	
	datagram = (async_udp_datagram *) Z_OBJ_P(getThis());
	result = (async_udp_datagram *) async_udp_datagram_object_create(async_udp_datagram_ce);
	
	result->data = zend_string_copy(datagram->data);
	result->address = zend_string_copy(address);
	result->port = port;
	
	ZVAL_OBJ(&obj, &result->std);
	
	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_udp_datagram_ctor, 0, 0, 3)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, address, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_datagram_debug_info, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_udp_datagram_with_data, 0, 1, Concurrent\\Network\\UdpDatagram, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_udp_datagram_with_peer, 0, 2, Concurrent\\Network\\UdpDatagram, 0)
	ZEND_ARG_TYPE_INFO(0, address, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_udp_datagram_functions[] = {
	ZEND_ME(UdpDatagram, __construct, arginfo_udp_datagram_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpDatagram, __debugInfo, arginfo_udp_datagram_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpDatagram, withData, arginfo_udp_datagram_with_data, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpDatagram, withPeer, arginfo_udp_datagram_with_peer, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_udp_socket_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\UdpSocket", async_udp_socket_functions);
	async_udp_socket_ce = zend_register_internal_class(&ce);
	async_udp_socket_ce->ce_flags |= ZEND_ACC_FINAL;
	async_udp_socket_ce->serialize = zend_class_serialize_deny;
	async_udp_socket_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_udp_socket_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_udp_socket_handlers.free_obj = async_udp_socket_object_destroy;
	async_udp_socket_handlers.dtor_obj = async_udp_socket_object_dtor;
	async_udp_socket_handlers.clone_obj = NULL;
	
	zend_class_implements(async_udp_socket_ce, 1, async_socket_ce);

	ASYNC_UDP_SOCKET_CONST("TTL", ASYNC_SOCKET_UDP_TTL);
	ASYNC_UDP_SOCKET_CONST("MULTICAST_LOOP", ASYNC_SOCKET_UDP_MULTICAST_LOOP);
	ASYNC_UDP_SOCKET_CONST("MULTICAST_TTL", ASYNC_SOCKET_UDP_MULTICAST_TTL);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\UdpDatagram", async_udp_datagram_functions);
	async_udp_datagram_ce = zend_register_internal_class(&ce);
	async_udp_datagram_ce->ce_flags |= ZEND_ACC_FINAL;
	async_udp_datagram_ce->create_object = async_udp_datagram_object_create;
	async_udp_datagram_ce->serialize = zend_class_serialize_deny;
	async_udp_datagram_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_udp_datagram_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_udp_datagram_handlers.free_obj = async_udp_datagram_object_destroy;
	async_udp_datagram_handlers.clone_obj = NULL;
	async_udp_datagram_handlers.has_property = async_udp_datagram_has_property;
	async_udp_datagram_handlers.read_property = async_udp_datagram_read_property;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
