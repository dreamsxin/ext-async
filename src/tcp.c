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
zend_class_entry *async_tcp_client_encryption_ce;
zend_class_entry *async_tcp_server_encryption_ce;

static zend_object_handlers async_tcp_socket_handlers;
static zend_object_handlers async_tcp_socket_reader_handlers;
static zend_object_handlers async_tcp_socket_writer_handlers;
static zend_object_handlers async_tcp_server_handlers;
static zend_object_handlers async_tcp_client_encryption_handlers;
static zend_object_handlers async_tcp_server_encryption_handlers;

static async_tcp_socket *async_tcp_socket_object_create();
static async_tcp_socket_reader *async_tcp_socket_reader_object_create(async_tcp_socket *socket);
static async_tcp_socket_writer *async_tcp_socket_writer_object_create(async_tcp_socket *socket);

static void write_to_socket(async_tcp_socket *socket, uv_buf_t *buffer, int num, zend_execute_data *execute_data);

#ifdef HAVE_ASYNC_SSL

static int async_index;

typedef struct _socket_buffer socket_buffer;

struct _socket_buffer {
	uv_buf_t buf;
	socket_buffer *next;
};

static int ssl_error_continue(async_tcp_socket *socket, int code)
{
	int error = SSL_get_error(socket->ssl, code);

	switch (error) {
	case SSL_ERROR_NONE:
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
		return 1;
	}

	return 0;
}

static int ssl_cert_passphrase_cb(char *buf, int size, int rwflag, void *obj)
{
	async_tcp_cert *cert;

	cert = (async_tcp_cert *) obj;

	if (cert == NULL || cert->passphrase == NULL) {
		return 0;
	}

	strcpy(buf, ZSTR_VAL(cert->passphrase));

	return ZSTR_LEN(cert->passphrase);
}

static SSL_CTX *ssl_create_context()
{
	SSL_CTX *ctx;

	int options;
	char *cadir;

	options = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
	options |= SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET;
	options &= ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;

	ctx = SSL_CTX_new(SSLv23_method());

	SSL_CTX_set_options(ctx, options);
	SSL_CTX_set_cipher_list(ctx, "HIGH:!SSLv2:!aNULL:!eNULL:!EXPORT:!DES:!MD5:!RC4:!ADH");

	// Use default X509 cert directory.
	cadir = getenv(X509_get_default_cert_dir_env());
	SSL_CTX_load_verify_locations(ctx, NULL, (cadir == NULL) ? X509_get_default_cert_dir() : cadir);

	return ctx;
}

static void ssl_send_handshake_bytes(async_tcp_socket *socket, zend_execute_data *execute_data)
{
	uv_buf_t buffer[1];
	size_t len;

	while ((len = BIO_ctrl_pending(socket->wbio)) > 0) {
		buffer[0].base = emalloc(len);
		buffer[0].len = BIO_read(socket->wbio, buffer[0].base, len);

		write_to_socket(socket, buffer, 1, execute_data);

		efree(buffer[0].base);

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
			switch (error = SSL_get_error(socket->ssl, len)) {
			case SSL_ERROR_WANT_READ:
				return 0;
			default:
				return error;
			}
		}

		socket->buffer.len += len;
		base += len;
	}

	return 0;
}

static zend_bool ssl_match_hostname(const char *subjectname, const char *certname)
{
	char *wildcard = NULL;
	ptrdiff_t prefix_len;
	size_t suffix_len, subject_len;

	if (strcasecmp(subjectname, certname) == 0) {
		return 1;
	}

	if (!(wildcard = strchr(certname, '*')) || memchr(certname, '.', wildcard - certname)) {
		return 0;
	}

	prefix_len = wildcard - certname;
	if (prefix_len && strncasecmp(subjectname, certname, prefix_len) != 0) {
		return 0;
	}

	suffix_len = strlen(wildcard + 1);
	subject_len = strlen(subjectname);

	if (suffix_len <= subject_len) {
		return strcasecmp(wildcard + 1, subjectname + subject_len - suffix_len) == 0 && memchr(subjectname + prefix_len, '.', subject_len - suffix_len - prefix_len) == NULL;
	}

	return strcasecmp(wildcard + 2, subjectname + subject_len - suffix_len);
}

#define ASYNC_SSL_RETURN_VERIFY_ERROR(ctx) do { \
	X509_STORE_CTX_set_error(ctx, X509_V_ERR_APPLICATION_VERIFICATION); \
	return 0; \
} while (0);

static int ssl_check_san_names(async_tcp_socket *socket, X509 *cert, X509_STORE_CTX *ctx)
{
	GENERAL_NAMES *names;
	GENERAL_NAME *entry;

	zend_string *peer_name;
	unsigned char *cn;

	int count;
	int i;

	names = X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0);

	if (names == NULL) {
		return 1;
	}

	for (count = sk_GENERAL_NAME_num(names), i = 0; i < count; i++) {
		entry = sk_GENERAL_NAME_value(names, i);

		if (entry == NULL || GEN_DNS != entry->type) {
			continue;
		}

		ASN1_STRING_to_UTF8(&cn, entry->d.dNSName);

		if ((size_t) ASN1_STRING_length(entry->d.dNSName) != strlen((const char *) cn)) {
			OPENSSL_free(cn);
			break;
		}

		if (socket->encryption != NULL && socket->encryption->peer_name != NULL) {
			peer_name = socket->encryption->peer_name;
		} else {
			peer_name = socket->name;
		}

		if (ssl_match_hostname(ZSTR_VAL(peer_name), (const char *) cn)) {
			OPENSSL_free(cn);
			sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
			return 1;
		}

		OPENSSL_free(cn);
	}

	sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

	return 0;
}

static int ssl_verify_callback(int preverify, X509_STORE_CTX *ctx)
{
	async_tcp_socket *socket;
	zend_string *peer_name;
	SSL *ssl;

	X509 *cert;
	X509_NAME_ENTRY *entry;
	ASN1_STRING *str;

	unsigned char *cn;

	int depth;
	int err;
	int i;

	cert = X509_STORE_CTX_get_current_cert(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);
	err = X509_STORE_CTX_get_error(ctx);

	ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	socket = (async_tcp_socket *) SSL_get_ex_data(ssl, async_index);

	// Allow self-signed cert only as first cert in chain.
	if (depth == 0 && err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) {
		if (socket->encryption != NULL && socket->encryption->allow_self_signed) {
			err = 0;
			preverify = 1;
			X509_STORE_CTX_set_error(ctx, X509_V_OK);
		}
	}

	if (!cert || err || socket == NULL) {
		return preverify;
	}

	if (depth == 0) {
		if (ssl_check_san_names(socket, cert, ctx)) {
			return preverify;
		}

		if ((i = X509_NAME_get_index_by_NID(X509_get_subject_name((X509 *) cert), NID_commonName, -1)) < 0) {
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		if (NULL == (entry = X509_NAME_get_entry(X509_get_subject_name((X509 *) cert), i))) {
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		if (NULL == (str = X509_NAME_ENTRY_get_data(entry))) {
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		ASN1_STRING_to_UTF8(&cn, str);

		if ((size_t) ASN1_STRING_length(str) != strlen((const char *) cn)) {
			OPENSSL_free(cn);
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		if (socket->encryption != NULL && socket->encryption->peer_name != NULL) {
			peer_name = socket->encryption->peer_name;
		} else {
			peer_name = socket->name;
		}

		if (!ssl_match_hostname(ZSTR_VAL(peer_name), (const char *) cn)) {
			OPENSSL_free(cn);
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		OPENSSL_free(cn);
	}

	return preverify;
}

static int ssl_servername_cb(SSL *ssl, int *ad, void *arg)
{
	async_tcp_server *server;
	async_tcp_cert *cert;
	zend_string *key;

	const char *name;

	if (ssl == NULL) {
		return SSL_TLSEXT_ERR_NOACK;
	}

	name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

	if (name == NULL || name[0] == '\0') {
		return SSL_TLSEXT_ERR_NOACK;
	}

	server = (async_tcp_server *) arg;
	key = zend_string_init(name, strlen(name), 0);

	cert = server->encryption->certs.first;

	while (cert != NULL) {
		if (zend_string_equals(key, cert->host)) {
			SSL_set_SSL_CTX(ssl, cert->ctx);
			break;
		}

		cert = cert->next;
	}

	zend_string_release(key);

	return SSL_TLSEXT_ERR_OK;
}

#endif


static void socket_disposed(uv_handle_t *handle)
{
	async_tcp_socket *socket;

	socket = (async_tcp_socket *) handle->data;

	if (socket->buffer.base != NULL) {
		efree(socket->buffer.base);
		socket->buffer.base = NULL;
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

static void write_to_socket(async_tcp_socket *socket, uv_buf_t *buffer, int num, zend_execute_data *execute_data)
{
	uv_write_t write;
	zval retval;

	int result;

	while (num == 1 && socket->writes.first == NULL) {
		result = uv_try_write((uv_stream_t *) &socket->handle, buffer, num);

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
	}

	result = uv_write(&write, (uv_stream_t *) &socket->handle, buffer, 1, socket_write);

	if (UNEXPECTED(result != 0)) {
		zend_throw_exception_ex(async_stream_exception_ce, 0, "Failed to queue socket write: %s", uv_strerror(result));
		return;
	}

	ZVAL_UNDEF(&retval);
	async_task_suspend(&socket->writes, &retval, execute_data, NULL);
	zval_ptr_dtor(&retval);
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

#ifdef HAVE_ASYNC_SSL
	if (socket->ssl != NULL) {
		SSL_free(socket->ssl);
	}

	if (socket->ctx != NULL && socket->server == NULL) {
		SSL_CTX_free(socket->ctx);
	}

	if (socket->encryption != NULL) {
		OBJ_RELEASE(&socket->encryption->std);
	}
#endif

	if (socket->name != NULL) {
		zend_string_release(socket->name);
	}

	zval_ptr_dtor(&socket->read_error);
	zval_ptr_dtor(&socket->write_error);

	if (socket->server != NULL) {
		OBJ_RELEASE(&socket->server->std);
	}

	zend_object_std_dtor(&socket->std);
}

ZEND_METHOD(Socket, connect)
{
	async_tcp_socket *socket;

	zend_string *name;
	zend_long port;

	zval *tls;
	zval ip;
	zval tmp;
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

	if (tls != NULL && Z_TYPE_P(tls) != IS_NULL) {
#ifdef HAVE_ASYNC_SSL
		socket->encryption = (async_tcp_client_encryption *) Z_OBJ_P(tls);

		GC_ADDREF(&socket->encryption->std);
#else
		zend_throw_error(NULL, "Socket encryption requires async extension to be compiled with SSL support");
		OBJ_RELEASE(&socket->std);
		return;
#endif
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

ZEND_METHOD(Socket, setNodelay)
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

#ifdef HAVE_ASYNC_SSL
	if (socket->ssl != NULL) {
		uv_buf_t *buffer;

		socket_buffer *first;
		socket_buffer *last;
		socket_buffer *current;

		size_t len;
		size_t remaining;

		char *tmp;
		int i;
		int w;
		int num;

		remaining = ZSTR_LEN(data);
		tmp = ZSTR_VAL(data);

		num = 0;

		buffer = NULL;
		first = NULL;
		last = NULL;

		while (remaining > 0) {
			w = SSL_write(socket->ssl, tmp, remaining);

			if (w < 1 && !ssl_error_continue(socket, w)) {
				num = 0;

				zend_throw_error(NULL, "SSL write operation failed [%d]: %s", w, ERR_reason_error_string(w));
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

				current->buf.base = emalloc(len);
				current->buf.len = BIO_read(socket->wbio, current->buf.base, len);

				num++;
			}
		}

		if (num == 1) {
			write_to_socket(socket, &first->buf, 1, execute_data);
		} else if (num > 1) {
			buffer = ecalloc(num, sizeof(uv_buf_t));

			current = first;
			i = 0;

			while (current != NULL) {
				buffer[i++] = current->buf;

				current = current->next;
			}

			write_to_socket(socket, buffer, num, execute_data);
		}

		current = first;

		while (current != NULL) {
			efree(current->buf.base);

			last = current;
			current = current->next;

			efree(last);
		}

		if (buffer != NULL) {
			efree(buffer);
		}

		ASYNC_RETURN_ON_ERROR();

		return;
	}
#endif

	uv_buf_t buffer[1];

	buffer[0].base = ZSTR_VAL(data);
	buffer[0].len = ZSTR_LEN(data);

	write_to_socket(socket, buffer, 1, execute_data);
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

ZEND_METHOD(Socket, encrypt)
{
#ifndef HAVE_ASYNC_SSL
	zend_throw_error(NULL, "Async extension was not compiled with SSL support");
#else
	async_tcp_socket *socket;

	X509 *cert;

	int code;
	long result;

	ZEND_PARSE_PARAMETERS_NONE();

	socket = (async_tcp_socket *) Z_OBJ_P(getThis());

	if (socket->server == NULL) {
		socket->ctx = ssl_create_context();

		SSL_CTX_set_default_passwd_cb_userdata(socket->ctx, NULL);
		SSL_CTX_set_default_passwd_cb(socket->ctx, ssl_cert_passphrase_cb);

		SSL_CTX_set_verify(socket->ctx, SSL_VERIFY_PEER, ssl_verify_callback);
		SSL_CTX_set_verify_depth(socket->ctx, 10);
	} else {
		if (socket->server->encryption == NULL) {
			zend_throw_error(NULL, "No encryption settings have been passed to TcpServer::listen()");
			return;
		}

		socket->ctx = socket->server->ctx;
	}

	socket->ssl = SSL_new(socket->ctx);
	socket->rbio = BIO_new(BIO_s_mem());
	socket->wbio = BIO_new(BIO_s_mem());

	BIO_set_mem_eof_return(socket->rbio, -1);
	BIO_set_mem_eof_return(socket->wbio, -1);

	SSL_set_bio(socket->ssl, socket->rbio, socket->wbio);
	SSL_set_read_ahead(socket->ssl, 1);
	SSL_set_ex_data(socket->ssl, async_index, socket);

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

		if (!ssl_error_continue(socket, code)) {
			zend_throw_error(NULL, "SSL handshake failed [%d]: %s", code, ERR_reason_error_string(code));
			return;
		}
	}

	zval retval;

	while (!SSL_is_init_finished(socket->ssl)) {
		ssl_send_handshake_bytes(socket, execute_data);

		ASYNC_RETURN_ON_ERROR();

		uv_read_start((uv_stream_t *) &socket->handle, socket_read_alloc, socket_read);

		ZVAL_UNDEF(&retval);
		async_task_suspend(&socket->reads, &retval, execute_data, NULL);
		zval_ptr_dtor(&retval);

		ASYNC_RETURN_ON_ERROR();

		if (socket->eof) {
			zend_throw_error(NULL, "SSL handshake failed due to closed socket");
			return;
		}

		code = SSL_do_handshake(socket->ssl);

		if (!ssl_error_continue(socket, code)) {
			zend_throw_error(NULL, "SSL handshake failed [%d]: %s", code, ERR_reason_error_string(code));
			return;
		}
	}

	ssl_send_handshake_bytes(socket, execute_data);

	ASYNC_RETURN_ON_ERROR();

	if (socket->server == NULL) {
		cert = SSL_get_peer_certificate(socket->ssl);

		if (cert == NULL) {
			X509_free(cert);
			zend_throw_error(NULL, "Failed to access server SSL certificate");
			return;
		}

		X509_free(cert);

		result = SSL_get_verify_result(socket->ssl);

		if (X509_V_OK != result) {
			zend_throw_error(NULL, "Failed to verify server SSL certificate [%ld]: %s", result, X509_verify_cert_error_string(result));
			return;
		}
	}

	code = ssl_feed_data(socket);

	if (code != 0) {
		zend_throw_error(NULL, "SSL data feed failed [%d]: %s", code, ERR_reason_error_string(code));
		return;
	}
#endif
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_socket_connect, 0, 2, Concurrent\\Network\\TcpSocket, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
	ZEND_ARG_OBJ_INFO(0, encryption, Concurrent\\Network\\ClientEncryption, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_pair, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_set_nodelay, 0, 1, IS_VOID, 0)
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

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_socket_encrypt, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_socket_functions[] = {
	ZEND_ME(Socket, connect, arginfo_tcp_socket_connect, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Socket, pair, arginfo_tcp_socket_pair, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Socket, close, arginfo_tcp_socket_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, setNodelay, arginfo_tcp_socket_set_nodelay, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, getLocalPeer, arginfo_tcp_socket_get_local_peer, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, getRemotePeer, arginfo_tcp_socket_get_remote_peer, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, read, arginfo_tcp_socket_read, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, readStream, arginfo_tcp_socket_read_stream, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, write, arginfo_tcp_socket_write, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, writeStream, arginfo_tcp_socket_write_stream, ZEND_ACC_PUBLIC)
	ZEND_ME(Socket, encrypt, arginfo_tcp_socket_encrypt, ZEND_ACC_PUBLIC)
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

#ifdef HAVE_ASYNC_SSL
	if (server->ctx != NULL) {
		SSL_CTX_free(server->ctx);
	}

	if (server->encryption != NULL) {
		OBJ_RELEASE(&server->encryption->std);
	}
#endif

	if (server->name != NULL) {
		zend_string_release(server->name);
	}

	zend_object_std_dtor(&server->std);
}

ZEND_METHOD(Server, listen)
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

	if (UNEXPECTED(code != 0)) {
		zend_throw_error(NULL, "Failed to assemble IP address: %s", uv_strerror(code));
		return;
	}

	server = async_tcp_server_object_create();
	server->name = zend_string_copy(name);
	server->port = (uint16_t) port;

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

	if (tls != NULL && Z_TYPE_P(tls) != IS_NULL) {
#ifdef HAVE_ASYNC_SSL
		server->encryption = (async_tcp_server_encryption *) Z_OBJ_P(tls);

		GC_ADDREF(&server->encryption->std);

		server->ctx = ssl_create_context();

		SSL_CTX_set_default_passwd_cb(server->ctx, ssl_cert_passphrase_cb);
		SSL_CTX_set_default_passwd_cb_userdata(server->ctx, &server->encryption->cert);

		SSL_CTX_use_certificate_file(server->ctx, ZSTR_VAL(server->encryption->cert.file), SSL_FILETYPE_PEM);
		SSL_CTX_use_PrivateKey_file(server->ctx, ZSTR_VAL(server->encryption->cert.key), SSL_FILETYPE_PEM);

		SSL_CTX_set_tlsext_servername_callback(server->ctx, ssl_servername_cb);
		SSL_CTX_set_tlsext_servername_arg(server->ctx, server);
#else
		zend_throw_error(NULL, "Serevr encryption requires async extension to be compiled with SSL support");
		OBJ_RELEASE(&server->std);
		return;
#endif
	}

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

ZEND_METHOD(Server, getHost)
{
	async_tcp_server *server;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	RETURN_STR(server->name);
}

ZEND_METHOD(Server, getPort)
{
	async_tcp_server *server;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	RETURN_LONG(server->port);
}

ZEND_METHOD(Server, getPeer)
{
	async_tcp_server *server;

	ZEND_PARSE_PARAMETERS_NONE();

	server = (async_tcp_server *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		assemble_peer(&server->handle, 0, return_value, execute_data);
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

	socket->server = server;

	GC_ADDREF(&server->std);

	ZVAL_OBJ(&obj, &socket->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_listen, 0, 2, Concurrent\\Network\\TcpServer, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
	ZEND_ARG_OBJ_INFO(0, encryption, Concurrent\\Network\\ServerEncryption, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_get_host, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_get_port, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_tcp_server_get_peer, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_accept, 0, 0, Concurrent\\Network\\TcpSocket, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_server_functions[] = {
	ZEND_ME(Server, listen, arginfo_tcp_server_listen, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Server, close, arginfo_tcp_server_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Server, getHost, arginfo_tcp_server_get_host, ZEND_ACC_PUBLIC)
	ZEND_ME(Server, getPort, arginfo_tcp_server_get_port, ZEND_ACC_PUBLIC)
	ZEND_ME(Server, getPeer, arginfo_tcp_server_get_peer, ZEND_ACC_PUBLIC)
	ZEND_ME(Server, accept, arginfo_tcp_server_accept, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *async_tcp_client_encryption_object_create(zend_class_entry *ce)
{
	async_tcp_client_encryption *encryption;

	encryption = emalloc(sizeof(async_tcp_client_encryption));
	ZEND_SECURE_ZERO(encryption, sizeof(async_tcp_client_encryption));

	zend_object_std_init(&encryption->std, ce);
	encryption->std.handlers = &async_tcp_client_encryption_handlers;

	return &encryption->std;
}

static async_tcp_client_encryption *clone_client_encryption(async_tcp_client_encryption *encryption)
{
	async_tcp_client_encryption *result;

	result = (async_tcp_client_encryption *) async_tcp_client_encryption_object_create(async_tcp_client_encryption_ce);
	result->allow_self_signed = encryption->allow_self_signed;

	if (encryption->peer_name != NULL) {
		result->peer_name = zend_string_copy(encryption->peer_name);
	}

	return result;
}

static void async_tcp_client_encryption_object_destroy(zend_object *object)
{
	async_tcp_client_encryption *encryption;

	encryption = (async_tcp_client_encryption *) object;

	if (encryption->peer_name != NULL) {
		zend_string_release(encryption->peer_name);
	}

	zend_object_std_dtor(&encryption->std);
}

ZEND_METHOD(ClientEncryption, withAllowSelfSigned)
{
	async_tcp_client_encryption *encryption;

	zend_bool allow;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_BOOL(allow)
	ZEND_PARSE_PARAMETERS_END();

	zval obj;

	encryption = clone_client_encryption((async_tcp_client_encryption *) Z_OBJ_P(getThis()));
	encryption->allow_self_signed = allow;

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(ClientEncryption, withPeerName)
{
	async_tcp_client_encryption *encryption;

	zend_string *name;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();

	zval obj;

	encryption = clone_client_encryption((async_tcp_client_encryption *) Z_OBJ_P(getThis()));
	encryption->peer_name = zend_string_copy(name);

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_client_encryption_with_allow_self_signed, 0, 1, Concurrent\\Network\\ClientEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, allow, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_client_encryption_with_peer_name, 0, 1, Concurrent\\Network\\ClientEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_client_encryption_functions[] = {
	ZEND_ME(ClientEncryption, withAllowSelfSigned, arginfo_tcp_client_encryption_with_allow_self_signed, ZEND_ACC_PUBLIC)
	ZEND_ME(ClientEncryption, withPeerName, arginfo_tcp_client_encryption_with_peer_name, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *async_tcp_server_encryption_object_create(zend_class_entry *ce)
{
	async_tcp_server_encryption *encryption;

	encryption = emalloc(sizeof(async_tcp_server_encryption));
	ZEND_SECURE_ZERO(encryption, sizeof(async_tcp_server_encryption));

	zend_object_std_init(&encryption->std, ce);
	encryption->std.handlers = &async_tcp_server_encryption_handlers;

	return &encryption->std;
}

static async_tcp_server_encryption *clone_server_encryption(async_tcp_server_encryption *encryption)
{
	async_tcp_server_encryption *result;

	result = (async_tcp_server_encryption *) async_tcp_server_encryption_object_create(async_tcp_server_encryption_ce);

	if (encryption->cert.file != NULL) {
		result->cert.file = zend_string_copy(encryption->cert.file);
	}

	if (encryption->cert.key != NULL) {
		result->cert.key = zend_string_copy(encryption->cert.key);
	}

	if (encryption->cert.passphrase != NULL) {
		result->cert.passphrase = zend_string_copy(encryption->cert.passphrase);
	}

	return result;
}

static void async_tcp_server_encryption_object_destroy(zend_object *object)
{
	async_tcp_server_encryption *encryption;

	encryption = (async_tcp_server_encryption *) object;

	if (encryption->cert.file != NULL) {
		zend_string_release(encryption->cert.file);
	}

	if (encryption->cert.key != NULL) {
		zend_string_release(encryption->cert.key);
	}

	if (encryption->cert.passphrase != NULL) {
		zend_string_release(encryption->cert.passphrase);
	}

#ifdef HAVE_ASYNC_SSL
	if (encryption->cert.ctx != NULL) {
		SSL_CTX_free(encryption->cert.ctx);
	}
#endif

	zend_object_std_dtor(&encryption->std);
}

ZEND_METHOD(ServerEncryption, withDefaultCertificate)
{
	async_tcp_server_encryption *encryption;

	zend_string *file;
	zend_string *key;
	zend_string *passphrase;

	zval obj;

	passphrase = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_STR(file)
		Z_PARAM_STR(key)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR(passphrase)
	ZEND_PARSE_PARAMETERS_END();

	encryption = clone_server_encryption((async_tcp_server_encryption *) Z_OBJ_P(getThis()));

	encryption->cert.file = zend_string_copy(file);
	encryption->cert.key = zend_string_copy(key);

	if (passphrase != NULL) {
		encryption->cert.passphrase = zend_string_copy(passphrase);
	}

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(ServerEncryption, withCertificate)
{
	async_tcp_server_encryption *encryption;
	async_tcp_cert *cert;
	async_tcp_cert *current;

	zend_string *host;
	zend_string *file;
	zend_string *key;
	zend_string *passphrase;

	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 3, 4)
		Z_PARAM_STR(host)
		Z_PARAM_STR(file)
		Z_PARAM_STR(key)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR(passphrase)
	ZEND_PARSE_PARAMETERS_END();

	encryption = clone_server_encryption((async_tcp_server_encryption *) Z_OBJ_P(getThis()));

	cert = emalloc(sizeof(async_tcp_cert));
	ZEND_SECURE_ZERO(cert, sizeof(async_tcp_cert));

	cert->host = zend_string_copy(host);
	cert->file = zend_string_copy(file);
	cert->key = zend_string_copy(key);

	if (passphrase != NULL) {
		cert->passphrase = zend_string_copy(passphrase);
	}

#ifdef HAVE_ASYNC_SSL
	cert->ctx = ssl_create_context();

	SSL_CTX_set_default_passwd_cb(cert->ctx, ssl_cert_passphrase_cb);
	SSL_CTX_set_default_passwd_cb_userdata(cert->ctx, cert);

	SSL_CTX_use_certificate_file(cert->ctx, ZSTR_VAL(cert->file), SSL_FILETYPE_PEM);
	SSL_CTX_use_PrivateKey_file(cert->ctx, ZSTR_VAL(cert->key), SSL_FILETYPE_PEM);
#endif

	current = encryption->certs.first;

	while (current != NULL) {
		if (zend_string_equals(current->host, cert->host)) {
			ASYNC_Q_DETACH(&encryption->certs, current);
			break;
		}

		current = current->next;
	}

	ASYNC_Q_ENQUEUE(&encryption->certs, cert);

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tcp_server_encryption_with_default_certificate, 0, 2, Concurrent\\Network\\ServerEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, cert, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, passphrase, IS_STRING, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tcp_server_encryption_functions[] = {
	ZEND_ME(ServerEncryption, withDefaultCertificate, arginfo_tcp_server_encryption_with_default_certificate, ZEND_ACC_PUBLIC)
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

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\ClientEncryption", async_tcp_client_encryption_functions);
	async_tcp_client_encryption_ce = zend_register_internal_class(&ce);
	async_tcp_client_encryption_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tcp_client_encryption_ce->create_object = async_tcp_client_encryption_object_create;

	memcpy(&async_tcp_client_encryption_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_client_encryption_handlers.free_obj = async_tcp_client_encryption_object_destroy;
	async_tcp_client_encryption_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\ServerEncryption", async_tcp_server_encryption_functions);
	async_tcp_server_encryption_ce = zend_register_internal_class(&ce);
	async_tcp_server_encryption_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tcp_server_encryption_ce->create_object = async_tcp_server_encryption_object_create;

	memcpy(&async_tcp_server_encryption_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tcp_server_encryption_handlers.free_obj = async_tcp_server_encryption_object_destroy;
	async_tcp_server_encryption_handlers.clone_obj = NULL;

#ifdef HAVE_ASYNC_SSL
	async_index = SSL_get_ex_new_index(0, "ext-async", NULL, NULL, NULL);
#endif
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
