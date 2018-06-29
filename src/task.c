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
zend_class_entry *concurrent_task_continuation_ce;

static zend_object_handlers concurrent_task_handlers;
static zend_object_handlers concurrent_task_scheduler_handlers;
static zend_object_handlers concurrent_task_continuation_handlers;


static void concurrent_task_start(concurrent_task *task)
{
	task->operation = CONCURRENT_TASK_OPERATION_NONE;
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

static void concurrent_task_continue(concurrent_task *task)
{
	task->operation = CONCURRENT_TASK_OPERATION_NONE;
	task->status = CONCURRENT_FIBER_STATUS_RUNNING;

	if (!concurrent_fiber_switch_to((concurrent_fiber *) task)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}
}

static void concurrent_task_notify_success(concurrent_task *task, zval *result)
{
	zval args[2];
	zval retval;

	ZVAL_COPY(&task->result, result);

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

	zval_ptr_dtor(&task->awaiter.function_name);
	zval_ptr_dtor(&retval);
}

static void concurrent_task_notify_failure(concurrent_task *task, zval *error)
{
	zval args[1];
	zval retval;

	ZVAL_COPY(&task->result, error);

	if (!task->await) {
		return;
	}

	ZVAL_COPY(&args[0], error);

	task->awaiter.param_count = 1;
	task->awaiter.params = args;
	task->awaiter.retval = &retval;
	task->await = 0;

	zend_call_function(&task->awaiter, &task->awaiter_cache);

	zval_ptr_dtor(&task->awaiter.function_name);
	zval_ptr_dtor(&retval);
}

static zend_bool concurrent_task_schedule(concurrent_task *task, concurrent_task_scheduler *scheduler)
{
	zval args[1];
	zval retval;

	if (task->status == CONCURRENT_FIBER_STATUS_INIT) {
		task->operation = CONCURRENT_TASK_OPERATION_START;
	} else if (task->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		task->operation = CONCURRENT_TASK_OPERATION_RESUME;
	} else {
		return 0;
	}

	task->scheduler = scheduler;

	if (scheduler->last == NULL) {
		scheduler->first = task;
		scheduler->last = task;
	} else {
		scheduler->last->next = task;
		scheduler->last = task;
	}

	GC_ADDREF(&task->std);

	scheduler->scheduled++;

	if (scheduler->activator && scheduler->activate && !scheduler->running) {
		scheduler->activate = 0;

		ZVAL_OBJ(&args[0], &scheduler->std);

		scheduler->activator_fci.param_count = 1;
		scheduler->activator_fci.params = args;
		scheduler->activator_fci.retval = &retval;

		zend_call_function(&scheduler->activator_fci, &scheduler->activator_fcc);
		zval_ptr_dtor(&retval);
	}

	return 1;
}


ZEND_METHOD(Awaitable, continueWith) { }

ZEND_BEGIN_ARG_INFO_EX(arginfo_awaitable_continue_with, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry awaitable_functions[] = {
	ZEND_ME(Awaitable, continueWith, arginfo_awaitable_continue_with, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};


static zend_object *concurrent_task_continuation_object_create(concurrent_task *task, concurrent_task_scheduler *scheduler)
{
	concurrent_task_continuation *cont;

	cont = emalloc(sizeof(concurrent_task_continuation));
	ZEND_SECURE_ZERO(cont, sizeof(concurrent_task_continuation));

	cont->task = task;
	cont->scheduler = scheduler;

	GC_ADDREF(&task->std);

	zend_object_std_init(&cont->std, concurrent_task_continuation_ce);
	cont->std.handlers = &concurrent_task_continuation_handlers;

	return &cont->std;
}

static void concurrent_task_continuation_object_destroy(zend_object *object)
{
	concurrent_task_continuation *cont;

	cont = (concurrent_task_continuation *) object;

	if (cont->task != NULL) {
		OBJ_RELEASE(&cont->task->std);
	}

	zend_object_std_dtor(&cont->std);
}

ZEND_METHOD(TaskContinuation, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Task continuations must not be constructed from userland code");
}

ZEND_METHOD(TaskContinuation, __invoke)
{
	concurrent_task_continuation *cont;
	concurrent_task *task;

	zval *error;
	zval *val;

	cont = (concurrent_task_continuation *) Z_OBJ_P(getThis());

	if (cont->task == NULL) {
		return;
	}

	error = NULL;
	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_ZVAL(error)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	task = cont->task;
	cont->task = NULL;

	if (error == NULL || Z_TYPE_P(error) == IS_NULL) {
		if (task->value != NULL) {
			ZVAL_COPY(task->value, val);
		}
	} else {
		ZVAL_COPY(&task->error, error);
	}

	task->value = NULL;

	if (task->status != CONCURRENT_FIBER_STATUS_RUNNING) {
		concurrent_task_schedule(task, cont->scheduler);
	} else {
		task->operation = CONCURRENT_TASK_OPERATION_RESUME;
	}

	OBJ_RELEASE(&task->std);
}

ZEND_METHOD(TaskContinuation, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task continuation is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_continuation_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_continuation_invoke, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

static const zend_function_entry task_continuation_functions[] = {
	ZEND_ME(TaskContinuation, __construct, arginfo_task_continuation_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(TaskContinuation, __invoke, arginfo_task_continuation_invoke, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskContinuation, __wakeup, arginfo_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static concurrent_task *concurrent_task_object_create()
{
	concurrent_task *task;
	zend_long stack_size;

	task = emalloc(sizeof(concurrent_task));
	ZEND_SECURE_ZERO(task, sizeof(concurrent_task));

	task->status = CONCURRENT_FIBER_STATUS_INIT;

	task->id = TASK_G(counter) + 1;
	TASK_G(counter) = task->id;

	stack_size = TASK_G(stack_size);

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	task->stack_size = stack_size;

	ZVAL_NULL(&task->result);
	ZVAL_UNDEF(&task->error);

	zend_object_std_init(&task->std, concurrent_task_ce);
	task->std.handlers = &concurrent_task_handlers;

	return task;
}

static void concurrent_task_object_destroy(zend_object *object)
{
	concurrent_task *task;
	concurrent_task_scheduler *prev;

	task = (concurrent_task *) object;

	if (task->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		prev = TASK_G(scheduler);
		TASK_G(scheduler) = NULL;

		task->status = CONCURRENT_FIBER_STATUS_DEAD;

		concurrent_fiber_switch_to((concurrent_fiber *) task);

		TASK_G(scheduler) = prev;
	}

	if (task->status == CONCURRENT_FIBER_STATUS_INIT) {
		zend_fcall_info_args_clear(&task->fci, 1);

		zval_ptr_dtor(&task->fci.function_name);
	}

	zval_ptr_dtor(&task->result);
	zval_ptr_dtor(&task->error);

	if (task->await) {
		zval_ptr_dtor(&task->awaiter.function_name);
	}

	concurrent_fiber_destroy(task->context);

	zend_object_std_dtor(&task->std);
}

ZEND_METHOD(Task, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Tasks must not be constructed by userland code");
}

ZEND_METHOD(Task, continueWith)
{
	concurrent_task *task;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval args[2];
	zval result;

	task = (concurrent_task *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	fci.no_separation = 1;

	if (task->status == CONCURRENT_FIBER_STATUS_FINISHED) {
		ZVAL_NULL(&args[0]);
		ZVAL_COPY(&args[1], &task->result);

		fci.param_count = 2;
		fci.params = args;
		fci.retval = &result;

		zend_call_function(&fci, &fcc);
		zval_ptr_dtor(&result);

		return;
	}

	if (task->status == CONCURRENT_FIBER_STATUS_DEAD) {
		ZVAL_COPY(&args[0], &task->result);

		fci.param_count = 1;
		fci.params = args;
		fci.retval = &result;

		zend_call_function(&fci, &fcc);
		zval_ptr_dtor(&result);

		return;
	}

	task->await = 1;
	task->awaiter = fci;
	task->awaiter_cache = fcc;
	task->awaiter.object = task->awaiter_cache.object;

	Z_TRY_ADDREF_P(&task->awaiter.function_name);
}

ZEND_METHOD(Task, async)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task * task;

	zval *params;
	zval obj;

	scheduler = TASK_G(scheduler);

	if (UNEXPECTED(scheduler == NULL)) {
		zend_throw_error(NULL, "Cannot create an async task while no task scheduler is running");
		return;
	}

	task = concurrent_task_object_create();
	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(task->fci, task->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->fci.no_separation = 1;

	if (params == NULL) {
		task->fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fci, params);
	}

	Z_TRY_ADDREF_P(&task->fci.function_name);

	concurrent_task_schedule(task, scheduler);

	ZVAL_OBJ(&obj, &task->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, await)
{
	zval *val;
	zend_class_entry *ce;
	concurrent_task *task;
	concurrent_task *inner;
	zend_execute_data *exec;
	size_t stack_page_size;

	zval retval;
	zval cont;
	zval error;
	zval *value;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(val);
	ZEND_PARSE_PARAMETERS_END();

	task = (concurrent_task *) TASK_G(current_fiber);

	if (UNEXPECTED(task == NULL)) {
		zend_throw_error(NULL, "Await must be called from within a running task");
		return;
	}

	if (UNEXPECTED(task->status != CONCURRENT_FIBER_STATUS_RUNNING)) {
		zend_throw_error(NULL, "Cannot await in a task that is not running");
		return;
	}

	if (Z_TYPE_P(val) != IS_OBJECT) {
		RETURN_ZVAL(val, 1, 0);
	}

	ce = Z_OBJCE_P(val);

	if (ce == concurrent_task_ce) {
		inner = (concurrent_task *) Z_OBJ_P(val);

		if (inner->status == CONCURRENT_FIBER_STATUS_FINISHED) {
			RETURN_ZVAL(&inner->result, 1, 0);
		}

		if (inner->status == CONCURRENT_FIBER_STATUS_DEAD) {
			exec = EG(current_execute_data);

			exec->opline--;
			zend_throw_exception_internal(&inner->result);
			exec->opline++;

			return;
		}
	}

	if (instanceof_function_ex(ce, concurrent_awaitable_ce, 1) != 1) {
		if (task->scheduler->adapter) {
			task->scheduler->adapter_fci.param_count = 1;
			task->scheduler->adapter_fci.params = val;
			task->scheduler->adapter_fci.retval = &retval;

			zend_call_function(&task->scheduler->adapter_fci, &task->scheduler->adapter_fcc);
			zval_ptr_dtor(val);

			ZVAL_COPY(val, &retval);
			zval_ptr_dtor(&retval);

			if (Z_TYPE_P(val) != IS_OBJECT) {
				RETURN_ZVAL(val, 1, 0);
			}

			ce = Z_OBJCE_P(val);
		}

		if (instanceof_function_ex(ce, concurrent_awaitable_ce, 1) != 1) {
			RETURN_ZVAL(val, 1, 0);
		}
	}

	value = task->value;
	task->value = USED_RET() ? return_value : NULL;

	ZVAL_OBJ(&cont, concurrent_task_continuation_object_create(task, task->scheduler));
	zend_call_method_with_1_params(val, NULL, NULL, "continueWith", NULL, &cont);
	zval_ptr_dtor(&cont);

	// Resume without fiber context switch if the awaitable is already resolved.
	if (task->operation == CONCURRENT_TASK_OPERATION_RESUME) {
		task->operation = CONCURRENT_TASK_OPERATION_NONE;
		task->value = value;

		return;
	}

	task->status = CONCURRENT_FIBER_STATUS_SUSPENDED;

	CONCURRENT_FIBER_BACKUP_EG(task->stack, stack_page_size, task->exec);
	concurrent_fiber_yield(task->context);
	CONCURRENT_FIBER_RESTORE_EG(task->stack, stack_page_size, task->exec);

	task->value = value;

	if (task->status == CONCURRENT_FIBER_STATUS_DEAD) {
		zend_throw_error(NULL, "Task has been destroyed");
		return;
	}

	if (Z_TYPE_P(&task->error) != IS_UNDEF) {
		error = task->error;
		ZVAL_UNDEF(&task->error);

		exec = EG(current_execute_data);

		exec->opline--;
		zend_throw_exception_internal(&error);
		exec->opline++;
	}
}

ZEND_METHOD(Task, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_async, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, arguments, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_await, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

static const zend_function_entry task_functions[] = {
	ZEND_ME(Task, __construct, arginfo_task_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(Task, continueWith, arginfo_awaitable_continue_with, ZEND_ACC_PUBLIC)
	ZEND_ME(Task, async, arginfo_task_async, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, await, arginfo_task_await, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, __wakeup, arginfo_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


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

		OBJ_RELEASE(&task->std);
	}

	if (scheduler->activator) {
		scheduler->activator = 0;

		zval_ptr_dtor(&scheduler->activator_fci.function_name);
	}

	if (scheduler->adapter) {
		scheduler->adapter = 0;

		zval_ptr_dtor(&scheduler->adapter_fci.function_name);
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

ZEND_METHOD(TaskScheduler, task)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task * task;

	zval *params;
	zval obj;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());
	task = concurrent_task_object_create();

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(task->fci, task->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->fci.no_separation = 1;

	if (params == NULL) {
		task->fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fci, params);
	}

	Z_TRY_ADDREF_P(&task->fci.function_name);

	concurrent_task_schedule(task, scheduler);

	ZVAL_OBJ(&obj, &task->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TaskScheduler, activator)
{
	concurrent_task_scheduler *scheduler;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(scheduler->activator_fci, scheduler->activator_fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (scheduler->activator) {
		zval_ptr_dtor(&scheduler->activator_fci.function_name);
	}

	scheduler->activator = 1;

	Z_TRY_ADDREF_P(&scheduler->activator_fci.function_name);
}

ZEND_METHOD(TaskScheduler, adapter)
{
	concurrent_task_scheduler *scheduler;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(scheduler->adapter_fci, scheduler->adapter_fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (scheduler->adapter) {
		zval_ptr_dtor(&scheduler->adapter_fci.function_name);
	}

	scheduler->adapter = 1;

	Z_TRY_ADDREF_P(&scheduler->adapter_fci.function_name);
}

ZEND_METHOD(TaskScheduler, run)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task_scheduler *prev;
	concurrent_task *task;
	zval result;

	scheduler = (concurrent_task_scheduler *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_NONE();

	prev = TASK_G(scheduler);
	TASK_G(scheduler) = scheduler;

	scheduler->running = 1;
	scheduler->activate = 0;

	while (scheduler->first != NULL) {
		task = scheduler->first;

		scheduler->scheduled--;
		scheduler->first = task->next;

		if (scheduler->last == task) {
			scheduler->last = NULL;
		}

		ZVAL_NULL(&result);

		task->next = NULL;
		task->value = &result;

		if (task->operation == CONCURRENT_TASK_OPERATION_START) {
			concurrent_task_start(task);
		} else {
			concurrent_task_continue(task);
		}

		task->scheduler = NULL;

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

		OBJ_RELEASE(&task->std);
		zval_ptr_dtor(&result);
	}

	scheduler->last = NULL;

	scheduler->running = 0;
	scheduler->activate = 1;

	TASK_G(scheduler) = prev;
}

ZEND_METHOD(TaskScheduler, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task scheduler is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_count, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_task, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, arguments, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_activator, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_scheduler_adapter, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_scheduler_run, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_scheduler_functions[] = {
	ZEND_ME(TaskScheduler, count, arginfo_task_scheduler_count, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, task, arginfo_task_scheduler_task, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, activator, arginfo_task_scheduler_activator, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, adapter, arginfo_task_scheduler_adapter, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, run, arginfo_task_scheduler_run, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskScheduler, __wakeup, arginfo_wakeup, ZEND_ACC_PUBLIC)
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
	concurrent_task_ce->serialize = zend_class_serialize_deny;
	concurrent_task_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_task_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_handlers.free_obj = concurrent_task_object_destroy;
	concurrent_task_handlers.clone_obj = NULL;

	zend_class_implements(concurrent_task_ce, 1, concurrent_awaitable_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskScheduler", task_scheduler_functions);
	concurrent_task_scheduler_ce = zend_register_internal_class(&ce);
	concurrent_task_scheduler_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_scheduler_ce->create_object = concurrent_task_scheduler_object_create;
	concurrent_task_scheduler_ce->serialize = zend_class_serialize_deny;
	concurrent_task_scheduler_ce->unserialize = zend_class_unserialize_deny;

	zend_class_implements(concurrent_task_scheduler_ce, 1, zend_ce_countable);

	memcpy(&concurrent_task_scheduler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_scheduler_handlers.free_obj = concurrent_task_scheduler_object_destroy;
	concurrent_task_scheduler_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskContinuation", task_continuation_functions);
	concurrent_task_continuation_ce = zend_register_internal_class(&ce);
	concurrent_task_continuation_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_continuation_ce->serialize = zend_class_serialize_deny;
	concurrent_task_continuation_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_task_continuation_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_continuation_handlers.free_obj = concurrent_task_continuation_object_destroy;
	concurrent_task_continuation_handlers.clone_obj = NULL;
}

void concurrent_task_ce_unregister()
{

}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
