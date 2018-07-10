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

#include "php_task.h"

ZEND_DECLARE_MODULE_GLOBALS(task)

zend_class_entry *concurrent_task_scheduler_ce;

static zend_object_handlers concurrent_task_scheduler_handlers;


concurrent_task_scheduler *concurrent_task_scheduler_get()
{
	concurrent_task_scheduler *scheduler;

	scheduler = TASK_G(current_scheduler);

	if (scheduler != NULL) {
		return scheduler;
	}

	scheduler = TASK_G(scheduler);

	if (scheduler != NULL) {
		return scheduler;
	}

	scheduler = emalloc(sizeof(concurrent_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(concurrent_task_scheduler));

	zend_object_std_init(&scheduler->std, concurrent_task_scheduler_ce);
	scheduler->std.handlers = &concurrent_task_scheduler_handlers;

	GC_ADDREF(&scheduler->std);

	TASK_G(scheduler) = scheduler;

	return scheduler;
}

zend_bool concurrent_task_scheduler_enqueue(concurrent_task *task)
{
	concurrent_task_scheduler *scheduler;

	zval obj;
	zval retval;

	scheduler = task->scheduler;

	ZEND_ASSERT(scheduler != NULL);

	if (task->fiber.status == CONCURRENT_FIBER_STATUS_INIT) {
		task->operation = CONCURRENT_TASK_OPERATION_START;
	} else if (task->fiber.status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		task->operation = CONCURRENT_TASK_OPERATION_RESUME;
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

	if (scheduler->activate && !scheduler->running) {
		scheduler->activate = 0;

		ZVAL_OBJ(&obj, &scheduler->std);

		zend_string *name = zend_string_init("activate", sizeof("activate")-1, 0);
		zval *entry = zend_hash_find_ex(&scheduler->std.ce->function_table, name, 0);
		zend_function *func = Z_FUNC_P(entry);

		zend_call_method_with_0_params(&obj, Z_OBJCE_P(&obj), &func, "activate", &retval);
		zval_ptr_dtor(&retval);
	}

	return 1;
}

void concurrent_task_scheduler_run(concurrent_task_scheduler *scheduler)
{
	concurrent_task *task;

	scheduler->running = 1;
	scheduler->activate = 0;

	while (scheduler->first != NULL) {
		task = scheduler->first;

		scheduler->scheduled--;
		scheduler->first = task->next;

		if (scheduler->last == task) {
			scheduler->last = NULL;
		}

		// A task scheduled for start might have been inlined, do not take action in this case.
		if (task->operation != CONCURRENT_TASK_OPERATION_NONE) {
			task->next = NULL;

			if (task->operation == CONCURRENT_TASK_OPERATION_START) {
				concurrent_task_start(task);
			} else {
				concurrent_task_continue(task);
			}

			if (UNEXPECTED(EG(exception))) {
				ZVAL_OBJ(&task->result, EG(exception));
				EG(exception) = NULL;

				task->fiber.status = CONCURRENT_FIBER_STATUS_DEAD;
			}

			if (task->fiber.status == CONCURRENT_FIBER_STATUS_FINISHED) {
				concurrent_awaitable_trigger_continuation(&task->continuation, &task->result, 1);
			} else if (task->fiber.status == CONCURRENT_FIBER_STATUS_DEAD) {
				concurrent_awaitable_trigger_continuation(&task->continuation, &task->result, 0);
			}
		}

		OBJ_RELEASE(&task->fiber.std);
	}

	scheduler->last = NULL;

	scheduler->running = 0;
	scheduler->activate = 1;
}


static zend_object *concurrent_task_scheduler_object_create(zend_class_entry *ce)
{
	concurrent_task_scheduler *scheduler;

	scheduler = emalloc(sizeof(concurrent_task_scheduler));
	ZEND_SECURE_ZERO(scheduler, sizeof(concurrent_task_scheduler));

	scheduler->activate = 1;

	zend_object_std_init(&scheduler->std, ce);
	scheduler->std.handlers = &concurrent_task_scheduler_handlers;

	return &scheduler->std;
}

static void concurrent_task_scheduler_object_destroy(zend_object *object)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task *task;

	scheduler = (concurrent_task_scheduler *) object;

	while (scheduler->first != NULL) {
		task = scheduler->first;

		scheduler->scheduled--;
		scheduler->first = task->next;

		OBJ_RELEASE(&task->fiber.std);
	}

	zend_object_std_dtor(&scheduler->std);
}

ZEND_METHOD(TaskScheduler, count)
{
	concurrent_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (concurrent_task_scheduler *)Z_OBJ_P(getThis());

	RETURN_LONG(scheduler->scheduled);
}

ZEND_METHOD(TaskScheduler, activate)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(TaskScheduler, run)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task_scheduler *prev;
	concurrent_task *task;

	zval *params;
	zval obj;
	zval retval;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	task = concurrent_task_object_create();
	task->scheduler = scheduler;
	task->context = concurrent_context_get();

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(task->fiber.fci, task->fiber.fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->fiber.fci.no_separation = 1;

	if (params == NULL) {
		task->fiber.fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fiber.fci, params);
	}

	Z_TRY_ADDREF_P(&task->fiber.fci.function_name);

	GC_ADDREF(&task->context->std);

	concurrent_task_scheduler_enqueue(task);

	prev = TASK_G(current_scheduler);
	TASK_G(current_scheduler) = scheduler;

	ZVAL_OBJ(&obj, &scheduler->std);

	zend_string *name = zend_string_init("runloop", sizeof("runloop")-1, 0);
	zval *entry = zend_hash_find_ex(&scheduler->std.ce->function_table, name, 0);
	zend_function *func = Z_FUNC_P(entry);

	zend_call_method_with_0_params(&obj, Z_OBJCE_P(&obj), &func, "runloop", &retval);
	zval_ptr_dtor(&retval);

	TASK_G(current_scheduler) = prev;

	if (task->fiber.status == CONCURRENT_FIBER_STATUS_FINISHED) {
		RETURN_ZVAL(&task->result, 1, 0);
	}

	if (task->fiber.status == CONCURRENT_FIBER_STATUS_DEAD) {
		Z_ADDREF_P(&task->result);

		execute_data->opline--;
		zend_throw_exception_internal(&task->result);
		execute_data->opline++;

		return;
	}

	zend_throw_error(NULL, "Scheduled task did not run to completion");
}

ZEND_METHOD(TaskScheduler, runWithContext)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task_scheduler *prev;
	concurrent_task *task;

	zval *ctx;
	zval *params;
	zval obj;
	zval retval;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	task = concurrent_task_object_create();
	task->scheduler = scheduler;

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(task->fiber.fci, task->fiber.fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->context = (concurrent_context *) Z_OBJ_P(ctx);
	task->fiber.fci.no_separation = 1;

	if (params == NULL) {
		task->fiber.fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fiber.fci, params);
	}

	Z_TRY_ADDREF_P(&task->fiber.fci.function_name);

	GC_ADDREF(&task->context->std);

	concurrent_task_scheduler_enqueue(task);

	prev = TASK_G(current_scheduler);
	TASK_G(current_scheduler) = scheduler;

	ZVAL_OBJ(&obj, &scheduler->std);

	zend_string *name = zend_string_init("runloop", sizeof("runloop")-1, 0);
	zval *entry = zend_hash_find_ex(&scheduler->std.ce->function_table, name, 0);
	zend_function *func = Z_FUNC_P(entry);

	zend_call_method_with_0_params(&obj, Z_OBJCE_P(&obj), &func, "runloop", &retval);
	zval_ptr_dtor(&retval);

	TASK_G(current_scheduler) = prev;

	if (task->fiber.status == CONCURRENT_FIBER_STATUS_FINISHED) {
		RETURN_ZVAL(&task->result, 1, 0);
	}

	if (task->fiber.status == CONCURRENT_FIBER_STATUS_DEAD) {
		Z_ADDREF_P(&task->result);

		execute_data->opline--;
		zend_throw_exception_internal(&task->result);
		execute_data->opline++;

		return;
	}

	zend_throw_error(NULL, "Scheduled task did not run to completion");
}

ZEND_METHOD(TaskScheduler, dispatch)
{
	concurrent_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	if (scheduler->running) {
		zend_throw_error(NULL, "Cannot dispatch tasks because the dispatcher is already running");
		return;
	}

	concurrent_task_scheduler_run(scheduler);
}

ZEND_METHOD(TaskScheduler, runLoop)
{
	concurrent_task_scheduler *scheduler;

	ZEND_PARSE_PARAMETERS_NONE();

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	if (scheduler->running) {
		zend_throw_error(NULL, "Cannot dispatch tasks because the dispatcher is already running");
		return;
	}

	concurrent_task_scheduler_run(scheduler);
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
	ZEND_ARG_ARRAY_INFO(0, arguments, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_run_with_context, 0, 0, 2)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, arguments, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_run_loop, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_dispatch, 0)
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
	ZEND_ME(TaskScheduler, __wakeup, arginfo_task_scheduler_wakeup, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL)
	ZEND_FE_END
};


void concurrent_task_scheduler_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskScheduler", task_scheduler_functions);
	concurrent_task_scheduler_ce = zend_register_internal_class(&ce);
	concurrent_task_scheduler_ce->create_object = concurrent_task_scheduler_object_create;
	concurrent_task_scheduler_ce->serialize = zend_class_serialize_deny;
	concurrent_task_scheduler_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(concurrent_task_scheduler_ce, 1, zend_ce_countable);

	memcpy(&concurrent_task_scheduler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_scheduler_handlers.free_obj = concurrent_task_scheduler_object_destroy;
	concurrent_task_scheduler_handlers.clone_obj = NULL;
}

void concurrent_task_scheduler_shutdown()
{
	concurrent_task_scheduler *scheduler;

	scheduler = TASK_G(scheduler);

	if (scheduler != NULL) {
		concurrent_task_scheduler_object_destroy(&scheduler->std);
	}
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
