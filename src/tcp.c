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
#include "async_stream.h"
#include "async_task.h"
#include "zend_inheritance.h"

#ifdef ZEND_WIN32
#include "win32/sockets.h"
#endif

#define ASYNC_SOCKET_TCP_NODELAY 100
#define ASYNC_SOCKET_TCP_KEEPALIVE 101
#define ASYNC_SOCKET_TCP_SIMULTANEOUS_ACCEPTS 150

zend_class_entry *async_tcp_socket_ce;
zend_class_entry *async_tcp_socket_reader_ce;
zend_class_entry *async_tcp_socket_writer_ce;
zend_class_entry *async_tcp_server_ce;

static zend_object_handlers async_tcp_socket_handlers;
static zend_object_handlers async_tcp_socket_reader_handlers;
static zend_object_handlers async_tcp_socket_writer_handlers;
static zend_object_handlers async_tcp_server_handlers;

typedef struct {
	/* PHP object handle. */
	zend_object std;
	
	/* Task scheduler being used. */
	async_task_scheduler *scheduler;

	/* UV TCP handle. */
	uv_tcp_t handle;

	/* Hostname or IP address that was used to establish the connection. */
	zend_string *name;

	zend_string *addr;
	uint16_t port;

	/* Number of pending connection attempts queued in the backlog. */
	zend_uchar pending;

	/* Error being used to close the server. */
	zval error;
	
	/* Number of referenced accept operations. */
	zend_uchar ref_count;

	/* Queue of tasks waiting to accept a socket connection. */
	async_op_queue accepts;
	
	async_cancel_cb cancel;

#ifdef HAVE_ASYNC_SSL
	/* TLS server encryption settings. */
	async_tls_server_encryption *encryption;

	/* Server SSL context (shared between all socket connections). */
	SSL_CTX *ctx;
#endif
} async_tcp_server;

typedef struct {
	/* PHP object handle. */
	zend_object std;

	/* UV TCP handle. */
	uv_tcp_t handle;

	/* Task scheduler being used. */
	async_task_scheduler *scheduler;
	
	async_cancel_cb cancel;

	/* Hostname or IP address that was used to establish the connection. */
	zend_string *name;
	
	zend_string *local_addr;
	uint16_t local_port;
	
	zend_string *remote_addr;
	uint16_t remote_port;

	/* Refers to the (local) server that accepted the TCP socket connection. */
	async_tcp_server *server;
	
	async_stream *stream;

	/* Error being used to close the read stream. */
	zval read_error;

	/* Error being used to close the write stream. */
	zval write_error;

#ifdef HAVE_ASYNC_SSL
	/* TLS client encryption settings. */
	async_tls_client_encryption *encryption;
#endif
} async_tcp_socket;

typedef struct {
	/* PHP object handle. */
	zend_object std;

	/* Socket being used to delegate reads. */
	async_tcp_socket *socket;
} async_tcp_socket_reader;

typedef struct {
	/* PHP object handle. */
	zend_object std;

	/* Socket being used to delegate writes. */
	async_tcp_socket *socket;
} async_tcp_socket_writer;

static async_tcp_socket *async_tcp_socket_object_create();
static async_tcp_socket_reader *async_tcp_socket_reader_object_create(async_tcp_socket *socket);
static async_tcp_socket_writer *async_tcp_socket_writer_object_create(async_tcp_socket *socket);

#define ASYNC_TCP_SOCKET_CONST(name, value) \
	zend_declare_class_constant_long(async_tcp_socket_ce, name, sizeof(name)-1, (zend_long)value);

#define ASYNC_TCP_SERVER_CONST(name, value) \
	zend_declare_class_constant_long(async_tcp_server_ce, name, sizeof(name)-1, (zend_long)value);

typedef struct {
	uv_write_t request;
	async_tcp_socket *socket;
	zend_string *data;
	uv_buf_t *buffers;
	unsigned int nbufs;
	uv_buf_t *tmp;
} async_tcp_write;


static inline void assemble_peer(uv_tcp_t *tcp, zend_bool remote, zend_string **address, uint16_t *port)
{
	struct sockaddr_storage addr;

	char name[64];

	int len;
	int code;

	len = sizeof(struct sockaddr_storage);

	if (remote) {
		code = uv_tcp_getpeername(tcp, (struct sockaddr *) &addr, &len);
	} else {
		code = uv_tcp_getsockname(tcp, (struct sockaddr *) &addr, &len);
	}

	ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to get peer name: %s", uv_strerror(code));

	if (addr.ss_family == AF_INET) {
		code = uv_ip4_name((const struct sockaddr_in *) &addr, name, len);
		ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));

		*port = ntohs(((struct sockaddr_in *) &addr)->sin_port);
	} else {
		code = uv_ip6_name((const struct sockaddr_in6 *) &addr, name, len);
		ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));

		*port = ntohs(((struct sockaddr_in6 *) &addr)->sin6_port);
	}
	
	*address = zend_string_init(name, strlen(name), 0);
}

static void socket_disposed(uv_handle_t *handle)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) handle->data;
	
	ZEND_ASSERT(socket != NULL);
	
	ASYNC_DELREF(&socket->std);
}

static void shutdown_socket(void *arg, zval *error)
{
	async_tcp_socket *socket;
	
	socket = (async_tcp_socket *) arg;
	
	ZEND_ASSERT(socket != NULL);
	
	socket->cancel.func = NULL;
	
	if (error != NULL) {
		if (Z_TYPE_P(&socket->read_error) == IS_UNDEF) {
			ZVAL_COPY(&socket->read_error, error);
		}
		
		if (Z_TYPE_P(&socket->write_error) == IS_UNDEF) {
			ZVAL_COPY(&socket->write_error, error);
		}
	}
	
	if (socket->stream == NULL) {
		if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
			ASYNC_ADDREF(&socket->std);
			
			socket->handle.data = socket;
			
			uv_close((uv_handle_t *) &socket->handle, socket_disposed);
		}
	} else if (!(socket->stream->flags & ASYNC_STREAM_CLOSED)) {
		ASYNC_ADDREF(&socket->std);
		
		async_stream_close(socket->stream, socket_disposed, socket);
	}
}


static async_tcp_socket *async_tcp_socket_object_create()
{
	async_tcp_socket *socket;

	socket = emalloc(sizeof(async_tcp_socket));
	ZEND_SECURE_ZERO(socket, sizeof(async_tcp_socket));

	zend_object_std_init(&socket->std, async_tcp_socket_ce);
	socket->std.handlers = &async_tcp_socket_handlers;
	
	socket->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&socket->scheduler->std);
	
	socket->cancel.object = socket;
	socket->cancel.func = shutdown_socket;
	
	ASYNC_Q_ENQUEUE(&socket->scheduler->shutdown, &socket->cancel);

	uv_tcp_init(&socket->scheduler->loop, &socket->handle);

	socket->stream = async_stream_init((uv_stream_t *) &socket->handle, 0);

	return socket;
}

static void async_tcp_socket_object_dtor(zend_object *object)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) object;
	
	if (socket->cancel.func != NULL) {
		ASYNC_Q_DETACH(&socket->scheduler->shutdown, &socket->cancel);
		
		socket->cancel.func(socket, NULL);
	}
}

static void async_tcp_socket_object_destroy(zend_object *object)
{
	async_tcp_socket *socket;
	
	socket = (async_tcp_socket *) object;

#ifdef HAVE_ASYNC_SSL
	if (socket->stream->ssl.ssl != NULL) {
		async_ssl_dispose_engine(&socket->stream->ssl, (socket->server == NULL) ? 1 : 0);
	}

	if (socket->encryption != NULL) {
		ASYNC_DELREF(&socket->encryption->std);
	}
#endif

	if (socket->stream != NULL) {
		async_stream_free(socket->stream);
	}

	if (socket->server != NULL) {
		ASYNC_DELREF(&socket->server->std);
	}
	
	ASYNC_DELREF(&socket->scheduler->std);

	zval_ptr_dtor(&socket->read_error);
	zval_ptr_dtor(&socket->write_error);

	if (socket->name != NULL) {
		zend_string_release(socket->name);
	}
	
	if (socket->local_addr != NULL) {
		zend_string_release(socket->local_addr);
	}
	
	if (socket->remote_addr != NULL) {
		zend_string_release(socket->remote_addr);
	}

	zend_object_std_dtor(&socket->std);
}

static void connect_cb(uv_connect_t *req, int status)
{
	async_uv_op *op;

	op = (async_uv_op *) req->data;

	ZEND_ASSERT(op != NULL);
	
	op->code = status;
	
	ASYNC_FINISH_OP(op);
}

ZEND_METHOD(TcpSocket, connect)
{
	async_tcp_socket *socket;
	async_context *context;
	async_uv_op *op;
	
	zend_string *name;
	zend_long port;

	zval *tls;
	zval obj;

	uv_connect_t req;
	struct sockaddr_in dest;
	int code;

	tls = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
	    Z_PARAM_STR(name)
		Z_PARAM_LONG(port)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(tls)
	ZEND_PARSE_PARAMETERS_END();
	
	code = async_dns_lookup_ipv4(ZSTR_VAL(name), &dest, IPPROTO_TCP);
	
	ASYNC_CHECK_EXCEPTION(code < 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));

	dest.sin_port = htons(port);

	socket = async_tcp_socket_object_create();
	socket->name = zend_string_copy(name);

	code = uv_tcp_connect(&req, &socket->handle, (const struct sockaddr *) &dest, connect_cb);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to intialize socket connect operation: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
	
	req.data = op;
	
	context = async_context_get();
	
	if (!context->background && 1 == ++socket->stream->ref_count) {
		uv_ref((uv_handle_t *) &socket->handle);
	}
	
	code = async_await_op((async_op *) op);
	
	if (!context->background && 0 == --socket->stream->ref_count) {
		uv_unref((uv_handle_t *) &socket->handle);
	}
	
	if (code == FAILURE) {
		ASYNC_DELREF(&socket->std);
		ASYNC_FORWARD_OP_ERROR(op);
		ASYNC_FREE_OP(op);
		
		return;
	}
	
	code = op->code;
	
	ASYNC_FREE_OP(op);
	
	if (code < 0) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to connect socket: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		
		return;
	}

	if (tls != NULL && Z_TYPE_P(tls) != IS_NULL) {
#ifdef HAVE_ASYNC_SSL
		socket->encryption = async_clone_client_encryption((async_tls_client_encryption *) Z_OBJ_P(tls));
		socket->encryption->settings.mode = ASYNC_SSL_MODE_CLIENT;

		if (socket->encryption->settings.peer_name == NULL) {
			socket->encryption->settings.peer_name = zend_string_copy(socket->name);
		}
#else
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Socket encryption requires async extension to be compiled with SSL support");
		ASYNC_DELREF(&socket->std);
		return;
#endif
	}
	
	assemble_peer(&socket->handle, 0, &socket->local_addr, &socket->local_port);
	assemble_peer(&socket->handle, 1, &socket->remote_addr, &socket->remote_port);

	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TcpSocket, pair)
{
	async_tcp_socket *socket;

	zval sockets[2];
	int domain;
	int i;

	ZEND_PARSE_PARAMETERS_NONE();

	if (!USED_RET()) {
		return;
	}

#ifdef ZEND_WIN32
	SOCKET tmp[2];
	domain = AF_INET;
#else
	int tmp[2];
	domain = AF_UNIX;
#endif

	i = socketpair(domain, SOCK_STREAM, IPPROTO_IP, tmp);
	
	ASYNC_CHECK_EXCEPTION(i != 0, async_socket_exception_ce, "Failed to create socket pair");

	array_init_size(return_value, 2);

	for (i = 0; i < 2; i++) {
		socket = async_tcp_socket_object_create();

		uv_tcp_open(&socket->handle, (uv_os_sock_t) tmp[i]);

		ZVAL_OBJ(&sockets[i], &socket->std);

		zend_hash_index_update(Z_ARRVAL_P(return_value), i, &sockets[i]);
		
		socket->local_addr = zend_string_init("127.0.0.1", sizeof("127.0.0.1")-1, 0);
		socket->remote_addr = zend_string_init("127.0.0.1", sizeof("127.0.0.1")-1, 0);
	}
}

ZEND_METHOD(TcpSocket, close)
{
	async_tcp_socket *socket;

	zval error;
	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	if (socket->cancel.func == NULL) {
		return;
	}

	ASYNC_PREPARE_EXCEPTION(&error, async_stream_closed_exception_ce, "Socket has been closed");

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	ASYNC_Q_DETACH(&socket->scheduler->shutdown, &socket->cancel);
	
	socket->cancel.func(socket, &error);

	zval_ptr_dtor(&error);
}

ZEND_METHOD(TcpSocket, getAddress)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	RETURN_STR_COPY(socket->local_addr);
}

ZEND_METHOD(TcpSocket, getPort)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	RETURN_LONG(socket->local_port);
}

ZEND_METHOD(TcpSocket, getRemoteAddress)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	RETURN_STR_COPY(socket->remote_addr);
}

ZEND_METHOD(TcpSocket, getRemotePort)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	RETURN_LONG(socket->remote_port);
}

ZEND_METHOD(TcpSocket, setOption)
{
	async_tcp_socket *socket;

	zend_long option;
	zval *val;

	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_LONG(option)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	code = 0;

	switch ((int) option) {
	case ASYNC_SOCKET_TCP_NODELAY:
		code = uv_tcp_nodelay(&socket->handle, Z_LVAL_P(val) ? 1 : 0);
		break;
	case ASYNC_SOCKET_TCP_KEEPALIVE:
		code = uv_tcp_keepalive(&socket->handle, Z_LVAL_P(val) ? 1 : 0, (unsigned int) Z_LVAL_P(val));
		break;
	}

	RETURN_BOOL((code < 0) ? 0 : 1);
}

static inline void call_read(async_tcp_socket *socket, zval *return_value, zend_execute_data *execute_data)
{
	zend_string *str;
	zval *hint;
	size_t len;
	int code;
	
	hint = NULL;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(hint)
	ZEND_PARSE_PARAMETERS_END();
	
	if (hint == NULL || Z_TYPE_P(hint) == IS_NULL) {
		len = socket->stream->buffer.size;
	} else if (Z_LVAL_P(hint) < 1) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Invalid read length: %d", (int) Z_LVAL_P(hint));
		return;
	} else {
		len = (size_t) Z_LVAL_P(hint);
	}

	if (Z_TYPE_P(&socket->read_error) != IS_UNDEF) {
		Z_ADDREF_P(&socket->read_error);

		execute_data->opline--;
		zend_throw_exception_internal(&socket->read_error);
		execute_data->opline++;

		return;
	}

	code = async_stream_read_string(socket->stream, &str, len);
	
	if (code > 0) {
		RETURN_STR(str);
	}
}

ZEND_METHOD(TcpSocket, read)
{
	call_read((async_tcp_socket *) Z_OBJ_P(getThis()), return_value, execute_data);
}

ZEND_METHOD(TcpSocket, getReadableStream)
{
	async_tcp_socket *socket;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &async_tcp_socket_reader_object_create(socket)->std);

	RETURN_ZVAL(&obj, 1, 1);
}

static inline void call_write(async_tcp_socket *socket, zval *return_value, zend_execute_data *execute_data)
{
	zend_string *data;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(data)
	ZEND_PARSE_PARAMETERS_END();

	if (Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
		Z_ADDREF_P(&socket->write_error);

		execute_data->opline--;
		zend_throw_exception_internal(&socket->write_error);
		execute_data->opline++;

		return;
	}
	
	async_stream_write(socket->stream, ZSTR_VAL(data), ZSTR_LEN(data));
}

ZEND_METHOD(TcpSocket, write)
{
	call_write((async_tcp_socket *) Z_OBJ_P(getThis()), return_value, execute_data);
}

static void write_async_cb(void *arg)
{
	async_tcp_socket *socket;
	
	socket = (async_tcp_socket *) arg;
	
	ZEND_ASSERT(socket != NULL);
	
	ASYNC_DELREF(&socket->std);
}

ZEND_METHOD(TcpSocket, writeAsync)
{
	async_tcp_socket *socket;
	
	zend_string *data;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(data)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
		Z_ADDREF_P(&socket->write_error);

		execute_data->opline--;
		zend_throw_exception_internal(&socket->write_error);
		execute_data->opline++;

		return;
	}
	
	ASYNC_ADDREF(&socket->std);
	
	async_stream_async_write_string(socket->stream, data, write_async_cb, socket);
	
	if (EXPECTED(EG(exception) == NULL)) {		
		RETURN_LONG(socket->handle.write_queue_size);
	} else {
		ASYNC_DELREF(&socket->std);
	}
}

ZEND_METHOD(TcpSocket, getWriteQueueSize)
{
	async_tcp_socket *socket;
	
	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	RETURN_LONG((Z_TYPE_P(&socket->write_error) == IS_UNDEF) ? socket->handle.write_queue_size : 0);
}

ZEND_METHOD(TcpSocket, getWritableStream)
{
	async_tcp_socket *socket;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &async_tcp_socket_writer_object_create(socket)->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TcpSocket, encrypt)
{
#ifndef HAVE_ASYNC_SSL
	zend_throw_exception_ex(async_socket_exception_ce, 0, "Async extension was not compiled with SSL support");
#else

	async_tcp_socket *socket;
	async_ssl_handshake_data data;

	char name[256];
	int code;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (socket->server == NULL) {
		socket->stream->ssl.ctx = async_ssl_create_context();
		
		async_ssl_setup_verify_callback(socket->stream->ssl.ctx, &socket->encryption->settings);
	} else {
		ASYNC_CHECK_EXCEPTION(socket->server->encryption == NULL, async_socket_exception_ce, "No encryption settings have been passed to TcpServer::listen()");

		socket->stream->ssl.ctx = socket->server->ctx;
	}
	
	async_ssl_create_engine(&socket->stream->ssl);
	async_ssl_setup_encryption(socket->stream->ssl.ssl, &socket->encryption->settings);
	
	ZEND_SECURE_ZERO(&data, sizeof(async_ssl_handshake_data));

	if (socket->server == NULL) {
		if (socket->encryption != NULL && socket->encryption->settings.peer_name != NULL) {
			strcpy(name, ZSTR_VAL(socket->encryption->settings.peer_name));
		} else {
			strcpy(name, ZSTR_VAL(socket->name));
		}
		
		data.host = name;
		data.allow_self_signed = socket->encryption->settings.allow_self_signed;
	}
	
	uv_tcp_nodelay(&socket->handle, 1);
	
	code = async_stream_ssl_handshake(socket->stream, &data);

	uv_tcp_nodelay(&socket->handle, 0);
	
	if (code == FAILURE) {
		if (data.error != NULL) {
			zend_throw_exception_ex(async_socket_exception_ce, 0, "SSL handshake failed: %s", ZSTR_VAL(data.error));
			zend_string_release(data.error);
			return;
		}
	
		if (data.uv_error < 0) {
			zend_throw_exception_ex(async_socket_exception_ce, 0, "SSL handshake failed due to network error: %s", uv_strerror(data.uv_error));
			return;
		}
		
		if (data.ssl_error != SSL_ERROR_NONE) {
			zend_throw_exception_ex(async_socket_exception_ce, 0, "SSL handshake failed [%d]: %s", data.ssl_error, ERR_reason_error_string(data.ssl_error));
			return;
		}
	}
	
	if (data.error != NULL) {
		zend_string_release(data.error);
	}
	
#endif
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_connect, 0, 2, Concurrent\\Network\\TcpSocket, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
	ZEND_ARG_OBJ_INFO(0, tls, Concurrent\\Network\\TlsClientEncryption, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_pair, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_address, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_port, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_remote_address, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_remote_port, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_set_option, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_read, 0, 0, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_get_readable_stream, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_write_async, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_write_queue_size, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_get_writable_stream, 0, 0, Concurrent\\Stream\\WritableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_encrypt, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_socket_functions[] = {
	ZEND_ME(TcpSocket, connect, arginfo_tcp_socket_connect, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(TcpSocket, pair, arginfo_tcp_socket_pair, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(TcpSocket, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getAddress, arginfo_tcp_socket_get_address, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getPort, arginfo_tcp_socket_get_port, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, setOption, arginfo_tcp_socket_set_option, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getRemoteAddress, arginfo_tcp_socket_get_remote_address, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getRemotePort, arginfo_tcp_socket_get_remote_port, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, read, arginfo_tcp_socket_read, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getReadableStream, arginfo_tcp_socket_get_readable_stream, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, writeAsync, arginfo_tcp_socket_write_async, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getWriteQueueSize, arginfo_tcp_socket_get_write_queue_size, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, getWritableStream, arginfo_tcp_socket_get_writable_stream, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, encrypt, arginfo_tcp_socket_encrypt, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_tcp_socket_reader *async_tcp_socket_reader_object_create(async_tcp_socket *socket)
{
	async_tcp_socket_reader *reader;

	reader = emalloc(sizeof(async_tcp_socket_reader));
	ZEND_SECURE_ZERO(reader, sizeof(async_tcp_socket_reader));

	zend_object_std_init(&reader->std, async_tcp_socket_reader_ce);
	reader->std.handlers = &async_tcp_socket_reader_handlers;

	reader->socket = socket;

	ASYNC_ADDREF(&socket->std);

	return reader;
}

static void async_tcp_socket_reader_object_destroy(zend_object *object)
{
	async_tcp_socket_reader *reader;

	reader = (async_tcp_socket_reader *) object;

	ASYNC_DELREF(&reader->socket->std);

	zend_object_std_dtor(&reader->std);
}

ZEND_METHOD(TcpSocketReader, close)
{
	async_tcp_socket_reader *reader;
	async_tcp_socket *socket;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	reader = (async_tcp_socket_reader *) Z_OBJ_P(getThis());
	socket = reader->socket;

	if (Z_TYPE_P(&socket->read_error) != IS_UNDEF) {
		return;
	}

	ASYNC_PREPARE_EXCEPTION(&socket->read_error, async_stream_closed_exception_ce, "Socket has been closed");

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&socket->read_error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}
	
	async_stream_shutdown(socket->stream, ASYNC_STREAM_SHUT_RD);
}

ZEND_METHOD(TcpSocketReader, read)
{
	call_read(((async_tcp_socket_reader *) Z_OBJ_P(getThis()))->socket, return_value, execute_data);
}

static const zend_function_entry async_tcp_socket_reader_functions[] = {
	ZEND_ME(TcpSocketReader, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocketReader, read, arginfo_tcp_socket_read, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_tcp_socket_writer *async_tcp_socket_writer_object_create(async_tcp_socket *socket)
{
	async_tcp_socket_writer *writer;

	writer = emalloc(sizeof(async_tcp_socket_writer));
	ZEND_SECURE_ZERO(writer, sizeof(async_tcp_socket_writer));

	zend_object_std_init(&writer->std, async_tcp_socket_writer_ce);
	writer->std.handlers = &async_tcp_socket_writer_handlers;

	writer->socket = socket;

	ASYNC_ADDREF(&socket->std);

	return writer;
}

static void async_tcp_socket_writer_object_destroy(zend_object *object)
{
	async_tcp_socket_writer *writer;

	writer = (async_tcp_socket_writer *) object;

	ASYNC_DELREF(&writer->socket->std);

	zend_object_std_dtor(&writer->std);
}

ZEND_METHOD(TcpSocketWriter, close)
{
	async_tcp_socket_writer *writer;
	async_tcp_socket *socket;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	writer = (async_tcp_socket_writer *) Z_OBJ_P(getThis());
	socket = writer->socket;

	if (Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
		return;
	}

	ASYNC_PREPARE_EXCEPTION(&socket->write_error, async_stream_closed_exception_ce, "Socket has been closed");

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&socket->write_error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}
	
	async_stream_shutdown(socket->stream, ASYNC_STREAM_SHUT_WR);
}

ZEND_METHOD(TcpSocketWriter, write)
{
	call_write(((async_tcp_socket_writer *) Z_OBJ_P(getThis()))->socket, return_value, execute_data);
}

static const zend_function_entry async_tcp_socket_writer_functions[] = {
	ZEND_ME(TcpSocketWriter, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocketWriter, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static void server_disposed(uv_handle_t *handle)
{
	async_tcp_server *server;

	server = (async_tcp_server *) handle->data;
	
	ZEND_ASSERT(server != NULL);

	ASYNC_DELREF(&server->std);
}

static void shutdown_server(void *obj, zval *error)
{
	async_tcp_server *server;
	
	server = (async_tcp_server *) obj;
	
	ZEND_ASSERT(server != NULL);
	
	server->cancel.func = NULL;
	
	if (error != NULL && Z_TYPE_P(&server->error) == IS_UNDEF) {
		ZVAL_COPY(&server->error, error);
	}
	
	if (!uv_is_closing((uv_handle_t *) &server->handle)) {
		ASYNC_ADDREF(&server->std);

		uv_close((uv_handle_t *) &server->handle, server_disposed);
	}
}

static async_tcp_server *async_tcp_server_object_create()
{
	async_tcp_server *server;

	server = emalloc(sizeof(async_tcp_server));
	ZEND_SECURE_ZERO(server, sizeof(async_tcp_server));

	zend_object_std_init(&server->std, async_tcp_server_ce);
	server->std.handlers = &async_tcp_server_handlers;
	
	server->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&server->scheduler->std);
	
	server->cancel.object = server;
	server->cancel.func = shutdown_server;
	
	ASYNC_Q_ENQUEUE(&server->scheduler->shutdown, &server->cancel);

	uv_tcp_init(&server->scheduler->loop, &server->handle);

	server->handle.data = server;

	return server;
}

static void async_tcp_server_object_dtor(zend_object *object)
{
	async_tcp_server *server;

	server = (async_tcp_server *) object;

	if (server->cancel.func != NULL) {
		ASYNC_Q_DETACH(&server->scheduler->shutdown, &server->cancel);
	
		server->cancel.func(server, NULL);
	}
}

static void async_tcp_server_object_destroy(zend_object *object)
{
	async_tcp_server *server;

	server = (async_tcp_server *) object;

#ifdef HAVE_ASYNC_SSL
	if (server->ctx != NULL) {
		SSL_CTX_free(server->ctx);
	}

	if (server->encryption != NULL) {
		ASYNC_DELREF(&server->encryption->std);
	}
#endif

	ASYNC_DELREF(&server->scheduler->std);
	
	zval_ptr_dtor(&server->error);
	
	if (server->name != NULL) {
		zend_string_release(server->name);
	}
	
	if (server->addr != NULL) {
		zend_string_release(server->addr);
	}

	zend_object_std_dtor(&server->std);
}

static void server_connected(uv_stream_t *stream, int status)
{
	async_tcp_server *server;
	async_uv_op *op;

	server = (async_tcp_server *) stream->data;
	
	ZEND_ASSERT(server != NULL);
	
	if (server->accepts.first == NULL) {
		server->pending++;
	} else {
		ASYNC_DEQUEUE_CUSTOM_OP(&server->accepts, op, async_uv_op);
		
		op->code = status;
				
		ASYNC_FINISH_OP(op);
	}
}

ZEND_METHOD(TcpServer, listen)
{
	async_tcp_server *server;

	zend_string *name;
	zend_long port;

	zval *tls;
	zval obj;

	struct sockaddr_in bind;
	int code;

	tls = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_STR(name)
		Z_PARAM_LONG(port)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(tls)
	ZEND_PARSE_PARAMETERS_END();

	code = async_dns_lookup_ipv4(ZSTR_VAL(name), &bind, IPPROTO_TCP);
	
	ASYNC_CHECK_EXCEPTION(code < 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));
	
	bind.sin_port = htons(port);

	server = async_tcp_server_object_create();
	server->name = zend_string_copy(name);
	server->port = (uint16_t) port;

	code = uv_tcp_bind(&server->handle, (const struct sockaddr *) &bind, 0);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to bind server: %s", uv_strerror(code));
		ASYNC_DELREF(&server->std);
		return;
	}

	code = uv_listen((uv_stream_t *) &server->handle, 128, server_connected);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Server failed to listen: %s", uv_strerror(code));
		ASYNC_DELREF(&server->std);
		return;
	}

	uv_unref((uv_handle_t *) &server->handle);

	if (tls != NULL && Z_TYPE_P(tls) != IS_NULL) {
#ifdef HAVE_ASYNC_SSL
		server->encryption = (async_tls_server_encryption *) Z_OBJ_P(tls);

		ASYNC_ADDREF(&server->encryption->std);

		server->ctx = async_ssl_create_context();

		SSL_CTX_set_default_passwd_cb_userdata(server->ctx, &server->encryption->cert);

		SSL_CTX_use_certificate_file(server->ctx, ZSTR_VAL(server->encryption->cert.file), SSL_FILETYPE_PEM);
		SSL_CTX_use_PrivateKey_file(server->ctx, ZSTR_VAL(server->encryption->cert.key), SSL_FILETYPE_PEM);

		async_ssl_setup_sni(server->ctx, server->encryption);
#else
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Server encryption requires async extension to be compiled with SSL support");
		ASYNC_DELREF(&server->std);
		return;
#endif
	}
	
	assemble_peer(&server->handle, 0, &server->addr, &server->port);

	ZVAL_OBJ(&obj, &server->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TcpServer, close)
{
	async_tcp_server *server;

	zval error;
	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	if (server->cancel.func == NULL) {
		return;
	}

	ASYNC_PREPARE_EXCEPTION(&error, async_socket_exception_ce, "Server has been closed");

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}
	
	ASYNC_Q_DETACH(&server->scheduler->shutdown, &server->cancel);
	
	server->cancel.func(server, &error);

	zval_ptr_dtor(&error);
}

ZEND_METHOD(TcpServer, getAddress)
{
	async_tcp_server *server;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	RETURN_STR_COPY(server->name);
}

ZEND_METHOD(TcpServer, getPort)
{
	async_tcp_server *server;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());
	
	RETURN_LONG(server->port);
}

ZEND_METHOD(TcpServer, setOption)
{
	async_tcp_server *server;

	zend_long option;
	zval *val;

	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_LONG(option)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	server = (async_tcp_server *) Z_OBJ_P(getThis());
	code = 0;

	switch ((int) option) {
	case ASYNC_SOCKET_TCP_SIMULTANEOUS_ACCEPTS:
		code = uv_tcp_simultaneous_accepts(&server->handle, Z_LVAL_P(val) ? 1 : 0);
		break;
	}

	RETURN_BOOL((code < 0) ? 0 : 1);
}

ZEND_METHOD(TcpServer, accept)
{
	async_tcp_server *server;
	async_tcp_socket *socket;
	async_context *context;
	async_uv_op *op;

	zval obj;
	int code;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	if (server->pending == 0) {
		if (Z_TYPE_P(&server->error) != IS_UNDEF) {
			Z_ADDREF_P(&server->error);

			execute_data->opline--;
			zend_throw_exception_internal(&server->error);
			execute_data->opline++;

			return;
		}
		
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
		ASYNC_ENQUEUE_OP(&server->accepts, op);
		
		context = async_context_get();
		
		ASYNC_UNREF_ENTER(context, server);
		code = async_await_op((async_op *) op);
		ASYNC_UNREF_EXIT(context, server);
		
		if (code == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
			ASYNC_FREE_OP(op);
			
			return;
		}
		
		code = op->code;
		
		ASYNC_FREE_OP(op);
	} else {
		server->pending--;
	}

	socket = async_tcp_socket_object_create();

	code = uv_accept((uv_stream_t *) &server->handle, (uv_stream_t *) &socket->handle);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to accept socket connection: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}

	socket->server = server;
	
	assemble_peer(&socket->handle, 0, &socket->local_addr, &socket->local_port);
	assemble_peer(&socket->handle, 1, &socket->remote_addr, &socket->remote_port);

	ASYNC_ADDREF(&server->std);

	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_listen, 0, 2, Concurrent\\Network\\TcpServer, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
	ZEND_ARG_OBJ_INFO(0, tls, Concurrent\\Network\\TlsServerEncryption, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_get_address, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_get_port, 0, 0, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_set_option, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_accept, 0, 0, Concurrent\\Network\\SocketStream, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_server_functions[] = {
	ZEND_ME(TcpServer, listen, arginfo_tcp_server_listen, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(TcpServer, close, arginfo_tcp_server_close, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpServer, getAddress, arginfo_tcp_server_get_address, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpServer, getPort, arginfo_tcp_server_get_port, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpServer, setOption, arginfo_tcp_server_set_option, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpServer, accept, arginfo_tcp_server_accept, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_tcp_ce_register()
{
	zend_class_entry ce;
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\TcpSocket", async_tcp_socket_functions);
	async_tcp_socket_ce = zend_register_internal_class(&ce);
	async_tcp_socket_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tcp_socket_ce->serialize = zend_class_serialize_deny;
	async_tcp_socket_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(async_tcp_socket_ce, 1, async_socket_stream_ce);

	memcpy(&async_tcp_socket_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_socket_handlers.dtor_obj = async_tcp_socket_object_dtor;
	async_tcp_socket_handlers.free_obj = async_tcp_socket_object_destroy;
	async_tcp_socket_handlers.clone_obj = NULL;

	ASYNC_TCP_SOCKET_CONST("NODELAY", ASYNC_SOCKET_TCP_NODELAY);
	ASYNC_TCP_SOCKET_CONST("KEEPALIVE", ASYNC_SOCKET_TCP_KEEPALIVE);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\TcpSocketReader", async_tcp_socket_reader_functions);
	async_tcp_socket_reader_ce = zend_register_internal_class(&ce);
	async_tcp_socket_reader_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tcp_socket_reader_ce->serialize = zend_class_serialize_deny;
	async_tcp_socket_reader_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(async_tcp_socket_reader_ce, 1, async_readable_stream_ce);

	memcpy(&async_tcp_socket_reader_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_socket_reader_handlers.free_obj = async_tcp_socket_reader_object_destroy;
	async_tcp_socket_reader_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\TcpSocketWriter", async_tcp_socket_writer_functions);
	async_tcp_socket_writer_ce = zend_register_internal_class(&ce);
	async_tcp_socket_writer_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tcp_socket_writer_ce->serialize = zend_class_serialize_deny;
	async_tcp_socket_writer_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(async_tcp_socket_writer_ce, 1, async_writable_stream_ce);

	memcpy(&async_tcp_socket_writer_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_socket_writer_handlers.free_obj = async_tcp_socket_writer_object_destroy;
	async_tcp_socket_writer_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\TcpServer", async_tcp_server_functions);
	async_tcp_server_ce = zend_register_internal_class(&ce);
	async_tcp_server_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tcp_server_ce->serialize = zend_class_serialize_deny;
	async_tcp_server_ce->unserialize = zend_class_unserialize_deny;
	
	zend_class_implements(async_tcp_server_ce, 1, async_server_ce);

	memcpy(&async_tcp_server_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_server_handlers.dtor_obj = async_tcp_server_object_dtor;
	async_tcp_server_handlers.free_obj = async_tcp_server_object_destroy;
	async_tcp_server_handlers.clone_obj = NULL;

	ASYNC_TCP_SERVER_CONST("SIMULTANEOUS_ACCEPTS", ASYNC_SOCKET_TCP_SIMULTANEOUS_ACCEPTS);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
