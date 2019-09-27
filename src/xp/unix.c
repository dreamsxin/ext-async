/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) Martin Schröder 2019                                   |
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

#include "async/pipe.h"
#include "async/xp.h"

#include "ext/standard/url.h"

static php_stream_transport_factory orig_unix_factory;

static php_stream *unix_socket_factory(const char *proto, size_t plen, const char *res, size_t reslen,
	const char *pid, int options, int flags, struct timeval *timeout, php_stream_context *context STREAMS_DC)
{
	return async_pipe_object_create2(pid);
}

static size_t async_pipe_write(php_stream *stream, const char *buf, size_t count)
{
	async_pipe *data;
	async_stream_write_req write;
	
	zval ref;

	data = (async_pipe *) stream->abstract;
	
	ZVAL_RES(&ref, stream->res);
	
	memset(&write, 0, sizeof(async_stream_write_req));

	write.in.len = count;
	write.in.buffer = (char *) buf;
	write.in.ref = &ref;
	
	if (UNEXPECTED(FAILURE == async_stream_write(data->astream, &write))) {
		return 0;
	}
	
	return count;
}

static size_t async_pipe_read(php_stream *stream, char *buf, size_t count)
{
	async_pipe *data;
	async_stream_read_req read;
	
	int code;
	
	data = (async_pipe *) stream->abstract;
	
	read.in.len = count;
	read.in.buffer = buf;
	read.in.handle = NULL;
	read.in.timeout = data->timeout;
	read.in.flags = 0;
	
	code = async_stream_read(data->astream, &read);
	
	if (EXPECTED(code == SUCCESS)) {
		return read.out.len;
	}
	
	if (UNEXPECTED(EG(exception))) {
		return 0;
	}

	php_error_docref(NULL, E_WARNING, "Read operation failed: %s", uv_strerror(read.out.error));

	return 0;
}

static int async_pipe_close(php_stream *stream, int close_handle)
{
	async_pipe *pipe;

	pipe = (async_pipe *) stream->abstract;

	if (pipe->astream == NULL) {
		pipe->handle.data = pipe;

		ASYNC_UV_TRY_CLOSE_REF(&pipe->std, &pipe->handle, pipe_disposed);
	} else {
		zval obj;
		ZVAL_OBJ(&obj, &pipe->std);
		
		async_stream_close(pipe->astream, &obj);
	}
	
	return 0;
}

static int async_pipe_flush(php_stream *stream)
{
	async_pipe *pipe = (async_pipe *) stream->abstract;
	
	async_stream_flush(pipe->astream);
    return 0;
}

static int async_pipe_cast(php_stream *stream, int castas, void **ret)
{
	async_pipe *data;
	php_socket_t fd;
	
	data = (async_pipe *) stream->abstract;
	
	switch (castas) {
	case PHP_STREAM_AS_FD_FOR_SELECT:
	case PHP_STREAM_AS_FD:
		if (0 != uv_fileno((const uv_handle_t *) &data->handle, (uv_os_fd_t *) &fd)) {
			return FAILURE;
		}
		
		if (ret) {
			*(php_socket_t *)ret = fd;
		}
	
		return SUCCESS;
	}
	
	return FAILURE;
}

static int async_pipe_stat(php_stream *stream, php_stream_statbuf *ssb)
{
	return FAILURE;
}

static int unix_socket_shutdown(php_stream *stream, async_pipe *data, int how)
{
	zval ref;

	int flag;
	
	switch (how) {
	case ASYNC_XP_SOCKET_SHUT_RD:
		flag = ASYNC_STREAM_SHUT_RD;
		break;
	case ASYNC_XP_SOCKET_SHUT_WR:
		flag = ASYNC_STREAM_SHUT_WR;
		break;
	default:
		flag = ASYNC_STREAM_SHUT_RDWR;
	}
	
	ZVAL_RES(&ref, stream->res);
	async_stream_shutdown(data->astream, flag, &ref);
	
	return SUCCESS;
}

ASYNC_CALLBACK connect_timer_cb(uv_timer_t *timer)
{
	async_pipe *pipe;
	
	pipe = (async_pipe *) timer->data;

	if (pipe->flags & ASYNC_PIPE_FLAG_INIT) {
		ASYNC_UV_CLOSE(&pipe->handle, NULL);
	}
}

static int async_pipe_xport_api(php_stream *stream, async_pipe *pipe, php_stream_xport_param *xparam STREAMS_DC)
{
	switch (xparam->op) {
	case STREAM_XPORT_OP_ACCEPT:
		xparam->outputs.returncode = FAILURE;
		break;
	case STREAM_XPORT_OP_BIND:
		xparam->outputs.returncode = FAILURE;
		break;
	case STREAM_XPORT_OP_CONNECT:
	case STREAM_XPORT_OP_CONNECT_ASYNC:
		{
			async_context *context;
			async_uv_op *op;
			uv_connect_t req;
			uint64_t timeout;
			int code;

			pipe->name = zend_string_init(xparam->inputs.name, xparam->inputs.namelen, 0);

			pipe->flags |= ASYNC_PIPE_FLAG_INIT;
			uv_pipe_connect(&req, &pipe->handle, ZSTR_VAL(pipe->name), connect_cb);
		
			ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op));
			
			req.data = op;
		
			timeout = ((uint64_t) xparam->inputs.timeout->tv_sec) * 1000 + ((uint64_t) xparam->inputs.timeout->tv_usec) / 1000;
		
			if (timeout > 0) {
				uv_timer_start(&pipe->timer, connect_timer_cb, timeout, 0);
			}

			context = async_context_get();
	
			if (!async_context_is_background(context) && 1 == ++pipe->astream->ref_count) {
				uv_ref((uv_handle_t *) &pipe->handle);
			}

			code = async_await_op((async_op *) op);

			if (timeout > 0) {
				uv_timer_stop(&pipe->timer);
			}

			if (!async_context_is_background(context) && 0 == --pipe->astream->ref_count) {
				uv_unref((uv_handle_t *) &pipe->handle);
			}
			
			if (UNEXPECTED(code == FAILURE)) {
				ASYNC_DELREF(&pipe->std);
				ASYNC_FORWARD_OP_ERROR(op);
				ASYNC_FREE_OP(op);
			} else {
				code = op->code;
				
				ASYNC_FREE_OP(op);
				
				if (UNEXPECTED(code < 0)) {
					xparam->outputs.returncode = FAILURE;
					php_error_docref(NULL, E_WARNING, "Failed to connect pipe: %s", uv_strerror(code));
					ASYNC_DELREF(&pipe->std);
				}
			}
		}
		break;
	case STREAM_XPORT_OP_GET_NAME:
	case STREAM_XPORT_OP_GET_PEER_NAME:
		xparam->outputs.returncode = FAILURE;
		break;
	case STREAM_XPORT_OP_LISTEN:
		xparam->outputs.returncode = FAILURE;
		break;
	case STREAM_XPORT_OP_RECV:
		xparam->outputs.returncode = FAILURE;
		break;
	case STREAM_XPORT_OP_SEND:
		xparam->outputs.returncode = FAILURE;
		break;
	case STREAM_XPORT_OP_SHUTDOWN:
		xparam->outputs.returncode = unix_socket_shutdown(stream, pipe, xparam->how);
		break;
	}
	
	return PHP_STREAM_OPTION_RETURN_OK;
}

static int async_pipe_set_option(php_stream *stream, int option, int value, void *ptrparam)
{
	async_pipe *data;
	
	data = (async_pipe *) stream->abstract;
	
	switch (option) {
	case PHP_STREAM_OPTION_XPORT_API:
		return async_pipe_xport_api(stream, data, (php_stream_xport_param *) ptrparam STREAMS_CC);
	case PHP_STREAM_OPTION_META_DATA_API:
		add_assoc_bool((zval *) ptrparam, "timed_out", (data->flags & ASYNC_PIPE_FLAG_TIMED_OUT) ? 1 : 0);
		add_assoc_bool((zval *) ptrparam, "blocked", (data->flags & ASYNC_PIPE_FLAG_BLOCKING) ? 1 : 0);
		add_assoc_bool((zval *) ptrparam, "eof", 0);
		
		return PHP_STREAM_OPTION_RETURN_OK;
	case PHP_STREAM_OPTION_BLOCKING:
		if (value) {
			data->flags |= ASYNC_PIPE_FLAG_BLOCKING;
		} else {
			data->flags = (data->flags | ASYNC_PIPE_FLAG_BLOCKING) ^ ASYNC_PIPE_FLAG_BLOCKING;
		}

		return PHP_STREAM_OPTION_RETURN_OK;
	case PHP_STREAM_OPTION_CHECK_LIVENESS:		
		if (EXPECTED(data->astream != NULL && async_socket_is_alive(data->astream))) {
			return PHP_STREAM_OPTION_RETURN_OK;
		}
		
		return PHP_STREAM_OPTION_RETURN_ERR;
	case PHP_STREAM_OPTION_READ_TIMEOUT: {		
		struct timeval tv = *(struct timeval *) ptrparam;
		
		data->timeout = ((uint64_t) tv.tv_sec * 1000) + (uint64_t) (tv.tv_usec / 1000);
		
		if (data->timeout == 0 && tv.tv_usec > 0) {
			data->timeout = 1;
		}

		return PHP_STREAM_OPTION_RETURN_OK;
	}
	case PHP_STREAM_OPTION_READ_BUFFER:
		if (value == PHP_STREAM_BUFFER_NONE) {
			stream->readbuf = perealloc(stream->readbuf, 0, stream->is_persistent);
			stream->flags |= PHP_STREAM_FLAG_NO_BUFFER;
		} else {
			stream->readbuflen = MAX(*((size_t *) ptrparam), 0x8000);
			stream->readbuf = perealloc(stream->readbuf, stream->readbuflen, stream->is_persistent);
			stream->flags &= ~PHP_STREAM_FLAG_NO_BUFFER;
		}

		return PHP_STREAM_OPTION_RETURN_OK;
	}

	return PHP_STREAM_OPTION_RETURN_NOTIMPL;
}

void async_pipe_populate_ops(php_stream_ops *ops, const char *label)
{
	ops->write = async_pipe_write;
	ops->read = async_pipe_read;
	ops->close = async_pipe_close;
	ops->flush = async_pipe_flush;
	ops->label = label;
	ops->seek = NULL;
	ops->cast = async_pipe_cast;
	ops->stat = async_pipe_stat;
	ops->set_option = async_pipe_set_option;
}

void async_unix_socket_init()
{
	async_pipe_populate_ops(&unix_socket_ops, "unix_socket/async");

	if (ASYNC_G(unix_enabled)) {
		orig_unix_factory = async_xp_socket_register("unix", unix_socket_factory);
	}
	
	php_stream_xport_register("async-unix", unix_socket_factory);
}

void async_unix_socket_shutdown()
{
	php_stream_xport_unregister("async-unix");

	if (ASYNC_G(unix_enabled)) {
		php_stream_xport_register("unix", orig_unix_factory);
	}
}
