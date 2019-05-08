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
#include "async_pipe.h"

#include "SAPI.h"
#include "php_main.h"
#include "zend_inheritance.h"

ASYNC_API zend_class_entry *async_thread_ce;

static zend_object_handlers async_thread_handlers;

static zend_string *str_main;

typedef int (*php_sapi_deactivate_t)(void);

static php_sapi_deactivate_t sapi_deactivate_func;

#define ASYNC_THREAD_FLAG_RUNNING 1
#define ASYNC_THREAD_FLAG_TERMINATED (1 << 1)

typedef struct {
	zend_object std;
	
	uint16_t flags;
	
	async_task_scheduler *scheduler;
	async_cancel_cb shutdown;
	
	uv_thread_t impl;
	uv_async_t handle;
	
	async_pipe *master;
	async_pipe *slave;
	
#ifdef PHP_WIN32
	uv_pipe_t server;
	async_uv_op accept;
	async_uv_op connect;
	char ipc[128];
#else
	uv_file pipes[2];
#endif
	
	zend_string *bootstrap;
	zend_string *message;
	zend_string *file;
	zend_long line;
	
	zval error;
	
	async_op_list join;
} async_thread;

#define GET_ERROR_BASE_CE(obj) (instanceof_function(Z_OBJCE_P(obj), zend_ce_exception) ? zend_ce_exception : zend_ce_error)
#define GET_ERROR_PROP(obj, name, retval) zend_read_property_ex(GET_ERROR_BASE_CE(obj), obj, ZSTR_KNOWN(name), 1, retval)


static void run_bootstrap(async_thread *thread)
{
	zend_file_handle handle;
	zend_op_array *ops;
	
	zend_string *str;
	zval error;	
	zval retval;
	
	if (SUCCESS != php_stream_open_for_zend_ex(ZSTR_VAL(thread->bootstrap), &handle, USE_PATH | REPORT_ERRORS | STREAM_OPEN_FOR_INCLUDE)) {
		return;
	}
	
	if (!handle.opened_path) {
		handle.opened_path = zend_string_dup(thread->bootstrap, 0);
	}
	
	zend_hash_add_empty_element(&EG(included_files), handle.opened_path);
	
	ops = zend_compile_file(&handle, ZEND_REQUIRE);
	zend_destroy_file_handle(&handle);
	
	if (ops) {
		ZVAL_UNDEF(&retval);
		zend_execute(ops, &retval);
		destroy_op_array(ops);
		efree(ops);
		
		if (!EG(exception)) {
			zval_ptr_dtor(&retval);
		}
	}
	
	if (EG(exception)) {
		ZVAL_OBJ(&error, EG(exception));
		
		if (NULL != (str = zval_get_string(GET_ERROR_PROP(&error, ZEND_STR_MESSAGE, &retval)))) {
			thread->message = zend_string_init(ZSTR_VAL(str), ZSTR_LEN(str), 1);
			zend_string_release(str);
		}
		
		if (NULL != (str = zval_get_string(GET_ERROR_PROP(&error, ZEND_STR_FILE, &retval)))) {
			thread->file = zend_string_init(ZSTR_VAL(str), ZSTR_LEN(str), 1);
			zend_string_release(str);
		}
		
		thread->line = zval_get_long(GET_ERROR_PROP(&error, ZEND_STR_LINE, &retval));
		
		zend_clear_exception();
	}
}

ASYNC_CALLBACK run_thread(void *arg)
{
	async_thread *thread;
	
	thread = (async_thread *) arg;
	
	ts_resource(0);

	TSRMLS_CACHE_UPDATE();
	
	PG(expose_php) = 0;
	PG(auto_globals_jit) = 1;
	
	php_request_startup();
	
	PG(during_request_startup) = 0;
	SG(sapi_started) = 0;
	SG(headers_sent) = 1;
	SG(request_info).no_headers = 1;
	
	ASYNC_G(cli) = 1;
	ASYNC_G(thread) = &thread->std;
	
	zend_first_try {
		run_bootstrap(thread);
	} zend_catch {
		ASYNC_G(exit) = 1;
	} zend_end_try();
	
	if (async_stream_call_close_obj(&thread->slave->std)) {
		ASYNC_DELREF(&thread->slave->std);
		thread->slave = NULL;
	}
	
	php_request_shutdown(NULL);
	ts_free_thread();
	
	uv_async_send(&thread->handle);
}

#ifdef PHP_WIN32

ASYNC_CALLBACK ipc_close_cb(uv_handle_t *handle)
{
	async_thread *thread;
	
	thread = (async_thread *) handle->data;
	
	ZEND_ASSERT(thread != NULL);
	
	ASYNC_DELREF(&thread->std);
}

#endif

ASYNC_CALLBACK shutdown_cb(void *arg, zval *error)
{
	async_thread *thread;
	
	thread = (async_thread *) arg;
	
	thread->shutdown.func = NULL;
	
	async_stream_call_close_obj(&thread->master->std);
	
#ifdef PHP_WIN32
	if (!uv_is_closing((uv_handle_t *) &thread->server)) {
		ASYNC_ADDREF(&thread->std);
		
		uv_close((uv_handle_t *) &thread->server, ipc_close_cb);
	}
#endif
}

ASYNC_CALLBACK close_async_cb(uv_handle_t *handle)
{
	async_thread *thread;
	
	thread = (async_thread *) handle->data;
	
	ZEND_ASSERT(thread != NULL);
	
	ASYNC_DELREF(&thread->std);
}

ASYNC_CALLBACK notify_thread_cb(uv_async_t *handle)
{
	async_thread *thread;
	
	zval val;
	
	thread = (async_thread *) handle->data;
	
	ZEND_ASSERT(thread != NULL);
	
	thread->flags |= ASYNC_THREAD_FLAG_TERMINATED;
	
	if (EXPECTED(thread->shutdown.func)) {
		thread->shutdown.func = NULL;
		
		ASYNC_LIST_REMOVE(&thread->scheduler->shutdown, &thread->shutdown);
	}
	
	if (UNEXPECTED(thread->message != NULL)) {
		ASYNC_PREPARE_ERROR(&thread->error, "%s", ZSTR_VAL(thread->message));
		
		if (thread->file != NULL) {
			ZVAL_STR_COPY(&val, thread->file);
			zend_update_property_ex(GET_ERROR_BASE_CE(&thread->error), &thread->error, ZSTR_KNOWN(ZEND_STR_FILE), &val);
		}
		
		ZVAL_LONG(&val, thread->line);
		zend_update_property_ex(GET_ERROR_BASE_CE(&thread->error), &thread->error, ZSTR_KNOWN(ZEND_STR_LINE), &val);
		
		while (thread->join.first != NULL) {
			ASYNC_FAIL_OP(thread->join.first, &thread->error);
		}
	} else {
		while (thread->join.first != NULL) {
			ASYNC_FINISH_OP(thread->join.first);
		}
	}
	
#ifdef PHP_WIN32
	if (thread->accept.base.status == ASYNC_STATUS_RUNNING) {
		thread->accept.code = UV_ECONNREFUSED;
		
		ASYNC_FINISH_OP(&thread->accept);
	}
#else
	closesocket(thread->pipes[1]);
#endif

	if (!uv_is_closing((uv_handle_t *) handle)) {
		uv_close((uv_handle_t *) handle, close_async_cb);
	}
}

static zend_object *async_thread_object_create(zend_class_entry *ce)
{
	async_thread *thread;
	
	thread = ecalloc(1, sizeof(async_thread));
	
	zend_object_std_init(&thread->std, ce);
	thread->std.handlers = &async_thread_handlers;
	
	thread->scheduler = async_task_scheduler_ref();
	
	thread->shutdown.func = shutdown_cb;
	thread->shutdown.object = thread;
	
	ASYNC_LIST_APPEND(&thread->scheduler->shutdown, &thread->shutdown);
	
	uv_async_init(&thread->scheduler->loop, &thread->handle, notify_thread_cb);
	
	thread->handle.data = thread;
	
	ASYNC_ADDREF(&thread->std);

	return &thread->std;
}

static void async_thread_object_dtor(zend_object *object)
{
	async_thread *thread;
	
	thread = (async_thread *) object;
	
	if (thread->shutdown.func != NULL) {
		ASYNC_LIST_REMOVE(&thread->scheduler->shutdown, &thread->shutdown);
		
		thread->shutdown.func(thread, NULL);
	}
}

static void async_thread_object_destroy(zend_object *object)
{
	async_thread *thread;
	
	thread = (async_thread *) object;
	
	if (thread->flags & ASYNC_THREAD_FLAG_RUNNING) {
		thread->flags &= ~ASYNC_THREAD_FLAG_RUNNING;
		
		uv_thread_join(&thread->impl);
	}
	
	if (async_stream_call_close_obj(&thread->master->std)) {
		ASYNC_DELREF(&thread->master->std);
		thread->master = NULL;
	}
	
	async_task_scheduler_unref(thread->scheduler);
	
	zval_ptr_dtor(&thread->error);
	
	if (thread->bootstrap != NULL) {
		zend_string_release(thread->bootstrap);
	}
	
	if (thread->message != NULL) {
		zend_string_release(thread->message);
	}
	
	if (thread->file != NULL) {
		zend_string_release(thread->file);
	}
	
	zend_object_std_dtor(&thread->std);
}

static ZEND_METHOD(Thread, __construct)
{
#ifndef ZTS
	zend_throw_error(NULL, "Threads require PHP to be compiled in thread safe mode (ZTS)");
#else
	async_thread *thread;
	zend_string *file;
	
	char path[MAXPATHLEN];
	int code;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(file)
	ZEND_PARSE_PARAMETERS_END();
	
	ASYNC_CHECK_ERROR(!ASYNC_G(cli), "Threads are only supported if PHP is run from the command line (cli)");
	
	thread = (async_thread *) Z_OBJ_P(getThis());
	
	if (UNEXPECTED(!VCWD_REALPATH(ZSTR_VAL(file), path))) {
		if (!uv_is_closing((uv_handle_t *) &thread->handle)) {
			uv_close((uv_handle_t *) &thread->handle, close_async_cb);
		}

		zend_throw_error(NULL, "Failed to locate thread bootstrap file: %s", ZSTR_VAL(file));
		return;
	}
	
	ASYNC_CHECK_ERROR(!VCWD_REALPATH(ZSTR_VAL(file), path), "Failed to locate thread bootstrap file: %s", ZSTR_VAL(file));
	
	thread->bootstrap = zend_string_init(path, strlen(path), 1);
	
#ifdef PHP_WIN32
	int i;
	
	code = uv_pipe_init(&thread->scheduler->loop, &thread->server, 0);
	
	ASYNC_CHECK_ERROR(code < 0, "Failed to create IPC pipe: %s", uv_strerror(code));
	
	thread->server.data = thread;
	
	for (i = 0; i < 5; i++) {
		sprintf(thread->ipc, "\\\\.\\pipe\\php\\%p-%lu.sock", (void *)(((char *) thread) + i), GetCurrentProcessId());
		
		code = uv_pipe_bind(&thread->server, thread->ipc);
		
		if (EXPECTED(code != UV_EADDRINUSE)) {
			break;
		}
	}
	
	if (UNEXPECTED(code != 0)) {
		if (!uv_is_closing((uv_handle_t *) &thread->handle)) {
			uv_close((uv_handle_t *) &thread->handle, close_async_cb);
		}
	
		zend_throw_error(NULL, "Failed to create IPC pipe: %s", uv_strerror(code));
		return;
	}
	
	uv_unref((uv_handle_t *) &thread->server);
	uv_pipe_pending_instances(&thread->server, 1);
#else
	code = socketpair(AF_UNIX, SOCK_STREAM, 0, thread->pipes);
	
	if (UNEXPECTED(code == -1)) {
		if (!uv_is_closing((uv_handle_t *) &thread->handle)) {
			uv_close((uv_handle_t *) &thread->handle, close_async_cb);
		}
	
		zend_throw_error(NULL, "Failed to create IPC pipe: %s", uv_strerror(uv_translate_sys_error(errno)));
		return;
	}
#endif
	
	thread->flags |= ASYNC_THREAD_FLAG_RUNNING;
	
	code = uv_thread_create(&thread->impl, run_thread, thread);
	
	if (UNEXPECTED(code < 0)) {
		if (!uv_is_closing((uv_handle_t *) &thread->handle)) {
			uv_close((uv_handle_t *) &thread->handle, close_async_cb);
		}
	
		zend_throw_error(NULL, "Failed to create thread: %s", uv_strerror(code));
		return;
	}
#endif
}

static ZEND_METHOD(Thread, isAvailable)
{
	ZEND_PARSE_PARAMETERS_NONE();
	
#ifdef ZTS
	RETURN_BOOL(ASYNC_G(cli));
#else
	RETURN_FALSE;
#endif
}

static ZEND_METHOD(Thread, isWorker)
{
	ZEND_PARSE_PARAMETERS_NONE();
	
	RETURN_BOOL(ASYNC_G(thread) != NULL);
}

#ifdef PHP_WIN32

ASYNC_CALLBACK ipc_connect_cb(uv_connect_t *req, int status)
{
	async_thread *thread;
	
	thread = (async_thread *) req->data;
	
	ZEND_ASSERT(thread != NULL);
	
	thread->connect.code = status;
	
	ASYNC_FINISH_OP(&thread->connect);
}

#endif

static ZEND_METHOD(Thread, connect)
{
	async_thread *thread;
	async_pipe *pipe;
	
	int code;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	ASYNC_CHECK_ERROR(ASYNC_G(thread) == NULL, "Only threads can connect to the parent process");
	
	thread = (async_thread *) ASYNC_G(thread);
	
	if (thread->slave != NULL) {
		ASYNC_ADDREF(&thread->slave->std);
		RETURN_OBJ(&thread->slave->std);
	}
	
#ifdef PHP_WIN32
	uv_connect_t *req;
	
	pipe = async_pipe_init_ipc();
	
	req = emalloc(sizeof(uv_connect_t));
	req->data = thread;
	
	uv_pipe_connect(req, &pipe->handle, thread->ipc, ipc_connect_cb);
	
	if (UNEXPECTED(FAILURE == async_await_op((async_op *) &thread->connect))) {
		ASYNC_FORWARD_OP_ERROR(&thread->connect);
		ASYNC_RESET_OP(&thread->connect);
		ASYNC_DELREF(&pipe->std);
		
		return;
	}
	
	code = thread->connect.code;
	ASYNC_RESET_OP(&thread->connect);
	
	if (UNEXPECTED(code < 0)) {
		ASYNC_DELREF(&pipe->std);
		
		zend_throw_error(NULL, "Failed to connect IPC pipe: %s", uv_strerror(code));
		return;
	}
#else
	pipe = async_pipe_init_ipc();
	code = uv_pipe_open(&pipe->handle, thread->pipes[1]);
	
	if (UNEXPECTED(code < 0)) {
		ASYNC_DELREF(&pipe->std);
		
		zend_throw_error(NULL, "Failed to open IPC pipe: %s", uv_strerror(code));
		return;
	}
#endif

	thread->slave = pipe;
	
	ASYNC_ADDREF(&pipe->std);
	RETURN_OBJ(&pipe->std);
}

#ifdef PHP_WIN32

ASYNC_CALLBACK ipc_listen_cb(uv_stream_t *handle, int status)
{
	async_thread *thread;
	async_pipe *pipe;
	
	zval obj;
	
	thread = (async_thread *) handle->data;
	
	ZEND_ASSERT(thread != NULL);
	
	if (thread->accept.base.status != ASYNC_STATUS_RUNNING) {
		return;
	}
	
	thread->accept.code = status;
	
	if (UNEXPECTED(status < 0)) {
		ASYNC_FINISH_OP(&thread->accept);
	} else {
		pipe = async_pipe_init_ipc();
		uv_accept(handle, (uv_stream_t *) &pipe->handle);
		
		ZVAL_OBJ(&obj, &pipe->std);
		ASYNC_RESOLVE_OP(&thread->accept, &obj);
		zval_ptr_dtor(&obj);
	}
}

#endif

static ZEND_METHOD(Thread, getIpc)
{
	async_thread *thread;
	async_pipe *pipe;
	
	int code;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	thread = (async_thread *) Z_OBJ_P(getThis());
	
	if (thread->master != NULL) {
		ASYNC_ADDREF(&thread->master->std);
		RETURN_OBJ(&thread->master->std);
	}
	
	ASYNC_CHECK_ERROR(thread->flags & ASYNC_THREAD_FLAG_TERMINATED, "Cannot access IPC pipe of a closed thread");
	
#ifdef PHP_WIN32
	uv_ref((uv_handle_t *) &thread->server);
	
	code = uv_listen((uv_stream_t *) &thread->server, 0, ipc_listen_cb);
	
	if (UNEXPECTED(code < 0)) {
		uv_unref((uv_handle_t *) &thread->server);
		
		zend_throw_error(NULL, "Failed to aquire IPC pipe: %s", uv_strerror(code));
		return;
	}
	
	if (UNEXPECTED(FAILURE == async_await_op((async_op *) &thread->accept))) {
		uv_unref((uv_handle_t *) &thread->server);
		
		ASYNC_FORWARD_OP_ERROR(&thread->accept);
		ASYNC_RESET_OP(&thread->accept);
		
		return;
	}
	
	code = thread->accept.code;
	
	if (UNEXPECTED(code < 0)) {
		ASYNC_RESET_OP(&thread->accept);
		
		if (!uv_is_closing((uv_handle_t *) &thread->server)) {
			ASYNC_ADDREF(&thread->std);
			
			uv_close((uv_handle_t *) &thread->server, ipc_close_cb);
		}
		
		zend_throw_error(NULL, "Failed to aquire IPC pipe: %s", uv_strerror(code));
		return;
	}
	
	pipe = (async_pipe *) Z_OBJ_P(&thread->accept.base.result);
	
	ASYNC_ADDREF(&pipe->std);	
	ASYNC_RESET_OP(&thread->accept);
	
	if (!uv_is_closing((uv_handle_t *) &thread->server)) {
		ASYNC_ADDREF(&thread->std);
		
		uv_close((uv_handle_t *) &thread->server, ipc_close_cb);
	}
#else
	pipe = async_pipe_init_ipc();
	code = uv_pipe_open(&pipe->handle, thread->pipes[0]);
	
	if (UNEXPECTED(code < 0)) {
		ASYNC_DELREF(&pipe->std);
		
		zend_throw_error(NULL, "Failed to aquire IPC pipe: %s", uv_strerror(code));
		return;
	}
#endif
	
	thread->master = pipe;
	
	ASYNC_ADDREF(&pipe->std);
	RETURN_OBJ(&pipe->std);
}

static ZEND_METHOD(Thread, kill)
{
	async_thread *thread;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	thread = (async_thread *) Z_OBJ_P(getThis());
	
	// TODO: Improve kill support...
	
	if (thread->shutdown.func != NULL) {
		thread->shutdown.func(thread, NULL);
	}
}

static ZEND_METHOD(Thread, join)
{
	async_thread *thread;
	async_op *op;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	thread = (async_thread *) Z_OBJ_P(getThis());
	
	if (!(thread->flags & ASYNC_THREAD_FLAG_TERMINATED)) {
		ASYNC_ALLOC_OP(op);
		ASYNC_APPEND_OP(&thread->join, op);
		
		if (UNEXPECTED(FAILURE == async_await_op(op))) {
			ASYNC_FORWARD_OP_ERROR(op);
		}
		
		ASYNC_FREE_OP(op);
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_thread_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, file, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_thread_is_available, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_thread_is_worker, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_thread_connect, 0, 0, Concurrent\\Network\\Pipe, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_thread_get_ipc, 0, 0, Concurrent\\Network\\Pipe, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_thread_kill, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_thread_join, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry thread_funcs[] = {
	ZEND_ME(Thread, __construct, arginfo_thread_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(Thread, isAvailable, arginfo_thread_is_available, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Thread, isWorker, arginfo_thread_is_worker, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Thread, connect, arginfo_thread_connect, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Thread, getIpc, arginfo_thread_get_ipc, ZEND_ACC_PUBLIC)
	ZEND_ME(Thread, kill, arginfo_thread_kill, ZEND_ACC_PUBLIC)
	ZEND_ME(Thread, join, arginfo_thread_join, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_thread_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Thread", thread_funcs);
	async_thread_ce = zend_register_internal_class(&ce);
	async_thread_ce->ce_flags |= ZEND_ACC_FINAL;
	async_thread_ce->create_object = async_thread_object_create;
	async_thread_ce->serialize = zend_class_serialize_deny;
	async_thread_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_thread_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_thread_handlers.free_obj = async_thread_object_destroy;
	async_thread_handlers.dtor_obj = async_thread_object_dtor;
	async_thread_handlers.clone_obj = NULL;
	
	str_main = zend_new_interned_string(zend_string_init(ZEND_STRL("main"), 1));
	
#ifdef ZTS
	if (ASYNC_G(cli)) {
		sapi_deactivate_func = sapi_module.deactivate;
		sapi_module.deactivate = NULL;
	}
#endif
}

void async_thread_ce_unregister()
{
#ifdef ZTS
	if (ASYNC_G(cli)) {
		sapi_module.deactivate = sapi_deactivate_func;
	}
#endif

	zend_string_release(str_main);
}
