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


#include "php.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"

#include "php_async.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_task_ce;

const zend_uchar ASYNC_FIBER_TYPE_TASK = 1;

const zend_uchar ASYNC_TASK_OPERATION_NONE = 0;
const zend_uchar ASYNC_TASK_OPERATION_START = 1;
const zend_uchar ASYNC_TASK_OPERATION_RESUME = 2;

static zend_object_handlers async_task_handlers;


void async_task_start(async_task *task)
{
	async_context *context;

	task->operation = ASYNC_TASK_OPERATION_NONE;
	task->fiber.context = async_fiber_create_context();

	if (task->fiber.context == NULL) {
		zend_throw_error(NULL, "Failed to create native fiber context");
		return;
	}

	if (!async_fiber_create(task->fiber.context, async_fiber_run, task->fiber.stack_size)) {
		zend_throw_error(NULL, "Failed to create native fiber");
		return;
	}

	task->fiber.stack = (zend_vm_stack) emalloc(ASYNC_FIBER_VM_STACK_SIZE);
	task->fiber.stack->top = ZEND_VM_STACK_ELEMENTS(task->fiber.stack) + 1;
	task->fiber.stack->end = (zval *) ((char *) task->fiber.stack + ASYNC_FIBER_VM_STACK_SIZE);
	task->fiber.stack->prev = NULL;

	task->fiber.status = ASYNC_FIBER_STATUS_RUNNING;

	context = ASYNC_G(current_context);
	ASYNC_G(current_context) = task->context;

	if (!async_fiber_switch_to(&task->fiber)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}

	ASYNC_G(current_context) = context;

	zend_fcall_info_args_clear(&task->fiber.fci, 1);
}

void async_task_continue(async_task *task)
{
	task->operation = ASYNC_TASK_OPERATION_NONE;
	task->fiber.status = ASYNC_FIBER_STATUS_RUNNING;

	if (!async_fiber_switch_to(&task->fiber)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}
}

static void async_task_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_task *task;

	task = (async_task *) obj;

	if (task->fiber.status != ASYNC_FIBER_STATUS_SUSPENDED) {
		task->fiber.value = NULL;

		return;
	}

	if (result != NULL) {
		if (success) {
			if (task->fiber.value != NULL) {
				ZVAL_COPY(task->fiber.value, result);
			}
		} else {
			ZVAL_COPY(&task->error, result);
		}
	}

	task->fiber.value = NULL;

	async_task_scheduler_enqueue(task);

	OBJ_RELEASE(&task->fiber.std);
}

static void async_task_execute_inline(async_task *task, async_task *inner)
{
	async_context *context;
	zend_bool success;

	inner->operation = ASYNC_TASK_OPERATION_NONE;

	context = ASYNC_G(current_context);
	ASYNC_G(current_context) = inner->context;

	inner->fiber.fci.retval = &inner->result;

	zend_call_function(&inner->fiber.fci, &inner->fiber.fcc);

	zval_ptr_dtor(&inner->fiber.fci.function_name);
	zend_fcall_info_args_clear(&inner->fiber.fci, 1);

	ASYNC_G(current_context) = context;

	if (UNEXPECTED(EG(exception))) {
		inner->fiber.status = ASYNC_FIBER_STATUS_DEAD;

		ZVAL_OBJ(&inner->result, EG(exception));
		EG(exception) = NULL;
	} else {
		inner->fiber.status = ASYNC_FIBER_STATUS_FINISHED;

		success = 1;
	}

	async_awaitable_trigger_continuation(&inner->continuation, &inner->result, success);
}

async_task *async_task_object_create()
{
	async_task *task;
	zend_long stack_size;

	task = emalloc(sizeof(async_task));
	ZEND_SECURE_ZERO(task, sizeof(async_task));

	task->fiber.type = ASYNC_FIBER_TYPE_TASK;
	task->fiber.status = ASYNC_FIBER_STATUS_INIT;

	task->id = ASYNC_G(counter) + 1;
	ASYNC_G(counter) = task->id;

	stack_size = ASYNC_G(stack_size);

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	task->fiber.stack_size = stack_size;

	ZVAL_NULL(&task->result);
	ZVAL_UNDEF(&task->error);

	// The final send value is the task result, pointer will be overwritten and restored during await.
	task->fiber.value = &task->result;

	zend_object_std_init(&task->fiber.std, async_task_ce);
	task->fiber.std.handlers = &async_task_handlers;

	return task;
}

static void async_task_object_destroy(zend_object *object)
{
	async_task *task;

	task = (async_task *) object;

	if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
		task->fiber.status = ASYNC_FIBER_STATUS_DEAD;

		async_fiber_switch_to(&task->fiber);
	}

	if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
		zend_fcall_info_args_clear(&task->fiber.fci, 1);

		zval_ptr_dtor(&task->fiber.fci.function_name);
	}

	if (task->continuation != NULL) {
		async_awaitable_dispose_continuation(&task->continuation);
	}

	zval_ptr_dtor(&task->result);
	zval_ptr_dtor(&task->error);

	OBJ_RELEASE(&task->context->std);

	async_fiber_destroy(task->fiber.context);

	zend_object_std_dtor(&task->fiber.std);
}

ZEND_METHOD(Task, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Tasks must not be constructed by userland code");
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
	async_task * task;
	uint32_t count;

	zval *params;
	zval obj;

	task = async_task_object_create();
	task->scheduler = async_task_scheduler_get();
	task->context = async_context_get();

	ZEND_ASSERT(task->scheduler != NULL);

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_FUNC_EX(task->fiber.fci, task->fiber.fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	task->fiber.fci.no_separation = 1;

	if (count == 0) {
		task->fiber.fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&task->fiber.fci, count, params);
	}

	Z_TRY_ADDREF_P(&task->fiber.fci.function_name);

	GC_ADDREF(&task->context->std);

	async_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->fiber.std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, asyncWithContext)
{
	async_task * task;
	uint32_t count;

	zval *ctx;
	zval *params;
	zval obj;

	task = async_task_object_create();

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, -1)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(task->fiber.fci, task->fiber.fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	task->fiber.fci.no_separation = 1;

	if (count == 0) {
		task->fiber.fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&task->fiber.fci, count, params);
	}

	Z_TRY_ADDREF_P(&task->fiber.fci.function_name);

	task->scheduler = async_task_scheduler_get();
	task->context = (async_context *) Z_OBJ_P(ctx);

	ZEND_ASSERT(task->scheduler != NULL);

	GC_ADDREF(&task->context->std);

	async_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->fiber.std);

	RETURN_ZVAL(&obj, 1, 1);
}

static void async_task_top_level_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_task_stop_info *stop;

	stop = (async_task_stop_info *) obj;

	if (stop->required) {
		stop->required = 0;

		async_task_scheduler_stop_loop(stop->scheduler);
	} else {
		efree(stop);
	}
}

ZEND_METHOD(Task, await)
{
	zend_class_entry *ce;
	async_fiber *fiber;
	async_task *task;
	async_task *inner;
	async_task_scheduler *scheduler;
	async_deferred *defer;
	async_context *context;
	size_t stack_page_size;

	async_task_stop_info *stop;

	zval *val;
	zval *value;
	zval error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	fiber = ASYNC_G(current_fiber);

	if (fiber == NULL) {
		ce = Z_OBJCE_P(val);

		if (Z_TYPE_P(val) != IS_OBJECT) {
			RETURN_ZVAL(val, 1, 0);
		}

		if (ce == async_deferred_awaitable_ce) {
			scheduler = async_task_scheduler_get();

			if (scheduler->running) {
				zend_throw_error(NULL, "Cannot dispatch tasks because the scheduler is already running");
				return;
			}

			defer = ((async_deferred_awaitable *) Z_OBJ_P(val))->defer;

			stop = (async_task_stop_info *) emalloc(sizeof(async_task_stop_info));
			stop->scheduler = scheduler;
			stop->required = 1;

			async_awaitable_register_continuation(&defer->continuation, stop, NULL, async_task_top_level_continuation);

			async_task_scheduler_run_loop(scheduler);

			if (stop->required) {
				stop->required = 0;
			} else {
				efree(stop);
			}

			if (defer->status == ASYNC_DEFERRED_STATUS_RESOLVED) {
				RETURN_ZVAL(&defer->result, 1, 0);
			}

			if (defer->status == ASYNC_DEFERRED_STATUS_FAILED) {
				Z_ADDREF_P(&defer->result);

				execute_data->opline--;
				zend_throw_exception_internal(&defer->result);
				execute_data->opline++;

				return;
			}

			zend_throw_error(NULL, "Awaitable has not been resolved");
			return;
		}

		inner = (async_task *) Z_OBJ_P(val);

		ZEND_ASSERT(inner->scheduler != NULL);

		if (inner->scheduler->running) {
			zend_throw_error(NULL, "Cannot dispatch tasks because the scheduler is already running");
			return;
		}

		stop = (async_task_stop_info *) emalloc(sizeof(async_task_stop_info));
		stop->scheduler = inner->scheduler;
		stop->required = 1;

		async_awaitable_register_continuation(&inner->continuation, stop, NULL, async_task_top_level_continuation);

		async_task_scheduler_run_loop(inner->scheduler);

		if (stop->required) {
			stop->required = 0;
		} else {
			efree(stop);
		}

		if (inner->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
			OBJ_RELEASE(&inner->fiber.std);
		}

		if (inner->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
			RETURN_ZVAL(&inner->result, 1, 0);
		}

		if (inner->fiber.status == ASYNC_FIBER_STATUS_DEAD) {
			Z_ADDREF_P(&inner->result);

			execute_data->opline--;
			zend_throw_exception_internal(&inner->result);
			execute_data->opline++;

			return;
		}

		zend_throw_error(NULL, "Awaitable has not been resolved");

		return;
	}

	if (fiber->type != ASYNC_FIBER_TYPE_TASK) {
		zend_throw_error(NULL, "Await must be called from within a running task");
		return;
	}

	if (UNEXPECTED(fiber->status != ASYNC_FIBER_STATUS_RUNNING)) {
		zend_throw_error(NULL, "Cannot await in a task that is not running");
		return;
	}

	task = (async_task *) fiber;

	ZEND_ASSERT(task->scheduler != NULL);

	if (Z_TYPE_P(val) != IS_OBJECT) {
		RETURN_ZVAL(val, 1, 0);
	}

	ce = Z_OBJCE_P(val);

	// Immediately return non-awaitable objects.
	if (instanceof_function_ex(ce, async_awaitable_ce, 1) != 1) {
		RETURN_ZVAL(val, 1, 0);
	}

	if (ce == async_task_ce) {
		inner = (async_task *) Z_OBJ_P(val);

		if (inner->scheduler != task->scheduler) {
			zend_throw_error(NULL, "Cannot await a task that runs on a different task scheduler");
			return;
		}

		if (inner->fiber.status == ASYNC_FIBER_STATUS_INIT) {
			if (inner->fiber.stack_size <= task->fiber.stack_size) {
				async_task_execute_inline(task, inner);
			}
		}

		if (inner->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
			RETURN_ZVAL(&inner->result, 1, 0);
		}

		if (inner->fiber.status == ASYNC_FIBER_STATUS_DEAD) {
			Z_ADDREF_P(&inner->result);

			execute_data->opline--;
			zend_throw_exception_internal(&inner->result);
			execute_data->opline++;

			return;
		}

		async_awaitable_register_continuation(&inner->continuation, task, NULL, async_task_continuation);
	} else if (ce == async_deferred_awaitable_ce) {
		defer = ((async_deferred_awaitable *) Z_OBJ_P(val))->defer;

		if (defer->status == ASYNC_DEFERRED_STATUS_RESOLVED) {
			RETURN_ZVAL(&defer->result, 1, 0);
		}

		if (defer->status == ASYNC_DEFERRED_STATUS_FAILED) {
			Z_ADDREF_P(&defer->result);

			execute_data->opline--;
			zend_throw_exception_internal(&defer->result);
			execute_data->opline++;

			return;
		}

		async_awaitable_register_continuation(&defer->continuation, task, NULL, async_task_continuation);
	} else {
		RETURN_ZVAL(val, 1, 0);
	}

	// Switch the value pointer to the return value of await() until the task is continued.
	value = task->fiber.value;
	task->fiber.value = USED_RET() ? return_value : NULL;

	task->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;

	GC_ADDREF(&task->fiber.std);

	context = ASYNC_G(current_context);

	ASYNC_FIBER_BACKUP_EG(task->fiber.stack, stack_page_size, task->fiber.exec);
	async_fiber_yield(task->fiber.context);
	ASYNC_FIBER_RESTORE_EG(task->fiber.stack, stack_page_size, task->fiber.exec);

	ASYNC_G(current_context) = context;

	task->fiber.value = value;

	if (Z_TYPE_P(&task->error) != IS_UNDEF) {
		error = task->error;
		ZVAL_UNDEF(&task->error);

		execute_data->opline--;
		zend_throw_exception_internal(&error);
		execute_data->opline++;

		return;
	}

	if (task->fiber.status == ASYNC_FIBER_STATUS_DEAD) {
		zend_throw_error(NULL, "Task has been destroyed");
		return;
	}
}

ZEND_METHOD(Task, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_ctor, 0)
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
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_functions[] = {
	ZEND_ME(Task, __construct, arginfo_task_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
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
