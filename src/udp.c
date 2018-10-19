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

typedef struct _async_udp_send {
	uv_udp_send_t request;
	async_udp_socket *socket;
	async_udp_datagram *datagram;
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

	ASYNC_CHECK_ERROR(code != 0, "Failed to get peer name: %s", uv_strerror(code));

	if (addr.ss_family == AF_INET) {
		code = uv_ip4_name((const struct sockaddr_in *) &addr, name, len);
		ASYNC_CHECK_ERROR(code != 0, "Failed to assemble IP address: %s", uv_strerror(code));

		port = ntohs(((struct sockaddr_in *) &addr)->sin_port);
	} else {
		code = uv_ip6_name((const struct sockaddr_in6 *) &addr, name, len);
		ASYNC_CHECK_ERROR(code != 0, "Failed to assemble IP address: %s", uv_strerror(code));

		port = ntohs(((struct sockaddr_in6 *) &addr)->sin6_port);
	}
	
	socket->ip = zend_string_init(name, strlen(name), 0);
	socket->port = port;
}

static void socket_closed(uv_handle_t *handle)
{
	async_udp_socket *socket;
	
	socket = (async_udp_socket *) handle->data;
	
	OBJ_RELEASE(&socket->std);
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
	
	uv_udp_recv_stop(udp);
	
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
		
		return;
	}
	
	zend_throw_exception_ex(async_stream_exception_ce, 0, "UDP receive error: %s", uv_strerror((int) nread));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&socket->receivers, &data, 0);
}

static void socket_sent(uv_udp_send_t *req, int status)
{
	async_udp_socket *socket;
	
	zval data;
	
	socket = (async_udp_socket *) req->data;
	
	if (status == 0) {
		ZVAL_NULL(&data);
	
		async_awaitable_trigger_next_continuation(&socket->senders, &data, 1);
		
		return;
	}
	
	zend_throw_exception_ex(async_stream_exception_ce, 0, "UDP send error: %s", uv_strerror((int) status));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&socket->senders, &data, 0);
}

static void socket_sent_async(uv_udp_send_t *req, int status)
{
	async_udp_send *send;
	
	send = (async_udp_send *) req->data;
	
	OBJ_RELEASE(&send->datagram->std);
	OBJ_RELEASE(&send->socket->std);
	
	efree(send);
}


static async_udp_socket *async_udp_socket_object_create()
{
	async_udp_socket *socket;

	socket = emalloc(sizeof(async_udp_socket));
	ZEND_SECURE_ZERO(socket, sizeof(async_udp_socket));

	zend_object_std_init(&socket->std, async_udp_socket_ce);
	socket->std.handlers = &async_udp_socket_handlers;
	
	uv_udp_init(async_task_scheduler_get_loop(), &socket->handle);
	socket->handle.data = socket;
	
	ZVAL_UNDEF(&socket->error);

	return socket;
}

static void async_udp_socket_object_dtor(zend_object *object)
{
	async_udp_socket *socket;

	socket = (async_udp_socket *) object;
	
	if (Z_TYPE_P(&socket->error) == IS_UNDEF) {
		GC_ADDREF(&socket->std);
		
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
		zend_throw_error(NULL, "Failed to assemble IP address: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}
	
	code = uv_udp_bind(&socket->handle, (const struct sockaddr *) &dest, UV_UDP_REUSEADDR);
	
	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to bind UDP socket: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}
	
	assemble_peer(socket, return_value, execute_data);
	
	if (UNEXPECTED(EG(exception))) {
		OBJ_RELEASE(&socket->std);
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
		zend_throw_error(NULL, "Failed to assemble IP address: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}
	
	code = uv_udp_bind(&socket->handle, (const struct sockaddr *) &dest, UV_UDP_REUSEADDR);
	
	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to bind UDP socket: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}
	
	code = uv_udp_set_membership(&socket->handle, ZSTR_VAL(group), NULL, UV_JOIN_GROUP);
	
	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to join UDP multicast group: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}
	
	assemble_peer(socket, return_value, execute_data);
	
	if (UNEXPECTED(EG(exception))) {
		OBJ_RELEASE(&socket->std);
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
		GC_ADDREF(&socket->std);

		uv_close((uv_handle_t *) &socket->handle, socket_closed);
	}
}

ZEND_METHOD(UdpSocket, getHost)
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

ZEND_METHOD(UdpSocket, getPeer)
{
	async_udp_socket *socket;
	
	zval tmp;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	
	if (USED_RET()) {
		array_init_size(return_value, 2);
		
		ZVAL_STR(&tmp, socket->ip);
		zend_hash_index_update(Z_ARRVAL_P(return_value), 0, &tmp);
	
		ZVAL_LONG(&tmp, socket->port);
		zend_hash_index_update(Z_ARRVAL_P(return_value), 1, &tmp);
	}
}

ZEND_METHOD(UdpSocket, receive)
{
	async_udp_socket *socket;
	
	int code;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	
	code = uv_udp_recv_start(&socket->handle, socket_alloc_buffer, socket_received);
	
	ASYNC_CHECK_ERROR(code != 0, "Failed to receive UDP data: %s", uv_strerror(code));
	
	async_task_suspend(&socket->receivers, return_value, execute_data, NULL);
}

ZEND_METHOD(UdpSocket, send)
{
	async_udp_socket *socket;
	async_udp_datagram *datagram;
	
	struct sockaddr_in dest;
	uv_udp_send_t req;
	uv_buf_t buffers[1];
	int code;
	
	zval *val;
	zval retval;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	datagram = (async_udp_datagram *) Z_OBJ_P(val);
	
	code = uv_ip4_addr(ZSTR_VAL(datagram->address), (int) datagram->port, &dest);
	
	ASYNC_CHECK_ERROR(code != 0, "Failed to assemble remote IP address: %s", uv_strerror(code));
	
	buffers[0] = uv_buf_init(ZSTR_VAL(datagram->data), ZSTR_LEN(datagram->data));
	
	if (socket->senders.first == NULL) {
	    code = uv_udp_try_send(&socket->handle, buffers, 1, (const struct sockaddr *)&dest);
	    
	    if (code >= 0) {
	        return;
	    }
	    
	    ASYNC_CHECK_ERROR(code != UV_EAGAIN, "Failed to send UDP data: %s", uv_strerror(code));
	}
	
	req.data = socket;
	
	code = uv_udp_send(&req, &socket->handle, buffers, 1, (const struct sockaddr *)&dest, socket_sent);
	
	ASYNC_CHECK_ERROR(code != 0, "Failed to send UDP data: %s", uv_strerror(code));
	
	ZVAL_UNDEF(&retval);
	async_task_suspend(&socket->senders, &retval, execute_data, NULL);
	zval_ptr_dtor(&retval);
}

ZEND_METHOD(UdpSocket, sendAsync)
{
	async_udp_socket *socket;
	async_udp_datagram *datagram;
	async_udp_send *send;
	zend_long limit;
	
	struct sockaddr_in dest;
	uv_buf_t buffers[1];
	int code;
	
	zval *val;
	zval *val2;
	
	val2 = NULL;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_ZVAL(val)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val2)
	ZEND_PARSE_PARAMETERS_END();
	
	if (val2 != NULL && Z_TYPE_P(val2) != IS_NULL) {
		limit = Z_LVAL_P(val2);
		
		ASYNC_CHECK_ERROR(limit < 1024, "UDP buffer size must not be less than %d bytes", 1024);
	} else {
		limit = 0;
	}
	
	socket = (async_udp_socket *) Z_OBJ_P(getThis());
	datagram = (async_udp_datagram *) Z_OBJ_P(val);
	
	code = uv_ip4_addr(ZSTR_VAL(datagram->address), (int) datagram->port, &dest);
	
	ASYNC_CHECK_ERROR(code != 0, "Failed to assemble remote IP address: %s", uv_strerror(code));
	
	buffers[0] = uv_buf_init(ZSTR_VAL(datagram->data), ZSTR_LEN(datagram->data));
	
	if (socket->senders.first == NULL) {
	    code = uv_udp_try_send(&socket->handle, buffers, 1, (const struct sockaddr *)&dest);
	    
	    if (code >= 0) {
	        RETURN_TRUE;
	    }
	    
	    ASYNC_CHECK_ERROR(code != UV_EAGAIN, "Failed to send UDP data: %s", uv_strerror(code));
	}
	
	if (limit > 0 && (socket->handle.send_queue_size + buffers[0].len) > limit) {
		RETURN_FALSE;
	}
	
	send = emalloc(sizeof(async_udp_send));
	send->request.data = send;
	send->socket = socket;
	send->datagram = datagram;
	
	code = uv_udp_send(&send->request, &socket->handle, buffers, 1, (const struct sockaddr *)&dest, socket_sent_async);
	
	if (code != 0) {
		efree(send);
		
		zend_throw_error(NULL, "Failed to send UDP data: %s", uv_strerror(code));
		return;
	}
	
	GC_ADDREF(&socket->std);
	GC_ADDREF(&datagram->std);
	
	RETURN_TRUE;
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

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_get_host, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_get_port, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_get_peer, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_udp_socket_receive, 0, 0, Concurrent\\Network\\UdpDatagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_send, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, datagram, Concurrent\\Network\\UdpDatagram, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_udp_socket_send_async, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_OBJ_INFO(0, datagram, Concurrent\\Network\\UdpDatagram, 0)
	ZEND_ARG_TYPE_INFO(0, limit, IS_LONG, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_udp_socket_functions[] = {
	ZEND_ME(UdpSocket, bind, arginfo_udp_socket_bind, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(UdpSocket, multicast, arginfo_udp_socket_multicast, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(UdpSocket, close, arginfo_udp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, getHost, arginfo_udp_socket_get_host, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, getPort, arginfo_udp_socket_get_port, ZEND_ACC_PUBLIC)
	ZEND_ME(UdpSocket, getPeer, arginfo_udp_socket_get_peer, ZEND_ACC_PUBLIC)
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
	ZEND_ME(UdpDatagram, __construct, arginfo_udp_datagram_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
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
