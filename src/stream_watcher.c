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

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_stream_watcher_ce;

static zend_object_handlers async_stream_watcher_handlers;

#if ASYNC_SOCKETS
static int (*le_socket)(void);
#endif


static inline php_socket_t get_poll_fd(zval *val)
{
	php_socket_t fd;
	php_stream *stream;

	stream = (php_stream *) zend_fetch_resource_ex(val, NULL, php_file_le_stream());

#if ASYNC_SOCKETS
	php_socket *socket;

	if (!stream && le_socket && (socket = (php_socket *) zend_fetch_resource_ex(val, NULL, php_sockets_le_socket()))) {
		return socket->bsd_socket;
	}
#endif

	if (!stream) {
		return -1;
	}

	if (stream->wrapper) {
		if (!strcmp((char *)stream->wrapper->wops->label, "PHP")) {
			if (!stream->orig_path || (strncmp(stream->orig_path, "php://std", sizeof("php://std") - 1) && strncmp(stream->orig_path, "php://fd", sizeof("php://fd") - 1))) {
				return -1;
			}
		}
	}

	if (php_stream_cast(stream, PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL, (void *) &fd, 1) != SUCCESS) {
		return -1;
	}

	if (fd < 1) {
		return -1;
	}

	if (stream->wrapper && !strcmp((char *) stream->wrapper->wops->label, "plainfile")) {
#ifndef PHP_WIN32
		struct stat stat;
		fstat(fd, &stat);

		if (!S_ISFIFO(stat.st_mode)) {
			return -1;
		}
#else
		return -1;
#endif
	}

	return fd;
}

static void trigger_poll(uv_poll_t *handle, int status, int events)
{
	async_stream_watcher *watcher;

	zval result;

	watcher = (async_stream_watcher *) handle->data;

	ZEND_ASSERT(watcher != NULL);

	ZVAL_NULL(&result);

	if (events & (UV_READABLE | UV_DISCONNECT)) {
		async_awaitable_trigger_continuation(&watcher->reads, &result, 1);
	}

	if (events & (UV_WRITABLE | UV_DISCONNECT)) {
		async_awaitable_trigger_continuation(&watcher->writes, &result, 1);
	}
}

static void enable_poll(void *obj)
{
	async_stream_watcher *watcher;

	watcher = (async_stream_watcher *) obj;

	ZEND_ASSERT(watcher != NULL);

	watcher->events = watcher->new_events;

	if (watcher->events & (UV_READABLE | UV_WRITABLE)) {
		uv_poll_start(&watcher->poll, watcher->events, trigger_poll);
	} else {
		uv_poll_stop(&watcher->poll);
	}

	OBJ_RELEASE(&watcher->std);
}

static void close_poll(uv_handle_t *handle)
{
	async_stream_watcher *watcher;

	watcher = (async_stream_watcher *) handle->data;

	ZEND_ASSERT(watcher != NULL);

	OBJ_RELEASE(&watcher->scheduler->std);
	OBJ_RELEASE(&watcher->std);
}

static inline void suspend(async_stream_watcher *watcher, async_awaitable_queue *q, zval *return_value, zend_execute_data *execute_data)
{
	async_context *context;
	zend_bool cancelled;

	context = async_context_get();

	if (context->background) {
		watcher->unref_count++;

		if (watcher->unref_count == 1 && !watcher->ref_count) {
			uv_unref((uv_handle_t *) &watcher->poll);
		}
	} else {
		watcher->ref_count++;

		if (watcher->ref_count == 1 && watcher->unref_count) {
			uv_ref((uv_handle_t *) &watcher->poll);
		}
	}

	async_task_suspend(q, NULL, execute_data, &cancelled);

	if (context->background) {
		watcher->unref_count--;

		if (watcher->unref_count == 0 && watcher->ref_count) {
			uv_ref((uv_handle_t *) &watcher->poll);
		}
	} else {
		watcher->ref_count--;

		if (watcher->ref_count == 0 && watcher->unref_count) {
			uv_unref((uv_handle_t *) &watcher->poll);
		}
	}
}


static zend_object *async_stream_watcher_object_create(zend_class_entry *ce)
{
	async_stream_watcher *watcher;

	watcher = emalloc(sizeof(async_stream_watcher));
	ZEND_SECURE_ZERO(watcher, sizeof(async_stream_watcher));

	zend_object_std_init(&watcher->std, ce);
	watcher->std.handlers = &async_stream_watcher_handlers;

	ZVAL_UNDEF(&watcher->error);
	ZVAL_NULL(&watcher->resource);

	watcher->enable.object = watcher;
	watcher->enable.func = enable_poll;

	return &watcher->std;
}

static void async_stream_watcher_object_dtor(zend_object *object)
{
	async_stream_watcher *watcher;

	watcher = (async_stream_watcher *) object;

	if (watcher->enable.active) {
		ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
		watcher->enable.active = 0;
	}

	if (!uv_is_closing((uv_handle_t *) &watcher->poll)) {
		GC_ADDREF(&watcher->std);

		uv_close((uv_handle_t *) &watcher->poll, close_poll);
	}
}

static void async_stream_watcher_object_destroy(zend_object *object)
{
	async_stream_watcher *watcher;

	watcher = (async_stream_watcher *) object;

	zval_ptr_dtor(&watcher->error);
	zval_ptr_dtor(&watcher->resource);

	zend_object_std_dtor(&watcher->std);
}

ZEND_METHOD(StreamWatcher, __construct)
{
	async_stream_watcher *watcher;
	php_socket_t fd;

	zval *val;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	watcher = (async_stream_watcher *) Z_OBJ_P(getThis());

	fd = get_poll_fd(val);

	ASYNC_CHECK_ERROR(fd < 0, "Cannot cast resource to file descriptor");

	watcher->fd = fd;
	watcher->scheduler = async_task_scheduler_get();

	GC_ADDREF(&watcher->scheduler->std);

	ZVAL_COPY(&watcher->resource, val);

#ifdef PHP_WIN32
	uv_poll_init_socket(&watcher->scheduler->loop, &watcher->poll, (uv_os_sock_t) fd);
#else
	uv_poll_init(&watcher->scheduler->loop, &watcher->poll, fd);
#endif

	watcher->poll.data = watcher;
}

ZEND_METHOD(StreamWatcher, close)
{
	async_stream_watcher *watcher;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	watcher = (async_stream_watcher *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&watcher->error) != IS_UNDEF) {
		return;
	}

	if (watcher->enable.active) {
		ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
		watcher->enable.active = 0;
	} else {
		GC_ADDREF(&watcher->std);
	}

	uv_close((uv_handle_t *) &watcher->poll, close_poll);

	zend_throw_error(NULL, "IO watcher has been closed");

	ZVAL_OBJ(&watcher->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&watcher->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&watcher->reads, &watcher->error, 0);
	async_awaitable_trigger_continuation(&watcher->writes, &watcher->error, 0);
}

ZEND_METHOD(StreamWatcher, awaitReadable)
{
	async_stream_watcher *watcher;
	int events;

	ZEND_PARSE_PARAMETERS_NONE();

	watcher = (async_stream_watcher *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&watcher->error) != IS_UNDEF) {
		Z_ADDREF_P(&watcher->error);

		execute_data->opline--;
		zend_throw_exception_internal(&watcher->error);
		execute_data->opline++;

		return;
	}

	ZEND_ASSERT(zend_rsrc_list_get_rsrc_type(Z_RES_P(&watcher->resource)));

	events = UV_READABLE | UV_DISCONNECT;

	if (watcher->writes.first != NULL) {
		events |= UV_WRITABLE;
	}

	watcher->new_events = events;

	if (events != watcher->events) {
		if (!watcher->enable.active) {
			GC_ADDREF(&watcher->std);

			async_task_scheduler_enqueue_enable(watcher->scheduler, &watcher->enable);
		}
	} else if (watcher->enable.active) {
		ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
		watcher->enable.active = 0;

		OBJ_RELEASE(&watcher->std);
	}

	suspend(watcher, &watcher->reads, return_value, execute_data);

	if (Z_TYPE_P(&watcher->error) == IS_UNDEF) {
		if (watcher->reads.first == NULL) {
			if (watcher->writes.first == NULL) {
				watcher->new_events = 0;
			} else {
				watcher->new_events = UV_WRITABLE | UV_DISCONNECT;
			}
		}

		if (watcher->new_events != watcher->events) {
			if (!watcher->enable.active) {
				GC_ADDREF(&watcher->std);

				async_task_scheduler_enqueue_enable(watcher->scheduler, &watcher->enable);
			}
		} else if (watcher->enable.active) {
			ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
			watcher->enable.active = 0;

			OBJ_RELEASE(&watcher->std);
		}
	}
}

ZEND_METHOD(StreamWatcher, awaitWritable)
{
	async_stream_watcher *watcher;
	int events;

	ZEND_PARSE_PARAMETERS_NONE();

	watcher = (async_stream_watcher *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&watcher->error) != IS_UNDEF) {
		Z_ADDREF_P(&watcher->error);

		execute_data->opline--;
		zend_throw_exception_internal(&watcher->error);
		execute_data->opline++;

		return;
	}

	ZEND_ASSERT(zend_rsrc_list_get_rsrc_type(Z_RES_P(&watcher->resource)));

	events = UV_WRITABLE | UV_DISCONNECT;

	if (watcher->reads.first != NULL) {
		events |= UV_READABLE;
	}

	watcher->new_events = events;

	if (events != watcher->events) {
		if (!watcher->enable.active) {
			GC_ADDREF(&watcher->std);

			async_task_scheduler_enqueue_enable(watcher->scheduler, &watcher->enable);
		}
	} else if (watcher->enable.active) {
		ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
		watcher->enable.active = 0;

		OBJ_RELEASE(&watcher->std);
	}

	suspend(watcher, &watcher->writes, return_value, execute_data);

	if (Z_TYPE_P(&watcher->error) == IS_UNDEF) {
		if (watcher->writes.first == NULL) {
			if (watcher->reads.first == NULL) {
				watcher->new_events = 0;
			} else {
				watcher->new_events = UV_READABLE | UV_DISCONNECT;
			}
		}

		if (watcher->new_events != watcher->events) {
			if (!watcher->enable.active) {
				GC_ADDREF(&watcher->std);

				async_task_scheduler_enqueue_enable(watcher->scheduler, &watcher->enable);
			}
		} else if (watcher->enable.active) {
			ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
			watcher->enable.active = 0;

			OBJ_RELEASE(&watcher->std);
		}
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_stream_watcher_ctor, 0, 0, 1)
	ZEND_ARG_INFO(0, resource)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_watcher_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_watcher_await_readable, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_stream_watcher_await_writable, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_stream_watcher_functions[] = {
	ZEND_ME(StreamWatcher, __construct, arginfo_stream_watcher_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(StreamWatcher, close, arginfo_stream_watcher_close, ZEND_ACC_PUBLIC)
	ZEND_ME(StreamWatcher, awaitReadable, arginfo_stream_watcher_await_readable, ZEND_ACC_PUBLIC)
	ZEND_ME(StreamWatcher, awaitWritable, arginfo_stream_watcher_await_writable, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_stream_watcher_ce_register()
{
#if ASYNC_SOCKETS
	zend_module_entry *sockets;
#endif

	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\StreamWatcher", async_stream_watcher_functions);
	async_stream_watcher_ce = zend_register_internal_class(&ce);
	async_stream_watcher_ce->ce_flags |= ZEND_ACC_FINAL;
	async_stream_watcher_ce->create_object = async_stream_watcher_object_create;
	async_stream_watcher_ce->serialize = zend_class_serialize_deny;
	async_stream_watcher_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_stream_watcher_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_stream_watcher_handlers.free_obj = async_stream_watcher_object_destroy;
	async_stream_watcher_handlers.dtor_obj = async_stream_watcher_object_dtor;
	async_stream_watcher_handlers.clone_obj = NULL;

#if ASYNC_SOCKETS
	le_socket = NULL;

	if ((sockets = zend_hash_str_find_ptr(&module_registry, ZEND_STRL("sockets")))) {
		if (sockets->handle) { // shared
			le_socket = (int (*)(void)) DL_FETCH_SYMBOL(sockets->handle, "php_sockets_le_socket");

			if (le_socket == NULL) {
				le_socket = (int (*)(void)) DL_FETCH_SYMBOL(sockets->handle, "_php_sockets_le_socket");
			}
		}
	}
#endif
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
