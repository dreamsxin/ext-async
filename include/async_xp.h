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

#ifndef ASYNC_XP_H
#define ASYNC_XP_H

#include "async_ssl.h"
#include "async_stream.h"

#define ASYNC_XP_SOCKET_SSL_OPT(stream, name, val) \
	(PHP_STREAM_CONTEXT(stream) && ((val) = php_stream_context_get_option(PHP_STREAM_CONTEXT(stream), "ssl", name)) != NULL)

#define ASYNC_XP_SOCKET_SSL_OPT_STRING(stream, name, str) do { \
	zval *v; \
	if (ASYNC_XP_SOCKET_SSL_OPT(stream, name, v)) { \
		convert_to_string_ex(v); \
		(str) = Z_STRVAL_P(v); \
		zval_ptr_dtor(v); \
	} \
} while (0)

#define ASYNC_XP_SOCKET_SSL_OPT_LONG(stream, name, num) do { \
	zval *v; \
	if (ASYNC_XP_SOCKET_SSL_OPT(stream, name, v)) { \
		(num) = zval_get_long(v); \
		zval_ptr_dtor(v); \
	} \
} while (0)

#define ASYNC_XP_SOCKET_FLAG_BLOCKING 1
#define ASYNC_XP_SOCKET_FLAG_DGRAM 2
#define ASYNC_XP_SOCKET_FLAG_ACCEPTED 4

#define ASYNC_XP_SOCKET_SHUT_RD 0
#define ASYNC_XP_SOCKET_SHUT_WR 1
#define ASYNC_XP_SOCKET_SHUT_RDWR 2

typedef struct _async_xp_socket_data async_xp_socket_data;

#define ASYNC_XP_SOCKET_DATA_BASE \
	php_stream *stream; \
	async_task_scheduler *scheduler; \
	async_stream *astream; \
	uint8_t flags; \
	zend_string *peer; \
    int (* connect)(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam); \
    int (* bind)(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam); \
    int (* listen)(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam); \
    int (* accept)(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam); \
    int (* shutdown)(async_xp_socket_data *data, int how); \
    size_t (* write)(php_stream *stream, async_xp_socket_data *data, const char *buf, size_t count); \
    size_t (* read)(php_stream *stream, async_xp_socket_data *data, char *buf, size_t count); \
    int (* send)(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam); \
    int (* receive)(php_stream *stream, async_xp_socket_data *data, php_stream_xport_param *xparam); \
    int (* get_peer)(async_xp_socket_data *data, zend_bool remote, zend_string **textaddr, struct sockaddr **addr, socklen_t *len);
    

struct _async_xp_socket_data {
	ASYNC_XP_SOCKET_DATA_BASE
    uv_handle_t handle;
};

char *async_xp_parse_ip(const char *str, size_t str_len, int *portno, int get_err, zend_string **err);

void async_xp_socket_populate_ops(php_stream_ops *ops, const char *label);
php_stream *async_xp_socket_create(async_xp_socket_data *data, php_stream_ops *ops, const char *pid STREAMS_DC);

php_stream_transport_factory async_xp_socket_register(const char *protocol, php_stream_transport_factory factory);

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
