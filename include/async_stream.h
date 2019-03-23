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

#ifndef ASYNC_STREAM_H
#define ASYNC_STREAM_H

#include "async_buffer.h"
#include "async_ssl.h"

#ifdef PHP_WIN32
#define ASYNC_STREAM_UV_BUF_SIZE(len) (ULONG)(len)
#else
#define ASYNC_STREAM_UV_BUF_SIZE(len) (size_t)(len)
#endif

#define ASYNC_STREAM_EOF 1
#define ASYNC_STREAM_CLOSED (1 << 1)
#define ASYNC_STREAM_SHUT_RD (1 << 2)
#define ASYNC_STREAM_SHUT_WR (1 << 3)
#define ASYNC_STREAM_READING (1 << 4)
#define ASYNC_STREAM_SHUTDOWN (1 << 5)
#define ASYNC_STREAM_WANT_READ (1 << 6)
#define ASYNC_STREAM_DELAY_SHUTDOWN (1 << 7)
#define ASYNC_STREAM_WRITING (1 << 8)
#define ASYNC_STREAM_SSL_FATAL (1 << 9)

#define ASYNC_STREAM_SHUT_RDWR (ASYNC_STREAM_SHUT_RD | ASYNC_STREAM_SHUT_WR)

typedef struct _async_stream async_stream;

typedef void (* async_stream_write_cb)(void *arg);

typedef struct {
	struct {
		size_t len;
		char *buffer;
		uint64_t timeout;
	} in;
	struct {
		size_t len;
		zend_string *str;
		int error;
#ifdef HAVE_ASYNC_SSL
		int ssl_error;
#endif
	} out;
} async_stream_read_req;

#define ASYNC_STREAM_WRITE_REQ_FLAG_ASYNC 1

typedef struct {
	struct {
		size_t len;
		char *buffer;
		zend_string *str;
		zval *ref;
		uint8_t flags;
	} in;
	struct {
		int error;
#ifdef HAVE_ASYNC_SSL
		int ssl_error;
#endif
	} out;
} async_stream_write_req;

typedef struct {
	async_op base;
	async_stream_read_req *req;
} async_stream_read_op;

typedef struct {
	uv_shutdown_t req;
	zval ref;
} async_stream_shutdown_request;

struct _async_stream {
	uv_stream_t *handle;
	uv_timer_t timer;
	uint16_t flags;
	zend_uchar ref_count;
	async_ring_buffer buffer;
	async_ssl_engine ssl;
	async_stream_read_op read;
	async_stream_shutdown_request shutdown;
	async_op_list writes;
	async_op_list flush;
	zval read_error;
	zval write_error;
	zval ref;
};

#define ASYNC_STREAM_WRITE_OP_FLAG_NEEDS_FREE 1
#define ASYNC_STREAM_WRITE_OP_FLAG_ASYNC (1 << 1)
#define ASYNC_STREAM_WRITE_OP_FLAG_STARTED (1 << 2)

typedef struct {
	size_t size;
	size_t offset;
	char *data;
} async_stream_write_buf;

typedef struct {
	async_op base;
	async_stream *stream;
	async_context *context;
	uint8_t flags;
	int code;
#ifdef HAVE_ASYNC_SSL
	int ssl_error;
#endif
	uv_write_t req;
	zend_string *str;
	zval ref;
	async_stream_write_buf in;
	async_stream_write_buf out;
} async_stream_write_op;

async_stream *async_stream_init(uv_stream_t *handle, size_t bufsize);
void async_stream_free(async_stream *stream);
void async_stream_close(async_stream *stream, zval *ref);
void async_stream_close_cb(async_stream *stream, uv_close_cb callback, void *data);
void async_stream_shutdown(async_stream *stream, int how, zval *ref);
void async_stream_flush(async_stream *stream);
int async_stream_read(async_stream *stream, async_stream_read_req *req);
int async_stream_write(async_stream *stream, async_stream_write_req *req);

#ifdef HAVE_ASYNC_SSL
int async_stream_ssl_handshake(async_stream *stream, async_ssl_handshake_data *data);
#endif

static void zend_always_inline forward_stream_read_error(async_stream_read_req *req)
{
	ASYNC_RETURN_ON_ERROR();
	
#ifdef HAVE_ASYNC_SSL
	ASYNC_CHECK_EXCEPTION(req->out.ssl_error, async_stream_exception_ce, "Read operation failed: SSL %s", ERR_reason_error_string(req->out.ssl_error));
#endif

	ASYNC_CHECK_EXCEPTION(req->out.error == UV_EALREADY, async_pending_read_exception_ce, "Cannot read while another read is pending");

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Read operation failed: %s", uv_strerror(req->out.error));
}

static void zend_always_inline forward_stream_write_error(async_stream_write_req *req)
{
	ASYNC_RETURN_ON_ERROR();
	
#ifdef HAVE_ASYNC_SSL
	ASYNC_CHECK_EXCEPTION(req->out.ssl_error, async_stream_exception_ce, "Write operation failed: SSL %s", ERR_reason_error_string(req->out.ssl_error));
#endif

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Write operation failed: %s", uv_strerror(req->out.error));
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

// ReadableStream

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_stream_read, 0, 0, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 1)
ZEND_END_ARG_INFO()

// WritableStream

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_stream_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

// DuplexStream

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_duplex_stream_get_readable_stream, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_duplex_stream_get_writable_stream, 0, 0, Concurrent\\Stream\\WritableStream, 0)
ZEND_END_ARG_INFO()

#endif
