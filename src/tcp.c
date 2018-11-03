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

static void write_to_socket(async_tcp_socket *socket, uv_buf_t *buffer, int num, zend_execute_data *execute_data);
static void socket_disposed(uv_handle_t *handle);

#define ASYNC_TCP_SOCKET_CONST(name, value) \
	zend_declare_class_constant_long(async_tcp_socket_ce, name, sizeof(name)-1, (zend_long)value);

#define ASYNC_TCP_SERVER_CONST(name, value) \
	zend_declare_class_constant_long(async_tcp_server_ce, name, sizeof(name)-1, (zend_long)value);

typedef struct _async_tcp_write {
	uv_write_t request;
	async_tcp_socket *socket;
	zend_string *data;
	uv_buf_t *buffers;
	unsigned int nbufs;
	uv_buf_t *tmp;
} async_tcp_write;

#ifdef HAVE_ASYNC_SSL

#include "async_ssl.h"

typedef struct _socket_buffer socket_buffer;

struct _socket_buffer {
	uv_buf_t buf;
	char *base;
	socket_buffer *next;
};

static void ssl_send_handshake_bytes(async_tcp_socket *socket, zend_execute_data *execute_data)
{
	uv_buf_t buffer[1];
	char *base;
	size_t len;

	while ((len = BIO_ctrl_pending(socket->wbio)) > 0) {
		base = emalloc(len);
		
		buffer[0] = uv_buf_init(base, BIO_read(socket->wbio, base, len));

		write_to_socket(socket, buffer, 1, execute_data);

		efree(base);

		ASYNC_RETURN_ON_ERROR();
	}
}

static int ssl_feed_data(async_tcp_socket *socket)
{
	char *base;

	int len;
	int error;

	socket->buffer.current = socket->buffer.base;
	socket->buffer.len = 0;

	base = socket->buffer.base;

	while (SSL_is_init_finished(socket->ssl)) {
		len = SSL_read(socket->ssl, base, socket->buffer.size - socket->buffer.len);

		if (len < 1) {
			error = SSL_get_error(socket->ssl, len);

			return (error == SSL_ERROR_WANT_READ) ? 0 : error;
		}

		socket->buffer.len += len;
		base += len;
	}

	return 0;
}

#endif


static void assemble_peer(uv_tcp_t *tcp, zend_bool remote, zend_string **address, uint16_t *port)
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

static inline int socket_check_disposed(async_tcp_socket *socket)
{
	if (!socket->scheduler->disposed) {
		return 0;
	}
	
	if (Z_TYPE_P(&socket->read_error) == IS_UNDEF) {
		ZVAL_COPY(&socket->read_error, &socket->scheduler->error);
		
		uv_read_stop((uv_stream_t *) &socket->handle);
	}
	
	if (Z_TYPE_P(&socket->write_error) == IS_UNDEF) {
		ZVAL_COPY(&socket->write_error, &socket->scheduler->error);
	}
	
	if (!uv_is_closing((uv_handle_t *) &socket->handle)) {
		ASYNC_ADDREF(&socket->std);
		
		uv_close((uv_handle_t *) &socket->handle, socket_disposed);
	}
	
	return 1;
}

static uv_buf_t *assemble_write_buffers(async_tcp_socket *socket, zend_string *data, unsigned int *nbufs, zend_long *blen)
{
	uv_buf_t *buffers;

#ifdef HAVE_ASYNC_SSL
	if (socket->ssl != NULL) {
		socket_buffer *first;
		socket_buffer *last;
		socket_buffer *current;

		size_t len;
		size_t remaining;

		char *tmp;
		unsigned int num;
		zend_long bytes;
		int i;
		int w;

		remaining = ZSTR_LEN(data);
		tmp = ZSTR_VAL(data);

		num = 0;
		bytes = 0;

		first = NULL;
		last = NULL;

		while (remaining > 0) {
			w = SSL_write(socket->ssl, tmp, remaining);

			if (w < 1 && !async_ssl_error_continue(socket->ssl, w)) {
				num = 0;

				zend_throw_exception_ex(async_stream_exception_ce, 0, "SSL write operation failed [%d]: %s", w, ERR_reason_error_string(w));
				break;
			}

			tmp += w;
			remaining -= w;

			while ((len = BIO_ctrl_pending(socket->wbio)) > 0) {
				current = emalloc(sizeof(socket_buffer));
				current->next = NULL;

				if (first == NULL) {
					first = current;
				}

				if (last != NULL) {
					last->next = current;
				}

				last = current;

				current->base = emalloc(len);
				current->buf = uv_buf_init(current->base, BIO_read(socket->wbio, current->base, len));
				
				bytes += current->buf.len;

				num++;
			}
		}

		if (num) {
			buffers = ecalloc(num, sizeof(uv_buf_t));
			*blen = bytes;
		} else {
			buffers = NULL;
			*blen = 0;
		}
		
		*nbufs = num;

		current = first;
		i = 0;

		while (current != NULL) {
			if (num) {
				buffers[i++] = current->buf;
			} else {
				efree(current->base);
			}
			
			last = current;
			current = current->next;
			
			efree(last);
		}

		return buffers;
	}
#endif

	buffers = emalloc(sizeof(uv_buf_t));
	buffers[0] = uv_buf_init(ZSTR_VAL(data), ZSTR_LEN(data));

	*nbufs = 1;
	*blen = buffers[0].len;

	return buffers;
}

static void disassemble_write_buffers(async_tcp_socket *socket, uv_buf_t *buffers, unsigned int nbufs)
{	
#ifdef HAVE_ASYNC_SSL
	if (socket->ssl != NULL) {
		unsigned int i;
	
		for (i = 0; i < nbufs; i++) {
			efree(buffers[i].base);
		}
	}
#endif

	efree(buffers);
}


static void socket_disposed(uv_handle_t *handle)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) handle->data;

	if (socket->buffer.base != NULL) {
		efree(socket->buffer.base);
		socket->buffer.base = NULL;
	}

	ASYNC_DELREF(&socket->std);
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

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to connect socket: %s", uv_strerror(status));

	ZVAL_OBJ(&val, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&socket->reads, &val, 0);
}

static void socket_shutdown(uv_shutdown_t *shutdown, int status)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) shutdown->handle->data;

	efree(shutdown);

	ASYNC_DELREF(&socket->std);
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

	if (nread > 0) {
		socket->buffer.len = (size_t) nread;

#ifdef HAVE_ASYNC_SSL
		if (socket->ssl != NULL) {
			BIO_write(socket->rbio, socket->buffer.current, socket->buffer.len);

			ssl_feed_data(socket);

			if (SSL_is_init_finished(socket->ssl) && socket->buffer.len == 0) {
				return;
			}
		}
#endif

		uv_read_stop(stream);

		ZVAL_NULL(&data);

		async_awaitable_trigger_next_continuation(&socket->reads, &data, 1);

		return;
	}

	uv_read_stop(stream);

	socket->buffer.len = 0;

	if (nread == UV_EOF) {
		socket->eof = 1;

		ZVAL_NULL(&data);

		async_awaitable_trigger_continuation(&socket->reads, &data, 1);

		if (Z_TYPE_P(&socket->write_error) != IS_UNDEF) {
			ASYNC_ADDREF(&socket->std);

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

static void socket_write_async(uv_write_t *req, int status)
{
	async_tcp_write *write;
	
	write = (async_tcp_write *) req->data;
	
	if (write->data == NULL) {
		disassemble_write_buffers(write->socket, write->buffers, write->nbufs);
		
		if (write->tmp != NULL) {
			efree(write->tmp);
		}
	} else {
		zend_string_release(write->data);
	}
	
	ASYNC_DELREF(&write->socket->std);
	
	efree(write);
}

static void server_disposed(uv_handle_t *handle)
{
	async_tcp_server *server;

	server = (async_tcp_server *) handle->data;

	ASYNC_DELREF(&server->std);
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

static uv_buf_t *try_write_to_socket(async_tcp_socket *socket, uv_buf_t *buffer, int num, int *bnum)
{
	uv_buf_t *tmp;

	int i;
	int result;
	
	ZEND_ASSERT(socket->writes.first == NULL);

	tmp = ecalloc(num, sizeof(uv_buf_t));
	
	for (i = 0; i < num; i++) {
		tmp[i] = buffer[i];
	}
	
	buffer = tmp;

	for (i = 0; i < num; i++) {
		result = uv_try_write((uv_stream_t *) &socket->handle, buffer + i, num - i);

		if (result == UV_EAGAIN) {
			break;
		} else if (result < 0) {
			efree(tmp);
		
			zend_throw_exception_ex(async_stream_exception_ce, 0, "Socket write error: %s", uv_strerror(result));
			return NULL;
		}

		buffer[i].base += result;
		buffer[i].len -= result;

		if (buffer[i].len > 0) {
			i--;
		}
	}
	
	if (i == num) {
		efree(tmp);
		return NULL;
	}
	
	tmp += i;
	num -= i;
	
	*bnum = num;
	
	return tmp;
}

static void write_to_socket(async_tcp_socket *socket, uv_buf_t *buffer, int num, zend_execute_data *execute_data)
{
	async_context *context;
	zend_bool cancelled;

	uv_write_t write;

	int bnum;
	int result;
	
	if (socket->writes.first == NULL) {
		buffer = try_write_to_socket(socket, buffer, num, &bnum);
	} else {
		bnum = 0;
	}
	
	if (buffer == NULL) {
		return;
	}

	result = uv_write(&write, (uv_stream_t *) &socket->handle, buffer, bnum, socket_write);

	if (EXPECTED(result == 0)) {
		context = async_context_get();
	
		ASYNC_REF_ENTER(context, socket);
		async_task_suspend(&socket->writes, NULL, execute_data, &cancelled);
		ASYNC_REF_EXIT(context, socket);
	} else {
		zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to queue socket write: %s", uv_strerror(result));
	}
	
	if (bnum > 0) {
		efree(buffer);
	}
	
	socket_check_disposed(socket);
}


static async_tcp_socket *async_tcp_socket_object_create()
{
	async_tcp_socket *socket;

	socket = emalloc(sizeof(async_tcp_socket));
	ZEND_SECURE_ZERO(socket, sizeof(async_tcp_socket));

	zend_object_std_init(&socket->std, async_tcp_socket_ce);
	socket->std.handlers = &async_tcp_socket_handlers;
	
	socket->scheduler = async_task_scheduler_get();
	
	async_awaitable_queue_init(&socket->reads, socket->scheduler);
	async_awaitable_queue_init(&socket->writes, socket->scheduler);

	uv_tcp_init(&socket->scheduler->loop, &socket->handle);

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
		ASYNC_ADDREF(&socket->std);

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

#ifdef HAVE_ASYNC_SSL
	if (socket->ssl != NULL) {
		SSL_free(socket->ssl);
	}

	if (socket->ctx != NULL && socket->server == NULL) {
		SSL_CTX_free(socket->ctx);
	}

	if (socket->encryption != NULL) {
		ASYNC_DELREF(&socket->encryption->std);
	}
#endif

	if (socket->name != NULL) {
		zend_string_release(socket->name);
	}
	
	if (socket->local_addr != NULL) {
		zend_string_release(socket->local_addr);
	}
	
	if (socket->remote_addr != NULL) {
		zend_string_release(socket->remote_addr);
	}

	zval_ptr_dtor(&socket->read_error);
	zval_ptr_dtor(&socket->write_error);

	if (socket->server != NULL) {
		ASYNC_DELREF(&socket->server->std);
	}
	
	async_awaitable_queue_destroy(&socket->reads);
	async_awaitable_queue_destroy(&socket->writes);

	zend_object_std_dtor(&socket->std);
}

ZEND_METHOD(TcpSocket, connect)
{
	async_tcp_socket *socket;
	async_context *context;
	
	zend_bool cancelled;
	zend_string *name;
	zend_long port;

	zval *tls;
	zval ip;
	zval obj;

	uv_connect_t connect;
	struct sockaddr_in dest;
	int code;

	tls = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
	    Z_PARAM_STR(name)
		Z_PARAM_LONG(port)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(tls)
	ZEND_PARSE_PARAMETERS_END();

	async_gethostbyname(ZSTR_VAL(name), &ip, execute_data);

	ASYNC_RETURN_ON_ERROR();

	socket = async_tcp_socket_object_create();
	socket->name = zend_string_copy(name);

	code = uv_ip4_addr(Z_STRVAL_P(&ip), (int) port, &dest);

	zval_ptr_dtor(&ip);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to assemble IP address: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}

	code = uv_tcp_connect(&connect, &socket->handle, (const struct sockaddr *) &dest, socket_connected);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to intialize socket connect operation: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
		return;
	}
	
	context = async_context_get();

	ASYNC_REF_ENTER(context, socket);
	async_task_suspend(&socket->reads, NULL, execute_data, &cancelled);
	ASYNC_REF_EXIT(context, socket);

	if (cancelled || socket->scheduler->disposed || UNEXPECTED(EG(exception))) {
		ASYNC_DELREF(&socket->std);
		return;
	}

	if (tls != NULL && Z_TYPE_P(tls) != IS_NULL) {
#ifdef HAVE_ASYNC_SSL
		socket->encryption = async_clone_client_encryption((async_tls_client_encryption *) Z_OBJ_P(tls));
		socket->encryption->mode = ASYNC_SSL_MODE_CLIENT;

		if (socket->encryption->peer_name == NULL) {
			socket->encryption->peer_name = zend_string_copy(socket->name);
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
		ASYNC_ADDREF(&socket->std);

		uv_close((uv_handle_t *) &socket->handle, socket_disposed);
	}
}

ZEND_METHOD(TcpSocket, getAddress)
{
	async_tcp_socket *socket;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	RETURN_STR(socket->local_addr);
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
	
	RETURN_STR(socket->remote_addr);
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

	ASYNC_CHECK_EXCEPTION(code != 0 && code != UV_ENOTSUP, async_socket_exception_ce, "Failed to set socket option: %s", uv_strerror(code));

	RETURN_LONG((code == 0) ? 1 : 0);
}

static inline void socket_call_read(async_tcp_socket *socket, zval *return_value, zend_execute_data *execute_data)
{
	async_context *context;
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

	if (hint == NULL || Z_TYPE_P(hint) == IS_NULL) {
		len = socket->buffer.size;
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

	if (socket->reads.first != NULL) {
		zend_throw_exception(async_pending_read_exception_ce, "Cannot read from socket while another read is pending", 0);
		return;
	}

	if (socket->eof) {
		return;
	}

	if (socket->buffer.len == 0) {
		code = uv_read_start((uv_stream_t *) &socket->handle, socket_read_alloc, socket_read);

		ASYNC_CHECK_EXCEPTION(code != 0, async_stream_exception_ce, "Failed to start socket read: %s", uv_strerror(code));
		
		context = async_context_get();

		ASYNC_REF_ENTER(context, socket);
		async_task_suspend(&socket->reads, NULL, execute_data, &cancelled);
		ASYNC_REF_EXIT(context, socket);
		
		if (socket_check_disposed(socket)) {			
			return;
		}

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

ZEND_METHOD(TcpSocket, read)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	socket_call_read(socket, return_value, execute_data);
}

ZEND_METHOD(TcpSocket, readStream)
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

	uv_buf_t *buffers;
	unsigned int nbufs;
	zend_long len;

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
	
	buffers = assemble_write_buffers(socket, data, &nbufs, &len);
	
	if (buffers == NULL) {
		return;
	}
	
	write_to_socket(socket, buffers, nbufs, execute_data);
	
	disassemble_write_buffers(socket, buffers, nbufs);
}

ZEND_METHOD(TcpSocket, write)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	socket_call_write(socket, return_value, execute_data);
}

ZEND_METHOD(TcpSocket, writeAsync)
{
	async_tcp_socket *socket;
	async_tcp_write *write;
	zend_string *data;
	
	uv_buf_t *buffers;
	uv_buf_t *tmp;
	unsigned int nbufs;
	zend_long len;
	int code;
	int num;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(data)
	ZEND_PARSE_PARAMETERS_END();
	
	socket = (async_tcp_socket *) Z_OBJ_P(getThis());
	
	buffers = assemble_write_buffers(socket, data, &nbufs, &len);
	
	if (buffers == NULL) {
		return;
	}
	
	if (socket->writes.first == NULL) {
		tmp = try_write_to_socket(socket, buffers, nbufs, &num);
	} else {
		tmp = NULL;
	}
	
	if (tmp == NULL) {
		disassemble_write_buffers(socket, buffers, nbufs);
		
		RETURN_LONG(0);
	}
	
	write = emalloc(sizeof(async_tcp_write));
	write->request.data = write;
	write->socket = socket;
		
	code = uv_write(&write->request, (uv_stream_t *) &socket->handle, tmp, num, socket_write_async);

	if (UNEXPECTED(code != 0)) {
		if (tmp != NULL) {
			efree(tmp);
		}
		
		disassemble_write_buffers(socket, buffers, nbufs);
		efree(write);
		
		zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to queue socket write: %s", uv_strerror(code));
		return;
	}
	
#ifdef HAVE_ASYNC_SSL
	if (socket->ssl == NULL) {
		write->data = zend_string_copy(data);
	} else {
		write->data = NULL;
		write->buffers = buffers;
		write->nbufs = nbufs;
		write->tmp = tmp;
	}
#else
	write->data = zend_string_copy(data);
#endif
	
	ASYNC_ADDREF(&socket->std);
	
	RETURN_LONG(socket->handle.write_queue_size);
}

ZEND_METHOD(TcpSocket, writeStream)
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
	async_context *context;
	zend_bool cancelled;

	X509 *cert;

	int code;
	long result;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (socket->server == NULL) {
		socket->ctx = async_ssl_create_context();

		SSL_CTX_set_default_passwd_cb_userdata(socket->ctx, NULL);
		SSL_CTX_set_verify_depth(socket->ctx, 10);
		
		async_ssl_setup_verify_callback(socket->ctx, socket->encryption);
	} else {
		ASYNC_CHECK_EXCEPTION(socket->server->encryption == NULL, async_socket_exception_ce, "No encryption settings have been passed to TcpServer::listen()");

		socket->ctx = socket->server->ctx;
	}

	socket->ssl = SSL_new(socket->ctx);
	socket->rbio = BIO_new(BIO_s_mem());
	socket->wbio = BIO_new(BIO_s_mem());

	BIO_set_mem_eof_return(socket->rbio, -1);
	BIO_set_mem_eof_return(socket->wbio, -1);

	SSL_set_bio(socket->ssl, socket->rbio, socket->wbio);
	SSL_set_read_ahead(socket->ssl, 1);
	
	async_ssl_setup_encryption(socket->ssl, socket->encryption);

	if (socket->server != NULL) {
		SSL_set_accept_state(socket->ssl);
	} else {
		char name[256];

		if (socket->encryption != NULL && socket->encryption->peer_name != NULL) {
			strcpy(name, ZSTR_VAL(socket->encryption->peer_name));
		} else {
			strcpy(name, ZSTR_VAL(socket->name));
		}

		SSL_set_connect_state(socket->ssl);
		SSL_set_tlsext_host_name(socket->ssl, name);
	}

	SSL_set_mode(socket->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);

#ifdef SSL_MODE_RELEASE_BUFFERS
	SSL_set_mode(socket->ssl, SSL_get_mode(socket->ssl) | SSL_MODE_RELEASE_BUFFERS);
#endif

	if (socket->server == NULL) {
		code = SSL_do_handshake(socket->ssl);

		ASYNC_CHECK_EXCEPTION(!async_ssl_error_continue(socket->ssl, code), async_socket_exception_ce, "SSL handshake failed [%d]: %s", code, ERR_reason_error_string(code));
	}

	uv_tcp_nodelay(&socket->handle, 1);
	
	context = async_context_get();

	while (!SSL_is_init_finished(socket->ssl)) {
		ssl_send_handshake_bytes(socket, execute_data);

		ASYNC_RETURN_ON_ERROR();

		code = uv_read_start((uv_stream_t *) &socket->handle, socket_read_alloc, socket_read);
		
		ASYNC_CHECK_EXCEPTION(code != 0, async_stream_exception_ce, "Failed to read from TCP socket: %s", uv_strerror(code));

		ASYNC_REF_ENTER(context, socket);
		async_task_suspend(&socket->reads, NULL, execute_data, &cancelled);
		ASYNC_REF_EXIT(context, socket);
		
		if (socket_check_disposed(socket)) {
			return;
		}
		
		if (cancelled) {
			uv_read_stop((uv_stream_t *) &socket->handle);
			return;
		}
		
		ASYNC_RETURN_ON_ERROR();
		ASYNC_CHECK_EXCEPTION(socket->eof, async_stream_closed_exception_ce, "SSL handshake failed due to closed socket");

		code = SSL_do_handshake(socket->ssl);

		ASYNC_CHECK_EXCEPTION(!async_ssl_error_continue(socket->ssl, code), async_socket_exception_ce, "SSL handshake failed [%d]: %s", code, ERR_reason_error_string(code));
	}

	ssl_send_handshake_bytes(socket, execute_data);

	ASYNC_RETURN_ON_ERROR();

	if (socket->server == NULL) {
		cert = SSL_get_peer_certificate(socket->ssl);

		if (cert == NULL) {
			X509_free(cert);
			zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to access server SSL certificate");
			return;
		}

		X509_free(cert);

		result = SSL_get_verify_result(socket->ssl);

		if (X509_V_OK != result && !(socket->encryption->allow_self_signed && result == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)) {
			zend_throw_exception_ex(async_socket_exception_ce, 0, "Failed to verify server SSL certificate [%ld]: %s", result, X509_verify_cert_error_string(result));
			return;
		}
	}

	code = ssl_feed_data(socket);

	ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "SSL data feed failed [%d]: %s", code, ERR_reason_error_string(code));

	uv_tcp_nodelay(&socket->handle, 0);
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

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_read_stream, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_write_async, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_write_stream, 0, 0, Concurrent\\Stream\\WritableStream, 0)
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
	ZEND_ME(TcpSocket, readStream, arginfo_tcp_socket_read_stream, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, writeAsync, arginfo_tcp_socket_write_async, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocket, writeStream, arginfo_tcp_socket_write_stream, ZEND_ACC_PUBLIC)
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

ZEND_METHOD(TcpSocketReader, read)
{
	async_tcp_socket_reader *reader;

	reader = (async_tcp_socket_reader *) Z_OBJ_P(getThis());

	socket_call_read(reader->socket, return_value, execute_data);
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

	ASYNC_ADDREF(&socket->std);

	code = uv_shutdown(shutdown, (uv_stream_t *) &socket->handle, socket_shutdown);

	if (UNEXPECTED(code != 0)) {
		zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to initialize socket shutdown: %s", uv_strerror(code));
		ASYNC_DELREF(&socket->std);
	}
}

ZEND_METHOD(TcpSocketWriter, write)
{
	async_tcp_socket_writer *writer;

	writer = (async_tcp_socket_writer *) Z_OBJ_P(getThis());

	socket_call_write(writer->socket, return_value, execute_data);
}

static const zend_function_entry async_tcp_socket_writer_functions[] = {
	ZEND_ME(TcpSocketWriter, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(TcpSocketWriter, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_tcp_server *async_tcp_server_object_create()
{
	async_tcp_server *server;

	server = emalloc(sizeof(async_tcp_server));
	ZEND_SECURE_ZERO(server, sizeof(async_tcp_server));

	zend_object_std_init(&server->std, async_tcp_server_ce);
	server->std.handlers = &async_tcp_server_handlers;
	
	server->scheduler = async_task_scheduler_get();

	async_awaitable_queue_init(&server->accepts, server->scheduler);

	uv_tcp_init(&server->scheduler->loop, &server->handle);

	server->handle.data = server;

	ZVAL_UNDEF(&server->error);

	return server;
}

static void async_tcp_server_object_dtor(zend_object *object)
{
	async_tcp_server *server;

	server = (async_tcp_server *) object;

	if (!uv_is_closing((uv_handle_t *) &server->handle)) {
		ASYNC_ADDREF(&server->std);

		uv_close((uv_handle_t *) &server->handle, server_disposed);
	}
}

static void async_tcp_server_object_destroy(zend_object *object)
{
	async_tcp_server *server;

	server = (async_tcp_server *) object;

	zval_ptr_dtor(&server->error);

#ifdef HAVE_ASYNC_SSL
	if (server->ctx != NULL) {
		SSL_CTX_free(server->ctx);
	}

	if (server->encryption != NULL) {
		ASYNC_DELREF(&server->encryption->std);
	}
#endif

	if (server->name != NULL) {
		zend_string_release(server->name);
	}
	
	if (server->addr != NULL) {
		zend_string_release(server->addr);
	}
	
	async_awaitable_queue_destroy(&server->accepts);

	zend_object_std_dtor(&server->std);
}

ZEND_METHOD(TcpServer, listen)
{
	async_tcp_server *server;

	zend_string *name;
	zend_long port;

	zval *tls;
	zval ip;
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

	async_gethostbyname(ZSTR_VAL(name), &ip, execute_data);

	ASYNC_RETURN_ON_ERROR();

	code = uv_ip4_addr(Z_STRVAL_P(&ip), (int) port, &bind);

	zval_ptr_dtor(&ip);

	ASYNC_CHECK_EXCEPTION(code != 0, async_socket_exception_ce, "Failed to assemble IP address: %s", uv_strerror(code));

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
		zend_throw_exception_ex(async_socket_exception_ce, 0, "Serevr encryption requires async extension to be compiled with SSL support");
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

	zend_throw_exception_ex(async_socket_exception_ce, 0, "Server has been closed");

	ZVAL_OBJ(&server->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&server->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&server->accepts, &server->error, 0);

	if (!uv_is_closing((uv_handle_t *) &server->handle)) {
		ASYNC_ADDREF(&server->std);

		uv_close((uv_handle_t *) &server->handle, server_disposed);
	}
}

ZEND_METHOD(TcpServer, getAddress)
{
	async_tcp_server *server;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	RETURN_STR(server->name);
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

	ASYNC_CHECK_EXCEPTION(code != 0 && code != UV_ENOTSUP, async_socket_exception_ce, "Failed to set socket option: %s", uv_strerror(code));

	RETURN_LONG((code == 0) ? 1 : 0);
}

ZEND_METHOD(TcpServer, accept)
{
	async_tcp_server *server;
	async_tcp_socket *socket;
	async_context *context;

	zend_bool cancelled;
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
		
		context = async_context_get();
		
		if (!context->background) {
			server->ref_count++;
			
			if (server->ref_count == 1) {
				uv_ref((uv_handle_t *) &server->handle);
			}
		}

		async_task_suspend(&server->accepts, NULL, execute_data, &cancelled);
		
		if (!context->background) {
			server->ref_count--;
			
			if (server->ref_count == 0) {
				uv_unref((uv_handle_t *) &server->handle);
			}
		}
		
		if (server->scheduler->disposed && Z_TYPE_P(&server->error) == IS_UNDEF) {
			ZVAL_COPY(&server->error, &server->scheduler->error);
			
			if (!uv_is_closing((uv_handle_t *) &server->handle)) {
				ASYNC_ADDREF(&server->std);
		
				uv_close((uv_handle_t *) &server->handle, server_disposed);
			}
			
			return;
		}
		
		if (cancelled) {
			return;
		}
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
