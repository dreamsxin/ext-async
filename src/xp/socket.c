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
#include "async_xp.h"

char *async_xp_parse_ip(const char *str, size_t str_len, int *portno, int get_err, zend_string **err)
{
    char *colon;
    char *host = NULL;

#ifdef HAVE_IPV6
    char *p;

    if (*(str) == '[' && str_len > 1) {
        /* IPV6 notation to specify raw address with port (i.e. [fe80::1]:80) */
        p = (char*) memchr(str + 1, ']', str_len - 2);
        
        if (!p || *(p + 1) != ':') {
            if (get_err) {
                *err = strpprintf(0, "Failed to parse IPv6 address \"%s\"", str);
            }
            return NULL;
        }
        
        *portno = atoi(p + 2);
        return estrndup(str + 1, p - str - 1);
    }
#endif

    if (str_len) {
        colon = (char*) memchr(str, ':', str_len - 1);
    } else {
        colon = NULL;
    }
    
    if (colon) {
        *portno = atoi(colon + 1);
        host = estrndup(str, colon - str);
    } else {
        if (get_err) {
            *err = strpprintf(0, "Failed to parse address \"%s\"", str);
        }
        return NULL;
    }

    return host;
}


static size_t async_xp_socket_write(php_stream *stream, const char *buf, size_t count)
{
	async_xp_socket_data *data;

	data = (async_xp_socket_data *) stream->abstract;
	
	if (data->write != NULL) {
		return data->write(stream, data, buf, count);
	}
	
	async_stream_write(data->astream, (char *) buf, count);
	
	return count;
}

static size_t async_xp_socket_read(php_stream *stream, char *buf, size_t count)
{
	async_xp_socket_data *data;
	
	int code;
	
	data = (async_xp_socket_data *) stream->abstract;
	
	if (data->read != NULL) {
		return data->read(stream, data, buf, count);
	}
	
	code = async_stream_read(data->astream, buf, count);
	
	if (code < 1) {
		if (code == 0) {
			
		} else {
			php_error_docref(NULL, E_WARNING, "Socket read failed: %s", uv_strerror(code));
		}
		
		return 0;
	}

	return code;
}

static void close_cb(uv_handle_t *handle)
{
	async_xp_socket_data *data;
	
	data = (async_xp_socket_data *) handle->data;
	
	ZEND_ASSERT(data != NULL);
	
#ifdef HAVE_ASYNC_SSL
	if (data->astream->ssl.ssl != NULL) {
		async_ssl_dispose_engine(&data->astream->ssl, 1);
	}
#endif
	
	async_stream_free(data->astream);
	
	ASYNC_DELREF(&data->scheduler->std);
	
	efree(data);
}

static void close_dgram_cb(uv_handle_t *handle)
{
	async_xp_socket_data *data;
	
	data = (async_xp_socket_data *) handle->data;
	
	ZEND_ASSERT(data != NULL);
	
	ASYNC_DELREF(&data->scheduler->std);
	
	efree(data);
}

static int async_xp_socket_close(php_stream *stream, int close_handle)
{
	async_xp_socket_data *data;
	
	data = (async_xp_socket_data *) stream->abstract;
	
	if (data->astream == NULL) {
		uv_close(&data->handle, close_dgram_cb);
	} else {
		async_stream_close(data->astream, close_cb, data);
	}
	
	return 0;
}

static int async_xp_socket_flush(php_stream *stream)
{
    return 0;
}

static int async_xp_socket_cast(php_stream *stream, int castas, void **ret)
{
	return FAILURE;
}

static int async_xp_socket_stat(php_stream *stream, php_stream_statbuf *ssb)
{
	return FAILURE;
}

static inline int async_xp_socket_xport_api(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam STREAMS_DC)
{
	switch (xparam->op) {
	case STREAM_XPORT_OP_ACCEPT:
		if (data->accept == NULL) {
			xparam->outputs.returncode = FAILURE;
		} else {
			xparam->outputs.returncode = data->accept(stream, data, xparam);
			
			if (xparam->outputs.returncode == SUCCESS) {
				ZEND_ASSERT(((async_xp_socket_data *) xparam->outputs.client->abstract)->astream != NULL);
			}
		}
		break;
	case STREAM_XPORT_OP_BIND:
		if (data->bind == NULL) {
			xparam->outputs.returncode = FAILURE;
		} else {
			xparam->outputs.returncode = data->bind(stream, data, xparam);
		}
		break;
	case STREAM_XPORT_OP_CONNECT:
	case STREAM_XPORT_OP_CONNECT_ASYNC:
		if (data->connect == NULL) {
			xparam->outputs.returncode = FAILURE;
		} else {
			xparam->outputs.returncode = data->connect(stream, data, xparam);
			
			if (xparam->outputs.returncode == SUCCESS) {
				ZEND_ASSERT(data->astream != NULL);
			}
		}
		break;
	case STREAM_XPORT_OP_GET_NAME:
	case STREAM_XPORT_OP_GET_PEER_NAME:
		xparam->outputs.returncode = data->get_peer(
			data,
			(xparam->op == STREAM_XPORT_OP_GET_NAME) ? 0 : 1,
			xparam->want_textaddr ? &xparam->outputs.textaddr : NULL,
			xparam->want_addr ? &xparam->outputs.addr : NULL,
			xparam->want_addr ? &xparam->outputs.addrlen : NULL
		);
		break;
	case STREAM_XPORT_OP_LISTEN:
		if (data->listen == NULL) {
			xparam->outputs.returncode = FAILURE;
		} else {
			xparam->outputs.returncode = data->listen(stream, data, xparam);
		}
		break;
	case STREAM_XPORT_OP_RECV:
		if (data->receive == NULL) {
			xparam->outputs.returncode = FAILURE;
		} else {
			xparam->outputs.returncode = data->receive(stream, data, xparam);
		}
		break;
	case STREAM_XPORT_OP_SEND:
		if (data->send == NULL) {
			xparam->outputs.returncode = FAILURE;
		} else {
			xparam->outputs.returncode = data->send(stream, data, xparam);
		}
		break;
	case STREAM_XPORT_OP_SHUTDOWN:
		if (data->shutdown == NULL) {
			xparam->outputs.returncode = SUCCESS;
		} else {
			xparam->outputs.returncode = data->shutdown(data, xparam->how);
		}
		break;
	}
	
	return PHP_STREAM_OPTION_RETURN_OK;
}

static int async_xp_socket_set_option(php_stream *stream, int option, int value, void *ptrparam)
{
	async_xp_socket_data *data;
	
	data = (async_xp_socket_data *) stream->abstract;
	
	switch (option) {
	case PHP_STREAM_OPTION_XPORT_API:
		return async_xp_socket_xport_api(stream, data, (php_stream_xport_param *) ptrparam STREAMS_CC);
	case PHP_STREAM_OPTION_META_DATA_API:
		add_assoc_bool((zval *) ptrparam, "timed_out", 0);
		add_assoc_bool((zval *) ptrparam, "blocked", (data->flags & ASYNC_XP_SOCKET_FLAG_BLOCKING) ? 1 : 0);
		
		if (data->astream == NULL) {
			add_assoc_bool((zval *) ptrparam, "eof", 0);
		} else {
			add_assoc_bool((zval *) ptrparam, "eof", (data->astream->flags & ASYNC_STREAM_EOF && data->astream->buffer.len == 0) ? 1 : 0);
		}
		
		return PHP_STREAM_OPTION_RETURN_OK;
	case PHP_STREAM_OPTION_BLOCKING:
		if (value) {
			data->flags |= ASYNC_XP_SOCKET_FLAG_BLOCKING;
		} else {
			data->flags = (data->flags | ASYNC_XP_SOCKET_FLAG_BLOCKING) ^ ASYNC_XP_SOCKET_FLAG_BLOCKING;
		}

		return PHP_STREAM_OPTION_RETURN_OK;
	case PHP_STREAM_OPTION_READ_BUFFER:
		if (value == PHP_STREAM_BUFFER_NONE) {
			stream->readbuf = perealloc(stream->readbuf, 0, stream->is_persistent);
			stream->flags |= PHP_STREAM_FLAG_NO_BUFFER;
		} else {
			stream->readbuflen = MAX(*((size_t *) ptrparam), 0x8000);
			stream->readbuf = perealloc(stream->readbuf, stream->readbuflen, stream->is_persistent);
			stream->flags ^= PHP_STREAM_FLAG_NO_BUFFER;
		}

		return PHP_STREAM_OPTION_RETURN_OK;
	}

	return PHP_STREAM_OPTION_RETURN_NOTIMPL;
}

void async_xp_socket_populate_ops(php_stream_ops *ops, const char *label)
{
	ops->write = async_xp_socket_write;
	ops->read = async_xp_socket_read;
	ops->close = async_xp_socket_close;
	ops->flush = async_xp_socket_flush;
	ops->label = label;
	ops->seek = NULL;
	ops->cast = async_xp_socket_cast;
	ops->stat = async_xp_socket_stat;
	ops->set_option = async_xp_socket_set_option;
}

php_stream *async_xp_socket_create(async_xp_socket_data *data, php_stream_ops *ops, const char *pid STREAMS_DC)
{
	async_task_scheduler *scheduler;
	php_stream *stream;
	
	stream = php_stream_alloc_rel(ops, data, pid, "r+");
	
	if (stream == NULL) {
		return NULL;
	}
	
	scheduler = async_task_scheduler_get();
	
	data->scheduler = scheduler;
	data->stream = stream;
	data->flags |= ASYNC_XP_SOCKET_FLAG_BLOCKING;
	data->handle.data = data;
	
	ASYNC_ADDREF(&scheduler->std);
 	
	return stream;
}

php_stream_transport_factory async_xp_socket_register(const char *protocol, php_stream_transport_factory factory)
{
	php_stream_transport_factory prev;
	HashTable *hash;
	
	hash = php_stream_xport_get_hash();
	
	prev = (php_stream_transport_factory) zend_hash_str_find_ptr(hash, ZEND_STRL(protocol));
	php_stream_xport_register(protocol, factory);
	
	return prev;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
