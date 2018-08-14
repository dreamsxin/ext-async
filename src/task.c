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
#include "async_helper.h"
#include "async_task.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_task_ce;

const zend_uchar ASYNC_FIBER_TYPE_TASK = 1;

const zend_uchar ASYNC_TASK_OPERATION_NONE = 0;
const zend_uchar ASYNC_TASK_OPERATION_START = 1;
const zend_uchar ASYNC_TASK_OPERATION_RESUME = 2;

static zend_object_handlers async_task_handlers;

#define ASYNC_TASK_DELEGATE_RESULT(status, result) do { \
	if (status == ASYNC_OP_RESOLVED) { \
		RETURN_ZVAL(result, 1, 0); \
	} else if (status == ASYNC_OP_FAILED) { \
		Z_ADDREF_P(result); \
		execute_data->opline--; \
		zend_throw_exception_internal(result); \
		execute_data->opline++; \
		return; \
	} \
} while (0)


static void async_task_fiber_func(async_fiber *fiber)
{
	async_task *task;

	zval retval;

	ZEND_ASSERT(fiber->type == ASYNC_FIBER_TYPE_TASK);

	task = (async_task *) fiber;

	fiber->fci.retval = &retval;

	zend_call_function(&fiber->fci, &fiber->fcc);
	zval_ptr_dtor(&fiber->fci.function_name);

	// TODO: Call await instead...

	if (Z_TYPE_P(&retval) == IS_OBJECT && instanceof_function_ex(Z_OBJCE_P(&retval), async_awaitable_ce, 1) != 0) {
		zend_throw_error(NULL, "Task must not return an object implementing Awaitable");
	}

	if (EG(exception)) {
		fiber->status = ASYNC_FIBER_STATUS_FAILED;

		ZVAL_OBJ(&task->result, EG(exception));
		EG(exception) = NULL;

		async_awaitable_trigger_continuation(&task->continuation, &task->result, 0);
	} else {
		fiber->status = ASYNC_FIBER_STATUS_FINISHED;

		ZVAL_COPY(&task->result, &retval);

		async_awaitable_trigger_continuation(&task->continuation, &task->result, 1);
	}

	zval_ptr_dtor(&retval);

	zend_clear_exception();
}

void async_task_start(async_task *task)
{
	async_context *context;

	task->operation = ASYNC_TASK_OPERATION_NONE;
	task->fiber.context = async_fiber_create_context();

	ASYNC_CHECK_FATAL(task->fiber.context == NULL, "Failed to create native fiber context");
	ASYNC_CHECK_FATAL(!async_fiber_create(task->fiber.context, async_fiber_run, task->fiber.stack_size), "Failed to create native fiber");

	task->fiber.stack = (zend_vm_stack) emalloc(ASYNC_FIBER_VM_STACK_SIZE);
	task->fiber.stack->top = ZEND_VM_STACK_ELEMENTS(task->fiber.stack) + 1;
	task->fiber.stack->end = (zval *) ((char *) task->fiber.stack + ASYNC_FIBER_VM_STACK_SIZE);
	task->fiber.stack->prev = NULL;

	task->fiber.status = ASYNC_FIBER_STATUS_RUNNING;
	task->fiber.func = async_task_fiber_func;

	context = ASYNC_G(current_context);
	ASYNC_G(current_context) = task->context;

	ASYNC_CHECK_FATAL(!async_fiber_switch_to(&task->fiber), "Failed to switch to fiber");

	ASYNC_G(current_context) = context;

	zend_fcall_info_args_clear(&task->fiber.fci, 1);
}

void async_task_continue(async_task *task)
{
	task->operation = ASYNC_TASK_OPERATION_NONE;
	task->fiber.status = ASYNC_FIBER_STATUS_RUNNING;

	ASYNC_CHECK_FATAL(!async_fiber_switch_to(&task->fiber), "Failed to switch to fiber");
}

static void task_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_task *task;

	task = (async_task *) obj;

	task->suspended = NULL;

	if (task->operation != ASYNC_TASK_OPERATION_NONE) {
		return;
	}

	if (result == NULL || task->fiber.status != ASYNC_FIBER_STATUS_SUSPENDED) {
		task->fiber.status = ASYNC_FIBER_STATUS_FAILED;
	} else if (success) {
		if (task->fiber.value != NULL) {
			ZVAL_COPY(task->fiber.value, result);
		}
	} else {
		ZVAL_COPY(&task->error, result);
	}

	task->fiber.value = NULL;

	async_task_scheduler_enqueue(task);
}

static void top_level_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_task_scheduler *scheduler;

	scheduler = (async_task_scheduler *) obj;

	ZEND_ASSERT(scheduler != NULL);

	async_task_scheduler_stop_loop(scheduler);
}

static void suspend_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_task_suspended *suspended;

	suspended = (async_task_suspended *) obj;

	ZEND_ASSERT(suspended->scheduler != NULL);

	suspended->state = success ? ASYNC_OP_RESOLVED : ASYNC_OP_FAILED;

	if (result != NULL) {
		ZVAL_COPY(&suspended->result, result);
	}

	async_task_scheduler_stop_loop(suspended->scheduler);
}

static void cancel_suspend(void *obj, zval *error)
{
	async_task *task;

	task = (async_task *) obj;

	if (task->operation != ASYNC_TASK_OPERATION_NONE) {
		return;
	}

	ZVAL_COPY(&task->error, error);

	task->fiber.value = NULL;

	async_task_scheduler_enqueue(task);
}

void async_task_suspend(async_awaitable_queue *q, zval *return_value, zend_execute_data *execute_data, zend_bool cancellable, zval *data)
{
	async_fiber *fiber;
	async_task *task;
	async_task_scheduler *scheduler;
	async_awaitable_cb *cont;
	async_context *context;
	async_task_suspended info;
	size_t stack_page_size;

	zval error;

	fiber = ASYNC_G(current_fiber);

	if (fiber == NULL) {
		scheduler = async_task_scheduler_get();

		ASYNC_CHECK_ERROR(scheduler->running, "Cannot await in the fiber that is running the task scheduler loop");

		info.scheduler = scheduler;
		info.state = 0;

		cont = async_awaitable_register_continuation(q, &info, data, suspend_continuation);
		async_task_scheduler_run_loop(scheduler);

		if (info.state == ASYNC_OP_RESOLVED) {
			if (USED_RET()) {
				ZVAL_COPY(return_value, &info.result);
			}

			zval_ptr_dtor(&info.result);

			return;
		}

		if (info.state == ASYNC_OP_FAILED) {
			execute_data->opline--;
			zend_throw_exception_internal(&info.result);
			execute_data->opline++;

			return;
		}

		async_awaitable_dispose_continuation(q, cont);

		if (EXPECTED(EG(exception) == NULL)) {
			zend_throw_error(NULL, "Awaitable has not been resolved");
		}

		return;
	}

	ASYNC_CHECK_ERROR(fiber->type != ASYNC_FIBER_TYPE_TASK, "Await must be called from within a running task");
	ASYNC_CHECK_ERROR(fiber->status != ASYNC_FIBER_STATUS_RUNNING, "Cannot await in a task that is not running");
	ASYNC_CHECK_ERROR(fiber->disposed, "Task has been destroyed");

	task = (async_task *) fiber;

	ASYNC_CHECK_ERROR(!ASYNC_G(current_scheduler)->dispatching, "Cannot await while the task scheduler is not dispatching tasks");

	context = ASYNC_G(current_context);

	if (cancellable && context->cancel != NULL) {
		// Context is already cancelled.
		if (Z_TYPE_P(&context->cancel->error) != IS_UNDEF) {
			Z_ADDREF_P(&context->cancel->error);

			execute_data->opline--;
			zend_throw_exception_internal(&context->cancel->error);
			execute_data->opline++;

			return;
		}

		ASYNC_Q_ENQUEUE(&context->cancel->callbacks, &task->cancel);

		GC_ADDREF(&context->std);
	}

	task->suspended = async_awaitable_register_continuation(q, task, data, task_continuation);

	task->fiber.value = USED_RET() ? return_value : NULL;
	task->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;

	ASYNC_FIBER_BACKUP_EG(task->fiber.stack, stack_page_size, task->fiber.exec);
	ASYNC_CHECK_FATAL(!async_fiber_yield(task->fiber.context), "Failed to yield from fiber");
	ASYNC_FIBER_RESTORE_EG(task->fiber.stack, stack_page_size, task->fiber.exec);

	ASYNC_G(current_context) = context;

	// Dispose of cancel handler if task continued without cancellation.
	if (cancellable && context->cancel != NULL) {
		if (task->suspended == NULL) {
			ASYNC_Q_DETACH(&context->cancel->callbacks, &task->cancel);
		}

		OBJ_RELEASE(&context->std);
	}

	// Dispose of continuation if task continues before continuation fired.
	if (task->suspended != NULL) {
		async_awaitable_dispose_continuation(q, task->suspended);
		task->suspended = NULL;
	}

	// Re-throw error provided by task scheduler.
	if (Z_TYPE_P(&task->error) != IS_UNDEF) {
		error = task->error;
		ZVAL_UNDEF(&task->error);

		execute_data->opline--;
		zend_throw_exception_internal(&error);
		execute_data->opline++;

		return;
	}

	ASYNC_CHECK_ERROR(task->fiber.disposed, "Task has been destroyed");
}

static void async_task_execute_inline(async_task *task, async_task *inner)
{
	async_context *context;
	zend_bool success;

	inner->operation = ASYNC_TASK_OPERATION_NONE;

	async_task_scheduler_dequeue(inner);

	// Mark inner fiber as suspended to avoid duplicate inlining attempts.
	inner->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;

	OBJ_RELEASE(&inner->fiber.std);

	context = ASYNC_G(current_context);
	ASYNC_G(current_context) = inner->context;

	inner->fiber.fci.retval = &inner->result;

	zend_call_function(&inner->fiber.fci, &inner->fiber.fcc);

	zval_ptr_dtor(&inner->fiber.fci.function_name);
	zend_fcall_info_args_clear(&inner->fiber.fci, 1);

	ASYNC_G(current_context) = context;

	if (UNEXPECTED(EG(exception))) {
		inner->fiber.status = ASYNC_FIBER_STATUS_FAILED;

		ZVAL_OBJ(&inner->result, EG(exception));
		EG(exception) = NULL;
	} else {
		inner->fiber.status = ASYNC_FIBER_STATUS_FINISHED;

		success = 1;
	}

	async_awaitable_trigger_continuation(&inner->continuation, &inner->result, success);
}

HashTable *async_task_get_debug_info(async_task *task, zend_bool include_result)
{
	HashTable *info;

	info = async_info_init();

	async_info_prop_cstr(info, "status", async_status_label(task->fiber.status));
	async_info_prop_bool(info, "suspended", task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED);
	async_info_prop_str(info, "file", task->fiber.file);
	async_info_prop_long(info, "line", task->fiber.line);

	if (include_result) {
		async_info_prop(info, "result", &task->result);
	}

	return info;
}


async_task *async_task_object_create(zend_execute_data *call, async_task_scheduler *scheduler, async_context *context)
{
	async_task *task;
	zend_long stack_size;

	ZEND_ASSERT(scheduler != NULL);
	ZEND_ASSERT(context != NULL);

	task = emalloc(sizeof(async_task));
	ZEND_SECURE_ZERO(task, sizeof(async_task));

	task->fiber.type = ASYNC_FIBER_TYPE_TASK;
	task->fiber.status = ASYNC_FIBER_STATUS_INIT;

	stack_size = ASYNC_G(stack_size);

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	task->fiber.stack_size = stack_size;

	ZVAL_NULL(&task->result);
	ZVAL_UNDEF(&task->error);

	task->scheduler = scheduler;
	task->context = context;

	GC_ADDREF(&scheduler->std);
	GC_ADDREF(&context->std);

	task->cancel.object = task;
	task->cancel.func = cancel_suspend;

	zend_object_std_init(&task->fiber.std, async_task_ce);
	task->fiber.std.handlers = &async_task_handlers;

	async_fiber_init_metadata(&task->fiber, call);

	return task;
}

void async_task_dispose(async_task *task)
{
	task->operation = ASYNC_TASK_OPERATION_NONE;

	if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
		task->fiber.disposed = 1;

		async_task_continue(task);
	}

	if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
		zend_fcall_info_args_clear(&task->fiber.fci, 1);
		zval_ptr_dtor(&task->fiber.fci.function_name);

		async_awaitable_trigger_continuation(&task->continuation, NULL, 0);
	}
}

static void async_task_object_destroy(zend_object *object)
{
	async_task *task;

	task = (async_task *) object;

	async_fiber_destroy(task->fiber.context);

	if (task->fiber.file != NULL) {
		zend_string_release(task->fiber.file);
	}

	OBJ_RELEASE(&task->context->std);
	OBJ_RELEASE(&task->scheduler->std);

	zval_ptr_dtor(&task->result);
	zval_ptr_dtor(&task->error);

	zend_object_std_dtor(&task->fiber.std);
}

ZEND_METHOD(Task, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Tasks must not be constructed by userland code");
}

ZEND_METHOD(Task, __debugInfo)
{
	async_task *task;

	ZEND_PARSE_PARAMETERS_NONE();

	task = (async_task *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		RETURN_ARR(async_task_get_debug_info(task, 1));
	}
}

ZEND_METHOD(Task, isRunning)
{
	async_fiber *fiber;

	ZEND_PARSE_PARAMETERS_NONE();

	fiber = ASYNC_G(current_fiber);

	RETURN_BOOL(fiber != NULL && fiber->type == ASYNC_FIBER_TYPE_TASK);
}

ZEND_METHOD(Task, async)
{
	async_task *task;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t count;
	uint32_t i;

	zval *params;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	for (i = 1; i <= count; i++) {
		ASYNC_CHECK_ERROR(ARG_SHOULD_BE_SENT_BY_REF(fcc.function_handler, i), "Cannot pass async call argument %d by reference", (int) i);
	}

	fci.no_separation = 1;

	if (count == 0) {
		fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&fci, count, params);
	}

	Z_TRY_ADDREF_P(&fci.function_name);

	task = async_task_object_create(EX(prev_execute_data), async_task_scheduler_get(), async_context_get());
	task->fiber.fci = fci;
	task->fiber.fcc = fcc;

	async_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->fiber.std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, asyncWithContext)
{
	async_task *task;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t count;
	uint32_t i;

	zval *ctx;
	zval *params;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, -1)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	for (i = 1; i <= count; i++) {
		ASYNC_CHECK_ERROR(ARG_SHOULD_BE_SENT_BY_REF(fcc.function_handler, i), "Cannot pass async call argument %d by reference", (int) i);
	}

	fci.no_separation = 1;

	if (count == 0) {
		fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&fci, count, params);
	}

	Z_TRY_ADDREF_P(&fci.function_name);

	task = async_task_object_create(EX(prev_execute_data), async_task_scheduler_get(), (async_context *) Z_OBJ_P(ctx));
	task->fiber.fci = fci;
	task->fiber.fcc = fcc;

	async_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->fiber.std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, await)
{
	zend_class_entry *ce;
	async_fiber *fiber;
	async_task *task;
	async_task *inner;
	async_awaitable_cb *cont;
	async_awaitable_queue *q;
	async_task_scheduler *scheduler;
	async_deferred *defer;
	async_context *context;
	size_t stack_page_size;

	zval *val;
	zval error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	fiber = ASYNC_G(current_fiber);

	ce = (Z_TYPE_P(val) == IS_OBJECT) ? Z_OBJCE_P(val) : NULL;

	// Check for root level await.
	if (fiber == NULL) {
		scheduler = async_task_scheduler_get();

		ASYNC_CHECK_ERROR(scheduler->running, "Cannot await in the fiber that is running the task scheduler loop");

		if (ce == async_deferred_awaitable_ce) {
			defer = ((async_deferred_awaitable *) Z_OBJ_P(val))->defer;

			ASYNC_TASK_DELEGATE_RESULT(defer->status, &defer->result);

			q = &defer->continuation;
			cont = async_awaitable_register_continuation(q, scheduler, NULL, top_level_continuation);
			async_task_scheduler_run_loop(scheduler);

			ASYNC_TASK_DELEGATE_RESULT(defer->status, &defer->result);
		} else {
			inner = (async_task *) Z_OBJ_P(val);

			ASYNC_CHECK_ERROR(inner->scheduler != scheduler, "Cannot await a task that runs on a different task scheduler");

			ASYNC_TASK_DELEGATE_RESULT(inner->fiber.status, &inner->result);

			q = &inner->continuation;
			cont = async_awaitable_register_continuation(q, inner->scheduler, NULL, top_level_continuation);
			async_task_scheduler_run_loop(inner->scheduler);

			ASYNC_TASK_DELEGATE_RESULT(inner->fiber.status, &inner->result);
		}

		async_awaitable_dispose_continuation(q, cont);

		if (EXPECTED(EG(exception) == NULL)) {
			zend_throw_error(NULL, "Awaitable has not been resolved");
		}

		return;
	}

	ASYNC_CHECK_ERROR(fiber->type != ASYNC_FIBER_TYPE_TASK, "Await must be called from within a running task");
	ASYNC_CHECK_ERROR(fiber->status != ASYNC_FIBER_STATUS_RUNNING, "Cannot await in a task that is not running");
	ASYNC_CHECK_ERROR(fiber->disposed, "Task has been destroyed");

	task = (async_task *) fiber;

	ASYNC_CHECK_ERROR(!ASYNC_G(current_scheduler)->dispatching, "Cannot await while the task scheduler is not dispatching tasks");

	if (ce == async_task_ce) {
		inner = (async_task *) Z_OBJ_P(val);

		ASYNC_CHECK_ERROR(inner->scheduler != task->scheduler, "Cannot await a task that runs on a different task scheduler");

		// Perform task-inlining optimization where applicable.
		if (inner->fiber.status == ASYNC_FIBER_STATUS_INIT && inner->fiber.stack_size <= task->fiber.stack_size) {
			async_task_execute_inline(task, inner);
		}

		ASYNC_TASK_DELEGATE_RESULT(inner->fiber.status, &inner->result);

		q = &inner->continuation;
		task->suspended = async_awaitable_register_continuation(q, task, NULL, task_continuation);
	} else {
		defer = ((async_deferred_awaitable *) Z_OBJ_P(val))->defer;

		ASYNC_TASK_DELEGATE_RESULT(defer->status, &defer->result);

		q = &defer->continuation;
		task->suspended = async_awaitable_register_continuation(q, task, NULL, task_continuation);
	}

	task->fiber.value = USED_RET() ? return_value : NULL;
	task->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;

	context = ASYNC_G(current_context);

	ASYNC_FIBER_BACKUP_EG(task->fiber.stack, stack_page_size, task->fiber.exec);
	ASYNC_CHECK_FATAL(!async_fiber_yield(task->fiber.context), "Failed to yield from fiber");
	ASYNC_FIBER_RESTORE_EG(task->fiber.stack, stack_page_size, task->fiber.exec);

	ASYNC_G(current_context) = context;

	// Dispose of continuation if task continues before continuation fired.
	if (task->suspended != NULL) {
		async_awaitable_dispose_continuation(q, task->suspended);
		task->suspended = NULL;
	}

	// Re-throw error provided by task scheduler.
	if (Z_TYPE_P(&task->error) != IS_UNDEF) {
		error = task->error;
		ZVAL_UNDEF(&task->error);

		execute_data->opline--;
		zend_throw_exception_internal(&error);
		execute_data->opline++;

		return;
	}

	ASYNC_CHECK_ERROR(task->fiber.disposed, "Task has been destroyed");
}

ZEND_METHOD(Task, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_debug_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_task_is_running, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_task_async, 0, 1, Concurrent\\Task, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_task_async_with_context, 0, 2, Concurrent\\Task, 0)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_await, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, awaitable, Concurrent\\Awaitable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_functions[] = {
	ZEND_ME(Task, __construct, arginfo_task_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(Task, __debugInfo, arginfo_task_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(Task, isRunning, arginfo_task_is_running, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, async, arginfo_task_async, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, asyncWithContext, arginfo_task_async_with_context, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, await, arginfo_task_await, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, __wakeup, arginfo_task_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_task_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Task", task_functions);
	async_task_ce = zend_register_internal_class(&ce);
	async_task_ce->ce_flags |= ZEND_ACC_FINAL;
	async_task_ce->serialize = zend_class_serialize_deny;
	async_task_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_task_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_task_handlers.free_obj = async_task_object_destroy;
	async_task_handlers.clone_obj = NULL;

	zend_class_implements(async_task_ce, 1, async_awaitable_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
