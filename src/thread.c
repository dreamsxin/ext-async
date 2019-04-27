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

#include "SAPI.h"
#include "php_main.h"
#include "zend_inheritance.h"

#include "thread/copy.h"

zend_string *php_parallel_runtime_main;

ASYNC_API zend_class_entry *async_thread_pool_ce;
ASYNC_API zend_class_entry *async_job_failed_ce;

static zend_object_handlers async_thread_pool_handlers;

typedef int (*php_sapi_deactivate_t)(void);

static php_sapi_deactivate_t sapi_deactivate_func;

#define ASYNC_THREAD_POOL_JOB_FLAG_NO_RETURN 1

typedef struct _async_thread async_thread;
typedef struct _async_thread_job async_thread_job;

#define ASYNC_THREAD_POOL_FLAG_CLOSED 1

typedef struct {
	/* Shared data flags. */
	uint8_t flags;
	
	/* Reference count, used to sync pool and handle dtor. */
	uint8_t refcount;
	
	/* Holds a reference to the task scheduler for cleanup. */
	async_task_scheduler *scheduler;
	
	/* Hook into the task schedulers automatic shutdown sequence. */
	async_cancel_cb shutdown;
	
	/* Holds an error after the pool has been closed. */
	zval error;
	
	/* Threaded worker bootstrap file. */
	zend_string *bootstrap;

	/* Maximum thread pool size. */
	int size;
	
	/* Number of active worker threads. */
	int alive;
	
	/* Number of threads that are currently being disposed. */
	int closing;
	
	/* Worker ID sequence counter. */
	int counter;
	
	/* Async handle that wakes up the event loop when workers have finished jobs. */
	uv_async_t handle;

	/* Mutex protects access to data shared with worker threads. */
	uv_mutex_t mutex;
	
	/* Condition being used by workers to await new jobs. */
	uv_cond_t cond;
	
	/* Active worker threads. */
	struct {
		async_thread *first;
		async_thread *last;
	} workers;
	
	/* Workers that are being disposed. */
	struct {
		async_thread *first;
		async_thread *last;
	} disposed;
	
	/* Jobs queued for execution. */
	struct {
		async_thread_job *first;
		async_thread_job *last;
	} jobs;
	
	/* Hobs that have been completed within workers and need to be forwared into the loop thread. */
	struct {
		async_thread_job *first;
		async_thread_job *last;
	} ready;
} async_thread_data;

struct _async_thread_job {
	/* Job flags. */
	uint8_t flags;
	
	/* Refers to the awaitable that forwards the job result. */
	async_deferred_custom_awaitable *awaitable;
	
	/* Shared thread data. */
	async_thread_data *data;
	
	/* Execution frame of the user-provided closure. */
	zend_execute_data *exec;
	
	/* Location being used to store the result of the function call. */
	zval retval;
	
	/* Will be set if an error occurs during job execution. */
	zend_bool failed;
	
	/* File and line of an error being thrown from within the job callback. */
	zend_string *file;
	zend_long line;
	
	/* Needed to enqueue jobs. */
	async_thread_job *prev;
	async_thread_job *next;
};

struct _async_thread {
	/* UV thread handle being used to start and join the thread. */
	uv_thread_t handle;
	
	/* Shared thread data. */
	async_thread_data *data;
	
	/* Unique ID of this worker thread. */
	int id;
	
	/* Needed to enqueue workers. */
	async_thread *prev;
	async_thread *next;
};

typedef struct {
	/* PHP object handle. */
	zend_object std;
	
	/* Shared thread data to sync with workers. */
	async_thread_data *data;
} async_thread_pool;

#define GET_ERROR_BASE_CE(obj) (instanceof_function(Z_OBJCE_P(obj), zend_ce_exception) ? zend_ce_exception : zend_ce_error)
#define GET_ERROR_PROP(obj, name, retval) zend_read_property_ex(GET_ERROR_BASE_CE(obj), obj, ZSTR_KNOWN(name), 1, retval)


static int run_bootstrap(async_thread *worker)
{
	zend_file_handle handle;
	zend_op_array *ops;
	
	zval retval;
	
	// ASYNC_DEBUG_LOG("Bootstrap worker #%d\n", worker->id);
	
	if (SUCCESS != php_stream_open_for_zend_ex(ZSTR_VAL(worker->data->bootstrap), &handle, USE_PATH | REPORT_ERRORS | STREAM_OPEN_FOR_INCLUDE)) {
		return FAILURE;
	}
	
	if (!handle.opened_path) {
		handle.opened_path = zend_string_dup(worker->data->bootstrap, 0);
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
			
			return SUCCESS;
		}
	}
	
	if (EG(exception)) {
		zend_clear_exception();
	}
	
	return FAILURE;
}

static void run_job(async_thread *worker, async_thread_job *job, zend_execute_data *frame)
{
	zend_string *str;
	
	zval error;
	zval retval;

	// ASYNC_DEBUG_LOG("Execute job using worker #%d\n", worker->id);
	
	zend_init_func_execute_data(frame, &frame->func->op_array, &job->retval);
	
	zend_first_try {
		zend_try {
			zend_execute_ex(frame);
			
			if (job->flags & ASYNC_THREAD_POOL_JOB_FLAG_NO_RETURN) {
				zval_ptr_dtor(&job->retval);
				ZVAL_UNDEF(&job->retval);
			} else {			
				if (UNEXPECTED(EG(exception))) {
					job->failed = 1;
					
					ZVAL_OBJ(&error, EG(exception));
					
					if (NULL != (str = zval_get_string(GET_ERROR_PROP(&error, ZEND_STR_MESSAGE, &retval)))) {
						ZVAL_STR(&job->retval, zend_string_init(ZSTR_VAL(str), ZSTR_LEN(str), 1));
						zend_string_release(str);
					}
					
					if (NULL != (str = zval_get_string(GET_ERROR_PROP(&error, ZEND_STR_FILE, &retval)))) {
						job->file = zend_string_init(ZSTR_VAL(str), ZSTR_LEN(str), 1);
						zend_string_release(str);
					}
					
					job->line = zval_get_long(GET_ERROR_PROP(&error, ZEND_STR_LINE, &retval));
					
					zend_clear_exception();
				} else {
					retval = job->retval;
					
					if (Z_REFCOUNTED(job->retval)) {
						php_parallel_copy_zval(&job->retval, &job->retval, 1);
						zval_ptr_dtor(&retval);
					}
				}
			}
		} zend_catch {
			// TODO: How to handle exit / bailout in threaded job?
		} zend_end_try();
		
		if (UNEXPECTED(EG(exception))) {
			zend_clear_exception();
		}
		
		uv_mutex_lock(&worker->data->mutex);
		ASYNC_LIST_APPEND(&worker->data->ready, job);
		uv_mutex_unlock(&worker->data->mutex);

		uv_async_send(&worker->data->handle);
		
		php_parallel_copy_free(frame->func, 0);
		zend_vm_stack_free_call_frame(frame);
	} zend_end_try();
	
	// ASYNC_DEBUG_LOG("Job done using worker #%d\n", worker->id);
}

ASYNC_CALLBACK run_thread(void *arg)
{
	async_thread *worker;
	async_thread_data *data;
	async_thread_job *job;
	
	zend_execute_data *frame;
	zval *slot;
	zval *param;
	
	int argc;
	int i;
	
	worker = (async_thread *) arg;
	data = worker->data;
	
	ts_resource(0);

	TSRMLS_CACHE_UPDATE();
	
	PG(expose_php) = 0;
	PG(auto_globals_jit) = 1;
	
	php_request_startup();
	
	PG(during_request_startup) = 0;
	SG(sapi_started) = 0;
	SG(headers_sent) = 1;
	SG(request_info).no_headers = 1;
	
	if (data->bootstrap != NULL && FAILURE == run_bootstrap(worker)) {
		goto cleanup;
	}
	
	do {
		uv_mutex_lock(&data->mutex);
		
		while (data->jobs.first == NULL) {
			if (data->flags & ASYNC_THREAD_POOL_FLAG_CLOSED) {
				uv_mutex_unlock(&data->mutex);
				
				goto cleanup;
			}
			
			uv_cond_wait(&data->cond, &data->mutex);
		}
		
		ASYNC_LIST_EXTRACT_FIRST(&data->jobs, job);
		
		uv_mutex_unlock(&data->mutex);
		
		argc = ZEND_CALL_NUM_ARGS(job->exec);
		
#if PHP_VERSION_ID < 70400
		frame = zend_vm_stack_push_call_frame(ZEND_CALL_TOP_FUNCTION, php_parallel_copy(job->exec->func, 0), argc, NULL, NULL);
#else
		frame = zend_vm_stack_push_call_frame(ZEND_CALL_TOP_FUNCTION, php_parallel_copy(job->exec->func, 0), argc, NULL);
#endif

		if (argc) {
			slot = (zval *) ZEND_CALL_ARG(job->exec, 1);
			param = (zval *) ZEND_CALL_ARG(frame, 1);
			
			for (i = 0; i < argc; i++) {
				php_parallel_copy_zval(&param[i], &slot[i], 0);
			}
		}
		
		run_job(worker, job, frame);
	} while (1);
	
cleanup:
	
	// ASYNC_DEBUG_LOG("Dispose of worker #%d\n", worker->id);
	
	// First notification is used to start up a new worker ASAP.
	uv_mutex_lock(&data->mutex);
	
	data->alive--;
	data->closing++;
	ASYNC_LIST_REMOVE(&data->workers, worker);
	
	uv_mutex_unlock(&data->mutex);
	uv_async_send(&data->handle);
	
	// Run PHP shutdown logic.
	php_request_shutdown(NULL);
	ts_free_thread();
	
	// Put worker into the dispose queue and notify pool again to free up resources.
	uv_mutex_lock(&data->mutex);
	
	ASYNC_LIST_APPEND(&data->disposed, worker);
	
	uv_mutex_unlock(&data->mutex);	
	uv_async_send(&data->handle);
}

static void finish_job(async_thread_job *job)
{
	zval val;
	zval error;
	
	if (job->flags & ASYNC_THREAD_POOL_JOB_FLAG_NO_RETURN) {
		async_resolve_awaitable((async_deferred_awaitable *) job->awaitable, NULL);
	} else {	
		if (job->failed) {
			if (Z_TYPE_P(&job->retval) == IS_STRING) {
				php_parallel_copy_zval(&val, &job->retval, 0);
			
				if (Z_REFCOUNTED(job->retval)) {
					php_parallel_zval_dtor(&job->retval);
				}
			
				ASYNC_PREPARE_EXCEPTION(&error, async_job_failed_ce, "%s", ZSTR_VAL(Z_STR_P(&val)));
				
				zval_ptr_dtor(&val);
				
				if (job->file != NULL) {
					ZVAL_STR(&val, job->file);
					zend_update_property_ex(GET_ERROR_BASE_CE(&error), &error, ZSTR_KNOWN(ZEND_STR_FILE), &val);
				}
				
				ZVAL_LONG(&val, job->line);
				zend_update_property_ex(GET_ERROR_BASE_CE(&error), &error, ZSTR_KNOWN(ZEND_STR_LINE), &val);
			} else {
				ASYNC_PREPARE_EXCEPTION(&error, async_job_failed_ce, "Job failed due to an unknown error");
			}
		
			async_fail_awaitable((async_deferred_awaitable *) job->awaitable, &error);
			
			zval_ptr_dtor(&error);
		} else {
			php_parallel_copy_zval(&val, &job->retval, 0);
		
			if (Z_REFCOUNTED(job->retval)) {
				php_parallel_zval_dtor(&job->retval);
			}
		
			async_resolve_awaitable((async_deferred_awaitable *) job->awaitable, &val);
			
			zval_ptr_dtor(&val);
		}
	}
	
	if (job->file != NULL) {
		zend_string_release(job->file);
	}
	
	pefree(job, 1);
}

ASYNC_CALLBACK close_notify_cb(uv_handle_t *handle)
{
	async_thread_data *data;
	
	data = (async_thread_data *) handle->data;
	
	ZEND_ASSERT(data != NULL);
	
	uv_cond_destroy(&data->cond);
	uv_mutex_destroy(&data->mutex);
	
	if (data->bootstrap != NULL) {
		zend_string_release(data->bootstrap);
	}
	
	async_task_scheduler_unref(data->scheduler);
	
	if (--data->refcount == 0) {
		zval_ptr_dtor(&data->error);
		
		pefree(data, 1);
	}
}

ASYNC_CALLBACK notify_pool_cb(uv_async_t *handle)
{
	async_thread_data *data;
	async_thread *worker;
	async_thread *disposed;
	
	async_thread_job *job;
	async_thread_job *prev;
	
	data = (async_thread_data *) handle->data;
	
	ZEND_ASSERT(data != NULL);
	
	uv_mutex_lock(&data->mutex);
	
	// Extract workers and jobs but do not take action while holding the mutex.
	disposed = data->disposed.first;
	job = data->ready.first;
	
	if (disposed) {
		data->disposed.first = NULL;
		data->disposed.last = NULL;
		
		worker = disposed;
		
		while (worker) {
			worker = worker->next;
			data->closing--;
		}
	}
	
	if (job) {
		data->ready.first = NULL;
		data->ready.last = NULL;
	}
	
	if (data->flags & ASYNC_THREAD_POOL_FLAG_CLOSED) {
		if (data->alive == 0 && data->closing == 0) {
			if (!uv_is_closing((uv_handle_t *) handle)) {
				if (data->shutdown.func) {
					ASYNC_LIST_REMOVE(&data->scheduler->shutdown, &data->shutdown);
				}
			
				uv_close((uv_handle_t *) handle, close_notify_cb);
			}
		}
	} else {
		if (data->jobs.first == NULL) {
			uv_unref((uv_handle_t *) &data->handle);
		}
	
		while (data->alive < data->size) {	
			worker = pecalloc(1, sizeof(async_thread), 1);
			worker->id = ++data->counter;
			worker->data = data;
			
			data->alive++;
			
			uv_thread_create(&worker->handle, run_thread, worker);
			
			ASYNC_LIST_APPEND(&data->workers, worker);
		}
	}
	
	uv_mutex_unlock(&data->mutex);
	
	while (job) {
		prev = job;
		job = job->next;
		
		finish_job(prev);
	}
	
	while (disposed) {
		worker = disposed;
		disposed = disposed->next;
		
		uv_thread_join(&worker->handle);
			
		pefree(worker, 1);
	}
}

ASYNC_CALLBACK shutdown_pool_cb(void *arg, zval *error)
{
	async_thread_data *data;
	
	data = (async_thread_data *) arg;
	
	data->shutdown.func = NULL;
	
	if (Z_TYPE_P(&data->error) == IS_UNDEF) {
		if (error) {
			ZVAL_COPY(&data->error, error);
		} else {
			ASYNC_PREPARE_ERROR(&data->error, "Thread pool has been closed");
		}
	}
	
	uv_mutex_lock(&data->mutex);
	
	if (!(data->flags & ASYNC_THREAD_POOL_FLAG_CLOSED)) {
		data->flags |= ASYNC_THREAD_POOL_FLAG_CLOSED;
		
		if (data->alive == 0 && data->closing == 0) {
			if (!uv_is_closing((uv_handle_t *) &data->handle)) {
				uv_close((uv_handle_t *) &data->handle, close_notify_cb);
			}
		} else {
			uv_ref((uv_handle_t *) &data->handle);
			uv_cond_broadcast(&data->cond);
		}
	}
	
	uv_mutex_unlock(&data->mutex);
}

static zend_object *async_thread_pool_object_create(zend_class_entry *ce)
{
	async_thread_pool *pool;
	
	pool = ecalloc(1, sizeof(async_thread_pool));
	
	zend_object_std_init(&pool->std, ce);
	pool->std.handlers = &async_thread_pool_handlers;
	
	return &pool->std;
}

static void async_thread_pool_object_dtor(zend_object *object)
{
	async_thread_pool *pool;
	async_thread_data *data;
	
	pool = (async_thread_pool *) object;
	data = pool->data;
	
	if (data != NULL) {
		if (data->shutdown.func) {
			ASYNC_LIST_REMOVE(&data->scheduler->shutdown, &data->shutdown);
			
			data->shutdown.func(data, NULL);
		} else if (--data->refcount == 0) {
			zval_ptr_dtor(&data->error);
			
			pefree(data, 1);
		}
	}
}

static void async_thread_pool_object_destroy(zend_object *object)
{
	async_thread_pool *pool;
	
	pool = (async_thread_pool *) object;
	
	zend_object_std_dtor(&pool->std);
}

static ZEND_METHOD(ThreadPool, __construct)
{
#ifndef ZTS
	zend_throw_error(NULL, "Threads require PHP to be compiled in thread safe mode (ZTS)");
#else
	async_thread_pool *pool;
	async_thread_data *data;
	async_thread *worker;
	
	zend_long size;
	zend_string *bootstrap;
	
	size = 1;
	bootstrap = NULL;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 2)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(size)
		Z_PARAM_STR(bootstrap)
	ZEND_PARSE_PARAMETERS_END();

	ASYNC_CHECK_ERROR(!ASYNC_G(cli), "Threads are only supported if PHP is run from the command line (cli)");
	
	pool = (async_thread_pool *) Z_OBJ_P(getThis());
	
	ASYNC_CHECK_ERROR(size < 1, "Thread pool size must be at least 1");
	ASYNC_CHECK_ERROR(size > 64, "maximum thread pool size is 64");
	
	data = pecalloc(1, sizeof(async_thread_data), 1);
	
	pool->data = data;
	
	data->refcount = 2;
	data->size = (int) size;
	data->scheduler = async_task_scheduler_ref();
	
	data->shutdown.object = data;
	data->shutdown.func = shutdown_pool_cb;
	
	ASYNC_LIST_APPEND(&data->scheduler->shutdown, &data->shutdown);
	
	uv_mutex_init_recursive(&data->mutex);
	uv_cond_init(&data->cond);
	
	uv_async_init(&data->scheduler->loop, &data->handle, notify_pool_cb);
	uv_unref((uv_handle_t *) &data->handle);
	
	data->handle.data = data;
	
	if (bootstrap != NULL) {
		data->bootstrap = zend_string_dup(bootstrap, 1);
	}
	
	uv_mutex_lock(&data->mutex);
	
	while (data->alive < data->size) {
		worker = pecalloc(1, sizeof(async_thread), 1);
		worker->id = ++data->counter; 
		worker->data = data;
		
		data->alive++;
		
		uv_thread_create(&worker->handle, run_thread, worker);
		
		ASYNC_LIST_APPEND(&data->workers, worker);
	}
	
	uv_mutex_unlock(&data->mutex);
#endif
}

static ZEND_METHOD(ThreadPool, isAvailable)
{
	ZEND_PARSE_PARAMETERS_NONE();
	
#ifdef ZTS
	RETURN_BOOL(ASYNC_G(cli));
#else
	RETURN_FALSE;
#endif
}

static ZEND_METHOD(ThreadPool, close)
{
	async_thread_pool *pool;
	async_thread_data *data;
	
	zval *val;
	zval error;

	val = NULL;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	pool = (async_thread_pool *) Z_OBJ_P(getThis());
	data = pool->data;
	
	if (data != NULL && data->shutdown.func) {
		ASYNC_PREPARE_ERROR(&error, "Thread pool has been closed");
		
		if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
			zend_exception_set_previous(Z_OBJ_P(&error), Z_OBJ_P(val));
			GC_ADDREF(Z_OBJ_P(val));
		}
		
		ASYNC_LIST_REMOVE(&data->scheduler->shutdown, &data->shutdown);
		
		data->shutdown.func(data, &error);
		
		zval_ptr_dtor(&error);
	}
}

ASYNC_CALLBACK job_dispose_cb(async_deferred_custom_awaitable *defer)
{
	async_thread_job *job;
	
	job = (async_thread_job *) defer->arg;
	
	ZEND_ASSERT(job != NULL);
	
	job->flags |= ASYNC_THREAD_POOL_JOB_FLAG_NO_RETURN;
}

static ZEND_METHOD(ThreadPool, submit)
{
	async_thread_pool *pool;
	async_thread_job *job;
	async_deferred_custom_awaitable *awaitable;
	
	zend_execute_data *frame;
	zend_function *func;
	zend_bool returns;
	
	uint32_t i;
	uint32_t argc;
	
	zval *closure;
	zval *tmp;
	zval *slot;
	zval argv;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_OBJECT_OF_CLASS(closure, zend_ce_closure)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', tmp, argc)
	ZEND_PARSE_PARAMETERS_END();
	
	pool = (async_thread_pool *) Z_OBJ_P(getThis());
	
	if (UNEXPECTED(pool->data->flags & ASYNC_THREAD_POOL_FLAG_CLOSED)) {
		ASYNC_FORWARD_ERROR(&pool->data->error);
		return;
	}
	
	func = (zend_function *) zend_get_closure_method_def(closure);
	returns = 0;
	
	if (argc) {
		array_init_size(&argv, argc);
		
		for (i = 0; i < argc; i++) {
			zend_hash_index_update(Z_ARRVAL_P(&argv), i, &tmp[i]);
		}
	} else {
		ZVAL_UNDEF(&argv);
	}
	
	if (!php_parallel_copy_check(EG(current_execute_data)->prev_execute_data, func, argc ? &argv : NULL, &returns)) {
		zval_ptr_dtor(&argv);
		return;
	}
	
	zval_ptr_dtor(&argv);
	
	frame = pecalloc(1, zend_vm_calc_used_stack(argc, func), 1);
	frame->func = php_parallel_copy(func, 1);
	frame->return_value = NULL;	
	
	if (argc) {
		slot = ZEND_CALL_ARG(frame, 1);
		
		for (i = 0; i < argc; i++) {
			php_parallel_copy_zval(&slot[i], &tmp[i], 1);
		}
	
		ZEND_CALL_NUM_ARGS(frame) = argc;
	} else {
		ZEND_CALL_NUM_ARGS(frame) = 0;
	}
	
	awaitable = ecalloc(1, sizeof(async_deferred_custom_awaitable));	
	async_init_awaitable(awaitable, job_dispose_cb, async_context_get());
	
	job = pecalloc(1, sizeof(async_thread_job), 1);
	job->awaitable = awaitable;
	job->data = pool->data;
	job->exec = frame;
	
	awaitable->arg = job;
	
	uv_mutex_lock(&pool->data->mutex);	
	
	if (pool->data->jobs.first == NULL) {
		uv_ref((uv_handle_t *) &pool->data->handle);
	}
	
	ASYNC_LIST_APPEND(&pool->data->jobs, job);
	
	uv_cond_signal(&pool->data->cond);
	uv_mutex_unlock(&pool->data->mutex);
	
	RETURN_OBJ(&awaitable->base.std);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_thread_pool_ctor, 0, 0, 0)
	ZEND_ARG_TYPE_INFO(0, size, IS_LONG, 1)
	ZEND_ARG_TYPE_INFO(0, bootstrap, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_thread_pool_is_available, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_thread_pool_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_thread_pool_submit, 0, 1, Concurrent\\Awaitable, 0)
	ZEND_ARG_OBJ_INFO(0, work, Closure, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

static const zend_function_entry thread_pool_funcs[] = {
	ZEND_ME(ThreadPool, __construct, arginfo_thread_pool_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(ThreadPool, isAvailable, arginfo_thread_pool_is_available, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(ThreadPool, close, arginfo_thread_pool_close, ZEND_ACC_PUBLIC)
	ZEND_ME(ThreadPool, submit, arginfo_thread_pool_submit, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static const zend_function_entry empty_funcs[] = {
	ZEND_FE_END
};

void async_thread_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\ThreadPool", thread_pool_funcs);
	async_thread_pool_ce = zend_register_internal_class(&ce);
	async_thread_pool_ce->ce_flags |= ZEND_ACC_FINAL;
	async_thread_pool_ce->create_object = async_thread_pool_object_create;
	async_thread_pool_ce->serialize = zend_class_serialize_deny;
	async_thread_pool_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_thread_pool_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_thread_pool_handlers.free_obj = async_thread_pool_object_destroy;
	async_thread_pool_handlers.dtor_obj = async_thread_pool_object_dtor;
	async_thread_pool_handlers.clone_obj = NULL;
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\JobFailedException", empty_funcs);
	async_job_failed_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_job_failed_ce, zend_ce_exception);
	
	php_parallel_runtime_main = zend_new_interned_string(zend_string_init(ZEND_STRL("main"), 1));
	
	if (ASYNC_G(cli)) {
		sapi_deactivate_func = sapi_module.deactivate;
		sapi_module.deactivate = NULL;
	}
	
	init_copy();
}

void async_thread_ce_unregister()
{
	if (ASYNC_G(cli)) {
		sapi_module.deactivate = sapi_deactivate_func;
	}

	zend_string_release(php_parallel_runtime_main);
}
