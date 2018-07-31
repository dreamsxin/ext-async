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

static zend_class_entry *async_task_scheduler_ce;
static zend_object_handlers async_task_scheduler_handlers;


static void dispatch_tasks(uv_idle_t *idle)
{
	async_task_scheduler *scheduler;
	async_task *task;

	scheduler = (async_task_scheduler *) idle->data;

	ZEND_ASSERT(scheduler != NULL);

	uv_idle_stop(idle);

	scheduler->dispatching = 1;

	while (scheduler->ready.first != NULL) {
		ASYNC_Q_DEQUEUE(&scheduler->ready, task);

		ZEND_ASSERT(task->operation != ASYNC_TASK_OPERATION_NONE);

		if (task->operation == ASYNC_TASK_OPERATION_START) {
			async_task_start(task);
		} else {
			async_task_continue(task);
		}

		if (task->fiber.status == ASYNC_OP_RESOLVED || task->fiber.status == ASYNC_OP_FAILED) {
			async_task_dispose(task);

			OBJ_RELEASE(&task->fiber.std);
		} else if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
			ASYNC_Q_ENQUEUE(&scheduler->suspended, task);
		}
	}

	scheduler->dispatching = 0;
}

static void async_task_scheduler_dispose(async_task_scheduler *scheduler)
{
	async_task_scheduler *prev;
	async_task *task;

	prev = ASYNC_G(current_scheduler);
	ASYNC_G(current_scheduler) = scheduler;

	do {
		async_task_scheduler_run_loop(scheduler);

		while (scheduler->ready.first != NULL) {
			ASYNC_Q_DEQUEUE(&scheduler->ready, task);

			async_task_dispose(task);

			OBJ_RELEASE(&task->fiber.std);
		}

		while (scheduler->suspended.first != NULL) {
			ASYNC_Q_DEQUEUE(&scheduler->suspended, task);

			async_task_dispose(task);

			OBJ_RELEASE(&task->fiber.std);
		}
	} while (uv_loop_alive(&scheduler->loop));

	ASYNC_G(current_scheduler) = prev;
}

async_task_scheduler *async_task_scheduler_get()
{
	async_task_scheduler *scheduler;
	async_task_scheduler_stack *stack;

	scheduler = ASYNC_G(current_scheduler);

	if (scheduler != NULL) {
		return scheduler;
	}

	stack = ASYNC_G(scheduler_stack);

	if (stack != NULL && stack->top != NULL) {
		return stack->top->scheduler;
	}

	scheduler = ASYNC_G(scheduler);

	if (scheduler != NULL) {
		return scheduler;
	}

	scheduler = emalloc(sizeof(async_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(async_task_scheduler));

	zend_object_std_init(&scheduler->std, async_task_scheduler_ce);
	object_properties_init(&scheduler->std, async_task_scheduler_ce);

	scheduler->std.handlers = &async_task_scheduler_handlers;

	uv_loop_init(&scheduler->loop);
	uv_idle_init(&scheduler->loop, &scheduler->idle);

	scheduler->idle.data = scheduler;

	ASYNC_G(scheduler) = scheduler;

	return scheduler;
}

uv_loop_t *async_task_scheduler_get_loop()
{
	return &async_task_scheduler_get()->loop;
}

zend_bool async_task_scheduler_enqueue(async_task *task)
{
	async_task_scheduler *scheduler;

	scheduler = task->scheduler;

	ZEND_ASSERT(scheduler != NULL);

	if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
		task->operation = ASYNC_TASK_OPERATION_START;

		GC_ADDREF(&task->fiber.std);
	} else if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
		task->operation = ASYNC_TASK_OPERATION_RESUME;

		ASYNC_Q_DETACH(&scheduler->suspended, task);
	} else {
		return 0;
	}

	if (!scheduler->dispatching && UNEXPECTED(EG(exception))) {
		if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
			ZVAL_OBJ(&task->result, EG(exception));
			EG(exception) = NULL;

			zend_clear_exception();

			zend_fcall_info_args_clear(&task->fiber.fci, 1);
			zval_ptr_dtor(&task->fiber.fci.function_name);

			task->operation = ASYNC_TASK_OPERATION_NONE;
			task->fiber.status = ASYNC_FIBER_STATUS_FAILED;

			async_awaitable_trigger_continuation(&task->continuation, &task->result, 0);
		} else if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
			ZVAL_OBJ(&task->error, EG(exception));
			EG(exception) = NULL;

			zend_clear_exception();

			async_task_dispose(task);
		}

		OBJ_RELEASE(&task->fiber.std);

		return 0;
	}

	if (!scheduler->dispatching && scheduler->ready.first == NULL) {
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

	ASYNC_CHECK_FATAL(scheduler->running, "Duplicate scheduler loop run detected");
	ASYNC_CHECK_FATAL(scheduler->dispatching, "Cannot run loop while dispatching");

	prev = ASYNC_G(current_scheduler);
	ASYNC_G(current_scheduler) = scheduler;

	scheduler->running = 1;

	uv_run(&scheduler->loop, UV_RUN_DEFAULT);

	scheduler->running = 0;

	ASYNC_G(current_scheduler) = prev;
}

void async_task_scheduler_stop_loop(async_task_scheduler *scheduler)
{
	ASYNC_CHECK_FATAL(scheduler->running == 0, "Cannot stop scheduler loop that is not running");

	uv_stop(&scheduler->loop);
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

	scheduler->idle.data = scheduler;

	return scheduler;
}

static void async_task_scheduler_object_destroy(zend_object *object)
{
	async_task_scheduler *scheduler;

	scheduler = (async_task_scheduler *)object;

	async_task_scheduler_dispose(scheduler);

	ZEND_ASSERT(!uv_loop_alive(&scheduler->loop));

	uv_loop_close(&scheduler->loop);

	zend_object_std_dtor(object);
}

typedef struct _debug_info {
	zend_bool inspect;
	async_task_scheduler *scheduler;
	async_context *context;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} debug_info;

static void debug_scope(void *obj, zval *data, zval *result, zend_bool success)
{
	debug_info *info;

	zval args[1];
	zval retval;

	info = (debug_info *) obj;

	ZEND_ASSERT(info != NULL);

	if (info->inspect) {
		ZVAL_OBJ(&args[0], &info->scheduler->std);

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

static void exec_scope(zend_fcall_info fci, zend_fcall_info_cache fcc, debug_info *info, zval *return_value, zend_execute_data *execute_data)
{
	async_task_scheduler *scheduler;
	async_task_scheduler_stack *stack;
	async_task_scheduler_stack_entry *entry;
	async_task *task;

	zend_uchar status;

	zval retval;

	scheduler = async_task_scheduler_object_create();
	stack = ASYNC_G(scheduler_stack);

	info->scheduler = scheduler;

	if (stack == NULL) {
		stack = emalloc(sizeof(async_task_scheduler_stack));
		stack->size = 0;
		stack->top = NULL;

		ASYNC_G(scheduler_stack) = stack;
	}

	entry = emalloc(sizeof(async_task_scheduler_stack_entry));
	entry->scheduler = scheduler;
	entry->prev = stack->top;

	stack->top = entry;
	stack->size++;

	fci.param_count = 0;

	Z_TRY_ADDREF_P(&fci.function_name);

	task = async_task_object_create(EX(prev_execute_data), scheduler, info->context);
	task->fiber.fci = fci;
	task->fiber.fcc = fcc;

	async_awaitable_register_continuation(&task->continuation, info, NULL, debug_scope);

	async_task_scheduler_enqueue(task);
	async_task_scheduler_dispose(scheduler);

	stack->top = entry->prev;
	stack->size--;

	efree(entry);

	status = task->fiber.status;
	ZVAL_COPY(&retval, &task->result);

	OBJ_RELEASE(&task->fiber.std);
	OBJ_RELEASE(&scheduler->std);

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

ZEND_METHOD(TaskScheduler, getPendingTasks)
{
	async_task_scheduler *scheduler;
	async_task *task;
	uint32_t size;

	zval obj;
	zend_ulong i;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (async_task_scheduler *) Z_OBJ_P(getThis());

	task = scheduler->ready.first;
	size = 0;

	while (task != NULL) {
		task = task->next;
		size++;
	}

	task = scheduler->suspended.first;

	while (task != NULL) {
		task = task->next;
		size++;
	}

	array_init_size(return_value, size);

	task = scheduler->ready.first;
	i = 0;

	while (task != NULL) {
		ZVAL_OBJ(&obj, &task->fiber.std);
		GC_ADDREF(&task->fiber.std);

		zend_hash_index_update(Z_ARRVAL_P(return_value), i, &obj);

		task = task->next;
		i++;
	}

	task = scheduler->suspended.first;

	while (task != NULL) {
		ZVAL_OBJ(&obj, &task->fiber.std);
		GC_ADDREF(&task->fiber.std);

		zend_hash_index_update(Z_ARRVAL_P(return_value), i, &obj);

		task = task->next;
		i++;
	}
}

ZEND_METHOD(TaskScheduler, run)
{
	debug_info info;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	info.inspect = 0;
	info.fci = empty_fcall_info;
	info.fcc = empty_fcall_info_cache;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_EX(info.fci, info.fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (ZEND_NUM_ARGS() > 1) {
		info.inspect = 1;
	}

	info.context = async_context_get();

	exec_scope(fci, fcc, &info, return_value, execute_data);
}

ZEND_METHOD(TaskScheduler, runWithContext)
{
	debug_info info;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	zval *ctx;

	info.inspect = 0;
	info.fci = empty_fcall_info;
	info.fcc = empty_fcall_info_cache;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_FUNC_EX(info.fci, info.fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (ZEND_NUM_ARGS() > 2) {
		info.inspect = 1;
	}

	info.context = (async_context *) Z_OBJ_P(ctx);

	exec_scope(fci, fcc, &info, return_value, execute_data);
}

ZEND_METHOD(TaskScheduler, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task scheduler is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_task_scheduler_get_pending_tasks, 0, 0, IS_ARRAY, 0)
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
	ZEND_ME(TaskScheduler, __construct, arginfo_task_scheduler_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(TaskScheduler, getPendingTasks, arginfo_task_scheduler_get_pending_tasks, ZEND_ACC_PUBLIC)
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

void async_task_scheduler_shutdown()
{
	async_task_scheduler *scheduler;
	async_task_scheduler_stack *stack;
	async_task_scheduler_stack_entry *entry;

	stack = ASYNC_G(scheduler_stack);

	if (stack != NULL) {
		ASYNC_G(scheduler_stack) = NULL;

		while (stack->top != NULL) {
			entry = stack->top;

			stack->top = entry->prev;
			stack->size--;

			async_task_scheduler_dispose(entry->scheduler);

			OBJ_RELEASE(&entry->scheduler->std);

			efree(entry);
		}

		efree(stack);
	}

	scheduler = ASYNC_G(scheduler);

	if (scheduler != NULL) {
		ASYNC_G(scheduler) = NULL;

		async_task_scheduler_dispose(scheduler);

		OBJ_RELEASE(&scheduler->std);
	}
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
