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
#include "task.h"

ZEND_DECLARE_MODULE_GLOBALS(task)

zend_class_entry *concurrent_awaitable_ce;
zend_class_entry *concurrent_task_ce;
zend_class_entry *concurrent_task_scheduler_ce;

zend_class_entry *concurrent_fiber_ce;

static zend_object_handlers concurrent_task_handlers;
static zend_object_handlers concurrent_task_scheduler_handlers;


static void concurrent_task_start(concurrent_task *task)
{
	printf("START TASK...\n");

	task->context = concurrent_fiber_create_context();

	if (task->context == NULL) {
		zend_throw_error(NULL, "Failed to create native fiber context");
		return;
	}

	if (!concurrent_fiber_create(task->context, concurrent_fiber_run, task->stack_size)) {
		zend_throw_error(NULL, "Failed to create native fiber");
		return;
	}

	task->stack = (zend_vm_stack) emalloc(CONCURRENT_FIBER_VM_STACK_SIZE);
	task->stack->top = ZEND_VM_STACK_ELEMENTS(task->stack) + 1;
	task->stack->end = (zval *) ((char *) task->stack + CONCURRENT_FIBER_VM_STACK_SIZE);
	task->stack->prev = NULL;

	task->status = CONCURRENT_FIBER_STATUS_RUNNING;

	if (!concurrent_fiber_switch_to((concurrent_fiber *) task)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}

	zend_fcall_info_args_clear(&task->fci, 1);
}

static void concurrent_task_resume(concurrent_task *task)
{
	printf("RESUME TASK...\n");

	task->status = CONCURRENT_FIBER_STATUS_RUNNING;
}

static void concurrent_task_resume_error(concurrent_task *task)
{
	printf("ERROR TASK...\n");

	task->status = CONCURRENT_FIBER_STATUS_RUNNING;
}

static void concurrent_task_notify_success(concurrent_task *task, zval *result)
{
	zval args[2];
	zval retval;

	if (!task->await) {
		return;
	}

	ZVAL_NULL(&args[0]);
	ZVAL_COPY(&args[1], result);

	task->awaiter.param_count = 2;
	task->awaiter.params = args;
	task->awaiter.retval = &retval;
	task->await = 0;

	zend_call_function(&task->awaiter, &task->awaiter_cache);

	if (task->awaiter.object) {
		GC_DELREF(task->awaiter.object);
	}

	zval_ptr_dtor(&task->awaiter.function_name);
}

static void concurrent_task_notify_failure(concurrent_task *task, zval *error)
{
	zval args[1];
	zval retval;

	if (!task->await) {
		return;
	}

	ZVAL_COPY(&args[0], error);

	task->awaiter.param_count = 1;
	task->awaiter.params = args;
	task->awaiter.retval = &retval;
	task->await = 0;

	zend_call_function(&task->awaiter, &task->awaiter_cache);

	if (task->awaiter.object) {
		GC_DELREF(task->awaiter.object);
	}

	zval_ptr_dtor(&task->awaiter.function_name);
}


ZEND_METHOD(Awaitable, continueWith) { }


ZEND_BEGIN_ARG_INFO_EX(arginfo_awaitable_continue_with, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry awaitable_functions[] = {
	ZEND_ME(Awaitable, continueWith, arginfo_awaitable_continue_with, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


static zend_object *concurrent_task_object_create(zend_class_entry *ce)
{
	concurrent_task *task;

	task = emalloc(sizeof(concurrent_task));
	memset(task, 0, sizeof(concurrent_task));

	zend_object_std_init(&task->std, ce);
	task->std.handlers = &concurrent_task_handlers;

	return &task->std;
}

static void concurrent_task_object_destroy(zend_object *object)
{
	concurrent_task *task;

	task = (concurrent_task *) object;

	if (task->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		task->status = CONCURRENT_FIBER_STATUS_DEAD;

		concurrent_fiber_switch_to((concurrent_fiber *) task);
	}

	if (task->status == CONCURRENT_FIBER_STATUS_INIT) {
		zend_fcall_info_args_clear(&task->fci, 1);

		if (task->fci.object) {
			GC_DELREF(task->fci.object);
		}

		zval_ptr_dtor(&task->fci.function_name);
	}

	concurrent_fiber_destroy(task->context);

	if (task->await) {
		if (task->awaiter.object) {
			GC_DELREF(task->awaiter.object);
		}

		zval_ptr_dtor(&task->awaiter.function_name);
	}

	zend_object_std_dtor(&task->std);
}

ZEND_METHOD(Task, __construct)
{
	concurrent_task *task;
	zval *params;
	zend_long stack_size;

	task = (concurrent_task *) Z_OBJ_P(getThis());
	params = NULL;
	stack_size = TASK_G(stack_size);

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(task->fci, task->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	task->status = CONCURRENT_FIBER_STATUS_INIT;
	task->stack_size = stack_size;
	task->fci.no_separation = 1;

	if (params == NULL) {
		task->fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fci, params);
	}

	if (task->fci.object) {
		GC_ADDREF(task->fci.object);
	}

	Z_TRY_ADDREF_P(&task->fci.function_name);
}

ZEND_METHOD(Task, continueWith)
{
	concurrent_task *task;

	task = (concurrent_task *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(task->awaiter, task->awaiter_cache, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	task->await = 1;
	task->awaiter.no_separation = 1;
	task->awaiter.object = task->awaiter_cache.object;

	if (task->awaiter.object) {
		GC_ADDREF(task->awaiter.object);
	}

	Z_TRY_ADDREF_P(&task->awaiter.function_name);
}

ZEND_METHOD(Task, await)
{
	zval *val;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(val);
	ZEND_PARSE_PARAMETERS_END();

	if (Z_TYPE_P(val) != IS_OBJECT || !instanceof_function(Z_OBJCE_P(val), concurrent_awaitable_ce)) {
		RETURN_ZVAL(val, 1, 1);
	}

	zend_call_method_with_1_params(NULL, concurrent_fiber_ce, NULL, "yield", NULL, val);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_ctor, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, params, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_await, 0, 0, 1)
	ZEND_ARG_INFO(0, awaitable)
ZEND_END_ARG_INFO()

static const zend_function_entry task_functions[] = {
	ZEND_ME(Task, __construct, arginfo_task_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(Task, continueWith, arginfo_awaitable_continue_with, ZEND_ACC_PUBLIC)
	ZEND_ME(Task, await, arginfo_task_await, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


static zend_object *concurrent_task_scheduler_object_create(zend_class_entry *ce)
{
	concurrent_task_scheduler *scheduler;

	scheduler = emalloc(sizeof(concurrent_task_scheduler));
	memset(scheduler, 0, sizeof(concurrent_task_scheduler));

	zend_object_std_init(&scheduler->std, ce);
	scheduler->std.handlers = &concurrent_task_scheduler_handlers;

	return &scheduler->std;
}

static void concurrent_task_scheduler_object_destroy(zend_object *object)
{
	concurrent_task_scheduler *scheduler;

	scheduler = (concurrent_task_scheduler *) object;

	zend_object_std_dtor(&scheduler->std);
}

ZEND_METHOD(TaskScheduler, start)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task *task;
	zval *val;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	task = (concurrent_task *) Z_OBJ_P(val);

	if (task->scheduler != NULL) {
		zend_throw_error(NULL, "Task is already started");
		return;
	}

	task->scheduler = scheduler;

	if (scheduler->last == NULL) {
		scheduler->first = task;
		scheduler->last = task;
		task->next = NULL;
	} else {
		scheduler->last->next = task;
		scheduler->last = task;
	}

	scheduler->scheduled++;

	printf("ENQUEUED TASK!\n");
}

ZEND_METHOD(TaskScheduler, schedule)
{

}

ZEND_METHOD(TaskScheduler, scheduleError)
{

}

ZEND_METHOD(TaskScheduler, run)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task *task;
	concurrent_task *next;
	zval result;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());
	task = scheduler->first;

	while (task != NULL) {
		scheduler->scheduled--;
		next = task->next;

		task->value = &result;

		if (task->operation == CONCURRENT_TASK_OPERATION_START) {
			concurrent_task_start(task);
		} else if (task->operation == CONCURRENT_TASK_OPERATION_RESUME) {
			concurrent_task_resume(task);
		} else {
			concurrent_task_resume_error(task);
		}

		if (UNEXPECTED(EG(exception))) {
			zval_ptr_dtor(&result);

			ZVAL_OBJ(&result, EG(exception));
			GC_ADDREF(EG(exception));

			zend_clear_exception();

			task->status = CONCURRENT_FIBER_STATUS_DEAD;
			concurrent_task_notify_failure(task, &result);

			GC_DELREF(Z_OBJ_P(&result));
		} else {
			if (task->status == CONCURRENT_FIBER_STATUS_FINISHED) {
				concurrent_task_notify_success(task, &result);
			} else if (task->status == CONCURRENT_FIBER_STATUS_DEAD) {
				concurrent_task_notify_failure(task, &result);
			}
		}

		zval_ptr_dtor(&result);

		task = next;
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_start, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, task, Concurrent\\Task, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_schedule, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, task, Concurrent\\Task, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_schedule_error, 0, 0, 2)
	ZEND_ARG_OBJ_INFO(0, task, Concurrent\\Task, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_run, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_scheduler_functions[] = {
	ZEND_ME(TaskScheduler, start, arginfo_task_scheduler_start, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, schedule, arginfo_task_scheduler_schedule, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, scheduleError, arginfo_task_scheduler_schedule_error, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, run, arginfo_task_scheduler_run, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void concurrent_task_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Awaitable", awaitable_functions);
	concurrent_awaitable_ce = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Task", task_functions);
	concurrent_task_ce = zend_register_internal_class(&ce);
	concurrent_task_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_ce->create_object = concurrent_task_object_create;

	memcpy(&concurrent_task_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_handlers.free_obj = concurrent_task_object_destroy;
	concurrent_task_handlers.clone_obj = NULL;

	zend_class_implements(concurrent_task_ce, 1, concurrent_awaitable_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskScheduler", task_scheduler_functions);
	concurrent_task_scheduler_ce = zend_register_internal_class(&ce);
	concurrent_task_scheduler_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_scheduler_ce->create_object = concurrent_task_scheduler_object_create;

	memcpy(&concurrent_task_scheduler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_scheduler_handlers.free_obj = concurrent_task_scheduler_object_destroy;
	concurrent_task_scheduler_handlers.clone_obj = NULL;
}

void concurrent_task_ce_unregister()
{

}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
