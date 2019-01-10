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
#include "zend_inheritance.h"

ASYNC_API zend_class_entry *async_duplex_stream_ce;
ASYNC_API zend_class_entry *async_pending_read_exception_ce;
ASYNC_API zend_class_entry *async_readable_stream_ce;
ASYNC_API zend_class_entry *async_stream_closed_exception_ce;
ASYNC_API zend_class_entry *async_stream_exception_ce;
ASYNC_API zend_class_entry *async_writable_stream_ce;

#define ASYNC_STREAM_SHOULD_READ(stream) (((stream)->buffer.size - (stream)->buffer.len) >= 4096)

//////////////////////////////////////////////////////////
// FIXME: Implement proper SSL shutdown!
/*

uv_buf_t bufs[1];

char *base;
int len;

SSL_shutdown(data->ssl->ssl);

if ((len = BIO_ctrl_pending(data->ssl->wbio)) > 0) {
	base = emalloc(len);
	bufs[0] = uv_buf_init(base, BIO_read(data->ssl->wbio, base, len));

	uv_try_write((uv_stream_t *) &data->handle, bufs, 1);

	efree(base);
}

*/
//////////////////////////////////////////////////////////

static inline int await_op(async_stream *stream, async_op *op)
{
	async_context *context;
	int code;
	
	context = async_context_get();
	
	if (!context->background && ++stream->ref_count == 1) {
		uv_ref((uv_handle_t *) stream->handle);
	}
	
	code = async_await_op((async_op *) op);
	
	if (!context->background && --stream->ref_count == 0) {
		uv_unref((uv_handle_t *) stream->handle);
	}
	
	return code;
}

static inline void init_buffer(async_stream *stream)
{
	stream->buffer.base = emalloc(stream->buffer.size);
	stream->buffer.rpos = stream->buffer.base;
	stream->buffer.wpos = stream->buffer.base;
}


async_stream *async_stream_init(uv_stream_t *handle, size_t bufsize)
{
	async_stream *stream;
	
	stream = emalloc(sizeof(async_stream));
	ZEND_SECURE_ZERO(stream, sizeof(async_stream));
	
	stream->buffer.size = MAX(bufsize, 0x8000);

	stream->handle = handle;
	handle->data = stream;
	
	uv_unref((uv_handle_t *) handle);
	
	return stream;
}

void async_stream_free(async_stream *stream)
{
	if (stream->buffer.base != NULL) {
		efree(stream->buffer.base);
		stream->buffer.base = NULL;
	}
	
	efree(stream);
}

void async_stream_close(async_stream *stream, uv_close_cb onclose, void *data)
{
	stream->flags |= ASYNC_STREAM_EOF | ASYNC_STREAM_CLOSED | ASYNC_STREAM_SHUT_WR;
	
	async_stream_shutdown(stream, ASYNC_STREAM_SHUT_RD);
	
	stream->handle->data = data;
	
	uv_close((uv_handle_t *) stream->handle, onclose);
}

static void shutdown_cb(uv_shutdown_t *req, int status)
{
	async_op *op;
	
	op = (async_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	ASYNC_FINISH_OP(op);
}

void async_stream_shutdown(async_stream *stream, int how)
{
	async_stream_read_op *read;
	async_op *op;
	
	uv_shutdown_t req;
	int code;
	
	if (how & ASYNC_STREAM_SHUT_RD) {
		stream->flags |= ASYNC_STREAM_EOF;
	}

	if (how & ASYNC_STREAM_SHUT_RD && !(stream->flags & ASYNC_STREAM_SHUT_RD)) {
		stream->flags |= ASYNC_STREAM_SHUT_RD;
		
		if (stream->flags & ASYNC_STREAM_READING) {
			uv_read_stop(stream->handle);
			
			stream->flags &= ~ASYNC_STREAM_READING;
		}
		
		if (stream->read != NULL) {
			read = stream->read;
			read->code = UV_ECANCELED;
			
			ASYNC_FINISH_OP(read);
		}
	}
	
	if (how & ASYNC_STREAM_SHUT_WR && !(stream->flags & ASYNC_STREAM_SHUT_WR)) {
		stream->flags |= ASYNC_STREAM_SHUT_WR;
		
		if (uv_is_closing((uv_handle_t *) stream->handle)) {
			return;
		}
		
		code = uv_shutdown(&req, stream->handle, shutdown_cb);
		
		if (code < 0) {
			zend_throw_error(NULL, "Shutdown failed: %s", uv_strerror(code));
			return;
		}
		
		ASYNC_ALLOC_OP(op);
		
		req.data = op;
		
		if (await_op(stream, op) == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
			ASYNC_FREE_OP(op);
			
			return;
		}
		
		ASYNC_FREE_OP(op);
	}
}

#ifdef HAVE_ASYNC_SSL

static inline int is_ssl_continue_error(SSL *ssl, int code)
{	
	switch (SSL_get_error(ssl, code)) {
	case SSL_ERROR_NONE:
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
	case SSL_ERROR_ZERO_RETURN:
		return 1;
	}
	
	return 0;
}

static inline int process_input_bytes(async_stream *stream, int count)
{
	int len;
	int code;

	if (stream->ssl.ssl == NULL) {
		async_ring_buffer_write_move(&stream->buffer, count);
		
		return SUCCESS;
	}
	
	if (count > 0) {
		len = BIO_write(stream->ssl.rbio, stream->buffer.wpos, count);
	}
	
	if (!SSL_is_init_finished(stream->ssl.ssl)) {
		return SUCCESS;
	}
	
	code = SSL_ERROR_NONE;
	
	while (1) {
		do {
			ERR_clear_error();
			
			len = SSL_read(stream->ssl.ssl, stream->buffer.wpos, async_ring_buffer_write_len(&stream->buffer));
			code = SSL_get_error(stream->ssl.ssl, len);
			
			if (len < 1) {
				if (!is_ssl_continue_error(stream->ssl.ssl, len)) {
					return ERR_get_error();
				}
				
				break;
			}

			async_ring_buffer_write_move(&stream->buffer, len);
			
			stream->ssl.pending += len;
		} while (SSL_pending(stream->ssl.ssl));
		
		if (code != SSL_ERROR_NONE) {
			break;
		}

		stream->ssl.available += stream->ssl.pending;
		stream->ssl.pending = 0;
	}
	
	return (stream->ssl.available > 0) ? SUCCESS : FAILURE;
}

#define ASYNC_STREAM_BUFFER_LEN(stream) (((stream)->ssl.ssl == NULL) ? (stream)->buffer.len : (stream)->ssl.available)
#define ASYNC_STREAM_BUFFER_CONSUME(stream, length) do { \
	if ((stream)->ssl.ssl != NULL) { \
		(stream)->ssl.available -= length; \
	} \
} while (0)

#else

static inline int process_input_bytes(async_stream *stream, int count)
{
	async_ring_buffer_write_move(&stream->buffer, count);
	
	return SUCCESS;
}

#define ASYNC_STREAM_BUFFER_LEN(stream) (stream)->buffer.len
#define ASYNC_STREAM_BUFFER_CONSUME(stream, length)

#endif

static void read_cb(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf)
{
	async_stream *stream;
	async_stream_read_op *read;
	
	size_t len;
	size_t blen;
	
	stream = (async_stream *) handle->data;

	if (nread == 0) {
		return;
	}
	
	if (nread == UV_ECONNRESET) {
		nread = UV_EOF;
	}
	
	if (nread < 0 && nread != UV_EOF) {
		uv_read_stop(handle);
		
		stream->flags &= ~ASYNC_STREAM_READING;
		
		while (stream->read != NULL) {
			read = stream->read;
			stream->read = NULL;
			
			read->code = (int) nread;
			
			ASYNC_FINISH_OP(read);
		}
		
		return;
	}

	if (nread == UV_EOF) {
		stream->flags |= ASYNC_STREAM_EOF;
	} else {
#ifdef HAVE_ASYNC_SSL
		int code;

		if (SUCCESS != (code = process_input_bytes(stream, (int) nread))) {
			if (code == FAILURE) {
				return;
			}
		
			uv_read_stop(handle);
			
			stream->flags &= ~ASYNC_STREAM_READING;
			
			while (stream->read != NULL) {
				read = stream->read;
				stream->read = NULL;
				
				read->code = FAILURE;
				read->error = ERR_reason_error_string(code);
				
				ASYNC_FINISH_OP(read);
			}
			
			return;
		}
#else
		process_input_bytes(stream, (int) nread);
#endif
	}

	while (stream->read != NULL && (blen = ASYNC_STREAM_BUFFER_LEN(stream)) > 0) {
		read = stream->read;
		stream->read = NULL;
		
		if (read->code == 1) {
			read->len = async_ring_buffer_read_string(&stream->buffer, &read->data.str, MIN(read->len, blen));
			
			ASYNC_STREAM_BUFFER_CONSUME(stream, read->len);
		} else {
			len = async_ring_buffer_read(&stream->buffer, read->data.buf.base, MIN(read->len, blen));
			
			read->data.buf.base += len;
			read->data.buf.len += len;
			read->len -= len;
			
			ASYNC_STREAM_BUFFER_CONSUME(stream, len);
		}

		ASYNC_FINISH_OP(read);
	}
	
	if (nread == UV_EOF) {
		uv_read_stop(handle);
		
		stream->flags &= ~ASYNC_STREAM_READING;

		while (stream->read != NULL) {
			read = stream->read;
			stream->read = NULL;
			
			read->code = UV_EOF;
			
			ASYNC_FINISH_OP(read);
		}
	
		return;
	}
	
	if (!ASYNC_STREAM_SHOULD_READ(stream)) {
		uv_read_stop(handle);
		
		stream->flags &= ~ASYNC_STREAM_READING;
	}
}

static void read_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	async_stream *stream;
	
	stream = (async_stream *) handle->data;
	
	ZEND_ASSERT(stream != NULL);
	
	buf->base = stream->buffer.wpos;
	buf->len = async_ring_buffer_write_len(&stream->buffer);
}

int async_stream_read(async_stream *stream, char *buf, size_t len)
{
	async_stream_read_op *op;

	size_t blen;
	int code;
	
#ifdef HAVE_ASYNC_SSL
	const char *error;
#endif
	
	ZEND_ASSERT(len > 0);
	
	if (stream->flags & ASYNC_STREAM_SHUT_RD) {
		zend_throw_error(NULL, "Stream reader has been closed");
		return FAILURE;
	}
	
	if (stream->read != NULL) {
		return UV_EALREADY;
	}
	
	if (stream->buffer.base == NULL) {
		init_buffer(stream);
	}

	if ((blen = ASYNC_STREAM_BUFFER_LEN(stream)) > 0) {
		len = async_ring_buffer_read(&stream->buffer, buf, MIN(len, blen));
		
		ASYNC_STREAM_BUFFER_CONSUME(stream, len);
		
		if (!(stream->flags && ASYNC_STREAM_EOF) && ASYNC_STREAM_SHOULD_READ(stream)) {
			if (!(stream->flags & ASYNC_STREAM_READING)) {
				uv_read_start(stream->handle, read_alloc_cb, read_cb);
				
				stream->flags |= ASYNC_STREAM_READING;
			}
		}
		
		return len;
	}
	
	if (stream->flags & ASYNC_STREAM_EOF) {
		return 0;
	}
	
	if (!(stream->flags & ASYNC_STREAM_READING)) {
		uv_read_start(stream->handle, read_alloc_cb, read_cb);
		
		stream->flags |= ASYNC_STREAM_READING;
	}
	
	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_stream_read_op));
	stream->read = op;
	
	op->data.buf.base = buf;
	op->data.buf.len = 0;
	op->len = len;
	
	code = await_op(stream, (async_op *) op);
	
	if (code == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
		ASYNC_FREE_OP(op);
		
		return FAILURE;
	}
	
	len = op->data.buf.len;
	code = op->code;
	
#ifdef HAVE_ASYNC_SSL
	error = op->error;
#endif
	
	ASYNC_FREE_OP(op);
	
	if (code == UV_EOF) {
		return 0;
	}
	
	if (code < 0) {
#ifdef HAVE_ASYNC_SSL
		if (code == FAILURE && stream->ssl.ssl != NULL && error != NULL) {
			zend_throw_error(NULL, "SSL error: %s", error);
			return FAILURE;
		}
#endif
		
		zend_throw_error(NULL, "Read operation failed: %s", uv_strerror(code));
		return FAILURE;
	}
	
	return len;
}

int async_stream_read_string(async_stream *stream, zend_string **str, size_t len)
{
	async_stream_read_op *op;
	
	zend_string *tmp;
	size_t blen;
	int code;
	
#ifdef HAVE_ASYNC_SSL
	const char *error;
#endif
	
	ZEND_ASSERT(len > 0);
	
	*str = NULL;
	
	if (stream->flags & ASYNC_STREAM_SHUT_RD) {
		zend_throw_error(NULL, "Stream reader has been closed");
		return FAILURE;
	}
	
	if (stream->read != NULL) {
		return UV_EALREADY;
	}

	if (stream->buffer.base == NULL) {
		init_buffer(stream);
	}

	if ((blen = ASYNC_STREAM_BUFFER_LEN(stream)) > 0) {
		len = async_ring_buffer_read_string(&stream->buffer, str, MIN(len, blen));
		
		ASYNC_STREAM_BUFFER_CONSUME(stream, len);
		
		if (!(stream->flags && ASYNC_STREAM_EOF) && ASYNC_STREAM_SHOULD_READ(stream)) {
			if (!(stream->flags & ASYNC_STREAM_READING)) {
				uv_read_start(stream->handle, read_alloc_cb, read_cb);
				
				stream->flags |= ASYNC_STREAM_READING;
			}
		}
		
		return len;
	}
	
	if (stream->flags & ASYNC_STREAM_EOF) {
		return 0;
	}
	
	if (!(stream->flags & ASYNC_STREAM_READING)) {
		uv_read_start(stream->handle, read_alloc_cb, read_cb);
		
		stream->flags |= ASYNC_STREAM_READING;
	}
	
	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_stream_read_op));
	stream->read = op;
	
	op->code = 1;
	op->len = len;

	code = await_op(stream, (async_op *) op);

	if (code == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
		ASYNC_FREE_OP(op);
		
		return code;
	}
	
	tmp = op->data.str;
	code = op->code;
	
#ifdef HAVE_ASYNC_SSL
	error = op->error;
#endif

	ASYNC_FREE_OP(op);
	
	if (code == UV_EOF) {
		return 0;
	}
	
	if (code < 0) {
#ifdef HAVE_ASYNC_SSL
		if (code == FAILURE && stream->ssl.ssl != NULL && error != NULL) {
			zend_throw_error(NULL, "SSL error: %s", error);
			return FAILURE;
		}
#endif

		zend_throw_error(NULL, "Read operation failed: %s", uv_strerror(code));
		return FAILURE;
	}
	
	*str = tmp;
	
	return ZSTR_LEN(tmp);
}

static int try_write(async_stream *stream, char *buf, size_t len)
{
	uv_buf_t bufs[1];
	
	int written;
	int code;
	
	bufs[0] = uv_buf_init(buf, len);
	
	written = 0;
	
	while (bufs[0].len > 0) {
		code = uv_try_write(stream->handle, bufs, 1);
		
		if (code == UV_EAGAIN) {
			break;
		}
		
		if (code < 0) {
			return code;
		}
		
		bufs[0].base += code;
		bufs[0].len -= code;
		
		written += code;
	}
	
	return written;
}

static void write_cb(uv_write_t *req, int status)
{
	async_stream_write_op *op;
	
	op = (async_stream_write_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	op->code = status;
	
#ifdef HAVE_ASYNC_SSL
	if (op->stream->ssl.ssl != NULL) {
		efree(op->data);
	}
#endif
	
	if (op->str != NULL) {
		zend_string_release(op->str);
	}
	
	ASYNC_FINISH_OP(op);
	
	if (op->cb != NULL) {
		op->cb(op->arg);
	
		ASYNC_DELREF(&op->context->std);
		ASYNC_FREE_OP(op);
	}
}

void async_stream_write(async_stream *stream, char *buf, size_t len)
{
	async_stream_write_op *op;
	
	char *base;
	int code;
	
	ZEND_ASSERT(len > 0);
	
	if (stream->flags & ASYNC_STREAM_SHUT_WR) {
		zend_throw_error(NULL, "Stream writer has been closed");
		
		return;
	}
	
	base = NULL;
	
#ifdef HAVE_ASYNC_SSL
	if (stream->ssl.ssl != NULL) {
		int offset;
		int blen;
		
		blen = 0;
	
		while (len > 0) {
			ERR_clear_error();
			offset = SSL_write(stream->ssl.ssl, buf, len);
			
			if (offset <= 0) {
				efree(base);
			
				zend_throw_error(NULL, "SSL error: %d\n", (int) SSL_get_error(stream->ssl.ssl, offset));
				return;
			}
			
			buf += offset;
			len -= offset;
			
			while ((offset = BIO_ctrl_pending(stream->ssl.wbio)) > 0) {
				if (base == NULL) {
					base = emalloc(blen + offset);
				} else {
					erealloc(base, blen + offset);
				}
				
				offset = BIO_read(stream->ssl.wbio, base + blen, offset);
								
				blen += offset;
			}
		}
		
		buf = base;
		len = blen;
	}
#endif

	if (stream->writes.first == NULL) {
		code = try_write(stream, buf, len);
		
		if (code < 0) {
			if (base != NULL) {
				efree(base);
			}
		
			zend_throw_error(NULL, "Write operation failed: %s", uv_strerror(code));
			return;
		}

		buf += code;
		len -= code;
		
		if (len == 0) {
			if (base != NULL) {
				efree(base);
			}
			
			return;
		}
	}

	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_stream_write_op));
	ASYNC_ENQUEUE_OP(&stream->writes, op);
	
	op->bufs[0] = uv_buf_init(buf, len);
	op->stream = stream;
	op->data = base;
	op->req.data = op;

	code = uv_write(&op->req, stream->handle, op->bufs, 1, write_cb);
	
	if (code < 0) {
		if (base != NULL) {
			efree(base);
		}
			
		ASYNC_FREE_OP(op);
		
		zend_throw_error(NULL, "Write operation failed: %s", uv_strerror(code));
		return;
	}
	
	if (await_op(stream, (async_op *) op) == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
		ASYNC_FREE_OP(op);
		
		return;
	}
	
	code = op->code;
	
	ASYNC_FREE_OP(op);
	
	if (code < 0) {
		zend_throw_error(NULL, "Write operation failed: %s", uv_strerror(code));
	}
}

void async_stream_async_write_string(async_stream *stream, zend_string *str, async_stream_write_cb cb, void *arg)
{
	async_stream_write_op *op;
	
	int code;
	char *base;
	
	char *buf;
	size_t len;
	
	ZEND_ASSERT(ZSTR_LEN(str) > 0);

	if (stream->flags & ASYNC_STREAM_SHUT_WR) {
		zend_throw_error(NULL, "Stream writer has been closed");
		
		return;
	}
	
	base = NULL;
	
	buf = ZSTR_VAL(str);
	len = ZSTR_LEN(str);
	
#ifdef HAVE_ASYNC_SSL
	if (stream->ssl.ssl != NULL) {
		int offset;
		int blen;
		
		blen = 0;
	
		while (len > 0) {
			ERR_clear_error();
			offset = SSL_write(stream->ssl.ssl, buf, len);
			
			if (offset <= 0) {
				efree(base);
			
				zend_throw_error(NULL, "SSL error: %d\n", (int) SSL_get_error(stream->ssl.ssl, offset));
				return;
			}
			
			buf += offset;
			len -= offset;
			
			while ((offset = BIO_ctrl_pending(stream->ssl.wbio)) > 0) {
				if (base == NULL) {
					base = emalloc(blen + offset);
				} else {
					erealloc(base, blen + offset);
				}
				
				offset = BIO_read(stream->ssl.wbio, base + blen, offset);
								
				blen += offset;
			}
		}
		
		buf = base;
		len = blen;
	}
#endif
	
	if (stream->writes.first == NULL) {
		code = try_write(stream, buf, len);
		
		if (code < 0) {
			if (base != NULL) {
				efree(base);
			}
		
			zend_throw_error(NULL, "Write operation failed: %s", uv_strerror(code));
			return;
		}

		buf += code;
		len -= code;
		
		if (len == 0) {
			if (base != NULL) {
				efree(base);
			}
			
			cb(arg);
			
			return;
		}
	}

	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_stream_write_op));
	ASYNC_ENQUEUE_OP(&stream->writes, op);
	
	op->bufs[0] = uv_buf_init(buf, len);
	op->stream = stream;
	op->data = base;
	op->req.data = op;

	code = uv_write(&op->req, stream->handle, op->bufs, 1, write_cb);
	
	if (code < 0) {
		if (base != NULL) {
			efree(base);
		}
			
		ASYNC_FREE_OP(op);
		
		zend_throw_error(NULL, "Write operation failed: %s", uv_strerror(code));
		return;
	}
	
	op->context = async_context_get();
	op->cb = cb;
	op->arg = arg;
	
#ifdef HAVE_ASYNC_SSL
	if (stream->ssl.ssl == NULL) {
		op->str = zend_string_copy(str);
	}
#else
	op->str = zend_string_copy(str);
#endif
	
	ASYNC_ADDREF(&op->context->std);
}

#ifdef HAVE_ASYNC_SSL

static void receive_handshake_bytes_cb(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf)
{
	async_stream *stream;
	async_ssl_op *op;
	
	stream = (async_stream *) handle->data;
	
	ZEND_ASSERT(stream != NULL);
	ZEND_ASSERT(stream->ssl.handshake != NULL);
	
	if (nread == 0) {
		return;
	}
	
	uv_read_stop(handle);
	
	op = stream->ssl.handshake;
	
	if (nread < 0) {
		op->uv_error = (int) nread;
	} else {
		op->ssl_error = process_input_bytes(stream, (int) nread);
	}
	
	ASYNC_FINISH_OP(op);
}

static void receive_handshake_bytes_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
	async_stream *stream;
	
	stream = (async_stream *) handle->data;
	
	ZEND_ASSERT(stream != NULL);
	
	buf->base = stream->buffer.wpos;
	buf->len = async_ring_buffer_write_len(&stream->buffer);
}

static int receive_handshake_bytes(async_stream *stream, async_ssl_handshake_data *data)
{
	async_ssl_op *op;
	
	int code;
	
	code = uv_read_start(stream->handle, receive_handshake_bytes_alloc_cb, receive_handshake_bytes_cb);
	
	if (code < 0) {
		data->uv_error = code;
		
		return FAILURE;
	}
	
	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_ssl_op));
	
	stream->ssl.handshake = op;
	
	if (await_op(stream, (async_op *) op) == FAILURE) {
		ASYNC_FREE_OP(op);
		
		return FAILURE;
	}
	
	if (op->uv_error < 0) {
		ASYNC_FREE_OP(op);
		
		data->uv_error = op->uv_error;
		
		return FAILURE;
	}
	
	code = op->ssl_error;

	ASYNC_FREE_OP(op);
	
	if (code != SSL_ERROR_NONE) {
		data->ssl_error = code;
		
		return FAILURE;
	}

	return SUCCESS;
}

static void send_handshake_bytes_cb(uv_write_t *req, int status)
{
	async_uv_op *op;
	
	op = (async_uv_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	op->code = status;
	
	ASYNC_FINISH_OP(op);
}

static int send_handshake_bytes(async_stream *stream, async_ssl_handshake_data *data)
{
	async_uv_op *op;

	uv_write_t req;
	uv_buf_t bufs[1];

	char *base;
	size_t len;
	int code;

	while ((len = BIO_ctrl_pending(stream->ssl.wbio)) > 0) {
		// TODO: Avoid memory alloc & copy by using BIO_get_mem_data()
		base = emalloc(len);
		len = BIO_read(stream->ssl.wbio, base, len);
		
		bufs[0] = uv_buf_init(base, len);
		
		while (bufs[0].len > 0) {
			code = uv_try_write(stream->handle, bufs, 1);
			
			if (code == UV_EAGAIN) {
				break;
			}
			
			if (code < 0) {
				efree(base);				
				data->uv_error = code;
		
				return FAILURE;
			}
			
			bufs[0].base += code;
			bufs[0].len -= code;
		}
		
		if (bufs[0].len == 0) {
			efree(base);
			
			continue;
		}
		
		code = uv_write(&req, stream->handle, bufs, 1, send_handshake_bytes_cb);
		
		if (code < 0) {
			efree(base);
			data->uv_error = code;
		
			return FAILURE;
		}
		
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
		
		req.data = op;
		
		if (await_op(stream, (async_op *) op) == FAILURE) {
			efree(base);
			ASYNC_FREE_OP(op);
			
			return FAILURE;
		}
		
		code = op->code;
		
		efree(base);
		ASYNC_FREE_OP(op);
		
		if (code < 0) {
			data->uv_error = code;
		
			return FAILURE;
		}
	}
	
	return SUCCESS;
}

int async_stream_ssl_handshake(async_stream *stream, async_ssl_handshake_data *data)
{
	X509 *cert;

	long result;
	int code;
	
	ZEND_ASSERT(stream->ssl.ssl != NULL);

	if (data->host == NULL) {
		SSL_set_accept_state(stream->ssl.ssl);
	} else {
		SSL_set_connect_state(stream->ssl.ssl);
		
#ifdef ASYNC_TLS_SNI
		SSL_set_tlsext_host_name(stream->ssl.ssl, data->host);
#endif
		
		ERR_clear_error();
		
		code = SSL_do_handshake(stream->ssl.ssl);

		if (!is_ssl_continue_error(stream->ssl.ssl, code)) {
			data->ssl_error = ERR_get_error();
			
			return FAILURE;
		}
	}
	
	if (stream->buffer.base == NULL) {
		init_buffer(stream);
	}

	uv_read_stop(stream->handle);
	
	while (!SSL_is_init_finished(stream->ssl.ssl)) {
		if (SUCCESS != send_handshake_bytes(stream, data)) {
			return FAILURE;
		}

		if (SUCCESS != receive_handshake_bytes(stream, data)) {
			return FAILURE;
		}
		
		ERR_clear_error();

		code = SSL_do_handshake(stream->ssl.ssl);

		if (!is_ssl_continue_error(stream->ssl.ssl, code)) {
			data->ssl_error = ERR_get_error();
			
			return FAILURE;
		}
	}
	
	// Feed remaining buffered input bytes into SSL engine to decrypt them.
	if (SUCCESS != (code = process_input_bytes(stream, 0))) {
		if (code != FAILURE) {
			data->ssl_error = code;
	
			return FAILURE;
		}
	}
	
	// Send remaining bytes that mark handshake completion as needed.
	if (SUCCESS != send_handshake_bytes(stream, data)) {
		return FAILURE;
	}
	
	if (data->host != NULL) {
		char buffer[1024];
		
		ZEND_SECURE_ZERO(buffer, 1024);
	
		cert = SSL_get_peer_certificate(stream->ssl.ssl);

		if (cert == NULL) {
			X509_free(cert);
			strcpy(buffer, "Failed to access server SSL certificate");
			data->error = zend_string_init(buffer, strlen(buffer), 0);
			
			return FAILURE;
		}

		X509_free(cert);

		result = SSL_get_verify_result(stream->ssl.ssl);
		
		if (X509_V_OK != result && !(data->allow_self_signed && result == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)) {
			php_sprintf(buffer, "Failed to verify server SSL certificate [%ld]: %s", result, X509_verify_cert_error_string(result));
			data->error = zend_string_init(buffer, strlen(buffer), 0);
			
			return FAILURE;
		}
	}
	
	return SUCCESS;
}

#endif


ZEND_METHOD(ReadableStream, close) { }
ZEND_METHOD(ReadableStream, read) { }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_stream_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_stream_read, 0, 0, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_readable_stream_functions[] = {
	ZEND_ME(ReadableStream, close, arginfo_readable_stream_close, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(ReadableStream, read, arginfo_readable_stream_read, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


ZEND_METHOD(WritableStream, close) { }
ZEND_METHOD(WritableStream, write) { }

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_stream_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_stream_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_writable_stream_functions[] = {
	ZEND_ME(WritableStream, close, arginfo_writable_stream_close, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(WritableStream, write, arginfo_writable_stream_write, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


ZEND_METHOD(DuplexStream, getReadableStream) { }
ZEND_METHOD(DuplexStream, getWritableStream) { }

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_duplex_stream_get_readable_stream, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_duplex_stream_get_writable_stream, 0, 0, Concurrent\\Stream\\WritableStream, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_duplex_stream_functions[] = {
	ZEND_ME(DuplexStream, getReadableStream, arginfo_duplex_stream_get_readable_stream, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(DuplexStream, getWritableStream, arginfo_duplex_stream_get_writable_stream, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


static const zend_function_entry empty_funcs[] = {
	ZEND_FE_END
};


void async_stream_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\ReadableStream", async_readable_stream_functions);
	async_readable_stream_ce = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\WritableStream", async_writable_stream_functions);
	async_writable_stream_ce = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\DuplexStream", async_duplex_stream_functions);
	async_duplex_stream_ce = zend_register_internal_interface(&ce);

	zend_class_implements(async_duplex_stream_ce, 2, async_readable_stream_ce, async_writable_stream_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\StreamException", empty_funcs);
	async_stream_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_stream_exception_ce, zend_ce_exception);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\StreamClosedException", empty_funcs);
	async_stream_closed_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_stream_closed_exception_ce, async_stream_exception_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Stream\\PendingReadException", empty_funcs);
	async_pending_read_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_pending_read_exception_ce, async_stream_exception_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
