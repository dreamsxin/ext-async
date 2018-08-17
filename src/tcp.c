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

#include "async_task.h"
#include "zend_inheritance.h"

#ifdef ZEND_WIN32
#include "win32/sockets.h"
#endif

zend_class_entry *async_tcp_socket_ce;
zend_class_entry *async_tcp_socket_reader_ce;
zend_class_entry *async_tcp_socket_writer_ce;
zend_class_entry *async_tcp_server_ce;

static zend_object_handlers async_tcp_socket_handlers;
static zend_object_handlers async_tcp_socket_reader_handlers;
static zend_object_handlers async_tcp_socket_writer_handlers;
static zend_object_handlers async_tcp_server_handlers;

static async_tcp_socket *async_tcp_socket_object_create();
static async_tcp_socket_reader *async_tcp_socket_reader_object_create(async_tcp_socket *socket);
static async_tcp_socket_writer *async_tcp_socket_writer_object_create(async_tcp_socket *socket);


static void socket_disposed(uv_handle_t *handle)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) handle->data;

	if (socket->buffer.base != NULL) {
		efree(socket->buffer.base);
	}

	OBJ_RELEASE(&socket->std);
}

static void socket_connected(uv_connect_t *connect, int status)
{
	async_tcp_socket *socket;

	zval val;

	socket = (async_tcp_socket *) connect->handle->data;

	ZEND_ASSERT(socket != NULL);

	if (status == 0) {
		ZVAL_NULL(&val);

		async_awaitable_trigger_continuation(&socket->reads, &val, 1);

		return;
	}

	zend_throw_error(NULL, "Failed to connect socket: %s", uv_strerror(status));

	ZVAL_OBJ(&val, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&socket->reads, &val, 0);
}

static void socket_shutdown(uv_shutdown_t *shutdown, int status)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) shutdown->handle->data;

	efree(shutdown);

	OBJ_RELEASE(&socket->std);
}

static void socket_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) handle->data;

	socket->buffer.current = socket->buffer.base;
	socket->buffer.len = 0;

	buffer->base = socket->buffer.base;
	buffer->len = socket->buffer.size;
}

static void socket_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
	async_tcp_socket *socket;

	zval data;

	socket = (async_tcp_socket *) stream->data;

	if (nread == 0) {
		return;
	}

	uv_read_stop(stream);

	if (nread > 0) {
		socket->buffer.len = (size_t) nread;

		ZVAL_NULL(&data);

		async_awaitable_trigger_next_continuation(&socket->reads, &data, 1);

		return;
	}

	socket->buffer.len = 0;

	if (nread == UV_EOF) {
		socket->eof = 1;

		ZVAL_NULL(&data);

		async_awaitable_trigger_continuation(&socket->reads, &data, 1);

		if (Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
			GC_ADDREF(&socket->std);

			uv_close((uv_handle_t *) stream, socket_disposed);
		}

		return;
	}

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Socket read error: %s", uv_strerror((int) nread));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&socket->reads, &data, 0);
}

static void socket_write(uv_write_t *write, int status)
{
	async_tcp_socket *socket;

	zval data;

	socket = (async_tcp_socket *) write->handle->data;

	if (status == 0) {
		ZVAL_NULL(&data);

		async_awaitable_trigger_next_continuation(&socket->writes, &data, 1);

		return;
	}

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Socket write error: %s", uv_strerror(status));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_next_continuation(&socket->writes, &data, 0);
}

static void server_disposed(uv_handle_t *handle)
{
	async_tcp_server *server;

	server = (async_tcp_server *) handle->data;

	OBJ_RELEASE(&server->std);
}

static void server_connected(uv_stream_t *stream, int status)
{
	async_tcp_server *server;

	zval data;

	server = (async_tcp_server *) stream->data;

	if (server->accepts.first == NULL) {
		server->pending++;
	} else {
		ZVAL_NULL(&data);

		async_awaitable_trigger_next_continuation(&server->accepts, &data, 1);
	}
}


static async_tcp_socket *async_tcp_socket_object_create()
{
	async_tcp_socket *socket;

	socket = emalloc(sizeof(async_tcp_socket));
	ZEND_SECURE_ZERO(socket, sizeof(async_tcp_socket));

	zend_object_std_init(&socket->std, async_tcp_socket_ce);
	socket->std.handlers = &async_tcp_socket_handlers;

	uv_tcp_init(async_task_scheduler_get_loop(), &socket->handle);

	socket->handle.data = socket;

	socket->buffer.size = 0x8000;
	socket->buffer.base = emalloc(sizeof(char) * socket->buffer.size);

	ZVAL_UNDEF(&socket->read_error);
	ZVAL_UNDEF(&socket->write_error);

	return socket;
}

static void async_tcp_socket_object_dtor(zend_object *object)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) object;

	if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
		GC_ADDREF(&socket->std);

		uv_close((uv_handle_t *) &socket->handle, socket_disposed);
	}
}

static void async_tcp_socket_object_destroy(zend_object *object)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) object;

	if (socket->buffer.base != NULL) {
		efree(socket->buffer.base);
	}

	zval_ptr_dtor(&socket->read_error);
	zval_ptr_dtor(&socket->write_error);

	zend_object_std_dtor(&socket->std);
}

ZEND_METHOD(Socket, connect)
{
	async_tcp_socket *socket;

	char *name;
	size_t len;
	zend_long port;

	zval ip;
	zval tmp;
	zval obj;

	uv_connect_t connect;
	struct sockaddr_in dest;
	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
	    Z_PARAM_STRING(name, len)
		Z_PARAM_LONG(port)
	ZEND_PARSE_PARAMETERS_END();

	async_gethostbyname(name, &ip, execute_data);

	if (UNEXPECTED(EG(exception))) {
		return;
	}

	socket = async_tcp_socket_object_create();

	code = uv_ip4_addr(Z_STRVAL_P(&ip), (int) port, &dest);

	zval_ptr_dtor(&ip);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to assemble IP address: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}

	code = uv_tcp_connect(&connect, &socket->handle, (const struct sockaddr *) &dest, socket_connected);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to intialize socket connect operation: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}

	async_task_suspend(&socket->reads, &tmp, execute_data, NULL);

	if (UNEXPECTED(EG(exception))) {
		OBJ_RELEASE(&socket->std);
		return;
	}

	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Socket, pair)
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

	if (0 != socketpair(domain, SOCK_STREAM, IPPROTO_IP, tmp)) {
		zend_throw_error(NULL, "Failed to create socket pair");
		return;
	}

	array_init_size(return_value, 2);

	for (i = 0; i < 2; i++) {
		socket = async_tcp_socket_object_create();

		uv_tcp_open(&socket->handle, (uv_os_sock_t) tmp[i]);

		ZVAL_OBJ(&sockets[i], &socket->std);

		zend_hash_index_update(Z_ARRVAL_P(return_value), i, &sockets[i]);
	}
}

ZEND_METHOD(Socket, close)
{
	async_tcp_socket *socket;

	zval *val;
	zval ex;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&socket->read_error) != IS_UNDEF || Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
		return;
	}

	zend_throw_exception(async_stream_closed_exception_ce, "Socket has been closed", 0);

	ZVAL_OBJ(&ex, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&ex), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	if (Z_TYPE_P(&socket->read_error) != IS_UNDEF) {
		ZVAL_COPY(&socket->read_error, &ex);

		socket->eof = 1;

		async_awaitable_trigger_continuation(&socket->reads, &socket->read_error, 0);
	}

	if (Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
		ZVAL_COPY(&socket->write_error, &ex);

		async_awaitable_trigger_continuation(&socket->writes, &socket->write_error, 0);
	}

	zval_ptr_dtor(&ex);

	if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
		GC_ADDREF(&socket->std);

		uv_close((uv_handle_t *) &socket->handle, socket_disposed);
	}
}

ZEND_METHOD(Socket, nodelay)
{
	async_tcp_socket *socket;

	zend_bool nodelay;
	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_BOOL(nodelay)
	ZEND_PARSE_PARAMETERS_END();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	code = uv_tcp_nodelay(&socket->handle, nodelay ? 1 : 0);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to set TCP nodelay: %s", uv_strerror(code));
	}
}

static void assemble_peer(uv_tcp_t *tcp, zend_bool remote, zval *return_value, zend_execute_data *execute_data)
{
	struct sockaddr_storage addr;

	char name[64];
	int port;

	int len;
	int code;

	zval tmp;

	len = sizeof(struct sockaddr_storage);

	if (remote) {
		code = uv_tcp_getpeername(tcp, (struct sockaddr *) &addr, &len);
	} else {
		code = uv_tcp_getsockname(tcp, (struct sockaddr *) &addr, &len);
	}

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

	array_init_size(return_value, 2);
	ZVAL_STRING(&tmp, name);
	zend_hash_index_update(Z_ARRVAL_P(return_value), 0, &tmp);

	ZVAL_LONG(&tmp, port);
	zend_hash_index_update(Z_ARRVAL_P(return_value), 1, &tmp);
}

ZEND_METHOD(Socket, getLocalPeer)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		assemble_peer(&socket->handle, 0, return_value, execute_data);
	}
}

ZEND_METHOD(Socket, getRemotePeer)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		assemble_peer(&socket->handle, 1, return_value, execute_data);
	}
}

static inline void socket_call_read(async_tcp_socket *socket, zval *return_value, zend_execute_data *execute_data)
{
	zend_bool cancelled;

	zval *hint;
	zval chunk;
	size_t len;

	int code;

	hint = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(hint)
	ZEND_PARSE_PARAMETERS_END();

	if (hint == NULL) {
		len = socket->buffer.size;
	} else if (Z_LVAL_P(hint) < 1) {
		zend_throw_error(NULL, "Invalid read length: %d", (int) Z_LVAL_P(hint));
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

	if (socket->reads.first != NULL) {
		zend_throw_exception(async_pending_read_exception_ce, "Cannot read from socket while another read is pending", 0);
		return;
	}

	if (socket->eof) {
		return;
	}

	if (socket->buffer.len == 0) {
		code = uv_read_start((uv_stream_t *) &socket->handle, socket_read_alloc, socket_read);

		if (UNEXPECTED(code != 0)) {
			zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to start socket read: %s", uv_strerror(code));
			return;
		}

		async_task_suspend(&socket->reads, return_value, execute_data, &cancelled);

		if (cancelled) {
			uv_read_stop((uv_stream_t *) &socket->handle);
			return;
		}

		if (socket->eof || UNEXPECTED(EG(exception))) {
			return;
		}
	}

	len = MIN(len, socket->buffer.len);

	ZVAL_STRINGL(&chunk, socket->buffer.current, len);

	socket->buffer.current += len;
	socket->buffer.len -= len;

	RETURN_ZVAL(&chunk, 1, 1);
}

ZEND_METHOD(Socket, read)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	socket_call_read(socket, return_value, execute_data);
}

ZEND_METHOD(Socket, readStream)
{
	async_tcp_socket *socket;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &async_tcp_socket_reader_object_create(socket)->std);

	RETURN_ZVAL(&obj, 1, 1);
}

static inline void socket_call_write(async_tcp_socket *socket, zval *return_value, zend_execute_data *execute_data)
{
	zend_string *data;
	uv_write_t write;
	uv_buf_t buffer[1];

	int result;

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

	buffer[0].base = ZSTR_VAL(data);
	buffer[0].len = ZSTR_LEN(data);

	// Attempt a non-blocking write first before queueing up writes.
	if (socket->writes.first == NULL) {
		do {
			result = uv_try_write((uv_stream_t *) &socket->handle, buffer, 1);

			if (result == UV_EAGAIN) {
				break;
			} else if (result < 0) {
				zend_throw_exception_ex(async_stream_exception_ce, 0, "Socket write error: %s", uv_strerror(result));

				return;
			}

			if (result == buffer[0].len) {
				return;
			}

			buffer[0].base += result;
			buffer[0].len -= result;
		} while (1);
	}

	result = uv_write(&write, (uv_stream_t *) &socket->handle, buffer, 1, socket_write);

	if (UNEXPECTED(result != 0)) {
		zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to queue socket write: %s", uv_strerror(result));
		return;
	}

	async_task_suspend(&socket->writes, return_value, execute_data, NULL);
}

ZEND_METHOD(Socket, write)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	socket_call_write(socket, return_value, execute_data);
}

ZEND_METHOD(Socket, writeStream)
{
	async_tcp_socket *socket;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &async_tcp_socket_writer_object_create(socket)->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_connect, 0, 2, Concurrent\\Network\\TcpSocket, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_pair, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_nodelay, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, enable, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_local_peer, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_get_remote_peer, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_read, 0, 0, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_read_stream, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_write_stream, 0, 0, Concurrent\\Stream\\WritableStream, 0)
ZEND_END_ARG_INFO()


static const zend_function_entry async_tcp_socket_functions[] = {
	ZEND_ME(Socket, connect, arginfo_tcp_socket_connect, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Socket, pair, arginfo_tcp_socket_pair, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Socket, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, nodelay, arginfo_tcp_socket_nodelay, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, getLocalPeer, arginfo_tcp_socket_get_local_peer, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, getRemotePeer, arginfo_tcp_socket_get_remote_peer, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, read, arginfo_tcp_socket_read, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, readStream, arginfo_tcp_socket_read_stream, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, writeStream, arginfo_tcp_socket_write_stream, ZEND_ACC_PUBLIC)
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

	GC_ADDREF(&socket->std);

	return reader;
}

static void async_tcp_socket_reader_object_destroy(zend_object *object)
{
	async_tcp_socket_reader *reader;

	reader = (async_tcp_socket_reader *) object;

	OBJ_RELEASE(&reader->socket->std);

	zend_object_std_dtor(&reader->std);
}

ZEND_METHOD(SocketReader, close)
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

	zend_throw_exception(async_stream_closed_exception_ce, "Socket has been closed", 0);

	ZVAL_OBJ(&socket->read_error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&socket->read_error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	socket->eof = 1;

	async_awaitable_trigger_continuation(&socket->reads, &socket->read_error, 0);
}

ZEND_METHOD(SocketReader, read)
{
	async_tcp_socket_reader *reader;

	reader = (async_tcp_socket_reader *) Z_OBJ_P(getThis());

	socket_call_read(reader->socket, return_value, execute_data);
}

static const zend_function_entry async_tcp_socket_reader_functions[] = {
	ZEND_ME(SocketReader, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(SocketReader, read, arginfo_tcp_socket_read, ZEND_ACC_PUBLIC)
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

	GC_ADDREF(&socket->std);

	return writer;
}

static void async_tcp_socket_writer_object_destroy(zend_object *object)
{
	async_tcp_socket_writer *writer;

	writer = (async_tcp_socket_writer *) object;

	OBJ_RELEASE(&writer->socket->std);

	zend_object_std_dtor(&writer->std);
}

ZEND_METHOD(SocketWriter, close)
{
	async_tcp_socket_writer *writer;
	async_tcp_socket *socket;

	uv_shutdown_t *shutdown;

	int code;

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

	zend_throw_exception(async_stream_closed_exception_ce, "Socket has been closed", 0);

	ZVAL_OBJ(&socket->write_error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&socket->write_error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&socket->writes, &socket->write_error, 0);

	shutdown = emalloc(sizeof(uv_shutdown_t));
	ZEND_SECURE_ZERO(shutdown, sizeof(uv_shutdown_t));

	GC_ADDREF(&socket->std);

	code = uv_shutdown(shutdown, (uv_stream_t *) &socket->handle, socket_shutdown);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to initialize socket shutdown: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
	}
}

ZEND_METHOD(SocketWriter, write)
{
	async_tcp_socket_writer *writer;

	writer = (async_tcp_socket_writer *) Z_OBJ_P(getThis());

	socket_call_write(writer->socket, return_value, execute_data);
}

static const zend_function_entry async_tcp_socket_writer_functions[] = {
	ZEND_ME(SocketWriter, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(SocketWriter, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_tcp_server *async_tcp_server_object_create()
{
	async_tcp_server *server;

	server = emalloc(sizeof(async_tcp_server));
	ZEND_SECURE_ZERO(server, sizeof(async_tcp_server));

	zend_object_std_init(&server->std, async_tcp_server_ce);
	server->std.handlers = &async_tcp_server_handlers;

	uv_tcp_init(async_task_scheduler_get_loop(), &server->handle);

	server->handle.data = server;

	ZVAL_UNDEF(&server->error);

	return server;
}

static void async_tcp_server_object_dtor(zend_object *object)
{
	async_tcp_server *server;

	server = (async_tcp_server *) object;

	uv_ref((uv_handle_t *) &server->handle);

	if (!uv_is_closing((uv_handle_t *) &server->handle)) {
		GC_ADDREF(&server->std);

		uv_close((uv_handle_t *) &server->handle, server_disposed);
	}
}

static void async_tcp_server_object_destroy(zend_object *object)
{
	async_tcp_server *server;

	server = (async_tcp_server *) object;

	zval_ptr_dtor(&server->error);

	zend_object_std_dtor(&server->std);
}

ZEND_METHOD(Server, listen)
{
	async_tcp_server *server;

	char *name;
	size_t len;
	zend_long port;

	zval ip;
	zval obj;

	struct sockaddr_in bind;
	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_STRING(name, len)
		Z_PARAM_LONG(port)
	ZEND_PARSE_PARAMETERS_END();

	async_gethostbyname(name, &ip, execute_data);

	code = uv_ip4_addr(Z_STRVAL_P(&ip), (int) port, &bind);

	if (UNEXPECTED(EG(exception))) {
		return;
	}

	zval_ptr_dtor(&ip);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to assemble IP address: %s", uv_strerror(code));
		return;
	}

	server = async_tcp_server_object_create();

	code = uv_tcp_bind(&server->handle, (const struct sockaddr *) &bind, 0);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to bind server: %s", uv_strerror(code));
		OBJ_RELEASE(&server->std);
		return;
	}

	code = uv_listen((uv_stream_t *) &server->handle, 128, server_connected);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Server failed to listen: %s", uv_strerror(code));
		OBJ_RELEASE(&server->std);
		return;
	}

	uv_unref((uv_handle_t *) &server->handle);

	ZVAL_OBJ(&obj, &server->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Server, close)
{
	async_tcp_server *server;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&server->error) != IS_UNDEF) {
		return;
	}

	zend_throw_error(NULL, "Server has been closed");

	ZVAL_OBJ(&server->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&server->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&server->accepts, &server->error, 0);

	if (!uv_is_closing((uv_handle_t *) &server->handle)) {
		GC_ADDREF(&server->std);

		uv_close((uv_handle_t *) &server->handle, server_disposed);
	}
}

ZEND_METHOD(Server, accept)
{
	async_tcp_server *server;
	async_tcp_socket *socket;

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

		uv_ref((uv_handle_t *) &server->handle);

		async_task_suspend(&server->accepts, return_value, execute_data, NULL);

		if (server->accepts.first == NULL) {
			uv_unref((uv_handle_t *) &server->handle);
		}
	} else {
		server->pending--;
	}

	socket = async_tcp_socket_object_create();

	code = uv_accept((uv_stream_t *) &server->handle, (uv_stream_t *) &socket->handle);

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to accept socket connection: %s", uv_strerror(code));
		OBJ_RELEASE(&socket->std);
		return;
	}

	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_listen, 0, 2, Concurrent\\Network\\TcpServer, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_accept, 0, 0, Concurrent\\Network\\TcpSocket, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_server_functions[] = {
	ZEND_ME(Server, listen, arginfo_tcp_server_listen, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Server, close, arginfo_tcp_server_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Server, accept, arginfo_tcp_server_accept, ZEND_ACC_PUBLIC)
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

	zend_class_implements(async_tcp_socket_ce, 1, async_duplex_stream_ce);

	memcpy(&async_tcp_socket_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_socket_handlers.dtor_obj = async_tcp_socket_object_dtor;
	async_tcp_socket_handlers.free_obj = async_tcp_socket_object_destroy;
	async_tcp_socket_handlers.clone_obj = NULL;

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

	memcpy(&async_tcp_server_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_server_handlers.dtor_obj = async_tcp_server_object_dtor;
	async_tcp_server_handlers.free_obj = async_tcp_server_object_destroy;
	async_tcp_server_handlers.clone_obj = NULL;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
