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

zend_class_entry *async_task_scheduler_ce;

static zend_object_handlers async_task_scheduler_handlers;

static zend_string *str_activate;
static zend_string *str_runloop;
static zend_string *str_stoploop;


async_task_scheduler *async_task_scheduler_get()
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

	scheduler = emalloc(sizeof(async_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(async_task_scheduler));

	zend_object_std_init(&scheduler->std, async_task_scheduler_ce);
	scheduler->std.handlers = &async_task_scheduler_handlers;

	ASYNC_G(scheduler) = scheduler;

	return scheduler;
}

zend_bool async_task_scheduler_enqueue(async_task *task)
{
	async_task_scheduler *scheduler;

	zval obj;
	zval retval;

	scheduler = task->scheduler;

	ZEND_ASSERT(scheduler != NULL);

	if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
		task->operation = ASYNC_TASK_OPERATION_START;
	} else if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
		task->operation = ASYNC_TASK_OPERATION_RESUME;
	} else {
		return 0;
	}

	if (scheduler->last == NULL) {
		scheduler->first = task;
		scheduler->last = task;
	} else {
		scheduler->last->next = task;
		scheduler->last = task;
	}

	GC_ADDREF(&task->fiber.std);

	scheduler->scheduled++;

	if (scheduler->activate && !scheduler->dispatching) {
		scheduler->activate = 0;

		ZVAL_OBJ(&obj, &scheduler->std);
		GC_ADDREF(&scheduler->std);

		zval *entry = zend_hash_find_ex(&scheduler->std.ce->function_table, str_activate, 1);
		zend_function *func = Z_FUNC_P(entry);

		zend_call_method_with_0_params(&obj, Z_OBJCE_P(&obj), &func, "activate", &retval);
		zval_ptr_dtor(&obj);
		zval_ptr_dtor(&retval);
	}

	return 1;
}

void async_task_scheduler_run_loop(async_task_scheduler *scheduler)
{
	async_task_scheduler *prev;

	zval obj;
	zval retval;

	prev = ASYNC_G(current_scheduler);
	ASYNC_G(current_scheduler) = scheduler;

	scheduler->running = 1;

	ZVAL_OBJ(&obj, &scheduler->std);
	GC_ADDREF(&scheduler->std);

	zval *entry = zend_hash_find_ex(&scheduler->std.ce->function_table, str_runloop, 1);
	zend_function *func = Z_FUNC_P(entry);

	zend_call_method_with_0_params(&obj, Z_OBJCE_P(&obj), &func, "runloop", &retval);
	zval_ptr_dtor(&obj);
	zval_ptr_dtor(&retval);

	scheduler->running = 0;

	ASYNC_G(current_scheduler) = prev;
}

void async_task_scheduler_stop_loop(async_task_scheduler *scheduler)
{
	zval obj;
	zval retval;

	ZVAL_OBJ(&obj, &scheduler->std);
	GC_ADDREF(&scheduler->std);

	zval *entry = zend_hash_find_ex(&scheduler->std.ce->function_table, str_stoploop, 1);
	zend_function *func = Z_FUNC_P(entry);

	zend_call_method_with_0_params(&obj, Z_OBJCE_P(&obj), &func, "stoploop", &retval);
	zval_ptr_dtor(&obj);
	zval_ptr_dtor(&retval);
}

static void async_task_scheduler_run(async_task_scheduler *scheduler)
{
	async_task *task;

	scheduler->dispatching = 1;
	scheduler->activate = 0;

	while (scheduler->first != NULL) {
		task = scheduler->first;

		scheduler->scheduled--;
		scheduler->first = task->next;

		if (scheduler->last == task) {
			scheduler->last = NULL;
		}

		task->next = NULL;

		// A task scheduled for start might have been inlined, do not take action in this case.
		if (task->operation != ASYNC_TASK_OPERATION_NONE) {
			if (task->operation == ASYNC_TASK_OPERATION_START) {
				async_task_start(task);
			} else {
				async_task_continue(task);
			}

			if (UNEXPECTED(EG(exception))) {
				ZVAL_OBJ(&task->result, EG(exception));
				EG(exception) = NULL;

				task->fiber.status = ASYNC_FIBER_STATUS_DEAD;
			}

			if (task->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
				if (Z_TYPE_P(&task->result) == IS_OBJECT && instanceof_function_ex(Z_OBJCE_P(&task->result), async_awaitable_ce, 1) != 0) {
					zend_throw_error(NULL, "Task must not return an object implementing Awaitable");

					zval_ptr_dtor(&task->result);
					ZVAL_OBJ(&task->result, EG(exception));
					EG(exception) = NULL;

					task->fiber.status = ASYNC_FIBER_STATUS_DEAD;
				}

				async_awaitable_trigger_continuation(&task->continuation, &task->result, 1);
			} else if (task->fiber.status == ASYNC_FIBER_STATUS_DEAD) {
				async_awaitable_trigger_continuation(&task->continuation, &task->result, 0);
			}
		}

		OBJ_RELEASE(&task->fiber.std);
	}

	scheduler->last = NULL;

	scheduler->dispatching = 0;
	scheduler->activate = 1;
}


static zend_object *async_task_scheduler_object_create(zend_class_entry *ce)
{
	async_task_scheduler *scheduler;

	scheduler = emalloc(sizeof(async_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(async_task_scheduler));

	scheduler->activate = 1;

	zend_object_std_init(&scheduler->std, ce);
	scheduler->std.handlers = &async_task_scheduler_handlers;

	return &scheduler->std;
}

static void async_task_scheduler_object_destroy(zend_object *object)
{
	async_task_scheduler *scheduler;

	scheduler = (async_task_scheduler *) object;

	// Do not re-run root scheduler, this is done by the executor hook instead.
	if (ASYNC_G(scheduler) != scheduler) {
		async_task_scheduler_run_loop(scheduler);
	}

	zend_object_std_dtor(&scheduler->std);
}

ZEND_METHOD(TaskScheduler, count)
{
	async_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (async_task_scheduler *)Z_OBJ_P(getThis());

	RETURN_LONG(scheduler->scheduled);
}

ZEND_METHOD(TaskScheduler, activate)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TaskScheduler, run)
{
	async_task_scheduler *scheduler;
	async_task *task;
	uint32_t count;

	zval *params;
	zval retval;

	scheduler = (async_task_scheduler *) Z_OBJ_P(getThis());

	task = async_task_object_create();
	task->scheduler = scheduler;
	task->context = async_context_get();

	GC_ADDREF(&task->context->std);

	params = NULL;

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

	async_task_scheduler_enqueue(task);
	async_task_scheduler_run_loop(scheduler);

	if (task->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
		ZVAL_COPY(&retval, &task->result);
		OBJ_RELEASE(&task->fiber.std);

		RETURN_ZVAL(&retval, 1, 1);
	}

	if (task->fiber.status == ASYNC_FIBER_STATUS_DEAD) {
		ZVAL_COPY(&retval, &task->result);
		OBJ_RELEASE(&task->fiber.std);

		execute_data->opline--;
		zend_throw_exception_internal(&retval);
		execute_data->opline++;

		return;
	}

	OBJ_RELEASE(&task->fiber.std);

	zend_throw_error(NULL, "Awaitable has not been resolved");
}

ZEND_METHOD(TaskScheduler, runWithContext)
{
	async_task_scheduler *scheduler;
	async_task *task;
	uint32_t count;

	zval *ctx;
	zval *params;
	zval retval;

	scheduler = (async_task_scheduler *) Z_OBJ_P(getThis());

	task = async_task_object_create();
	task->scheduler = scheduler;

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, -1)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(task->fiber.fci, task->fiber.fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	task->context = (async_context *) Z_OBJ_P(ctx);
	task->fiber.fci.no_separation = 1;

	GC_ADDREF(&task->context->std);

	if (count == 0) {
		task->fiber.fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&task->fiber.fci, count, params);
	}

	Z_TRY_ADDREF_P(&task->fiber.fci.function_name);

	async_task_scheduler_enqueue(task);
	async_task_scheduler_run_loop(scheduler);

	if (task->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
		ZVAL_COPY(&retval, &task->result);
		OBJ_RELEASE(&task->fiber.std);

		RETURN_ZVAL(&retval, 1, 1);
	}

	if (task->fiber.status == ASYNC_FIBER_STATUS_DEAD) {
		ZVAL_COPY(&retval, &task->result);
		OBJ_RELEASE(&task->fiber.std);

		execute_data->opline--;
		zend_throw_exception_internal(&retval);
		execute_data->opline++;

		return;
	}

	OBJ_RELEASE(&task->fiber.std);

	zend_throw_error(NULL, "Awaitable has not been resolved");
}

ZEND_METHOD(TaskScheduler, dispatch)
{
	async_task_scheduler *scheduler;

	// Left out on purpose to allow for simplified event-loop integration.
	// ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (async_task_scheduler *) Z_OBJ_P(getThis());

	if (scheduler->dispatching) {
		zend_throw_error(NULL, "Cannot dispatch tasks because the dispatcher is already running");
		return;
	}

	async_task_scheduler_run(scheduler);
}

ZEND_METHOD(TaskScheduler, runLoop)
{
	async_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (async_task_scheduler *) Z_OBJ_P(getThis());

	if (scheduler->dispatching) {
		zend_throw_error(NULL, "Cannot dispatch tasks because the dispatcher is already running");
		return;
	}

	async_task_scheduler_run(scheduler);
}

ZEND_METHOD(TaskScheduler, stopLoop)
{
	async_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (async_task_scheduler *) Z_OBJ_P(getThis());

	if (!scheduler->dispatching) {
		zend_throw_error(NULL, "Cannot stop loop that is not running");
		return;
	}
}

ZEND_METHOD(TaskScheduler, setDefaultScheduler)
{
	async_task_scheduler *scheduler;

	zval *val;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	scheduler = ASYNC_G(scheduler);

	if (scheduler != NULL) {
		zend_throw_error(NULL, "The default task scheduler must not be changed after it has been used for the first time");
		return;
	}

	scheduler = (async_task_scheduler *) Z_OBJ_P(val);

	ASYNC_G(scheduler) = scheduler;

	GC_ADDREF(&scheduler->std);
}

ZEND_METHOD(TaskScheduler, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task scheduler is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_count, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_activate, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_run, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_run_with_context, 0, 0, 2)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_run_loop, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_stop_loop, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_dispatch, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_set_default_scheduler, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, scheduler, Concurrent\\TaskScheduler, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_scheduler_functions[] = {
	ZEND_ME(TaskScheduler, count, arginfo_task_scheduler_count, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
	ZEND_ME(TaskScheduler, activate, arginfo_task_scheduler_activate, ZEND_ACC_PROTECTED)
	ZEND_ME(TaskScheduler, run, arginfo_task_scheduler_run, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
	ZEND_ME(TaskScheduler, runWithContext, arginfo_task_scheduler_run_with_context, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
	ZEND_ME(TaskScheduler, dispatch, arginfo_task_scheduler_dispatch, ZEND_ACC_PROTECTED | ZEND_ACC_FINAL)
	ZEND_ME(TaskScheduler, runLoop, arginfo_task_scheduler_run_loop, ZEND_ACC_PROTECTED)
	ZEND_ME(TaskScheduler, stopLoop, arginfo_task_scheduler_stop_loop, ZEND_ACC_PROTECTED)
	ZEND_ME(TaskScheduler, setDefaultScheduler, arginfo_task_scheduler_set_default_scheduler, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC | ZEND_ACC_FINAL)
	ZEND_ME(TaskScheduler, __wakeup, arginfo_task_scheduler_wakeup, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
	ZEND_FE_END
};


void async_task_scheduler_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskScheduler", task_scheduler_functions);
	async_task_scheduler_ce = zend_register_internal_class(&ce);
	async_task_scheduler_ce->create_object = async_task_scheduler_object_create;
	async_task_scheduler_ce->serialize = zend_class_serialize_deny;
	async_task_scheduler_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(async_task_scheduler_ce, 1, zend_ce_countable);

	memcpy(&async_task_scheduler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_task_scheduler_handlers.free_obj = async_task_scheduler_object_destroy;
	async_task_scheduler_handlers.clone_obj = NULL;

	str_activate = zend_string_init("activate", sizeof("activate")-1, 1);
	str_runloop = zend_string_init("runloop", sizeof("runloop")-1, 1);
	str_stoploop = zend_string_init("stoploop", sizeof("stoploop")-1, 1);

	ZSTR_HASH(str_activate);
	ZSTR_HASH(str_runloop);
	ZSTR_HASH(str_stoploop);
}

void async_task_scheduler_ce_unregister()
{
	zend_string_free(str_activate);
	zend_string_free(str_runloop);
	zend_string_free(str_stoploop);
}

void async_task_scheduler_shutdown()
{
	async_task_scheduler *scheduler;

	scheduler = ASYNC_G(scheduler);

	if (scheduler != NULL) {
		OBJ_RELEASE(&scheduler->std);
	}
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
