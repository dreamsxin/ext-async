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

#include "async_fiber.h"
#include "async_task.h"

ASYNC_API zend_class_entry *async_task_scheduler_ce;

static zend_object_handlers async_task_scheduler_handlers;


static async_task_scheduler *async_task_scheduler_object_create();


static void dispatch_tasks(uv_idle_t *idle)
{
	async_task_scheduler *scheduler;
	async_task *task;
	async_task *ref;

	scheduler = (async_task_scheduler *) idle->data;

	ZEND_ASSERT(scheduler != NULL);

	int count;

	ref = NULL;

	while (scheduler->ready.first != NULL) {
		task = scheduler->ready.first;
		count = 1;

		while (task->next != NULL) {
			count++;
			task = task->next;
		}

		if (task == ref) {
			break;
		}

		ASYNC_Q_DEQUEUE(&scheduler->ready, task);

		ZEND_ASSERT(task->operation != ASYNC_TASK_OPERATION_NONE);

		if (ref == NULL) {
			ref = task;
		}

		if (task->operation == ASYNC_TASK_OPERATION_START) {
			async_task_start(task);
		} else {
			async_task_continue(task);
		}

		if (task->fiber.status == ASYNC_OP_RESOLVED || task->fiber.status == ASYNC_OP_FAILED) {
			async_task_dispose(task);
			
			ASYNC_DELREF(&task->fiber.std);
		}
	}

	if (scheduler->ready.first == NULL) {
		uv_idle_stop(idle);
	}
}

static void async_task_scheduler_dispose(async_task_scheduler *scheduler)
{
	async_task_scheduler *prev;
	async_cancel_cb *cancel;
	async_op *op;
	
	zval error;
	
	if (scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED) {
		return;
	}

	prev = ASYNC_G(current_scheduler);
	ASYNC_G(current_scheduler) = scheduler;

	async_task_scheduler_run_loop(scheduler);

	scheduler->flags |= ASYNC_TASK_SCHEDULER_FLAG_DISPOSED;
	
	ASYNC_PREPARE_ERROR(&error, "Task scheduler has been disposed");

	while (scheduler->operations.first != NULL) {
		ASYNC_DEQUEUE_OP(&scheduler->operations, op);
		ASYNC_FAIL_OP(op, &error);
	}
	
	async_task_scheduler_run_loop(scheduler);

	while (scheduler->shutdown.first != NULL) {
		do {
			ASYNC_Q_DEQUEUE(&scheduler->shutdown, cancel);

			cancel->func(cancel->object, &error);
		} while (scheduler->shutdown.first != NULL);

		async_task_scheduler_run_loop(scheduler);
	}
	
	zval_ptr_dtor(&error);
	
	ASYNC_G(current_scheduler) = prev;
}

static void busy_timer(uv_timer_t *handle)
{
	// Dummy timer being used to keep the loop busy...
}

ASYNC_API async_task_scheduler *async_task_scheduler_get()
{
	async_task_scheduler *scheduler;

	scheduler = ASYNC_G(current_scheduler);

	if (scheduler != NULL) {
		return scheduler;
	}

	scheduler = ASYNC_G(scheduler);

	if (scheduler != NULL) {
		return scheduler;
	}
	
	scheduler = async_task_scheduler_object_create();

	ASYNC_G(scheduler) = scheduler;

	return scheduler;
}

zend_bool async_task_scheduler_enqueue(async_task *task)
{
	async_task_scheduler *scheduler;

	scheduler = task->scheduler;

	ZEND_ASSERT(scheduler != NULL);
	ZEND_ASSERT(task->operation == ASYNC_TASK_OPERATION_NONE);

	if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
		task->operation = ASYNC_TASK_OPERATION_START;

		ASYNC_ADDREF(&task->fiber.std);
	} else if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
		task->operation = ASYNC_TASK_OPERATION_RESUME;
	} else {
		return 0;
	}

	if (scheduler->ready.first == NULL && !uv_is_active((uv_handle_t *) &scheduler->idle)) {
		uv_idle_start(&scheduler->idle, dispatch_tasks);
	}

	ASYNC_Q_ENQUEUE(&scheduler->ready, task);

	return 1;
}

void async_task_scheduler_dequeue(async_task *task)
{
	async_task_scheduler *scheduler;

	scheduler = task->scheduler;

	ZEND_ASSERT(scheduler != NULL);
	ZEND_ASSERT(task->fiber.status == ASYNC_FIBER_STATUS_INIT);

	ASYNC_Q_DETACH(&scheduler->ready, task);
}

void async_task_scheduler_run_loop(async_task_scheduler *scheduler)
{
	async_task_scheduler *prev;

	ASYNC_CHECK_FATAL(scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_RUNNING, "Duplicate scheduler loop run detected");

	prev = ASYNC_G(current_scheduler);
	ASYNC_G(current_scheduler) = scheduler;

	scheduler->caller = async_fiber_context_get();

	scheduler->flags |= ASYNC_TASK_SCHEDULER_FLAG_RUNNING;

	async_fiber_context_switch((scheduler->current == NULL) ? scheduler->fiber : scheduler->current, 0);

	scheduler->flags &= ~ASYNC_TASK_SCHEDULER_FLAG_RUNNING;
	scheduler->caller = NULL;

	ASYNC_G(current_scheduler) = prev;
}

void async_task_scheduler_call_nowait(async_task_scheduler *scheduler, zend_fcall_info *fci, zend_fcall_info_cache *fcc)
{
	zend_bool flag;

	flag = (scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_NOWAIT) ? 1 : 0;
	scheduler->flags |= ASYNC_TASK_SCHEDULER_FLAG_NOWAIT;

	zend_call_function(fci, fcc);

	if (!flag) {
		scheduler->flags &= ~ASYNC_TASK_SCHEDULER_FLAG_NOWAIT;
	}
}

static void run_func()
{
	async_task_scheduler *scheduler;
	
	scheduler = ASYNC_G(current_scheduler);
	
	ZEND_ASSERT(scheduler->caller != NULL);

	ASYNC_G(current_fiber) = NULL;
	ASYNC_G(current_context) = NULL;

	while (1) {
		uv_run(&scheduler->loop, UV_RUN_DEFAULT);

		async_fiber_context_switch(scheduler->caller, 0);
	}
}

static async_task_scheduler *async_task_scheduler_object_create()
{
	async_task_scheduler *scheduler;

	scheduler = emalloc(sizeof(async_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(async_task_scheduler));

	zend_object_std_init(&scheduler->std, async_task_scheduler_ce);

	scheduler->std.handlers = &async_task_scheduler_handlers;
	
	uv_loop_init(&scheduler->loop);
	uv_idle_init(&scheduler->loop, &scheduler->idle);
	
	uv_timer_init(&scheduler->loop, &scheduler->busy);
	uv_timer_start(&scheduler->busy, busy_timer, 3600 * 1000, 3600 * 1000);	
	uv_unref((uv_handle_t *) &scheduler->busy);

	scheduler->idle.data = scheduler;
	
	scheduler->fiber = async_fiber_create_context();
	async_fiber_create(scheduler->fiber, run_func, 1024 * 1024 * 128);

	return scheduler;
}

static void walk_loop_cb(uv_handle_t *handle, void *arg)
{
	int *count;
	
	count = (int *) arg;
	
	(*count)++;
	
	printf(">> UV HANDLE LEAKED: [%d] %s -> %d\n", handle->type, uv_handle_type_name(handle->type), uv_is_closing(handle));
}

static int debug_handles(uv_loop_t *loop)
{
    int pending;
    
    pending = 0;

	uv_walk(loop, walk_loop_cb, &pending);
	
	return pending;
}

static void async_task_scheduler_object_destroy(zend_object *object)
{
	async_task_scheduler *scheduler;

	int code;

	scheduler = (async_task_scheduler *)object;

	async_task_scheduler_dispose(scheduler);

	uv_close((uv_handle_t *) &scheduler->busy, NULL);
	uv_close((uv_handle_t *) &scheduler->idle, NULL);
	
	// Run loop again to cleanup idle watcher.
	uv_run(&scheduler->loop, UV_RUN_DEFAULT);

	ZEND_ASSERT(!uv_loop_alive(&scheduler->loop));
	ZEND_ASSERT(debug_handles(&scheduler->loop) == 0);
	
	code = uv_loop_close(&scheduler->loop);
	ZEND_ASSERT(code == 0);
	
	zend_object_std_dtor(object);
	
	async_fiber_destroy(scheduler->fiber);
}

typedef struct {
	async_op base;
	zend_bool inspect;
	async_task_scheduler *scheduler;
	async_context *context;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} async_scheduler_debug_op;

static void debug_scope(async_op *op)
{
	async_scheduler_debug_op *info;
	async_task *task;

	uint32_t size;
	zend_ulong i;

	zval args[1];
	zval retval;
	zval obj;

	info = (async_scheduler_debug_op *) op;

	ZEND_ASSERT(info != NULL);

	if (info->inspect) {
		task = info->scheduler->ready.first;
		size = 0;

		while (task != NULL) {
			task = task->next;
			size++;
		}

		array_init_size(&args[0], size);

		task = info->scheduler->ready.first;
		i = 0;

		while (task != NULL) {
			zend_hash_index_update(Z_ARRVAL_P(&args[0]), i, async_task_get_debug_info(task, &obj));

			task = task->next;
			i++;
		}

		info->fci.param_count = 1;
		info->fci.params = args;
		info->fci.retval = &retval;
		info->fci.no_separation = 1;

		zend_call_function(&info->fci, &info->fcc);

		zval_ptr_dtor(&args[0]);
		zval_ptr_dtor(&retval);

		ASYNC_CHECK_FATAL(EG(exception), "Must not throw an error from scheduler inspector");
	}
}

static void exec_scope(zend_fcall_info fci, zend_fcall_info_cache fcc, async_scheduler_debug_op *op, zval *return_value, zend_execute_data *execute_data)
{
	async_task_scheduler *scheduler;
	async_task_scheduler *prev;
	async_task *task;

	zend_uchar status;

	zval retval;

	scheduler = async_task_scheduler_object_create();
	
	prev = ASYNC_G(current_scheduler);
	ASYNC_G(current_scheduler) = scheduler;

	op->scheduler = scheduler;

	fci.param_count = 0;

	task = async_task_object_create(EX(prev_execute_data), scheduler, op->context);
	task->fiber.fci = fci;
	task->fiber.fcc = fcc;
	
	ASYNC_ADDREF_CB(task->fiber.fci);
	
	ASYNC_ENQUEUE_OP(&task->operations, op);

	async_task_scheduler_enqueue(task);
	async_task_scheduler_dispose(scheduler);
	
	status = task->fiber.status;
	ZVAL_COPY(&retval, &task->result);

	ASYNC_DELREF(&task->fiber.std);
	ASYNC_DELREF(&scheduler->std);
	
	ASYNC_G(current_scheduler) = prev;

	if (status == ASYNC_FIBER_STATUS_FINISHED) {
		RETURN_ZVAL(&retval, 1, 1);
	}

	if (status == ASYNC_FIBER_STATUS_FAILED) {
		execute_data->opline--;
		zend_throw_exception_internal(&retval);
		execute_data->opline++;
		return;
	}

	zval_ptr_dtor(&retval);
}


ZEND_METHOD(TaskScheduler, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Task scheduler must not be constructed by userland code");
}

ZEND_METHOD(TaskScheduler, run)
{
	async_scheduler_debug_op *info;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	ASYNC_ALLOC_CUSTOM_OP(info, sizeof(async_scheduler_debug_op));

	info->inspect = 0;
	info->base.callback = debug_scope;
	
	info->fci = empty_fcall_info;
	info->fcc = empty_fcall_info_cache;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_EX(info->fci, info->fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (ZEND_NUM_ARGS() > 1) {
		info->inspect = 1;
	}
	
	info->context = async_context_get();
	
	exec_scope(fci, fcc, info, return_value, execute_data);
	
	ASYNC_FREE_OP(info);
}

ZEND_METHOD(TaskScheduler, runWithContext)
{
	async_scheduler_debug_op *info;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	zval *ctx;
	
	ASYNC_ALLOC_CUSTOM_OP(info, sizeof(async_scheduler_debug_op));

	info->inspect = 0;
	info->base.callback = debug_scope;
	
	info->fci = empty_fcall_info;
	info->fcc = empty_fcall_info_cache;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_EX(info->fci, info->fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (ZEND_NUM_ARGS() > 2) {
		info->inspect = 1;
	}
	
	info->context = (async_context *) Z_OBJ_P(ctx);

	exec_scope(fci, fcc, info, return_value, execute_data);
	
	ASYNC_FREE_OP(info);
}

ZEND_METHOD(TaskScheduler, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task scheduler is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_run, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_CALLABLE_INFO(0, finalizer, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_run_with_context, 0, 0, 2)
ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_CALLABLE_INFO(0, finalizer, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_scheduler_functions[] = {
	ZEND_ME(TaskScheduler, __construct, arginfo_task_scheduler_ctor, ZEND_ACC_PRIVATE)
	ZEND_ME(TaskScheduler, run, arginfo_task_scheduler_run, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(TaskScheduler, runWithContext, arginfo_task_scheduler_run_with_context, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(TaskScheduler, __wakeup, arginfo_task_scheduler_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_task_scheduler_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskScheduler", task_scheduler_functions);
	async_task_scheduler_ce = zend_register_internal_class(&ce);
	async_task_scheduler_ce->ce_flags |= ZEND_ACC_FINAL;
	async_task_scheduler_ce->serialize = zend_class_serialize_deny;
	async_task_scheduler_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_task_scheduler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_task_scheduler_handlers.offset = XtOffsetOf(async_task_scheduler, std);
	async_task_scheduler_handlers.free_obj = async_task_scheduler_object_destroy;
	async_task_scheduler_handlers.clone_obj = NULL;
}

void async_task_scheduler_run()
{
	async_task_scheduler *scheduler;

	scheduler = ASYNC_G(scheduler);

	if (scheduler != NULL) {
		async_task_scheduler_dispose(scheduler);
	}
}

void async_task_scheduler_shutdown()
{
	async_task_scheduler *scheduler;

	ZEND_ASSERT(ASYNC_G(current_scheduler) == NULL);

	scheduler = ASYNC_G(scheduler);
	
	if (scheduler != NULL) {
		ASYNC_G(scheduler) = NULL;
		
		async_task_scheduler_dispose(scheduler);
		
		ASYNC_DELREF(&scheduler->std);
	}
}
