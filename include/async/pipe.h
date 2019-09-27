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

#ifndef ASYNC_PIPE_H
#define ASYNC_PIPE_H

#include "socket.h"

#define ASYNC_PIPE_FLAG_IPC 1
#define ASYNC_PIPE_FLAG_LAZY (1 << 1)

#define ASYNC_PIPE_FLAG_BLOCKING (1 << 2)
#define ASYNC_PIPE_FLAG_DGRAM (1 << 3)
#define ASYNC_PIPE_FLAG_ACCEPTED (1 << 4)
#define ASYNC_PIPE_FLAG_TIMED_OUT (1 << 5)
#define ASYNC_PIPE_FLAG_INIT (1 << 6)

typedef struct _async_pipe_server {
	/* PHP object handle. */
	zend_object std;
	
	/* Task scheduler being used. */
	async_task_scheduler *scheduler;

	/* UV pipe handle. */
	uv_pipe_t handle;

	uint8_t flags;

	/* Hostname or IP address that was used to establish the connection. */
	zend_string *name;

	/* Number of pending connection attempts queued in the backlog. */
	zend_uchar pending;

	/* Error being used to close the server. */
	zval error;
	
	/* Number of referenced accept operations. */
	zend_uchar ref_count;

	/* Queue of tasks waiting to accept a socket connection. */
	async_op_list accepts;
	
	async_cancel_cb cancel;
} async_pipe_server;

typedef struct _async_pipe {
	/* PHP object handle. */
	zend_object std;

	/* Task scheduler being used. */
	async_task_scheduler *scheduler;

	/* UV pipe handle. */
	uv_pipe_t handle;
	
	uint8_t flags;
	
	async_cancel_cb cancel;

	/* Hostname or IP address that was used to establish the connection. */
	zend_string *name;

	php_stream *stream;

	/* Refers to the (local) server that accepted the pipe connection. */
	async_pipe_server *server;
	
	async_stream *astream;

	/* Error being used to close the read stream. */
	zval read_error;

	/* Error being used to close the write stream. */
	zval write_error;

	uint64_t timeout;
	uv_timer_t timer;
} async_pipe;

async_pipe *async_pipe_init_ipc();

void async_pipe_import_stream(async_pipe *pipe, uv_stream_t *handle);
void async_pipe_export_stream(async_pipe *pipe, uv_stream_t *handle);

extern zend_class_entry *async_pipe_ce;
extern zend_class_entry *async_pipe_server_ce;

void pipe_disposed(uv_handle_t *handle);
void shutdown_pipe(void *arg, zval *error);

async_pipe *async_pipe_object_create(int ipc);
php_stream *async_pipe_object_create2(const char *pid);
void async_pipe_object_dtor(zend_object *object);
void async_pipe_object_destroy(zend_object *object);
void connect_cb(uv_connect_t *req, int status);

extern php_stream_ops unix_socket_ops;
#endif
