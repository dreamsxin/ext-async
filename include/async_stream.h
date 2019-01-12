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

#include "async_ssl.h"

#define ASYNC_STREAM_EOF 1
#define ASYNC_STREAM_CLOSED (1 << 1)
#define ASYNC_STREAM_SHUT_RD (1 << 2)
#define ASYNC_STREAM_SHUT_WR (1 << 3)
#define ASYNC_STREAM_READING (1 << 4)

#define ASYNC_STREAM_SHUT_RDWR ASYNC_STREAM_SHUT_RD | ASYNC_STREAM_SHUT_WR

typedef void (* async_stream_write_cb)(void *arg);

typedef struct {
	async_op base;
	int code;
	size_t len;
	const char *error;
	union {
		struct {
			size_t len;
			char *base;
		} buf;
		zend_string *str;
	} data;
} async_stream_read_op;

typedef struct {
	uv_stream_t *handle;
	uint16_t flags;
	zend_uchar ref_count;
	async_ring_buffer buffer;
	async_ssl_engine ssl;
	async_stream_read_op read;
	async_op_queue writes;
	zval read_error;
	zval write_error;
} async_stream;

typedef struct {
	async_op base;
	async_stream *stream;
	async_context *context;
	int code;
	uv_write_t req;
	uv_buf_t bufs[1];
	char *data;
	zend_string *str;
	async_stream_write_cb cb;
	void *arg;
} async_stream_write_op;

async_stream *async_stream_init(uv_stream_t *handle, size_t bufsize);
void async_stream_free(async_stream *stream);
void async_stream_close(async_stream *stream, uv_close_cb onclose, void *data);
void async_stream_shutdown(async_stream *stream, int how);
int async_stream_read(async_stream *stream, char *buf, size_t len);
int async_stream_read_string(async_stream *stream, zend_string **str, size_t len);
void async_stream_write(async_stream *stream, char *buf, size_t len);
void async_stream_async_write_string(async_stream *stream, zend_string *str, async_stream_write_cb cb, void *arg);

#ifdef HAVE_ASYNC_SSL
int async_stream_ssl_handshake(async_stream *stream, async_ssl_handshake_data *data);
#endif

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
